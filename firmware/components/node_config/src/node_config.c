#include "node_config.h"
#include "node_log.h"

#ifndef NODE_HOST_TEST
#include "nvs.h"
#include "nvs_flash.h"
#endif // ifndef NODE_HOST_TEST

#include <string.h>

#ifndef NODE_HOST_TEST
static const char *TAG = "node_config";
static const char *NVS_NS = "node";
#endif // ifndef NODE_HOST_TEST

typedef struct {
    const char *key;
    char *cache;
    size_t cap;
} str_field_t;

static char s_device_id[64] = CONFIG_NODE_DEVICE_ID;
static char s_wifi_ssid[64] = CONFIG_NODE_WIFI_SSID;
static char s_wifi_pass[128] = CONFIG_NODE_WIFI_PASS;
static char s_mqtt_uri[192] = CONFIG_NODE_MQTT_URI;
static char s_gateway_url[192] = CONFIG_NODE_GATEWAY_URL;
static char s_device_token[128] = "";

static str_field_t str_fields[] = {
    {"device_id", s_device_id, sizeof(s_device_id)},
    {"wifi_ssid", s_wifi_ssid, sizeof(s_wifi_ssid)},
    {"wifi_pass", s_wifi_pass, sizeof(s_wifi_pass)},
    {"mqtt_uri", s_mqtt_uri, sizeof(s_mqtt_uri)},
    {"gateway_url", s_gateway_url, sizeof(s_gateway_url)},
    {"device_token", s_device_token, sizeof(s_device_token)},
};
static int s_telemetry_interval = CONFIG_NODE_TELEMETRY_INTERVAL_S;

#ifndef NODE_HOST_TEST
static void read_str(nvs_handle_t h, const str_field_t *f)
{
    size_t len = f->cap;
    if (nvs_get_str(h, f->key, f->cache, &len) == ESP_OK) {
        f->cache[f->cap - 1] = '\0';
    }
}
#endif // ifndef NODE_HOST_TEST

int node_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        NODE_LOGE(TAG, "nvs init failed: %d", err);
        return -1;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        NODE_LOGI(TAG, "no provisioned NVS, using Kconfig fallbacks");
        return 0;
    }
    for (size_t i = 0; i < sizeof(str_fields) / sizeof(str_fields[0]); i++) {
        read_str(h, &str_fields[i]);
    }
    int32_t iv = 0;
    if (nvs_get_i32(h, "telemetry_s", &iv) == ESP_OK && iv >= 5 && iv <= 3600) {
        s_telemetry_interval = (int)iv;
    }
    nvs_close(h);
    NODE_LOGI(TAG, "config loaded: device=%s mqtt=%s", s_device_id, s_mqtt_uri);
    return 0;
}

const char *node_config_device_id(void) { return s_device_id; }
const char *node_config_wifi_ssid(void) { return s_wifi_ssid; }
const char *node_config_wifi_pass(void) { return s_wifi_pass; }
const char *node_config_mqtt_uri(void) { return s_mqtt_uri; }
const char *node_config_gateway_url(void) { return s_gateway_url; }
const char *node_config_device_token(void) { return s_device_token; }
int node_config_telemetry_interval_s(void) { return s_telemetry_interval; }

// Provisioned = NVS holds wifi creds. The Kconfig default SSID counts as
// unprovisioned: a factory device must enter BLE provisioning, never try
// to join the placeholder network.
int node_config_is_provisioned(void)
{
#ifndef NODE_HOST_TEST
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
    {
        return 0;
    }
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, "wifi_ssid", NULL, &len);
    nvs_close(h);
    return err == ESP_OK && len > 1;
#else
    return s_wifi_ssid[0] != '\0';
#endif
}

static int write_field(const str_field_t *f, const char *val)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    esp_err_t err = nvs_set_str(h, f->key, val);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        strncpy(f->cache, val, f->cap - 1);
        f->cache[f->cap - 1] = '\0';
    }
    return err == ESP_OK ? 0 : -1;
}

static const str_field_t *find_field(const char *key)
{
    for (size_t i = 0; i < sizeof(str_fields) / sizeof(str_fields[0]); i++) {
        if (strcmp(str_fields[i].key, key) == 0) {
            return &str_fields[i];
        }
    }
    return NULL;
}

static int write_str(const char *key, const char *val)
{
    const str_field_t *f = find_field(key);
    return f ? write_field(f, val) : -1;
}

int node_config_set_wifi(const char *ssid, const char *pass)
{
    if (!ssid || !pass) {
        return -1;
    }
    if (write_str("wifi_ssid", ssid) != 0) {
        return -1;
    }
    return write_str("wifi_pass", pass);
}

int node_config_set_mqtt_uri(const char *uri)
{
    if (!uri) {
        return -1;
    }
    return write_str("mqtt_uri", uri);
}

int node_config_set_device_id(const char *id)
{
    if (!id) {
        return -1;
    }
    return write_str("device_id", id);
}

int node_config_set_gateway(const char *url, const char *token)
{
    if (!url || !token) {
        return -1;
    }
    if (write_str("gateway_url", url) != 0) {
        return -1;
    }
    return write_str("device_token", token);
}
