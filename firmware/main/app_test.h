#pragma once

#include <stddef.h>
#include <stdint.h>

#include "node_voice.h"

#ifdef __cplusplus
extern "C" {
#endif

// Test entry points (host + linux sim). Production uses the same flow:
// press/release/report/run_upload are the real functions, not shadows.
void node_app_test_reset(void);
int node_app_test_pending(void);
size_t node_app_test_len(void);
int node_app_test_cancelled(void);
void node_app_test_audio(size_t len, int16_t peak);
void node_app_on_press(void);
void node_app_on_release(void);
void node_app_test_tick(int ms);
void node_app_run_upload(size_t len);
void node_app_test_report(const node_voice_result_t *r, int rc);
void node_boot_test_record(int net, int ptt);
void jobs_consume(void);
void node_boot_test_latched(int *net, int *ptt);

#ifdef __cplusplus
}
#endif
