#include "node_ota.h"
#include "node_config.h"
#include "node_log.h"

#include <string.h>

#ifndef NODE_HOST_TEST
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "psa/crypto.h"
#endif

static const char *TAG = "node_ota";

int node_ota_init(void)
{
#ifndef NODE_HOST_TEST
    // Rollback insurance: a freshly OTA'd slot boots as PENDING_VERIFY. If
    // we reach init, the app is alive, so confirm it: the bootloader keeps
    // this slot on future boots. Without this, EVERY ota boot rolls back
    // (and a panicking app boot-loops instead of reverting once).
    // USB-flashed factory apps benefit identically. No-op on first boot
    // from a slot already valid.
    esp_ota_img_states_t st = ESP_OTA_IMG_NEW;
    if (esp_ota_get_state_partition(NULL, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY)
    {
        esp_ota_mark_app_valid_cancel_rollback();
        NODE_LOGI(TAG, "ota slot confirmed valid");
    }
#endif
    NODE_LOGI(TAG, "ota handler ready (call node_ota_start with https url)");
    return 0;
}

int node_ota_start(const char *https_url)
{
    return node_ota_start_sha(https_url, NULL);
}

int node_ota_start_sha(const char *https_url, const char *expected_sha256)
{
    // LAN relay serves plain http; accept http(s) but nothing schemeless.
    // The manifest URL always comes from our own relay, never user input.
    if (!https_url || (strncmp(https_url, "https://", 8) != 0 &&
                       strncmp(https_url, "http://", 7) != 0)) {
        NODE_LOGE(TAG, "ota url must be http(s)");
        return -1;
    }
#ifndef NODE_HOST_TEST
    NODE_LOGI(TAG, "flashing %s", https_url);
    esp_http_client_config_t http_cfg = {
        .url = https_url,
        .timeout_ms = 60000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&http_cfg);
    if (!c)
    {
        return -1;
    }
    if (esp_http_client_open(c, 0) != ESP_OK ||
        esp_http_client_fetch_headers(c) != ESP_OK)
    {
        esp_http_client_cleanup(c);
        return -1;
    }
    int len = esp_http_client_get_content_length(c);
    if (len <= 0)
    {
        NODE_LOGE(TAG, "ota: unknown length, refusing (no resume)");
        esp_http_client_cleanup(c);
        return -1;
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part)
    {
        esp_http_client_cleanup(c);
        return -1;
    }
    esp_ota_handle_t h = 0;
    if (esp_ota_begin(part, (size_t)len, &h) != ESP_OK)
    {
        esp_http_client_cleanup(c);
        return -1;
    }
    // Stream: hash while writing; mismatch aborts before ota_end, so the
    // slot never commits and the bootloader keeps the current app. Abort
    // leaves the inactive slot dirty but unbootable: harmless.
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS)
    {
        esp_http_client_cleanup(c);
        esp_ota_abort(h);
        return -1;
    }
    static char buf[4096];
    int total = 0, rc = 0;
    while (total < len)
    {
        int n = esp_http_client_read(c, buf, sizeof(buf));
        if (n <= 0)
        {
            NODE_LOGE(TAG, "ota: short read %d/%d", total, len);
            rc = -1;
            break;
        }
        if (psa_hash_update(&op, (const unsigned char *)buf, (size_t)n) != PSA_SUCCESS)
        {
            NODE_LOGE(TAG, "ota: hash failed");
            rc = -1;
            break;
        }
        if (esp_ota_write(h, buf, (size_t)n) != ESP_OK)
        {
            NODE_LOGE(TAG, "ota: flash write failed");
            rc = -1;
            break;
        }
        total += n;
    }
    esp_http_client_cleanup(c);
    unsigned char sum[32];
    size_t sum_len = 0;
    if (psa_hash_finish(&op, sum, sizeof(sum), &sum_len) != PSA_SUCCESS ||
        sum_len != 32)
    {
        esp_ota_abort(h);
        return -1;
    }
    if (rc != 0)
    {
        esp_ota_abort(h);
        return -1;
    }
    if (expected_sha256 && expected_sha256[0])
    {
        char hex[65] = {0};
        for (int i = 0; i < 32; i++)
        {
            snprintf(hex + 2 * i, 3, "%02x", sum[i]);
        }
        if (strcmp(hex, expected_sha256) != 0)
        {
            NODE_LOGE(TAG, "ota: sha mismatch, aborting (slot untouched)");
            esp_ota_abort(h);
            return -1;
        }
    }
    if (esp_ota_end(h) != ESP_OK)
    {
        NODE_LOGE(TAG, "ota: image invalid (magic/version check)");
        return -1;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK)
    {
        return -1;
    }
    NODE_LOGI(TAG, "ota ok (%d B), rebooting", total);
    esp_restart();
    return 0;
#else
    (void)https_url;
    (void)expected_sha256;
    return 0;
#endif
}

#ifdef CONFIG_NODE_AUTO_UPDATE
// Minimal manifest scan: fixed-shape JSON, no parser. Extracts string
// fields by key search; adequate because the relay writes the manifest
// (not a human) and any miss simply skips the update.
static int manifest_field(const char *json, const char *key, char *out, size_t n)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p)
    {
        return -1;
    }
    p = strchr(p + strlen(pat), '"');
    if (!p)
    {
        return -1;
    }
    p++;
    const char *q = strchr(p, '"');
    if (!q || (size_t)(q - p) >= n)
    {
        return -1;
    }
    memcpy(out, p, (size_t)(q - p));
    out[q - p] = '\0';
    return 0;
}

static int version_newer(const char *have, const char *want)
{
    // Dotted-numeric compare; non-numeric tails compare unequal = update.
    // Wrong direction only over-updates, never skips: safe bias.
    return strcmp(have, want) != 0;
}
#endif

int node_ota_poll(void)
{
#ifdef CONFIG_NODE_AUTO_UPDATE
    char url[256];
    snprintf(url, sizeof(url), "%s/v1/firmware/version?device_id=%s",
             node_config_gateway_url(), node_config_device_id());
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c)
    {
        return -1;
    }
    esp_http_client_set_header(c, "X-Device-ID", node_config_device_id());
    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", node_config_device_token());
    esp_http_client_set_header(c, "Authorization", auth);
    if (esp_http_client_open(c, 0) != ESP_OK ||
        esp_http_client_fetch_headers(c) != ESP_OK)
    {
        esp_http_client_cleanup(c);
        return -1;
    }
    int status = esp_http_client_get_status_code(c);
    if (status == 204)
    {
        esp_http_client_cleanup(c);
        return 0;
    }
    if (status != 200)
    {
        esp_http_client_cleanup(c);
        return -1;
    }
    static char body[1024];
    int n = esp_http_client_read_response(c, body, sizeof(body) - 1);
    esp_http_client_cleanup(c);
    if (n <= 0)
    {
        return -1;
    }
    body[n] = '\0';
    char ver[32] = {0}, path[128] = {0}, sha[72] = {0};
    if (manifest_field(body, "version", ver, sizeof(ver)) != 0 ||
        manifest_field(body, "url", path, sizeof(path)) != 0 ||
        manifest_field(body, "sha256", sha, sizeof(sha)) != 0)
    {
        return -1;
    }
    const esp_app_desc_t *app = esp_app_get_description();
    if (!version_newer(app->version, ver))
    {
        return 0;
    }
    NODE_LOGI(TAG, "update %s -> %s", app->version, ver);
    char full[256];
    snprintf(full, sizeof(full), "%s%s", node_config_gateway_url(), path);
    // Integrity: image must match the manifest SHA or the slot never
    // commits. Authenticity (MITM rewriting both) needs TLS pinning, still
    // open: LAN-only relay, documented in docs/architecture.md.
    return node_ota_start_sha(full, sha) == 0 ? 1 : -1;
#else
    (void)0;
    return 0;
#endif
}
