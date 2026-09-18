#include "node_ptt.h"
#include "node_log.h"

static const char *TAG = "node_ptt";

#ifndef NODE_HOST_TEST
#include "driver/gpio.h"
#include "esp_timer.h"
#endif // ifndef NODE_HOST_TEST

static node_ptt_config_t s_cfg;
static bool s_active = false;

bool node_ptt_fold(bool *reported, int *stable_ms, bool raw_active,
                         int sample_period_ms, int debounce_ms)
{
    if (raw_active == *reported) {
        *stable_ms = 0;
        return false;
    }
    *stable_ms += sample_period_ms;
    if (*stable_ms >= debounce_ms) {
        *reported = raw_active;
        *stable_ms = 0;
        return true;
    }
    return false;
}

int node_ptt_init(const node_ptt_config_t *cfg)
{
    if (!cfg || !cfg->on_change || cfg->debounce_ms == 0) {
        return -1;
    }
    s_cfg = *cfg;
    s_active = false;
#ifndef NODE_HOST_TEST
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << cfg->gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = cfg->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = cfg->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io) != ESP_OK) {
        NODE_LOGE(TAG, "gpio %d config failed", cfg->gpio);
        return -1;
    }
#endif // ifndef NODE_HOST_TEST
    NODE_LOGI(TAG, "init gpio=%d active_low=%d debounce=%lums", cfg->gpio,
              cfg->active_low, (unsigned long)cfg->debounce_ms);
    return 0;
}


static esp_timer_handle_t s_timer;
static int s_stable_ms = 0;
#define SAMPLE_MS 50

static void poll(void *arg)
{
    (void)arg;
    int level = gpio_get_level(s_cfg.gpio);
    bool raw = s_cfg.active_low ? (level == 0) : (level == 1);
    if (node_ptt_fold(&s_active, &s_stable_ms, raw, SAMPLE_MS, (int)s_cfg.debounce_ms)) {
        s_cfg.on_change(s_active, NULL);
    }
}

int node_ptt_start(void)
{
    if (s_timer) {
        return 0;
    }
    esp_timer_create_args_t args = {
        .callback = poll,
        .name = "ptt_poll",
    };
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        return -1;
    }
    return esp_timer_start_periodic(s_timer, SAMPLE_MS * 1000) == ESP_OK ? 0 : -1;
}

bool node_ptt_is_active(void)
{
    return s_active;
}
