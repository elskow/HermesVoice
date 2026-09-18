#include "node_health.h"
#include "node_config.h"
#include "node_log.h"
#include "node_mqtt.h"
#include "node_ota.h"
#include "node_wifi.h"

static unsigned long s_ptt_presses = 0;
static int s_last_stt_ms;
static int s_last_hermes_ms;

void node_health_note_latency(int stt_ms, int hermes_ms)
{
    s_last_stt_ms = stt_ms;
    s_last_hermes_ms = hermes_ms;
}

void node_health_note_ptt(bool active)
{
    if (active) {
        s_ptt_presses++;
    }
}

unsigned long node_health_ptt_count(void)
{
    return s_ptt_presses;
}

#include "esp_system.h"
#include "esp_timer.h"
#include <stdio.h>

static const char *TAG = "node_health";
static esp_timer_handle_t s_timer;

static void publish(void *arg)
{
    (void)arg;
    char json[256];
    snprintf(json, sizeof(json),
             "{\"uptime_s\":%lld,\"heap_free\":%lu,\"rssi\":%d,"
             "\"ptt_presses\":%lu,\"mqtt\":%d,\"stt_ms\":%d,\"hermes_ms\":%d}",
             (long long)(esp_timer_get_time() / 1000000),
             (unsigned long)esp_get_free_heap_size(), node_wifi_rssi_dbm(),
             s_ptt_presses, node_mqtt_is_connected(), s_last_stt_ms,
             s_last_hermes_ms);
    node_mqtt_publish_telemetry(json);
    NODE_LOGD(TAG, "telemetry %s", json);
    // Auto-update check rides the telemetry cadence (default 30 s): cheap
    // GET, 204 fast-path when nothing eligible. Voice path unaffected by
    // check failures. Reboots out from under us on success (by design).
    int upd = node_ota_poll();
    if (upd > 0)
    {
        NODE_LOGI(TAG, "update applied, rebooting");
    }
    else if (upd < 0)
    {
        NODE_LOGD(TAG, "update check failed, continuing");
    }
}

int node_health_init(void)
{
    if (s_timer) {
        return 0;
    }
    esp_timer_create_args_t args = {.callback = publish, .name = "health"};
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        return -1;
    }
    int period = node_config_telemetry_interval_s();
    if (period < 5) {
        period = 30;
    }
    NODE_LOGI(TAG, "telemetry every %ds", period);
    return esp_timer_start_periodic(s_timer, (uint64_t)period * 1000000) == ESP_OK ? 0 : -1;
}
