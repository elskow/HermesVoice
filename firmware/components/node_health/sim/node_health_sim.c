#include "node_health.h"
#include <stdio.h>
static unsigned long s_ptt_presses;
static int s_last_stt_ms, s_last_hermes_ms;
int node_health_init(void) { return 0; }
void node_health_note_ptt(bool active) { if (active) s_ptt_presses++; }
void node_health_note_latency(int s, int h)
{
    s_last_stt_ms = s;
    s_last_hermes_ms = h;
    printf("[health] stt=%d hermes=%d\n", s, h);
}
unsigned long node_health_ptt_count(void) { return s_ptt_presses; }
int node_health_test_last_stt(void) { return s_last_stt_ms; }
int node_health_test_last_hermes(void) { return s_last_hermes_ms; }
