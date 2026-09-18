#include "node_config.h"
#include <stdlib.h>
#include <string.h>
#ifndef CONFIG_NODE_DEVICE_ID
#define CONFIG_NODE_DEVICE_ID "sim-node"
#endif // ifndef CONFIG_NODE_DEVICE_ID
#ifndef CONFIG_NODE_GATEWAY_URL
#define CONFIG_NODE_GATEWAY_URL "http://127.0.0.1:8081"
#endif // ifndef CONFIG_NODE_GATEWAY_URL

static char s_id[64] = CONFIG_NODE_DEVICE_ID;
static char s_gw[192] = CONFIG_NODE_GATEWAY_URL;
static char s_tok[128];

int node_config_init(void)
{
    const char *e;
    if ((e = getenv("NODE_DEVICE_ID")))
    {
        strncpy(s_id, e, sizeof(s_id) - 1);
    }
    if ((e = getenv("NODE_GATEWAY_URL")))
    {
        strncpy(s_gw, e, sizeof(s_gw) - 1);
    }
    if ((e = getenv("NODE_GATEWAY_TOKEN")))
    {
        strncpy(s_tok, e, sizeof(s_tok) - 1);
    }
    return 0;
}

const char *node_config_device_id(void) { return s_id; }

const char *node_config_device_token(void) { return s_tok[0] ? s_tok : NULL; }

const char *node_config_gateway_url(void) { return s_gw; }

int node_config_telemetry_interval_s(void) { return 60; }

static int s_provisioned = 0;

int node_config_is_provisioned(void) { return s_provisioned; }

int node_config_set_wifi(const char *s, const char *p)
{
    if (!s || !p)
        return -1;
    s_provisioned = 1;
    return 0;
}

int node_config_set_mqtt_uri(const char *u) { return u ? 0 : -1; }

int node_config_set_device_id(const char *d)
{
    if (!d)
        return -1;
    strncpy(s_id, d, sizeof(s_id) - 1);
    return 0;
}

int node_config_set_gateway(const char *u, const char *t)
{
    if (!u || !t)
        return -1;
    strncpy(s_gw, u, sizeof(s_gw) - 1);
    strncpy(s_tok, t, sizeof(s_tok) - 1);
    return 0;
}
