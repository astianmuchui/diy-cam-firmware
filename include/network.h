#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

enum class Transport
{
    WIFI,
    GSM
};
class Network
{
public:
    explicit Network(Transport t);

    bool begin(const char *ssid_or_apn, const char *password = nullptr);

    bool connect_mqtt(const char *broker, uint16_t port,
                      const char *client_id,
                      const char *user = nullptr,
                      const char *pass = nullptr);

    bool publish(const char *topic, const char *payload);

    void loop();

    bool is_mqtt_connected();
    bool is_net_connected();

    int signal_strength();

    const char *transport_name() const;

    bool start_softap(const char *ssid, const char *password);
    bool is_softap_active() const;

    bool http_post_jpeg(const char *host, uint16_t port, const char *path,
                        const uint8_t *jpeg, size_t len, const char *reading_id);

private:
    Transport _transport;
    bool _softap_active = false;

    WiFiClient _wifi_client;
    PubSubClient _mqtt;

    const char *_broker = nullptr;
    uint16_t _port = 1883;
    const char *_client_id = nullptr;
    const char *_user = nullptr;
    const char *_pass = nullptr;

    bool wifi_connect(const char *ssid, const char *password);
    bool gsm_connect(const char *apn);
    bool reconnect_mqtt();
};
