#include "node_voice.h"
#include "node_audio.h"
#include "node_config.h"
#include "node_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_http_client.h"
static const char *TAG = "node_voice";

#define CHUNK 4096

// Response headers arrive via HTTP_EVENT_ON_HEADER during perform(),
// NOT via esp_http_client_get_header (that needs
// CONFIG_ESP_HTTP_CLIENT_SAVE_RESPONSE_HEADERS, off by default to save
// RAM). One handler serves all voice calls; ctx points at the capture.
typedef struct
{
    char upload_id[64];
    char session_id[64];
    char transcript[256];
    char error[64];
    char *body;
    size_t body_cap;
    size_t body_len;
    int stt_ms;
    int hermes_ms;
} voice_headers_t;

static void copy_header(voice_headers_t *h, const char *key, const char *value)
{
    if (!key || !value)
    {
        return;
    }
    if (strcasecmp(key, "X-Upload-ID") == 0)
    {
        strncpy(h->upload_id, value, sizeof(h->upload_id) - 1);
    }
    else if (strcasecmp(key, "X-Session-ID") == 0)
    {
        strncpy(h->session_id, value, sizeof(h->session_id) - 1);
    }
    else if (strcasecmp(key, "X-Transcript") == 0)
    {
        strncpy(h->transcript, value, sizeof(h->transcript) - 1);
    }
    else if (strcasecmp(key, "X-Error") == 0)
    {
        strncpy(h->error, value, sizeof(h->error) - 1);
    }
    else if (strcasecmp(key, "X-STT-MS") == 0)
    {
        h->stt_ms = atoi(value);
    }
    else if (strcasecmp(key, "X-Hermes-MS") == 0)
    {
        h->hermes_ms = atoi(value);
    }
}

static esp_err_t voice_header_event(esp_http_client_event_t *e)
{
    voice_headers_t *h = e->user_data;
    if (!h)
    {
        return ESP_OK;
    }
    if (e->event_id == HTTP_EVENT_ON_HEADER)
    {
        copy_header(h, e->header_key, e->header_value);
    }
    else if (e->event_id == HTTP_EVENT_ON_DATA && e->data && e->data_len > 0 && h->body && h->body_len < h->body_cap - 1)
    {
        size_t room = h->body_cap - 1 - h->body_len;
        size_t n = (size_t)e->data_len < room ? (size_t)e->data_len : room;
        memcpy(h->body + h->body_len, e->data, n);
        h->body_len += n;
        h->body[h->body_len] = '\0';
    }
    return ESP_OK;
}

static void headers_to_result(const voice_headers_t *h, node_voice_result_t *out)
{
    strncpy(out->session_id, h->session_id, sizeof(out->session_id) - 1);
    strncpy(out->transcript, h->transcript, sizeof(out->transcript) - 1);
    strncpy(out->error, h->error, sizeof(out->error) - 1);
    out->stt_ms = h->stt_ms;
    out->hermes_ms = h->hermes_ms;
}

static int gateway_ok(void)
{
    return strlen(node_config_gateway_url()) <= 160;
}

int node_voice_post(const void *pcm, size_t len, int sample_rate_hz,
                    int channels, node_voice_result_t *out)
{
    if (!pcm || !len || !out)
    {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (!gateway_ok())
    {
        snprintf(out->error, sizeof(out->error), "stt_failed");
        return -1;
    }

    char url[256], ctype[64];
    snprintf(url, sizeof(url), "%s/v1/voice", node_config_gateway_url());
    snprintf(ctype, sizeof(ctype), "audio/l16;rate=%d;channels=%d",
             sample_rate_hz, channels);

    voice_headers_t hdrs;
    memset(&hdrs, 0, sizeof(hdrs));
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 100000,
        .event_handler = voice_header_event,
        .user_data = &hdrs,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c)
    {
        return -1;
    }
    esp_http_client_set_header(c, "Content-Type", ctype);
    esp_http_client_set_header(c, "X-Device-ID", node_config_device_id());
    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", node_config_device_token());
    esp_http_client_set_header(c, "Authorization", auth);

    // Known-length open + 4KB writes: the recording buffer stays allocated
    // (capture needs it anyway) but no second copy is ever made; RAM peak
    // is the clip plus one CHUNK in flight through TLS.
    if (esp_http_client_open(c, (int)len) != ESP_OK)
    {
        NODE_LOGE(TAG, "open %s failed", url);
        esp_http_client_cleanup(c);
        return -1;
    }
    const char *p = pcm;
    size_t left = len;
    while (left > 0)
    {
        int n = left > CHUNK ? CHUNK : (int)left;
        int w = esp_http_client_write(c, p, n);
        if (w <= 0)
        {
            NODE_LOGE(TAG, "write failed");
            esp_http_client_cleanup(c);
            return -1;
        }
        p += w;
        left -= (size_t)w;
    }

    esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);

    int body_len = esp_http_client_read_response(c, out->reply, sizeof(out->reply) - 1);
    if (body_len > 0)
    {
        out->reply[body_len] = '\0';
    }

    headers_to_result(&hdrs, out);
    esp_http_client_cleanup(c);

    NODE_LOGI(TAG, "status=%d transcript=%s stt=%dms hermes=%dms", status,
              out->transcript, out->stt_ms, out->hermes_ms);
    return status == 200 ? 0 : -1;
}

typedef struct {
    esp_http_client_handle_t client;
    char auth[64];
} voice_session_t;

static void voice_auth(esp_http_client_handle_t c, char *auth, size_t n)
{
    snprintf(auth, n, "Bearer %s", node_config_device_token());
    esp_http_client_set_header(c, "X-Device-ID", node_config_device_id());
    esp_http_client_set_header(c, "Authorization", auth);
}

static int session_open(voice_session_t *s, voice_headers_t *hdrs, int timeout_ms)
{
    memset(hdrs, 0, sizeof(*hdrs));
    esp_http_client_config_t cfg = {
        .url = node_config_gateway_url(),
        .method = HTTP_METHOD_POST,
        .timeout_ms = timeout_ms,
        .event_handler = voice_header_event,
        .user_data = hdrs,
    };
    s->client = esp_http_client_init(&cfg);
    return s->client ? 0 : -1;
}

static int post_empty(voice_session_t *s, voice_headers_t *hdrs, const char *path, char *upload_id_out)
{
    char url[96];
    snprintf(url, sizeof(url), "%s%s", node_config_gateway_url(), path);
    esp_http_client_set_url(s->client, url);
    esp_http_client_set_header(s->client, "Content-Length", "0");
    voice_auth(s->client, s->auth, sizeof(s->auth));
    int rc = esp_http_client_perform(s->client) == ESP_OK && esp_http_client_get_status_code(s->client) == 200 ? 0 : -1;
    if (rc == 0 && upload_id_out)
    {
        if (hdrs->upload_id[0])
        {
            strncpy(upload_id_out, hdrs->upload_id, 63);
        }
        else
        {
            rc = -1;
        }
    }
    return rc;
}

static int post_chunk(voice_session_t *s, const char *upload_id, int seq, const void *data, size_t len)
{
    char url[96];
    snprintf(url, sizeof(url), "%s/v1/chunks/%.63s?seq=%d", node_config_gateway_url(), upload_id, seq);
    esp_http_client_set_url(s->client, url);
    esp_http_client_set_header(s->client, "Content-Type", "audio/l16;rate=16000;channels=1");
    voice_auth(s->client, s->auth, sizeof(s->auth));
    int rc = -1;
    if (esp_http_client_open(s->client, (int)len) == ESP_OK)
    {
        const char *q = data;
        size_t left = len;
        rc = 0;
        while (left > 0)
        {
            int n = left > CHUNK ? CHUNK : (int)left;
            int w = esp_http_client_write(s->client, q, n);
            if (w <= 0)
            {
                rc = -1;
                break;
            }
            q += w;
            left -= (size_t)w;
        }
        if (rc == 0)
        {
            esp_http_client_fetch_headers(s->client);
            rc = esp_http_client_get_status_code(s->client) == 200 ? 0 : -1;
        }
    }
    return rc;
}

int node_voice_post_chunked(node_voice_chunk_fn_t feed, void *ctx,
                            node_voice_result_t *out)
{
    if (!feed || !out)
    {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (!gateway_ok())
    {
        snprintf(out->error, sizeof(out->error), "stt_failed");
        return -1;
    }
    voice_session_t sess;
    voice_headers_t hdrs;
    if (session_open(&sess, &hdrs, 15000) != 0)
    {
        snprintf(out->error, sizeof(out->error), "stt_failed");
        return -1;
    }
    char upload_id[64] = {0};
    int rc = post_empty(&sess, &hdrs, "/v1/chunks/start", upload_id);
    static char chunk[NODE_VOICE_CHUNK_BYTES];
    int seq = 0;
    while (rc == 0)
    {
        size_t n = sizeof(chunk);
        int last = 0;
        if (feed(chunk, &n, &last, ctx) != 0 || n == 0)
        {
            snprintf(out->error, sizeof(out->error), "stt_failed");
            rc = -1;
            break;
        }
        if (post_chunk(&sess, upload_id, seq, chunk, n) != 0 && post_chunk(&sess, upload_id, seq, chunk, n) != 0)
        {
            snprintf(out->error, sizeof(out->error), "upload_gap");
            rc = -1;
            break;
        }
        if (last)
        {
            break;
        }
        if (++seq > 120)
        {
            snprintf(out->error, sizeof(out->error), "too_long");
            rc = -1;
            break;
        }
    }
    if (rc == 0)
    {
        char url[96];
        snprintf(url, sizeof(url), "%s/v1/chunks/%.63s/finish", node_config_gateway_url(), upload_id);
        hdrs.body = out->reply;
        hdrs.body_cap = sizeof(out->reply);
        esp_http_client_set_url(sess.client, url);
        esp_http_client_set_header(sess.client, "Content-Length", "0");
        voice_auth(sess.client, sess.auth, sizeof(sess.auth));
        rc = esp_http_client_perform(sess.client) == ESP_OK ? esp_http_client_get_status_code(sess.client) : -1;
        headers_to_result(&hdrs, out);
        rc = (rc == 200) ? 0 : -1;
    }
    esp_http_client_cleanup(sess.client);
    if (rc == 0)
    {
        NODE_LOGI(TAG, "chunked done transcript=%s stt=%dms hermes=%dms",
                  out->transcript, out->stt_ms, out->hermes_ms);
    }
    return rc;
}

int node_voice_cancel(void)
{
    if (!gateway_ok())
    {
        return -1;
    }
    char url[256];
    snprintf(url, sizeof(url), "%s/v1/cancel", node_config_gateway_url());
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
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
    esp_err_t err = esp_http_client_perform(c);
    esp_http_client_cleanup(c);
    return err == ESP_OK ? 0 : -1;
}
