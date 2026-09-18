#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

#define NODE_DISPLAY_COLS 21
#define NODE_DISPLAY_ROWS 8

typedef enum {
    NODE_SCREEN_BOOT,
    NODE_SCREEN_READY,
    NODE_SCREEN_RECORDING,
    NODE_SCREEN_UPLOADING,
    NODE_SCREEN_REPLY,
    NODE_SCREEN_ERROR,
} node_screen_t;

int node_display_init(void);

void node_display_state(node_screen_t screen);
void node_display_transcript(const char *text);
void node_display_reply(const char *text);
void node_display_net(const char *device_id, int rssi_dbm);

void node_display_snapshot(char out[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1]);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
