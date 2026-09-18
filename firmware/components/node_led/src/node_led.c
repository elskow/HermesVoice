#include "node_led.h"
#include "node_log.h"

#include "driver/gpio.h"
#include "esp_timer.h"
static esp_timer_handle_t s_timer;
static node_led_mode_t s_mode = NODE_LED_OFF;
static int s_level;

static void toggle(void *arg)
{
    (void)arg;
    s_level = !s_level;
    gpio_set_level(CONFIG_NODE_LED_GPIO, s_level);
}

int node_led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_NODE_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io) != ESP_OK) {
        return -1;
    }
    if (s_timer) {
        return 0;
    }
    esp_timer_create_args_t args = {.callback = toggle, .name = "led"};
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        return -1;
    }
    return 0;
}

node_led_mode_t node_led_test_mode(void)
{
    return s_mode;
}

void node_led_set(node_led_mode_t mode)
{
    s_mode = mode;
    esp_timer_stop(s_timer);
    switch (mode) {
    case NODE_LED_OFF:
        gpio_set_level(CONFIG_NODE_LED_GPIO, 0);
        break;
    case NODE_LED_ON:
        gpio_set_level(CONFIG_NODE_LED_GPIO, 1);
        break;
    case NODE_LED_SLOW_BLINK:
        esp_timer_start_periodic(s_timer, 500000);
        break;
    case NODE_LED_FAST_BLINK:
        esp_timer_start_periodic(s_timer, 100000);
        break;
    }
}

