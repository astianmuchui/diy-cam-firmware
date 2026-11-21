#include "network.h"
#include "defines.h"
#include "led.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <TinyGsmClient.h>

static HardwareSerial s_gsm_serial(1);
static TinyGsm s_modem(s_gsm_serial);
static TinyGsmClient s_gsm_client(s_modem, 0);
static TinyGsmClientSecure s_gsm_http_client(s_modem, 1);
static WiFiClientSecure s_wifi_http_client;

Network::Network(Transport t)
    : _transport(t), _mqtt(_wifi_client)
{
    _mqtt.setBufferSize(768);
}

bool Network::begin(const char *ssid_or_apn, const char *password)
{
    if (_transport == Transport::WIFI)
    {
        return wifi_connect(ssid_or_apn, password);
    }

    _mqtt.setClient(s_gsm_client);
    return gsm_connect(ssid_or_apn);
}

bool Network::wifi_connect(const char *ssid, const char *password)
{
    Serial.printf("[Net] Connecting to WiFi: %s\n", ssid);
    led_set(LedEvent::WIFI_CONNECTING);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, password);

    const uint32_t timeout_ms = 20000;
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - start > timeout_ms)
        {
            Serial.println("[Net] WiFi timeout");
            led_set(LedEvent::ERROR);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.printf("[Net] WiFi OK — IP: %s\n", WiFi.localIP().toString().c_str());
    led_set(LedEvent::WIFI_CONNECTED);

    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.print("[Net] NTP sync");
    time_t now = 0;
    uint8_t tries = 0;
    while (now < 1000000000UL && tries < 20)
    {
        time(&now);
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print('.');
        tries++;
    }
    if (now >= 1000000000UL)
        Serial.printf("\n[Net] NTP synced: %lu\n", (unsigned long)now);
    else
        Serial.println("\n[Net] NTP sync failed — timestamps will be invalid");

    start_softap(SOFTAP_SSID, SOFTAP_PASSWORD);
    return true;
}

static void gsm_sync_time()
{
    String dt = s_modem.getGSMDateTime(DATE_FULL);
    Serial.printf("[Net] GSM DateTime: %s\n", dt.c_str());
    int yr, mo, day, hr, mn, sec, tz_q;
    char sign = '+';
    if (sscanf(dt.c_str(), "%d/%d/%d,%d:%d:%d%c%d",
               &yr, &mo, &day, &hr, &mn, &sec, &sign, &tz_q) >= 6)
    {
        struct tm t = {};
        t.tm_year = yr + 100;
        t.tm_mon  = mo - 1;
        t.tm_mday = day;
        t.tm_hour = hr;
        t.tm_min  = mn;
        t.tm_sec  = sec;
        t.tm_isdst = -1;
        time_t epoch = mktime(&t);
        int tz_offset = tz_q * 15 * 60;
        if (sign == '-') tz_offset = -tz_offset;
        epoch -= tz_offset;
        if (epoch > 1000000000L)
        {
            timeval tv = {epoch, 0};
            settimeofday(&tv, nullptr);
            Serial.printf("[Net] Time synced from GSM: %lu\n", (unsigned long)epoch);
            return;
        }
    }
    Serial.println("[Net] GSM time parse failed — timestamps may be invalid");
}

bool Network::gsm_connect(const char *apn)
{
    Serial.println("[Net] Initialising SIM800...");
    s_gsm_serial.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);

    led_set(LedEvent::WIFI_CONNECTING);
    if (!s_modem.restart())
    {
        Serial.println("[Net] Modem restart failed");
        led_set(LedEvent::ERROR);
        return false;
    }
    Serial.printf("[Net] Modem: %s\n", s_modem.getModemInfo().c_str());

    Serial.print("[Net] Waiting for GSM network");
    if (!s_modem.waitForNetwork(30000))
    {
        Serial.println("\n[Net] GSM registration failed");
        return false;
    }
    Serial.println(" OK");

    if (!s_modem.gprsConnect(apn, "", ""))
    {
        Serial.println("[Net] GPRS attach failed");
        return false;
    }
    Serial.printf("[Net] GPRS OK — IP: %s\n", s_modem.localIP().toString().c_str());
    led_set(LedEvent::WIFI_CONNECTED);

    gsm_sync_time();
    start_softap(SOFTAP_SSID, SOFTAP_PASSWORD);
    return true;
}

bool Network::connect_mqtt(const char *broker, uint16_t port,
                           const char *client_id,
                           const char *user, const char *pass)
{
    _broker = broker;
    _port = port;
    _client_id = client_id;
    _user = (user && user[0]) ? user : nullptr;
    _pass = (pass && pass[0]) ? pass : nullptr;

    _mqtt.setServer(broker, port);
    return reconnect_mqtt();
}

bool Network::reconnect_mqtt()
{
    if (_mqtt.connected())
        return true;

    Serial.printf("[MQTT] Connecting to %s:%u as %s\n", _broker, _port, _client_id);

    bool ok = (_user)
                  ? _mqtt.connect(_client_id, _user, _pass)
                  : _mqtt.connect(_client_id);

    if (ok)
    {
        Serial.println("[MQTT] Connected");
        led_set(LedEvent::BROKER_CONNECTED);
    }
    else
    {
        Serial.printf("[MQTT] Failed (state=%d)\n", _mqtt.state());
    }

    return ok;
}

bool Network::publish(const char *topic, const char *payload)
{
    if (!_mqtt.connected())
    {
        if (!reconnect_mqtt())
            return false;
    }
    bool ok = _mqtt.publish(topic, payload, false);
    if (!ok)
        Serial.printf("[MQTT] Publish failed on %s (state=%d)\n", topic, _mqtt.state());
    return ok;
}

void Network::loop()
{
    if (!_mqtt.connected())
        reconnect_mqtt();
    _mqtt.loop();
}

int Network::signal_strength()
{
    if (_transport == Transport::WIFI)
        return WiFi.RSSI();
    return s_modem.getSignalQuality();
}

bool Network::is_mqtt_connected() { return _mqtt.connected(); }

bool Network::is_net_connected()
{
    if (_transport == Transport::WIFI)
        return WiFi.isConnected();
    return s_modem.isGprsConnected();
}

const char *Network::transport_name() const
{
    return (_transport == Transport::WIFI) ? "wifi" : "gsm";
}

bool Network::start_softap(const char *ssid, const char *password)
{
    WiFi.mode(WIFI_AP_STA);
    bool ok = WiFi.softAP(ssid, password, 1, 0, 1);
    _softap_active = ok;
    if (ok)
        Serial.printf("[Net] SoftAP up — SSID: %s  IP: %s\n",
                      ssid, WiFi.softAPIP().toString().c_str());
    else
        Serial.println("[Net] SoftAP failed");
    return ok;
}

bool Network::is_softap_active() const
{
    return _softap_active;
}

bool Network::http_post_jpeg(const char *host, uint16_t port, const char *path,
                              const uint8_t *jpeg, size_t len, const char *reading_id)
{
    Serial.printf("[Net] Uploading %zu B JPEG to %s%s\n", len, host, path);

    Client *c = nullptr;
    if (_transport == Transport::GSM)
    {
        c = static_cast<Client *>(&s_gsm_http_client);
    }
    else
    {
        s_wifi_http_client.setInsecure();
        c = static_cast<Client *>(&s_wifi_http_client);
    }

    if (!c->connect(host, port))
    {
        Serial.println("[Net] HTTP connect failed");
        return false;
    }

    c->printf(
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %zu\r\n"
        "X-Reading-ID: %s\r\n"
        "\r\n",
        path, host, len, reading_id);

    const size_t CHUNK = 512;
    size_t sent = 0;
    while (sent < len)
    {
        size_t chunk = min(CHUNK, len - sent);
        size_t written = c->write(jpeg + sent, chunk);
        if (written == 0)
        {
            Serial.println("[Net] HTTP write stalled");
            c->stop();
            return false;
        }
        sent += written;
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    uint32_t response_timeout = (_transport == Transport::GSM) ? 30000 : 10000;
    uint32_t t = millis();
    while (!c->available() && millis() - t < response_timeout)
        vTaskDelay(pdMS_TO_TICKS(200));

    String resp = c->readStringUntil('\n');
    c->stop();
    Serial.printf("[Net] Upload response: %s\n", resp.c_str());
    return resp.indexOf("200") != -1 || resp.indexOf("201") != -1;
}
