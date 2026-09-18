#include "node_audio.h"
#include "node_log.h"

#include <stdlib.h>
#include <string.h>

#ifndef NODE_HOST_TEST
#include "driver/i2s_std.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif // ifndef NODE_HOST_TEST

// Split-buffer pipeline. Static halves, no heap:
// 2 x 32 KB BSS ping-ponged between recorder (FILLING) and uploader (FULL).
// Exactly one writer per half at any time; EMPTY is the transfer state.
typedef struct
{
    int16_t buf[NODE_AUDIO_HALF_BYTES / 2];
    size_t len_bytes;
    audio_half_state_t state;
} audio_half_t;

static audio_half_t s_halves[NODE_AUDIO_HALVES];
static int s_fill_idx;
static int s_drain_idx;
static size_t s_total_bytes;
static int s_running;
static int s_overrun;
#ifndef NODE_HOST_TEST
static i2s_chan_handle_t s_rx;
static int s_stop_req;
#endif // ifndef NODE_HOST_TEST

size_t node_audio_bytes_for_ms(int ms)
{
    if (ms <= 0)
    {
        return 0;
    }
    size_t need = (size_t)ms * NODE_AUDIO_SAMPLE_RATE * 2 / 1000;
    return need;
}

#ifndef NODE_HOST_TEST
static int s_i2s_ready;
#endif // ifndef NODE_HOST_TEST

// Largest single object is one half (BSS, link-time): no heap guard
// needed. The old 320 KB malloc + largest-run check is gone with s_buf.
int node_audio_init(void)
{
#ifndef NODE_HOST_TEST
    if (s_i2s_ready)
    {
        return 0;
    }
    i2s_chan_handle_t rx = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, NULL, &rx) != ESP_OK)
    {
        return -1;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(NODE_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_NODE_MIC_SCK_GPIO,
            .ws = CONFIG_NODE_MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = CONFIG_NODE_MIC_SD_GPIO,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };
    if (i2s_channel_init_std_mode(rx, &std_cfg) != ESP_OK)
    {
        return -1;
    }
    s_rx = rx;
    s_i2s_ready = 1;
#endif // ifndef NODE_HOST_TEST
    return 0;
}

static void halves_reset(void)
{
    for (int i = 0; i < NODE_AUDIO_HALVES; i++)
    {
        s_halves[i].len_bytes = 0;
        s_halves[i].state = HALF_EMPTY;
    }
    s_fill_idx = 0;
    s_drain_idx = 0;
    s_total_bytes = 0;
    s_overrun = 0;
}

// Append bytes to the FILLING half; on full, flip to FULL and advance.
// If the next half is still FULL (uploader stalled), reuse the oldest FULL
// half (overrun): audio stays live, flag sticks for the reply prefix.
static void halves_push(const char *src, size_t n)
{
    while (n > 0)
    {
        audio_half_t *h = &s_halves[s_fill_idx];
        if (h->state == HALF_FULL)
        {
            // Wrapped onto an undrained half: oldest audio drops, flag sticks.
            s_overrun = 1;
            s_drain_idx = s_fill_idx;
            h->len_bytes = 0;
            h->state = HALF_EMPTY;
        }
        if (h->state == HALF_EMPTY)
        {
            h->state = HALF_FILLING;
            h->len_bytes = 0;
        }
        size_t room = NODE_AUDIO_HALF_BYTES - h->len_bytes;
        size_t w = n < room ? n : room;
        memcpy((char *)h->buf + h->len_bytes, src, w);
        src += w;
        n -= w;
        h->len_bytes += w;
        s_total_bytes += w;
        if (h->len_bytes >= NODE_AUDIO_HALF_BYTES)
        {
            h->state = HALF_FULL;
            s_fill_idx = (s_fill_idx + 1) % NODE_AUDIO_HALVES;
        }
    }
}

static void reader(void *arg)
{
    (void)arg;
    static char dma[4096];
    while (!s_stop_req)
    {
        size_t n = 0;
        if (i2s_channel_read(s_rx, dma, sizeof(dma), &n, 100) != ESP_OK)
        {
            break;
        }
        if (n > 0)
        {
            halves_push(dma, n);
        }
    }
    s_running = 0;
    vTaskDelete(NULL);
}

int node_audio_start(void)
{
    if (!s_rx)
    {
        return -1;
    }
    halves_reset();
    s_stop_req = 0;
    s_running = 1;
    i2s_channel_enable(s_rx);
    static StaticTask_t reader_tcb;
    static StackType_t reader_stack[4096 / sizeof(StackType_t)];
    xTaskCreateStatic(reader, "audio_rd", 4096, NULL, 5, reader_stack, &reader_tcb);
    return 0;
}

int node_audio_stop(void)
{
    if (!s_running)
    {
        return 0;
    }
    s_stop_req = 1;
    for (int i = 0; i < 50 && s_running; i++)
    {
        vTaskDelay(5);
    }
    i2s_channel_disable(s_rx);
    s_running = 0;
    return 0;
}

size_t node_audio_len(void)
{
    return s_total_bytes;
}

static int16_t peak_of(const int16_t *p, size_t n)
{
    int16_t peak = 0;
    for (size_t i = 0; i < n; i++)
    {
        int16_t v = p[i] < 0 ? -p[i] : p[i];
        if (v > peak)
        {
            peak = v;
        }
    }
    return peak;
}

int16_t node_audio_peak(void)
{
    int16_t peak = 0;
    for (int i = 0; i < NODE_AUDIO_HALVES; i++)
    {
        size_t n = s_halves[i].len_bytes / 2;
        if (n > 0)
        {
            int16_t p = peak_of(s_halves[i].buf, n);
            if (p > peak)
            {
                peak = p;
            }
        }
    }
    return peak;
}

size_t audio_half_take_full(const int16_t **out)
{
    audio_half_t *h = &s_halves[s_drain_idx];
    if (h->state != HALF_FULL)
    {
        return 0;
    }
    *out = h->buf;
    return h->len_bytes;
}

void audio_half_mark_empty(void)
{
    audio_half_t *h = &s_halves[s_drain_idx];
    h->len_bytes = 0;
    h->state = HALF_EMPTY;
    s_drain_idx = (s_drain_idx + 1) % NODE_AUDIO_HALVES;
}

size_t audio_half_seal(void)
{
    audio_half_t *h = &s_halves[s_fill_idx];
    if (h->state == HALF_FILLING && h->len_bytes > 0)
    {
        h->state = HALF_FULL;
        return h->len_bytes;
    }
    return 0;
}

int audio_half_overrun(void)
{
    return s_overrun;
}
