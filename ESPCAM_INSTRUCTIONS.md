# ESP-CAM Firmware Instructions

1. Connect to the SoftAP broadcasted by the main ESP32: SSID `MSONAR-<DEVICE_ID>`, password `SonarField#2024!`
2. Listen for a `POST /capture` request on port 80 — the body is JSON: `{"reading_id":"<uuid>"}`
3. On receiving `/capture`, take a JPEG photo immediately and respond with HTTP 200
4. POST the raw JPEG to `http://192.168.4.1/upload?id=<reading_id>` — this is the main ESP32's SoftAP IP
5. Set `Content-Type: image/jpeg` and `Content-Length` headers; include the `id` query param so the proxy can tag the upload
6. Do NOT attempt to upload directly to the Go backend — the SoftAP subnet has no internet route
7. The main ESP32 will forward the photo to the backend and handle retries
