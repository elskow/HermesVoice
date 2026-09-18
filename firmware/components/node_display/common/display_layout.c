#include "node_display.h"
#include "node_log.h"

#include <stdio.h>
#include <string.h>

// Shared layout engine: text grid, wrapping, screens. Compiled for every
// target (silicon, linux sim, host tests). Only the paint step differs:
// silicon flushes to SSD1306, sim/host print (see display_paint below).
static char s_lines[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1];
static node_screen_t s_screen = NODE_SCREEN_BOOT;

static const char *screen_label(node_screen_t s)
{
    switch (s)
    {
    case NODE_SCREEN_BOOT:
        return "boot";
    case NODE_SCREEN_READY:
        return "ready";
    case NODE_SCREEN_RECORDING:
        return "* recording";
    case NODE_SCREEN_UPLOADING:
        return "> uploading";
    case NODE_SCREEN_REPLY:
        return "= reply";
    case NODE_SCREEN_ERROR:
        return "! error";
    }
    return "?";
}

static void set_line(int row, const char *text)
{
    snprintf(s_lines[row], sizeof(s_lines[row]), "%-21.21s", text ? text : "");
}

static void render_transcript(const char *text)
{
    char wrapped[2][NODE_DISPLAY_COLS + 1];
    snprintf(wrapped[0], sizeof(wrapped[0]), "> %-19.19s", text ? text : "");
    size_t n = text ? strlen(text) : 0;
    if (n > 19)
    {
        snprintf(wrapped[1], sizeof(wrapped[1]), "  %-19.19s", text + 19);
    }
    else
    {
        wrapped[1][0] = '\0';
    }
    set_line(2, wrapped[0]);
    set_line(3, wrapped[1]);
}

static void render_reply(const char *text)
{
    char wrapped[3][NODE_DISPLAY_COLS + 1];
    size_t n = text ? strlen(text) : 0;
    snprintf(wrapped[0], sizeof(wrapped[0]), "< %-19.19s", text ? text : "");
    if (n > 19)
    {
        snprintf(wrapped[1], sizeof(wrapped[1]), "  %-19.19s", text + 19);
    }
    else
    {
        wrapped[1][0] = '\0';
    }
    if (n > 38)
    {
        snprintf(wrapped[2], sizeof(wrapped[2]), "  %-19.19s", text + 38);
    }
    else
    {
        wrapped[2][0] = '\0';
    }
    set_line(4, wrapped[0]);
    set_line(5, wrapped[1]);
    set_line(6, wrapped[2]);
}

#if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
static void display_paint(const char *what, const char *text)
{
    printf("[display] %s=%s\n", what, text ? text : "");
}

#else
#include "ssd1306_compat.h"
static void display_paint(const char *what, const char *text)
{
    (void)what;
    (void)text;
    ssd1306_flush(s_lines);
}
#endif // if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)

int node_display_init(void)
{
    for (int i = 0; i < NODE_DISPLAY_ROWS; i++)
    {
        s_lines[i][0] = '\0';
    }
    s_screen = NODE_SCREEN_BOOT;
#if !defined(NODE_HOST_TEST) && !defined(NODE_SIM_BUILD)
    // I2C bus + panel live in ssd1306_backend.c (esp_lcd, IDF 6.1 API).
    // SH1106 panels need their own vendor driver here instead.
    if (ssd1306_init() != 0)
    {
        return -1;
    }
    set_line(0, "voice-node");
    set_line(1, screen_label(NODE_SCREEN_BOOT));
    ssd1306_flush(s_lines);
#endif // if !defined(NODE_HOST_TEST) && !defined(NODE_SIM_BUILD)
    return 0;
}

void node_display_state(node_screen_t screen)
{
    s_screen = screen;
    set_line(1, screen_label(screen));
    if (screen == NODE_SCREEN_READY)
    {
        for (int i = 2; i <= 6; i++)
        {
            s_lines[i][0] = '\0';
        }
    }
    display_paint("state", screen_label(screen));
}

void node_display_transcript(const char *text)
{
    render_transcript(text);
    display_paint("transcript", text);
}

void node_display_reply(const char *text)
{
    s_screen = NODE_SCREEN_REPLY;
    set_line(1, screen_label(NODE_SCREEN_REPLY));
    // A reply replaces the whole exchange: clear the transcript rows so a
    // previous turn's question never lingers under a new answer or error.
    s_lines[2][0] = '\0';
    s_lines[3][0] = '\0';
    render_reply(text);
    display_paint("reply", text);
}

void node_display_net(const char *device_id, int rssi_dbm)
{
    char footer[NODE_DISPLAY_COLS + 1];
    snprintf(footer, sizeof(footer), "%-12.12s %4ddBm", device_id ? device_id : "?", rssi_dbm);
    set_line(0, "voice-node");
    set_line(7, footer);
#if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
    (void)footer;
#else
    ssd1306_flush(s_lines);
#endif // if defined(NODE_HOST_TEST) || defined(NODE_SIM_BUILD)
}

void node_display_snapshot(char out[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1])
{
    memcpy(out, s_lines, sizeof(s_lines));
}
