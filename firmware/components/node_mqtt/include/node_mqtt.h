#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

int node_mqtt_init(void);
int node_mqtt_start(void);
int node_mqtt_publish_state(const char *state);
int node_mqtt_publish_telemetry(const char *json);
bool node_mqtt_is_connected(void);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
