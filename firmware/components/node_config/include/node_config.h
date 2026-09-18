#pragma once

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

int node_config_init(void);

const char *node_config_device_id(void);
const char *node_config_wifi_ssid(void);
const char *node_config_wifi_pass(void);
const char *node_config_mqtt_uri(void);
const char *node_config_gateway_url(void);
const char *node_config_device_token(void);
int node_config_telemetry_interval_s(void);
int node_config_is_provisioned(void);

int node_config_set_wifi(const char *ssid, const char *pass);
int node_config_set_mqtt_uri(const char *uri);
int node_config_set_device_id(const char *id);
int node_config_set_gateway(const char *url, const char *token);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
