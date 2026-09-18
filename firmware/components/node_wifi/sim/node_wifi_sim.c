#include "node_wifi.h"
#include <stdio.h>
#ifdef NODE_HOST_TEST
static void sim_net_up(void) {}
#else
#include "esp_event.h"
#include "esp_netif.h"
static void sim_net_up(void) { esp_netif_init(); esp_event_loop_create_default(); }
#endif // ifdef NODE_HOST_TEST
int node_net_init(void) { sim_net_up(); return 0; }
int node_wifi_init(void) {
    printf("[sim] wifi init (no radio)\n");
    sim_net_up();
    return 0;
}
static int s_fail_connect;
void node_wifi_test_fail(int fail) { s_fail_connect = fail; }
int node_wifi_wait_connected(int ms)
{
    (void)ms;
    return s_fail_connect ? -1 : 0;
}
int node_wifi_rssi_dbm(void) { return -60; }
