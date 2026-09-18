#pragma once

#ifdef __cplusplus
extern "C"
{
#endif // ifdef __cplusplus

    int node_net_init(void);
    int node_wifi_init(void);
    int node_wifi_wait_connected(int timeout_ms);
    int node_wifi_rssi_dbm(void);

    // QEMU/open_eth path: same wait_connected/rssi contract over virtual Ethernet.
    // Host-test stub lives in node_wifi.c too (returns 0 like the rest).
    int node_eth_init(void);

#if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
    void node_wifi_test_fail(int fail);
#endif // if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
