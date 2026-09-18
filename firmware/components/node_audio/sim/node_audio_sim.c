#include "node_audio.h"
#include <stdlib.h>
#include <string.h>

// Linux-sim mirror of the split-buffer pipeline (same semantics, plain heap
// for the halves since glibc has no 64 KB problem; BSS on silicon).
// Keeps SIM behavior identical: halves ping-pong, overrun sticks.
typedef struct
{
    int16_t *buf;
    size_t len_bytes;
    audio_half_state_t state;
} sim_half_t;
static sim_half_t s_h[NODE_AUDIO_HALVES];
static int s_fill, s_drain;
static size_t s_total;
static int s_run, s_overrun;
static void h_reset(void)
{
    for (int i = 0; i < NODE_AUDIO_HALVES; i++)
    {
        if (!s_h[i].buf)
            s_h[i].buf = malloc(NODE_AUDIO_HALF_BYTES);
        s_h[i].len_bytes = 0;
        s_h[i].state = HALF_EMPTY;
    }
    s_fill = 0;
    s_drain = 0;
    s_total = 0;
    s_overrun = 0;
}
static void h_push(const char *src, size_t n)
{
    while (n > 0)
    {
        sim_half_t *h = &s_h[s_fill];
        if (h->state == HALF_FULL)
        {
            s_overrun = 1;
            s_drain = s_fill;
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
        s_total += w;
        if (h->len_bytes >= NODE_AUDIO_HALF_BYTES)
        {
            h->state = HALF_FULL;
            s_fill = (s_fill + 1) % NODE_AUDIO_HALVES;
        }
    }
}
int node_audio_init(void)
{
    h_reset();
    return s_h[0].buf ? 0 : -1;
}
int node_audio_start(void)
{
    h_reset();
    s_run = 1;
    return 0;
}
int node_audio_stop(void)
{
    s_run = 0;
    return 0;
}
size_t node_audio_len(void) { return s_total; }
size_t node_audio_bytes_for_ms(int ms) { return ms <= 0 ? 0 : (size_t)ms * 32; }
int16_t node_audio_peak(void)
{
    int16_t p = 0;
    for (int i = 0; i < NODE_AUDIO_HALVES; i++)
        for (size_t j = 0; j < s_h[i].len_bytes / 2; j++)
        {
            int16_t v = s_h[i].buf[j] < 0 ? -s_h[i].buf[j] : s_h[i].buf[j];
            if (v > p)
                p = v;
        }
    return p;
}
int node_audio_test_set(size_t len, int16_t peak)
{
    h_reset();
    s_run = 0;
    static int16_t fill[8192];
    for (size_t i = 0; i < sizeof(fill) / sizeof(fill[0]); i++)
        fill[i] = peak;
    size_t left = len;
    while (left > 0)
    {
        size_t w = left > sizeof(fill) ? sizeof(fill) : left;
        h_push((const char *)fill, w);
        left -= w;
    }
    return 0;
}
void node_audio_test_push(const int16_t *sm, size_t n)
{
    if (s_run)
        h_push((const char *)sm, n * 2);
}
size_t audio_half_take_full(const int16_t **out)
{
    sim_half_t *h = &s_h[s_drain];
    if (h->state != HALF_FULL)
        return 0;
    *out = h->buf;
    return h->len_bytes;
}
void audio_half_mark_empty(void)
{
    s_h[s_drain].len_bytes = 0;
    s_h[s_drain].state = HALF_EMPTY;
    s_drain = (s_drain + 1) % NODE_AUDIO_HALVES;
}
size_t audio_half_seal(void)
{
    sim_half_t *h = &s_h[s_fill];
    if (h->state == HALF_FILLING && h->len_bytes > 0)
    {
        h->state = HALF_FULL;
        return h->len_bytes;
    }
    return 0;
}
int audio_half_overrun(void) { return s_overrun; }
