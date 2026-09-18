#include "node_provision.h"

// Linux sim: provisioning is a no-op (sim_main feeds Kconfig defaults;
// NVS creds never exist on the sim). The endpoint parser in
// src/node_provision.c is shared source, tested on host.
int node_provision_run(void)
{
    return 0;
}
