#pragma once

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

// BLE provisioning owner. Call once at boot before node_wifi_init: if the
// device has no WiFi creds in NVS, starts the Espressif wifi_prov_mgr BLE
// service (service name HERMES-<mac>); the phone app / esp_prov script
// delivers SSID+pass, then our custom "node-cfg" endpoint delivers gateway
// URL + device token. On WIFI_PROV_CRED_SUCCESS the service stops, BTDM
// memory is freed, and boot continues into the normal WiFi path.
// Returns 1 if provisioning ran (caller should reboot after success),
// 0 if already provisioned (boot continues), <0 on error (boot continues
// offline; Kconfig fallbacks still apply).
int node_provision_run(void);

// Custom endpoint payload (plain text, one per line):
//   gateway_url=<url>
//   device_token=<token>
//   device_id=<id>        (optional; keeps Kconfig default otherwise)
int node_provision_apply(const char *payload, int len);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
