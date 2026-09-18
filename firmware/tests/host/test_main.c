#include "node_audio.h"
#include "node_config.h"
#include "node_provision.h"
#include "node_ota.h"
#include "node_led.h"
#include "node_display.h"
#include "node_ptt.h"
#include "node_health.h"
#include "node_mqtt.h"
#include "node_voice.h"
#include "app_test.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int s_cb_count;
static bool s_cb_last;

static void on_change(bool active, void *ctx)
{
    (void)ctx;
    s_cb_count++;
    s_cb_last = active;
}

static void test_debounce_filters_glitch(void)
{
    bool reported = false;
    int stable = 0;
    assert(!node_ptt_fold(&reported, &stable, true, 50, 200));
    assert(!node_ptt_fold(&reported, &stable, false, 50, 200));
    assert(!reported);
    printf("ok: glitch filtered\n");
}

static void test_debounce_reports_stable(void)
{
    bool reported = false;
    int stable = 0;
    bool changed = false;
    for (int i = 0; i < 4; i++)
    {
        changed = node_ptt_fold(&reported, &stable, true, 50, 200);
    }
    assert(changed && reported);
    printf("ok: stable ptt reported\n");
}

static void test_ptt_callback_path(void)
{
    s_cb_count = 0;
    node_ptt_config_t cfg = {
        .gpio = 4,
        .active_low = true,
        .debounce_ms = 200,
        .on_change = on_change,
    };
    assert(node_ptt_init(&cfg) == 0);
    for (int i = 0; i < 5; i++)
    {
        node_ptt_test_inject(true);
    }
    assert(s_cb_count == 1 && s_cb_last == true);
    printf("ok: callback fires once per transition\n");
}

static void test_config_provisioning(void)
{
    assert(node_config_init() == 0);
    assert(node_config_set_device_id("node-42") == 0);
    assert(strcmp(node_config_device_id(), "node-42") == 0);
    assert(node_config_set_wifi("ssid", "pass") == 0);
    assert(node_config_set_mqtt_uri("mqtt://x:1883") == 0);
    assert(node_config_set_wifi(NULL, "x") == -1);
    assert(node_config_is_provisioned() == 1);
    printf("ok: config provisioning + NULL guards\n");
}

static void test_provision_apply(void)
{
    // BLE endpoint payload: gateway required, token optional-ish (empty
    // string when open relay), unknown keys ignored, device_id optional.
    assert(node_provision_apply(NULL, 0) == -1);
    assert(node_provision_apply("device_token=x\n", 14) == -1);
    assert(node_provision_apply("gateway_url=http://r:8081\ndevice_token=t\n", 44) == 0);
    assert(strcmp(node_config_gateway_url(), "http://r:8081") == 0);
    assert(node_provision_apply("gateway_url=http://s:8081\nfuture_field=z\n", 44) == 0);
    printf("ok: provision endpoint parser\n");
}

static void test_health_counts_only_active(void)
{
    unsigned long before = node_health_ptt_count();
    node_health_note_ptt(true);
    node_health_note_ptt(false);
    assert(node_health_ptt_count() == before + 1);
    printf("ok: health counts rising edges only\n");
}

static void test_mqtt_stub_never_blocks(void)
{
    assert(node_mqtt_init() == 0);
    assert(node_mqtt_publish_state("idle") == 0);
    assert(node_mqtt_publish_telemetry("{}") == 0);
    printf("ok: mqtt stub publishes without hardware\n");
}

static void test_audio_capture_and_gate(void)
{
    assert(node_audio_init() == 0);
    assert(node_audio_bytes_for_ms(1000) == 32000);
    assert(node_audio_bytes_for_ms(0) == 0);
    assert(node_audio_bytes_for_ms(60000) == (size_t)60000 * 32);
    assert(node_audio_start() == 0);
    static const int16_t loud[160] = {[0 ... 159] = 2000};
    node_audio_test_push(loud, 160);
    assert(node_audio_stop() == 0);
    assert(node_audio_len() == 320);
    assert(node_audio_peak() == 2000);
    assert(node_audio_start() == 0);
    for (int i = 0; i < 3100; i++)
    {
        node_audio_test_push(loud, 160);
    }
    assert(node_audio_stop() == 0);
    // 3100 x 320 B = 992000 B total through ping-pong halves (overrun keeps
    // audio live past 128 KB static). No heap: halves are BSS.
    assert(node_audio_len() == (size_t)3100 * 320);
    assert(node_audio_init() == 0);
    printf("ok: audio capture, silence peak, overflow clips at cap\n");
}

static void test_led_modes(void)
{
    assert(node_led_init() == 0);
    node_led_set(NODE_LED_ON);
    assert(node_led_test_mode() == NODE_LED_ON);
    node_led_set(NODE_LED_SLOW_BLINK);
    assert(node_led_test_mode() == NODE_LED_SLOW_BLINK);
    node_led_set(NODE_LED_OFF);
    assert(node_led_test_mode() == NODE_LED_OFF);
    printf("ok: led modes latch\n");
}

static int test_feed(void *buf, size_t *len, int *last, void *ctx)
{
    int *calls = ctx;
    memset(buf, 0xAB, *len);
    (*calls)++;
    *last = *calls >= 3;
    return 0;
}

static void test_feed_slicing(void)
{
    // 70000 B through CHUNK slices: all full except the last (tail), last
    // flag only on the final slice. Slice count derives from the header
    // constant so buffer-size changes stay green.
    static const char pcm[70000] = {[0 ... 69999] = 0xAB};
    char buf[NODE_VOICE_CHUNK_BYTES];
    size_t off = 0, nslices = 0, last_at = 0;
    size_t first = 0, last_n = 0;
    int last = 0;
    while (off < sizeof(pcm))
    {
        size_t room = sizeof(pcm) - off;
        size_t n = room > sizeof(buf) ? sizeof(buf) : room;
        memcpy(buf, pcm + off, n);
        off += n;
        last = off >= sizeof(pcm);
        if (nslices == 0)
        {
            first = n;
        }
        last_n = n;
        last_at = nslices;
        nslices++;
    }
    size_t expect_full = sizeof(pcm) / NODE_VOICE_CHUNK_BYTES;
    size_t expect_tail = sizeof(pcm) % NODE_VOICE_CHUNK_BYTES;
    assert(first == NODE_VOICE_CHUNK_BYTES);
    assert(last == 1);
    assert(nslices == expect_full + (expect_tail ? 1 : 0));
    assert(last_n == (expect_tail ? expect_tail : NODE_VOICE_CHUNK_BYTES));
    assert(off == sizeof(pcm));
    assert(memcmp(buf, pcm + last_at * NODE_VOICE_CHUNK_BYTES, last_n) == 0);
    printf("ok: feed slices full/tail with last flag\n");
}

static void test_chunked_stub_contract(void)
{
    int calls = 0;
    node_voice_result_t r;
    static char fake[100];
    (void)fake;
    assert(node_voice_post_chunked(test_feed, &calls, &r) == 0);
    assert(calls == 3);
    assert(strlen(r.transcript) > 0);
    assert(node_voice_post_chunked(NULL, &calls, &r) == -1);
    printf("ok: chunked stub pulls 3 feeds, fills transcript\n");
}

static void test_voice_stub_contract(void)
{
    static const char fake_pcm[320] = {1};
    node_voice_result_t r;
    assert(node_voice_post(fake_pcm, sizeof(fake_pcm), 16000, 1, &r) == 0);
    assert(strlen(r.transcript) > 0);
    assert(strlen(r.reply) > 0);
    assert(node_voice_post(NULL, 0, 16000, 1, &r) == -1);
    assert(node_voice_cancel() == 0);
    printf("ok: voice stub fills transcript+reply, rejects empty\n");
}

void node_app_test_reset(void);
int node_app_test_pending(void);
size_t node_app_test_len(void);
int node_app_test_cancelled(void);
void node_app_test_audio(size_t len, int16_t peak);
void node_app_on_press(void);
void node_app_on_release(void);
void node_app_run_upload(size_t len);
void node_app_test_report(const node_voice_result_t *r, int rc);

static void test_ptt_flow_enqueues(void)
{
    node_app_test_reset();
    assert(node_app_test_pending() == 0);
    node_app_on_press();
    node_app_test_audio(4096, 2000);
    node_app_on_release();
    assert(node_app_test_pending() == 1);
    assert(node_app_test_len() == 4096);
    printf("ok: press+release enqueues 4096-byte upload\n");
}

static void test_ptt_tap_ignored(void)
{
    node_app_test_reset();
    node_app_on_press();
    node_app_test_audio(100, 2000);
    node_app_on_release();
    assert(node_app_test_pending() == 0);
    printf("ok: tap with 100 bytes enqueues nothing\n");
}

// Paging: tap advances through a multi-page reply and wraps; taps with
// no pages (fresh boot), one page, or after errors stay no-ops.
// Ticks between taps model the user pausing: without them consecutive taps
// sit 200 ms apart, inside the 400 ms double-press window.
void node_app_test_tick(int ms);
static void tap_once(void)
{
    node_app_test_tick(500);
    node_app_on_press();
    node_app_test_audio(100, 2000);
    node_app_on_release();
}

static int snap_has_text(char snap[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1],
                         const char *want)
{
    for (int row = 0; row < NODE_DISPLAY_ROWS; row++)
    {
        if (strstr(snap[row], want))
        {
            return 1;
        }
    }
    return 0;
}

static void test_tap_pages_reply(void)
{
    char snap[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1];

    // Fresh boot: tap is the historical ignore path.
    node_app_test_reset();
    tap_once();
    assert(node_app_test_pending() == 0);
    node_display_snapshot(snap);
    assert(!snap_has_text(snap, "01/"));

    // Short reply: one page, tap stays a no-op.
    node_voice_result_t r;
    memset(&r, 0, sizeof(r));
    snprintf(r.reply, sizeof(r.reply), "short");
    node_app_test_report(&r, 0);
    tap_once();
    assert(node_app_test_pending() == 0);
    node_display_snapshot(snap);
    assert(snap_has_text(snap, "short"));

    // Long reply: 200 chars = 4 pages (57/57/57/29).
    memset(&r, 0, sizeof(r));
    memset(r.reply, 'A', 57);
    memset(r.reply + 57, 'B', 57);
    memset(r.reply + 114, 'C', 57);
    memset(r.reply + 171, 'D', 29);
    r.reply[200] = '\0';
    node_app_test_report(&r, 0);
    node_display_snapshot(snap);
    assert(snap_has_text(snap, "AAA"));
    assert(snap_has_text(snap, "01/04"));
    tap_once();
    node_display_snapshot(snap);
    assert(snap_has_text(snap, "BBB"));
    assert(snap_has_text(snap, "02/04"));
    tap_once();
    node_display_snapshot(snap);
    assert(snap_has_text(snap, "CCC"));
    tap_once();
    node_display_snapshot(snap);
    assert(snap_has_text(snap, "DDD"));
    assert(snap_has_text(snap, "04/04"));
    tap_once();
    node_display_snapshot(snap);
    assert(snap_has_text(snap, "AAA"));
    assert(snap_has_text(snap, "01/04"));
    assert(node_app_test_pending() == 0);
    printf("ok: tap pages multi-page reply and wraps\n");

    // Error reply is single-page words: tap stays a no-op.
    memset(&r, 0, sizeof(r));
    snprintf(r.error, sizeof(r.error), "hermes_timeout");
    node_app_test_report(&r, -1);
    tap_once();
    node_display_snapshot(snap);
    assert(snap_has_text(snap, "agent busy"));
    printf("ok: tap on error words is a no-op\n");
}

static void test_ptt_silence_skipped(void)
{
    node_app_test_reset();
    node_app_on_press();
    node_app_test_audio(4096, 10);
    node_app_on_release();
    assert(node_app_test_pending() == 0);
    printf("ok: silence peak under threshold enqueues nothing\n");
}

void node_app_run_upload(size_t len);

static void test_worker_runs_queued_upload(void)
{
    node_app_test_reset();
    node_app_on_press();
    node_app_test_audio(40000, 2000);
    node_app_on_release();
    assert(node_app_test_pending() == 1);
    node_app_run_upload(node_app_test_len());
    assert(node_app_test_pending() == 1);
    printf("ok: worker upload path runs to reply\n");
}

// Each relay X-Error shows distinct user words, never the generic screen.
static void test_error_screens_map(void)
{
    static const struct
    {
        const char *err;
        const char *want;
    } cases[] = {
        {"silence", "no speech heard"},
        {"upload_gap", "cut out, try again"},
        {"hermes_timeout", "agent busy, retry"},
        {"unauthorized", "not provisioned"},
        {"unknown_upload", "session lost, retry"},
        {"stt_failed", "! error"},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        node_voice_result_t r;
        memset(&r, 0, sizeof(r));
        snprintf(r.error, sizeof(r.error), "%s", cases[i].err);
        node_app_test_report(&r, -1);
        char snap[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1];
        node_display_snapshot(snap);
        int found = 0;
        for (int row = 0; row < NODE_DISPLAY_ROWS; row++)
        {
            if (strstr(snap[row], cases[i].want))
            {
                found = 1;
            }
        }
        assert(found);
        printf("ok: error '%s' shows user words\n", cases[i].err);
    }
    node_voice_result_t r;
    memset(&r, 0, sizeof(r));
    snprintf(r.error, sizeof(r.error), "cancelled");
    node_app_test_report(&r, -1);
    char snap[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1];
    node_display_snapshot(snap);
    assert(snap[2][0] == '\0' && snap[4][0] == '\0');
    printf("ok: cancel returns to ready screen\n");
}

static void test_error_clears_stale_transcript(void)
{
    node_voice_result_t r;
    memset(&r, 0, sizeof(r));
    snprintf(r.transcript, sizeof(r.transcript),
             "old question long enough to wrap rows");
    snprintf(r.reply, sizeof(r.reply), "old answer");
    node_app_test_report(&r, 0);
    memset(&r, 0, sizeof(r));
    snprintf(r.error, sizeof(r.error), "hermes_timeout");
    node_app_test_report(&r, -1);
    char snap[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1];
    node_display_snapshot(snap);
    assert(snap[2][0] == '\0' && snap[3][0] == '\0');
    assert(strstr(snap[4], "agent busy") != NULL);
    printf("ok: error clears stale transcript\n");
}

static void test_truncated_flag_cleared_on_failure(void)
{
    node_app_test_reset();
    node_app_on_press();
    node_app_test_audio((size_t)NODE_AUDIO_HALF_BYTES * 3, 2000);
    node_app_on_release();
    node_voice_result_t r;
    memset(&r, 0, sizeof(r));
    snprintf(r.error, sizeof(r.error), "hermes_timeout");
    node_app_test_report(&r, -1);
    memset(&r, 0, sizeof(r));
    snprintf(r.transcript, sizeof(r.transcript), "fresh");
    snprintf(r.reply, sizeof(r.reply), "fresh answer");
    node_app_test_report(&r, 0);
    char snap[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1];
    node_display_snapshot(snap);
    for (int row = 0; row < NODE_DISPLAY_ROWS; row++)
    {
        assert(strstr(snap[row], "(part)") == NULL);
    }
    assert(strstr(snap[4], "fresh answer") != NULL);
    printf("ok: truncated flag cleared on failure\n");
}


static void test_boot_reports_outcomes(void)
{
    extern void app_main_run(void);
    extern void node_wifi_test_fail(int fail);
    extern void node_ptt_test_fail(int fail);

    node_wifi_test_fail(0);
    node_ptt_test_fail(0);
    app_main_run();
    int net = -1, ptt = -1;
    node_boot_test_latched(&net, &ptt);
    assert(net == 1 && ptt == 1);
    printf("ok: boot records net+ptt ok\n");

    node_wifi_test_fail(1);
    node_ptt_test_fail(0);
    app_main_run();
    net = -1;
    ptt = -1;
    node_boot_test_latched(&net, &ptt);
    assert(net == 0 && ptt == 1);
    char snap[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1];
    node_display_snapshot(snap);
    int found = 0;
    for (int row = 0; row < NODE_DISPLAY_ROWS; row++)
    {
        if (strstr(snap[row], "no wifi"))
        {
            found = 1;
        }
    }
    assert(found);
    node_wifi_test_fail(0);
    printf("ok: wifi fail paints offline words\n");

    node_ptt_test_fail(1);
    app_main_run();
    net = -1;
    ptt = -1;
    node_boot_test_latched(&net, &ptt);
    assert(ptt == 0);
    node_display_snapshot(snap);
    found = 0;
    for (int row = 0; row < NODE_DISPLAY_ROWS; row++)
    {
        if (strstr(snap[row], "button broken"))
        {
            found = 1;
        }
    }
    assert(found);
    node_ptt_test_fail(0);
    printf("ok: ptt fail halts on error screen\n");
}

static void test_truncated_prefix(void)
{
    // 3 halves undrained: third overwrites the first (overrun sticks).
    // Upload drains all three; reply carries the "(part)" prefix.
    node_app_test_reset();
    node_app_on_press();
    node_app_test_audio((size_t)NODE_AUDIO_HALF_BYTES * 3, 2000);
    node_app_on_release();
    assert(node_app_test_pending() == 1);
    node_app_run_upload(node_app_test_len());
    char snap[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1];
    node_display_snapshot(snap);
    int found = 0;
    for (int row = 0; row < NODE_DISPLAY_ROWS; row++)
    {
        if (strstr(snap[row], "(part)"))
        {
            found = 1;
        }
    }
    assert(found);
    printf("ok: overrun clip prefixes reply with part note\n");
}

static void test_display_layout(void)
{
    char snap[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1];
    assert(node_display_init() == 0);
    node_display_net("node-01", -67);
    node_display_state(NODE_SCREEN_RECORDING);
    node_display_transcript("deploy ke staging");
    node_display_snapshot(snap);
    assert(strstr(snap[2], "deploy ke staging") != NULL);
    node_display_reply("Done. #42 running, ETA 2 min.");
    node_display_snapshot(snap);
    assert(strncmp(snap[0], "voice-node", 10) == 0);
    assert(snap[2][0] == '\0' && snap[3][0] == '\0');
    assert(strstr(snap[4], "Done.") != NULL);
    assert(strstr(snap[7], "node-01") != NULL && strstr(snap[7], "-67") != NULL);
    node_display_state(NODE_SCREEN_READY);
    node_display_snapshot(snap);
    assert(snap[2][0] == '\0' && snap[4][0] == '\0');
    printf("ok: display 4-line layout + ready clears\n");
}

static void test_double_press_cancels(void)
{
    node_app_test_reset();
    node_app_on_press();
    node_app_on_press();
    assert(node_app_test_cancelled() == 1);
    assert(node_app_test_pending() == 0);
    printf("ok: double-press cancels, no job\n");
}

int main(void)
{
    test_debounce_filters_glitch();
    test_debounce_reports_stable();
    test_ptt_callback_path();
    test_config_provisioning();
    test_provision_apply();
    test_health_counts_only_active();
    test_mqtt_stub_never_blocks();
    test_audio_capture_and_gate();
    test_led_modes();
    test_voice_stub_contract();
    test_chunked_stub_contract();
    test_feed_slicing();
    test_ptt_flow_enqueues();
    test_ptt_tap_ignored();
    test_tap_pages_reply();
    test_double_press_cancels();
    test_display_layout();
    test_ptt_silence_skipped();
    test_worker_runs_queued_upload();
    test_error_screens_map();
    test_error_clears_stale_transcript();
    test_truncated_flag_cleared_on_failure();
    test_truncated_prefix();
    test_boot_reports_outcomes();
    printf("ALL HOST TESTS PASSED\n");
    return 0;
}
