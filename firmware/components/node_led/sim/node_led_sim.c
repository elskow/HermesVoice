#include "node_led.h"
#include <stdio.h>
static node_led_mode_t s_m;
int node_led_init(void) { return 0; }
void node_led_set(node_led_mode_t m) { s_m = m; printf("[led] %d\n", (int)m); }
node_led_mode_t node_led_test_mode(void) { return s_m; }
