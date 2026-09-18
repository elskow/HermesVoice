#include "node_voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int node_voice_post(const void *pcm, size_t len, int sample_rate_hz,
                    int channels, node_voice_result_t *out)
{
    (void)sample_rate_hz;
    (void)channels;
    if (!pcm || !len || !out) {
        return -1;
    }
    snprintf(out->session_id, sizeof(out->session_id), "sess_host123");
    snprintf(out->transcript, sizeof(out->transcript), "deploy ke staging");
    snprintf(out->reply, sizeof(out->reply), "Hermes heard: deploy ke staging");
    out->error[0] = '\0';
    out->stt_ms = 12;
    out->hermes_ms = 34;
    printf("[voice-sim] upload %zu bytes -> reply=%s\n", len, out->reply);
    return 0;
}

int node_voice_cancel(void)
{
    printf("[voice-sim] cancel\n");
    return 0;
}

int node_voice_post_chunked(node_voice_chunk_fn_t feed, void *ctx,
                            node_voice_result_t *out)
{
    if (!feed || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    static char fake[32768];
    for (int i = 0; i < 3; i++) {
        size_t n = sizeof(fake);
        int last = 0;
        if (feed(fake, &n, &last, ctx) != 0 || n == 0) {
            snprintf(out->error, sizeof(out->error), "stt_failed");
            return -1;
        }
        if (last) {
            break;
        }
    }
    snprintf(out->session_id, sizeof(out->session_id), "sess_host123");
    snprintf(out->transcript, sizeof(out->transcript), "deploy ke staging");
    snprintf(out->reply, sizeof(out->reply), "Hermes heard: deploy ke staging");
    return 0;
}

