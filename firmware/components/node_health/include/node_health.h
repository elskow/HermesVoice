#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

int node_health_init(void);
void node_health_note_ptt(bool active);
void node_health_note_latency(int stt_ms, int hermes_ms);
unsigned long node_health_ptt_count(void);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
