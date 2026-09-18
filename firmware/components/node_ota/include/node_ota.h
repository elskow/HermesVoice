#pragma once

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

int node_ota_init(void);
int node_ota_start(const char *https_url);
int node_ota_start_sha(const char *https_url, const char *expected_sha256);
// Same, but aborts before ota_end when the streamed image does not match
// expected_sha256 (64 lowercase hex, NULL skips the check). Integrity,
// not authenticity: manifest and image share the channel (see docs).
int node_ota_start_sha(const char *https_url, const char *expected_sha256);
// Poll relay manifest, OTA when a newer version is eligible for this
// device. Returns 1 if an update applied (caller should reboot; start
// reboots on success anyway), 0 if current, <0 on check failure (not
// fatal: voice path unaffected). Compiles to a no-op returning 0 when
// CONFIG_NODE_AUTO_UPDATE is off, so bench images never self-update.
int node_ota_poll(void);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
