#pragma once

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

typedef enum {
    NODE_LED_OFF,
    NODE_LED_ON,
    NODE_LED_SLOW_BLINK,
    NODE_LED_FAST_BLINK,
} node_led_mode_t;

int node_led_init(void);
void node_led_set(node_led_mode_t mode);
node_led_mode_t node_led_test_mode(void);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
