#include "node_mqtt.h"
#include "node_config.h"
#include <stdio.h>
static int s_up = 1;
int node_mqtt_init(void) { return 0; }
int node_mqtt_start(void) { s_up = 1; return 0; }
int node_mqtt_publish_event(bool active) { printf("[mqtt] event=%d\n", active); return 0; }
int node_mqtt_publish_telemetry(const char *j) { printf("[mqtt] telemetry\n"); (void)j; return 0; }
int node_mqtt_publish_state(const char *s) { printf("[mqtt] state=%s\n", s); return 0; }
bool node_mqtt_is_connected(void) { return s_up; }
