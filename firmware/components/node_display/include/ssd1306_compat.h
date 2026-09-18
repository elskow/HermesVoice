#pragma once

// Shim for the SSD1306 vendor component (target builds only).
// Resolved by the vendored driver at IDF build time; host tests never
// compile this path. SH1106 panels need their own init/flush here.
#ifdef NODE_HOST_TEST
#error "ssd1306_compat.h is target-only"
#endif // ifdef NODE_HOST_TEST

#include "node_display.h"

int ssd1306_init(void);
void ssd1306_flush(char lines[NODE_DISPLAY_ROWS][NODE_DISPLAY_COLS + 1]);
