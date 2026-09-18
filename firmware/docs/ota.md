# OTA policy

- Layout: `partitions.csv` gives factory + `app0`/`app1` OTA slots + NVS + SPIFFS.
- Transport: HTTPS only. `node_ota_start()` rejects non-https URLs.
- Production TODO before first release:
  1. Embed the update-server CA cert (`EMBED_TXTFILES`) instead of relying
     on the development trust path.
  2. Publish signed version manifests; verify signature before `esp_https_ota()`.
  3. Keep IDF rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`) on so a
     bad image reverts to the previous slot.
- Trigger: subscribe `node/<id>/ota/version` in `node_ota` and call
  `node_ota_start(url)` when manifest version > running version.
