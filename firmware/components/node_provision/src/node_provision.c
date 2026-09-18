#include "node_provision.h"
#include "node_config.h"
#include "node_log.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "provision";

// Plain-text endpoint parser shared by the BLE handler and host tests.
// One key=value per line; unknown keys ignored so the phone app can grow
// new fields without breaking old firmware.
int node_provision_apply(const char *payload, int len)
{
    if (!payload || len <= 0)
    {
        return -1;
    }
    char *buf = malloc((size_t)len + 1);
    if (!buf)
    {
        return -1;
    }
    memcpy(buf, payload, (size_t)len);
    buf[len] = '\0';
    char gw[192] = {0}, tok[160] = {0}, id[40] = {0};
    for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n"))
    {
        char *eq = strchr(line, '=');
        if (!eq || eq == line)
        {
            continue;
        }
        *eq = '\0';
        const char *v = eq + 1;
        if (strcmp(line, "gateway_url") == 0)
        {
            strncpy(gw, v, sizeof(gw) - 1);
        }
        else if (strcmp(line, "device_token") == 0)
        {
            strncpy(tok, v, sizeof(tok) - 1);
        }
        else if (strcmp(line, "device_id") == 0)
        {
            strncpy(id, v, sizeof(id) - 1);
        }
    }
    free(buf);
    if (!gw[0])
    {
        NODE_LOGE(TAG, "node-cfg without gateway_url");
        return -1;
    }
    if (id[0] && node_config_set_device_id(id) != 0)
    {
        return -1;
    }
    return node_config_set_gateway(gw, tok);
}

#ifndef NODE_HOST_TEST
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"

#define PROV_SERVICE_PREFIX "HERMES-"

static void prov_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == NETWORK_PROV_EVENT && id == NETWORK_PROV_WIFI_CRED_SUCCESS)
    {
        NODE_LOGI(TAG, "wifi creds received");
    }
}

// GATT endpoint behind the network_prov_mgr "node-cfg" name. Payload format
// is documented in node_provision.h; reply is a short ASCII ack.
static esp_err_t node_cfg_handler(uint32_t session, const uint8_t *in, ssize_t inlen,
                                  uint8_t **out, ssize_t *outlen, void *priv)
{
    (void)session;
    (void)priv;
    esp_err_t rc = ESP_FAIL;
    if (in && inlen > 0 && node_provision_apply((const char *)in, (int)inlen) == 0)
    {
        static const char ok[] = "OK";
        *out = malloc(sizeof(ok));
        if (*out)
        {
            memcpy(*out, ok, sizeof(ok));
            *outlen = sizeof(ok);
            rc = ESP_OK;
        }
    }
    if (rc != ESP_OK)
    {
        static const char err[] = "ERR need gateway_url=";
        *out = malloc(sizeof(err));
        if (*out)
        {
            memcpy(*out, err, sizeof(err));
            *outlen = sizeof(err);
        }
    }
    return rc;
}

int node_provision_run(void)
{
    // Already provisioned? Boot continues untouched (normal WiFi path).
    if (node_config_is_provisioned())
    {
        return 0;
    }
#ifndef CONFIG_NODE_WIFI_ENABLED
    // Radio-less builds (QEMU): BLE provisioning is meaningless without a
    // WiFi STA to configure. Skip instantly, boot continues offline.
    return 0;
#endif

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK)
    {
        return -1;
    }
    network_prov_mgr_config_t pcfg = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };
    if (network_prov_mgr_init(pcfg) != ESP_OK)
    {
        return -1;
    }
    if (esp_event_handler_register(NETWORK_PROV_EVENT, NETWORK_PROV_WIFI_CRED_SUCCESS,
                                   prov_event, NULL) != ESP_OK)
    {
        network_prov_mgr_deinit();
        return -1;
    }
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char service[16];
    snprintf(service, sizeof(service), "%s%02X%02X%02X",
             PROV_SERVICE_PREFIX, mac[3], mac[4], mac[5]);
    NODE_LOGI(TAG, "unprovisioned, advertising BLE as %s", service);
    if (network_prov_mgr_endpoint_create("node-cfg") != ESP_OK ||
        network_prov_mgr_endpoint_register("node-cfg", node_cfg_handler, NULL) != ESP_OK)
    {
        network_prov_mgr_deinit();
        return -1;
    }
    // Security 1 (proof-of-possession). POP = service name on the OLED, so
    // physical possession proves ownership. No POP = anyone in BLE range
    // provisions your device.
    if (network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, service,
                                         service, service) != ESP_OK)
    {
        network_prov_mgr_deinit();
        return -1;
    }
    // Block boot until creds arrive or the operator gives up (10 min).
    // The PTT worker is not started yet, so nothing else needs the CPU.
    // write_field updates the RAM cache on success, so the getter below
    // goes live the moment provisioning writes (no NVS re-read needed).
    for (int i = 0; i < 600; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (node_config_is_provisioned())
        {
            break;
        }
    }
    if (!node_config_is_provisioned())
    {
        network_prov_mgr_stop_provisioning();
    }
    network_prov_mgr_wait();
    network_prov_mgr_deinit();
    if (!node_config_is_provisioned())
    {
        NODE_LOGW(TAG, "prov timeout, continuing offline (Kconfig fallbacks)");
        return -1;
    }
    NODE_LOGI(TAG, "provisioned, rebooting into normal boot");
    esp_restart();
    return 1;
}
#else
int node_provision_run(void)
{
    return 0;
}
#endif
