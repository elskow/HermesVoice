#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif // ifdef __cplusplus

#define NODE_AUDIO_SAMPLE_RATE 16000
// Split-buffer pipeline: two static 16 KB halves, ping-ponged between
// recorder and uploader. No heap: BSS only. 16 KB (not 32) so the image
// still links with the BLE stack: BT controller reserves ~30 KB DRAM at
// link time and the segment is 180 KB. Slices stream, so halves/chunks
// can be small: 16 KB covers ~0.5 s of 32 KB/s audio jitter.
#define NODE_AUDIO_HALF_BYTES 16384
#define NODE_AUDIO_HALVES 2

#define NODE_VOICE_CHUNK_BYTES 16384

    typedef enum
    {
        HALF_EMPTY = 0,
        HALF_FILLING,
        HALF_FULL,
    } audio_half_state_t;

    int node_audio_init(void);

    int node_audio_start(void);
    int node_audio_stop(void);

    // Total captured bytes across halves (for tap gate).
    size_t node_audio_len(void);

    // Peak over captured bytes so far (for silence gate, first half decides).
    int16_t node_audio_peak(void);

    // Handoff: take oldest FULL half for upload. Returns byte length, 0 if
    // none full. Data valid until audio_half_mark_empty().
    size_t audio_half_take_full(const int16_t **out);

    // Release a drained half back to EMPTY. Overrun flag sticks until release.
    void audio_half_mark_empty(void);

    // Seal on PTT release: partial FILLING half becomes drainable with last=1.
    // Returns sealed byte length (0 if nothing captured).
    size_t audio_half_seal(void);

    // True if any half was reused while FULL (slow network): reply prefix.
    int audio_half_overrun(void);

    size_t node_audio_bytes_for_ms(int ms);

#if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
    int node_audio_test_set(size_t len, int16_t peak);
    void node_audio_test_push(const int16_t *samples, size_t n);
#endif // if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
