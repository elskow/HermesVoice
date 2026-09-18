#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

#define NODE_VOICE_MAX_REPLY 1024
#define NODE_VOICE_MAX_TRANSCRIPT 256

typedef struct {
    char session_id[64];
    char transcript[NODE_VOICE_MAX_TRANSCRIPT];
    char error[32];
    int stt_ms;
    int hermes_ms;
    char reply[NODE_VOICE_MAX_REPLY];
} node_voice_result_t;

int node_voice_post(const void *pcm, size_t len, int sample_rate_hz,
                    int channels, node_voice_result_t *out);
int node_voice_cancel(void);

typedef int (*node_voice_chunk_fn_t)(void *buf, size_t *len, int *last,
                                     void *ctx);
int node_voice_post_chunked(node_voice_chunk_fn_t feed, void *ctx,
                            node_voice_result_t *out);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
