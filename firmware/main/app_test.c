#include "app_private.h"
#include "app_test.h"

#include "node_audio.h"
#include "page.h"

// Test hook bodies (host + linux sim). Thin wrappers over production flow:
// no shadows, no alternate paths. All state lives in app_main.c, shared
// through app_private.h. Fake timer: host/sim have no IDF ets_timer.

#ifdef NODE_HOST_TEST
long long host_us;
long long esp_timer_get_time(void) { return host_us += 100000; }
#endif

void node_app_test_reset(void)
{
    s_job_head = s_job_tail = s_job_n = 0;
    s_test_cancelled = 0;
    s_overrun_noted = 0;
    page_clear();
    s_press_us = -1000000;
#ifdef NODE_HOST_TEST
    host_us = 0;
#endif
}

int node_app_test_pending(void)
{
    return s_job_n;
}

size_t node_app_test_len(void)
{
    return s_job_n > 0 ? s_job_len[s_job_head] : 0;
}

int node_app_test_cancelled(void)
{
    return s_test_cancelled;
}

void node_app_test_audio(size_t len, int16_t peak)
{
    node_audio_test_set(len, peak);
}

void node_app_test_tick(int ms)
{
#ifdef NODE_HOST_TEST
    if (ms > 0)
    {
        host_us += (long long)ms * 1000;
    }
#else
    (void)ms;
#endif
}

void node_app_on_press(void)
{
    on_press();
}

void node_app_on_release(void)
{
    on_release();
}

void node_app_run_upload(size_t len)
{
    run_upload(len);
}

void node_app_test_report(const node_voice_result_t *r, int rc)
{
    report_result(r, rc);
}

static int s_boot_net = -1;
static int s_boot_ptt = -1;

void node_boot_test_record(int net, int ptt)
{
    s_boot_net = net;
    s_boot_ptt = ptt;
}

void node_boot_test_latched(int *net, int *ptt)
{
    if (net) {
        *net = s_boot_net;
    }
    if (ptt) {
        *ptt = s_boot_ptt;
    }
}

void jobs_consume(void)
{
    extern int s_job_head, s_job_tail, s_job_n;
    (void)s_job_tail;
    if (s_job_n > 0)
    {
        extern size_t s_job_len[];
        (void)s_job_len;
        s_job_head = (s_job_head + 1) % 2;
        s_job_n--;
    }
}
