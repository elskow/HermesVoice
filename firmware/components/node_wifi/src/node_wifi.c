#include "node_wifi.h"
#include "node_config.h"
#include "node_log.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "node_wifi";
static EventGroupHandle_t s_events;
static int s_rssi = 0;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define MAX_RETRY 10

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    static int retries = 0;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (retries < MAX_RETRY)
        {
            esp_wifi_connect();
            retries++;
            NODE_LOGI(TAG, "retry %d/%d", retries, MAX_RETRY);
        }
        else
        {
            xEventGroupSetBits(s_events, WIFI_FAIL_BIT);
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        retries = 0;
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        NODE_LOGI(TAG, "connected ssid=%s", node_config_wifi_ssid());
    }
}

int node_net_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    return 0;
}

int node_wifi_init(void)
{
#if !CONFIG_NODE_WIFI_ENABLED
    (void)0;
    return -1;
#else
    s_events = xEventGroupCreate();
    node_net_init();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL);

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, node_config_wifi_ssid(), sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, node_config_wifi_pass(), sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_start();
    NODE_LOGI(TAG, "started, connecting...");
    return 0;
#endif // if !CONFIG_NODE_WIFI_ENABLED
}

int node_wifi_wait_connected(int timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, timeout_ms / portTICK_PERIOD_MS);
    if (bits & WIFI_CONNECTED_BIT)
    {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        {
            s_rssi = ap.rssi;
        }
        return 0;
    }
    NODE_LOGE(TAG, "connection timeout/fail");
    return -1;
}

int node_wifi_rssi_dbm(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    {
        s_rssi = ap.rssi;
    }
    return s_rssi;
}

#if CONFIG_ETH_USE_OPENETH
#include "esp_eth.h"
#include "esp_eth_mac_openeth.h"
#define ETH_CONNECTED_BIT BIT2

static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == ETH_EVENT && id == ETHERNET_EVENT_CONNECTED)
    {
        xEventGroupSetBits(s_events, ETH_CONNECTED_BIT);
        NODE_LOGI(TAG, "eth link up (QEMU open_eth)");
    }
    else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP)
    {
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        NODE_LOGI(TAG, "eth got ip");
    }
}

// QEMU path: virtual Ethernet instead of WiFi radio. Same wait/rssi
// contract as WiFi so app_main is unchanged (only the init call differs).
int node_eth_init(void)
{
    s_events = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_cfg);
    if (!mac)
    {
        return -1;
    }
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_cfg);
    if (!phy)
    {
        mac->del(mac);
        return -1;
    }
    esp_eth_handle_t eth = NULL;
    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
    if (esp_eth_driver_install(&cfg, &eth) != ESP_OK)
    {
        mac->del(mac);
        phy->del(phy);
        return -1;
    }
    esp_netif_config_t net_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&net_cfg);
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth);
    if (esp_netif_attach(netif, glue) != ESP_OK)
    {
        return -1;
    }
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_eth_event, NULL);
    if (esp_eth_start(eth) != ESP_OK)
    {
        return -1;
    }
    NODE_LOGI(TAG, "eth started, waiting for ip...");
    return 0;
}
#else
int node_eth_init(void) { return -1; }
#endif // if CONFIG_ETH_USE_OPENETH
