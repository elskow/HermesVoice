#include "errors.h"

#include "node_display.h"
#include "node_led.h"
#include "node_log.h"
#include "page.h"

#include <string.h>

void errors_show(const char *xerr)
{
    node_led_set(NODE_LED_FAST_BLINK);
    if (!xerr || !xerr[0]) {
        node_display_state(NODE_SCREEN_ERROR);
        return;
    }
    page_mark_single();
    if (strcmp(xerr, "silence") == 0) {
        node_display_reply("no speech heard");
    } else if (strcmp(xerr, "upload_gap") == 0) {
        node_display_reply("cut out, try again");
    } else if (strcmp(xerr, "hermes_timeout") == 0) {
        node_display_reply("agent busy, retry");
    } else if (strcmp(xerr, "unauthorized") == 0) {
        node_display_reply("not provisioned");
    } else if (strcmp(xerr, "cancelled") == 0) {
        node_display_state(NODE_SCREEN_READY);
    } else if (strcmp(xerr, "unknown_upload") == 0) {
        node_display_reply("session lost, retry");
    } else {
        node_display_state(NODE_SCREEN_ERROR);
    }
}
