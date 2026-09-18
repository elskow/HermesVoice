#include "node_mqtt.h"
#include "node_config.h"
#include "node_log.h"

#include "mqtt_client.h"
#include <stdio.h>

static const char *TAG = "node_mqtt";
static esp_mqtt_client_handle_t s_client;
static bool s_connected = false;

static void topic_for(char *out, size_t n, const char *leaf)
{
    snprintf(out, n, "node/%s/%s", node_config_device_id(), leaf);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t e = data;
    if (id == MQTT_EVENT_CONNECTED)
    {
        s_connected = true;
        char t[128];
        topic_for(t, sizeof(t), "status");
        esp_mqtt_client_publish(e->client, t, "online", 0, 1, 1);
        NODE_LOGI(TAG, "connected %s", node_config_mqtt_uri());
    }
    else if (id == MQTT_EVENT_DISCONNECTED)
    {
        s_connected = false;
        NODE_LOGW(TAG, "disconnected");
    }
}

int node_mqtt_init(void)
{
    char lwt[128];
    topic_for(lwt, sizeof(lwt), "status");
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = node_config_mqtt_uri(),
        .session.last_will.topic = lwt,
        .session.last_will.msg = "offline",
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
    };
    // uri pointer must outlive client; owned by node_config.
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, on_event, NULL);
    return s_client ? 0 : -1;
}

int node_mqtt_start(void)
{
    return s_client ? esp_mqtt_client_start(s_client) : -1;
}

int node_mqtt_publish_telemetry(const char *json)
{
    if (!s_connected || !s_client)
    {
        return -1;
    }
    char topic[128];
    topic_for(topic, sizeof(topic), "telemetry");
    return esp_mqtt_client_publish(s_client, topic, json ? json : "{}", 0, 0, 0);
}

bool node_mqtt_is_connected(void)
{
    return s_connected;
}

int node_mqtt_publish_state(const char *state)
{
    if (!s_connected || !s_client)
    {
        return -1;
    }
    char topic[128];
    topic_for(topic, sizeof(topic), "state");
    return esp_mqtt_client_publish(s_client, topic, state ? state : "idle", 0,
                                   1, 1);
}
