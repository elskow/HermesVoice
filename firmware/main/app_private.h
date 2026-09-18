#pragma once

// Shared state between app_main.c (owner) and app_test.c (test wrappers).
// Test targets only (host + linux sim); never compiled for silicon.
// Lets test hooks observe/reset production state without shadowing it.

#include "app_test.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int64_t s_press_us;
extern int s_overrun_noted;

// Provided by IDF on silicon, by app_test.c on host/sim.
long long esp_timer_get_time(void);

// Job ring backend (host/sim twin of the FreeRTOS queue).
#define TEST_JOBS 2
extern size_t s_job_len[TEST_JOBS];
extern int s_job_head, s_job_tail, s_job_n;
extern int s_test_cancelled;

// Production flow, non-static for test wrappers.
void on_press(void);
void on_release(void);
void run_upload(size_t len);
void report_result(const node_voice_result_t *r, int rc);

#ifdef __cplusplus
}
#endif
