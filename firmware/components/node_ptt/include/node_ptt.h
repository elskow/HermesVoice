#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

typedef void (*node_ptt_cb_t)(bool active, void *ctx);

typedef struct {
    int gpio;
    bool active_low;
    uint32_t debounce_ms;
    node_ptt_cb_t on_change;
} node_ptt_config_t;

bool node_ptt_fold(bool *reported, int *stable_ms, bool raw_active,
                         int sample_period_ms, int debounce_ms);

int node_ptt_init(const node_ptt_config_t *cfg);
int node_ptt_start(void);
bool node_ptt_is_active(void);
#if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
void node_ptt_test_fail(int fail);
void node_ptt_test_inject(bool raw_active);
#endif // if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
