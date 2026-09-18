#include "node_ptt.h"
#include <stdio.h>

static node_ptt_config_t s_cfg;
static bool s_active;
static int s_fail_init;

bool node_ptt_fold(bool *r, int *st, bool raw, int per, int deb)
{
    if (raw == *r) {
        *st = 0;
        return false;
    }
    *st += per;
    if (*st >= deb) {
        *r = raw;
        *st = 0;
        return true;
    }
    return false;
}
int node_ptt_init(const node_ptt_config_t *c)
{
    if (!c || !c->on_change || !c->debounce_ms) {
        return -1;
    }
    if (s_fail_init) {
        return -1;
    }
    s_cfg = *c;
    s_active = false;
    return 0;
}
int node_ptt_start(void)
{
    if (s_fail_init) {
        return -1;
    }
    printf("[ptt] on gpio %d\n", s_cfg.gpio);
    return 0;
}
bool node_ptt_is_active(void) { return s_active; }
void node_ptt_test_fail(int fail) { s_fail_init = fail; }
void node_ptt_test_inject(bool raw)
{
    static int st = 0;
    if (node_ptt_fold(&s_active, &st, raw, 50,
                      s_cfg.debounce_ms ? s_cfg.debounce_ms : 200)) {
        s_cfg.on_change(s_active, NULL);
    }
}
