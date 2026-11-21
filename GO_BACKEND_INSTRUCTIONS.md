# Go Backend Instructions — Photo Upload Fix

## Problem Summary

Photo uploads from the ESP32 are rejected with HTTP 400 because:
1. The firmware was generating a malformed correlation ID (`a1b2c3d4-e5f6a7b8`, 17 chars) that fails `uuid.Parse()`. **This is now fixed in firmware — it generates a valid UUID v4.**
2. Even with a valid UUID, the upload always arrives before the reading exists in the DB (MQTT publishes every 30 s; photo upload happens immediately). A strict `GetReadingByID` check will return 404 every time.

---

## Required Changes

### 1. `PhotoUploadHandler` — decouple upload from reading existence

The handler must accept the photo using the firmware's correlation ID as a filename key,
without requiring the reading to exist in the DB at upload time.

```go
func PhotoUploadHandler(c *fiber.Ctx) error {
    readingIDStr := c.FormValue("reading_id")
    if readingIDStr == "" {
        readingIDStr = c.Query("reading_id")
    }
    if readingIDStr == "" {
        readingIDStr = c.Get("X-Reading-ID")
    }
    if readingIDStr == "" {
        return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "missing reading_id"})
    }

    // Sanitise — prevent path traversal. Do not require UUID format;
    // the reading may not exist in the DB yet (arrives via MQTT up to 30 s later).
    safeID := filepath.Base(readingIDStr)
    if safeID == "." || safeID == "/" {
        return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "invalid reading_id"})
    }

    var jpeg []byte
    file, err := c.FormFile("photo")
    if err == nil {
        if file.Size > maxPhotoSize {
            return c.Status(fiber.StatusRequestEntityTooLarge).JSON(fiber.Map{"error": "photo exceeds 500KB limit"})
        }
        f, openErr := file.Open()
        if openErr != nil {
            return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "failed to open photo"})
        }
        defer f.Close()
        jpeg, err = io.ReadAll(f)
        if err != nil {
            return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "failed to read photo"})
        }
    } else {
        jpeg = c.Body()
        if len(jpeg) == 0 {
            return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "no photo provided"})
        }
        if len(jpeg) > maxPhotoSize {
            return c.Status(fiber.StatusRequestEntityTooLarge).JSON(fiber.Map{"error": "photo exceeds 500KB limit"})
        }
    }

    if err := os.MkdirAll(photoDir, 0755); err != nil {
        return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "failed to create photo directory"})
    }

    photoPath := filepath.Join(photoDir, safeID+".jpg")
    if err := os.WriteFile(photoPath, jpeg, 0644); err != nil {
        return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "failed to save photo"})
    }

    // Best-effort: link photo and run CNN only if the reading already exists.
    // If not, the MQTT handler will pick up the photo file when the reading arrives.
    if id, parseErr := uuid.Parse(safeID); parseErr == nil {
        if reading, _ := postgres.GetReadingByID(postgres.DB, id); reading != nil {
            _ = postgres.UpdateReadingPhoto(postgres.DB, id, photoPath)
            jpegCopy := make([]byte, len(jpeg))
            copy(jpegCopy, jpeg)
            go func() {
                result, classErr := ml.ClassifyPhoto(id.String(), jpegCopy)
                if classErr != nil {
                    log.Printf("photo cnn [reading %s]: %v", id, classErr)
                    return
                }
                if dbErr := postgres.UpdateReadingCNNResult(
                    postgres.DB, id,
                    result.IsFish, result.FishConfidence, result.SceneClass,
                    result.Species, result.SpeciesConfidence,
                ); dbErr != nil {
                    log.Printf("photo cnn [reading %s]: update failed: %v", id, dbErr)
                }
            }()
        }
    }

    return c.JSON(fiber.Map{
        "status":     "saved",
        "reading_id": safeID,
        "size":       len(jpeg),
    })
}
```

---

### 2. MQTT reading insertion — link orphaned photos

When the MQTT payload is processed and a new reading is inserted, check whether a photo
was already uploaded for that correlation ID and link it immediately.

In whichever function inserts the reading from the MQTT payload, add after the INSERT:

```go
// camera.reading_id is the firmware correlation ID sent in the MQTT payload
correlationID := mqttPayload.Camera.ReadingID
if correlationID != "" {
    photoPath := filepath.Join(photoDir, correlationID+".jpg")
    if _, err := os.Stat(photoPath); err == nil {
        // Photo arrived before the reading — link it now
        _ = postgres.UpdateReadingPhoto(postgres.DB, insertedReadingID, photoPath)

        jpegBytes, readErr := os.ReadFile(photoPath)
        if readErr == nil {
            go func() {
                result, classErr := ml.ClassifyPhoto(insertedReadingID.String(), jpegBytes)
                if classErr != nil {
                    log.Printf("deferred cnn [reading %s]: %v", insertedReadingID, classErr)
                    return
                }
                _ = postgres.UpdateReadingCNNResult(
                    postgres.DB, insertedReadingID,
                    result.IsFish, result.FishConfidence, result.SceneClass,
                    result.Species, result.SpeciesConfidence,
                )
            }()
        }
    }
}
```

---

## Summary of Changes

| Location | Change |
| -------- | ------ |
| `PhotoUploadHandler` | Remove `uuid.Parse` hard-fail; use `filepath.Base` for sanitisation only |
| `PhotoUploadHandler` | Remove `GetReadingByID` as a gate; make it best-effort after saving |
| MQTT insert handler | After inserting reading, check for an orphaned photo file and link + run CNN |

## Why the Photo Arrives Before the Reading

```
t=0     Fish detected → firmware generates correlation UUID
t=0     camera_trigger() → ESP-CAM captures photo
t=~2s   ESP-CAM POSTs photo to ESP32 → ESP32 forwards to backend  ← photo saved here
t=30s   MQTT publish fires → backend inserts reading row           ← reading created here
```

The MQTT interval is 30 seconds. The photo upload completes in ~2 seconds.
The reading will never exist in the DB when the photo arrives.
