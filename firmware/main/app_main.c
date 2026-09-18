#include "app_main.h"
#if !defined(NODE_HOST_TEST) && !defined(NODE_SIM_BUILD)
#include "node_provision.h"
#endif
#include "app_private.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "node_audio.h"
#include "node_config.h"
#include "node_display.h"
#include "node_health.h"
#include "node_led.h"
#include "node_log.h"
#include "node_mqtt.h"
#include "node_ota.h"
#include "node_ptt.h"
#include "node_voice.h"
#include "node_wifi.h"
#include "page.h"

static const char *TAG = "app";
int64_t s_press_us;

static void set_state(const char *state, node_led_mode_t led);
#if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
int node_app_test_pending(void);
size_t node_app_test_len(void);
void jobs_consume(void);
#endif

// Test entry points (host + linux sim). Production uses the same flow:
// press/release/report/run_upload are the real functions, not shadows.
// Only the job queue backend differs (tiny ring vs FreeRTOS queue).

#ifndef CONFIG_NODE_PTT_GPIO
#define CONFIG_NODE_PTT_GPIO 4
#endif // ifndef CONFIG_NODE_PTT_GPIO
#ifndef CONFIG_NODE_SILENCE_PEAK
#define CONFIG_NODE_SILENCE_PEAK 300
#endif // ifndef CONFIG_NODE_SILENCE_PEAK
#if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
#if defined(NODE_SIM_BUILD)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif // if defined(NODE_SIM_BUILD)
// Host/sim job ring: same depth-2 drop-oldest policy as the FreeRTOS queue.
#define TEST_JOBS 2
size_t s_job_len[TEST_JOBS];
int s_job_head, s_job_tail, s_job_n;
int s_test_cancelled;

static void enqueue_upload(size_t len, int16_t peak)
{
    (void)peak;
    if (s_job_n >= TEST_JOBS)
    {
        NODE_LOGE(TAG, "upload queue full, dropping %zu bytes", len);
        set_state("idle", NODE_LED_OFF);
        return;
    }
    s_job_len[s_job_tail] = len;
    s_job_tail = (s_job_tail + 1) % TEST_JOBS;
    s_job_n++;
}

static void jobs_drain(void)
{
    s_test_cancelled = 1;
    s_job_head = s_job_tail = s_job_n = 0;
}

#else
#include "esp_timer.h"
#if defined(NODE_SIM_BUILD)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#endif // if defined(NODE_SIM_BUILD)

#if !defined(NODE_SIM_BUILD)
static QueueHandle_t s_jobs;

typedef struct
{
    size_t len;
    int16_t peak;
} upload_job_t;
#endif // if !defined(NODE_SIM_BUILD)

static void enqueue_upload(size_t len, int16_t peak)
{
    upload_job_t job = {.len = len, .peak = peak};
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE)
    {
        NODE_LOGE(TAG, "upload queue full, dropping %zu bytes", len);
        set_state("idle", NODE_LED_OFF);
    }
}

static void jobs_drain(void)
{
    upload_job_t drain;
    while (xQueueReceive(s_jobs, &drain, 0) == pdTRUE)
    {
    }
}
#endif // if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)

void on_press(void)
{
    if (esp_timer_get_time() - s_press_us < 400000)
    {
        node_voice_cancel();
        NODE_LOGI(TAG, "ptt: double-press, cancel sent");
        s_press_us = 0;
        jobs_drain();
        return;
    }
    s_press_us = esp_timer_get_time();
    if (node_audio_start() != 0)
    {
        NODE_LOGE(TAG, "audio start failed");
        set_state("idle", NODE_LED_FAST_BLINK);
        return;
    }
    set_state("recording", NODE_LED_ON);
    NODE_LOGI(TAG, "ptt: recording...");
}

// Pipeline feed: one FULL half per call (half == 32 KB feed slice).
// The sealed tail half ends the stream (last=1).
typedef struct
{
    int sealed;
} upload_feed_t;

static int upload_feed(void *buf, size_t *len, int *last, void *ctx)
{
    upload_feed_t *f = ctx;
    const int16_t *pcm = NULL;
    size_t have = audio_half_take_full(&pcm);
    if (have == 0)
    {
        return -1;
    }
    size_t n = have < *len ? have : *len;
    memcpy(buf, pcm, n);
    *len = n;
    if (n >= have)
    {
        audio_half_mark_empty();
        const int16_t *peek = NULL;
        *last = f->sealed && audio_half_take_full(&peek) == 0;
    }
    else
    {
        *last = 0;
    }
    return 0;
}

int s_overrun_noted;

void report_result(const node_voice_result_t *r, int rc)
{
    set_state("idle", NODE_LED_OFF);
    if (rc == 0)
    {
        NODE_LOGI(TAG, "heard: %s", r->transcript);
        NODE_LOGI(TAG, "hermes: %s (stt=%dms h=%dms)", r->reply, r->stt_ms,
                  r->hermes_ms);
        node_health_note_latency(r->stt_ms, r->hermes_ms);
        node_display_transcript(r->transcript);
        if (s_overrun_noted)
        {
            s_overrun_noted = 0;
            NODE_LOGI(TAG, "overrun: oldest audio dropped, sent newer");
            char cut[NODE_VOICE_MAX_REPLY + 16];
            snprintf(cut, sizeof(cut), "(part) %s", r->reply);
            page_store(cut);
        }
        else
        {
            page_store(r->reply);
        }
    }
    else
    {
        NODE_LOGW(TAG, "voice failed err=%s msg=%s", r->error, r->reply);
        s_overrun_noted = 0;
        errors_show(r->error);
    }
}

void on_release(void)
{
    node_audio_stop();
    size_t len = node_audio_len();
    if (len < 3200)
    {
        // Tap: next page when several, keep the screen when one, ignore
        // when none. A tap never enqueues, so nothing to cancel.
        if (page_count() > 1)
        {
            page_next();
            return;
        }
        if (page_count() == 1)
        {
            return;
        }
        NODE_LOGI(TAG, "ptt: too short (%zu b), ignoring", len);
        set_state("idle", NODE_LED_OFF);
        return;
    }
    // Split-buffer pipeline: no total cap (halves ping-pong, overrun flag
    // sticks on slow networks). Truncation prefix now means overrun.
    s_overrun_noted = audio_half_overrun();
    int16_t peak = node_audio_peak();
    if (peak < CONFIG_NODE_SILENCE_PEAK)
    {
        NODE_LOGI(TAG, "ptt: silence (peak=%d), skipping upload", peak);
        set_state("idle", NODE_LED_OFF);
        return;
    }
    set_state("uploading", NODE_LED_SLOW_BLINK);
    node_health_note_ptt(true);
    enqueue_upload(len, peak);
}

void run_upload(size_t len)
{
    (void)len;
    node_voice_result_t r;
    // Seal the partial tail half so the feed sees a bounded stream.
    // Short clips (< 1 half) drain through the same chunked path.
    audio_half_seal();
    upload_feed_t feed = {0};
    feed.sealed = 1;
    report_result(&r, node_voice_post_chunked(upload_feed, &feed, &r));
}

#if !defined(NODE_HOST_TEST) && !defined(NODE_SIM_BUILD)
static void worker(void *arg)
{
    (void)arg;
    upload_job_t job;
    for (;;)
    {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        run_upload(job.len);
    }
}
#elif defined(NODE_SIM_BUILD)
static void worker(void *arg)
{
    (void)arg;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (node_app_test_pending() > 0)
        {
            size_t pending = node_app_test_len();
            jobs_consume();
            run_upload(pending);
        }
    }
}
#endif // if !defined(NODE_HOST_TEST) && !defined(NODE_SIM_BUILD)



static void set_state(const char *state, node_led_mode_t led)
{
    node_mqtt_publish_state(state);
    node_led_set(led);
    if (strcmp(state, "recording") == 0)
    {
        node_display_state(NODE_SCREEN_RECORDING);
    }
    else if (strcmp(state, "uploading") == 0 || strcmp(state, "working") == 0)
    {
        node_display_state(NODE_SCREEN_UPLOADING);
    }
    else
    {
        node_display_state(NODE_SCREEN_READY);
    }
}

static void on_ptt(bool active, void *ctx)
{
    (void)ctx;
    if (active)
    {
        on_press();
    }
    else
    {
        on_release();
    }
}

void app_main_run(void)
{
    NODE_LOGI(TAG, "voice-node starting");

    if (node_config_init() != 0)
    {
        NODE_LOGE(TAG, "config init failed, using Kconfig fallbacks");
    }

#if !defined(NODE_HOST_TEST) && !defined(NODE_SIM_BUILD)
    // Unprovisioned factory device: BLE service takes over boot until
    // creds arrive (then reboots). Provisioned devices skip instantly.
    node_provision_run();
#endif

    if (node_health_init() != 0)
    {
        NODE_LOGE(TAG, "health timer failed, continuing without telemetry");
    }

    int net_ok = 1;
#if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
    extern void node_boot_test_record(int net, int ptt);
#endif // if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
#ifdef CONFIG_NODE_WIFI_ENABLED
    if (node_wifi_init() != 0 || node_wifi_wait_connected(15000) != 0)
    {
        NODE_LOGE(TAG, "wifi failed, continuing offline (ptt still runs)");
        net_ok = 0;
    }
#else
#if CONFIG_ETH_USE_OPENETH
    // QEMU bench: virtual Ethernet (open_eth) instead of WiFi radio.
    // Same wait_connected contract, so the rest of boot is unchanged.
    if (node_eth_init() != 0 || node_wifi_wait_connected(15000) != 0)
    {
        NODE_LOGE(TAG, "eth failed, continuing offline (ptt still runs)");
        net_ok = 0;
    }
#else
    NODE_LOGI(TAG, "wifi disabled by config (QEMU/offline mode)");
    if (node_net_init() != 0)
    {
        NODE_LOGE(TAG, "net stack failed, uploads will fail");
    }
#endif // if CONFIG_ETH_USE_OPENETH
#endif // ifdef CONFIG_NODE_WIFI_ENABLED

    // Audio uses static halves (BSS, link-time): init cannot fail for
    // lack of heap. I2S setup can still fail without a mic, hence the guard.
    int audio_ok = node_audio_init() == 0;
    if (!audio_ok)
    {
        NODE_LOGE(TAG, "audio buffer unavailable, ptt disabled");
    }

    node_led_init();
    node_display_init();
    node_display_state(NODE_SCREEN_BOOT);
    node_led_set(NODE_LED_FAST_BLINK);
    node_ota_init();
    node_mqtt_init();
    node_mqtt_start();
    if (!audio_ok)
    {
        set_state("idle", NODE_LED_FAST_BLINK);
    }

    node_ptt_config_t ptt_cfg = {
        .gpio = CONFIG_NODE_PTT_GPIO,
#ifdef CONFIG_NODE_PTT_ACTIVE_LOW
        .active_low = true,
#else
        .active_low = false,
#endif // ifdef CONFIG_NODE_PTT_ACTIVE_LOW
        .debounce_ms = 200,
        .on_change = on_ptt,
    };
    int ptt_ok = node_ptt_init(&ptt_cfg) == 0 && node_ptt_start() == 0;
#if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
    node_boot_test_record(net_ok, ptt_ok);
#endif // if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
    if (!ptt_ok)
    {
        NODE_LOGE(TAG, "ptt init failed, input dead");
        node_display_state(NODE_SCREEN_ERROR);
        node_display_reply("button broken");
        node_led_set(NODE_LED_FAST_BLINK);
        return;
    }
#if !defined(NODE_HOST_TEST) && !defined(NODE_SIM_BUILD)
    s_jobs = xQueueCreate(2, sizeof(upload_job_t));
    static StaticTask_t worker_tcb;
    static StackType_t worker_stack[8192 / sizeof(StackType_t)];
    xTaskCreateStatic(worker, "voice_up", 8192, NULL, 5, worker_stack, &worker_tcb);
#elif defined(NODE_SIM_BUILD)
    xTaskCreate(worker, "voice_up", 8192, NULL, 5, NULL);
#endif // if !defined(NODE_HOST_TEST) && !defined(NODE_SIM_BUILD)

    set_state("idle", NODE_LED_OFF);
#ifdef CONFIG_NODE_WIFI_ENABLED
    node_display_net(node_config_device_id(), node_wifi_rssi_dbm());
#else
    node_display_net(node_config_device_id(), 0);
#endif // ifdef CONFIG_NODE_WIFI_ENABLED
    if (!net_ok)
    {
        node_display_reply("no wifi, offline");
    }
    NODE_LOGI(TAG, "ready. device_id=%s", node_config_device_id());
    printf("voice-node ready\n");
}
