#include "node_ota.h"
#include <stdio.h>
int node_ota_init(void) { return 0; }
int node_ota_start(const char *u) { printf("[sim] ota skip %s\n", u?u:"?"); return 0; }
int node_ota_poll(void) { return 0; }
