# IoT device workflows

Every workflow on the ESP32, happy path and break paths.
Read against `firmware/main/app_main.c` + `components/node_*/src/*.c`.

```
X = one PTT press turns into an agent reply on a 128x64 OLED
Graph (A):
  boot -> idle -> press -> record -> release -> gate -> upload -> reply -> idle
                    double-press -> cancel -> idle
E (breaks): tap / silence / heap / wifi-down / chunk-fail / server X-Errors
R (needs): NVS config, WiFi, gateway URL, relay, Hermes, MQTT (optional)
```

## W1. Boot to ready

A: `node_config_init` (NVS to Kconfig fallback) -> `node_health_init` ->
WiFi connect (15 s) -> led/display/mqtt/ota/audio/ptt init -> worker task ->
`idle` + `voice-node ready`.

| # | Path | Code | Surface |
|---|---|---|---|---|
| W1a | NVS provisioned | config.c: NVS read | `ready. device_id=X` |
| W1b | NVS empty to Kconfig fallback | `no provisioned NVS` log | same, works |
| W1c | NVS corrupt to erase + reinit | `nvs_flash_erase` retry | same |
| W1d | WiFi fail to offline, PTT still runs | `wifi failed, continuing offline` | "no wifi, offline" reply under READY |
| W1e | Audio heap fail to PTT disabled | `audio buffer unavailable` + fast-blink | idle/fast-blink |
| W1f | PTT init fail (bad gpio) | halt on error screen | "! error" + "button broken" |

Health timer failure only logs: telemetry is non-critical, device still talks.

## W2. Press to record

A: GPIO poll 50 ms -> 200 ms debounce fold -> `on_press` -> `audio_start` ->
`recording` state (LED on, `* recording`, MQTT state).

| # | Path |
|---|---|---|
| W2a | Clean press to recording |
| W2b | Bounce filtered by fold (`glitch filtered` test) |
| W2c | `audio_start` fails (no buf) | idle + fast-blink |
| W2d | Press while uploading (queue depth 2) | Third job drops with log only; display stays stale until survivor finishes |

## W3. Double-press to cancel

A: press <400 ms after last -> `node_voice_cancel` (POST /v1/cancel) ->
drain queue -> ready, no message.

| # | Path |
|---|---|---|
| W3a | Double-press cancels queued job |
| W3b | Cancel POST itself fails (offline) | device shows ready; server job superseded by newer request |

## W4. Release to gate to upload

A: `audio_stop` -> len gate (3200 B, about 100 ms tap) -> peak gate (300 silence)
-> `uploading` state -> chunked pipeline (32 KB halves, start/chunks/finish).

Capture is two 32 KB static halves ping-ponged between recorder (FILLING)
and uploader (FULL); no heap, no 10 s tank (linker: 2x64 KB overflowed
dram0_0_seg, 2x32 KB fits). Slow network fills both halves: recorder reuses
the oldest FULL half, overrun flag sticks, reply carries "(part)".

| # | Path | Display |
|---|---|---|---|
| W4a | Speech 32 KB or under to single POST | transcript + reply |
| W4b | Speech over 32 KB to start/chunks/finish | transcript + reply |
| W4c | Tap under 3200 B to ignore | back to ready, silent |
| W4d | Silence peak under 300 to skip | back to ready, silent |
| W4e | Slow network, both halves full (overrun) | recorder reuses oldest FULL half, flag sticks | "(part)" prefix on reply |
| W4f | Chunk POST fails, 1 retry, then `upload_gap` | "cut out, try again" |
| W4g | Over 120 chunks (~2 min) to `too_long` | generic `! error` (backstop; normal turns never reach it) |

## W5. Server verdict to screen

Error mapping lives in `report_result` (firmware/main/app_main.c); header names in relay `errReply`.

| X-Error | OLED |
|---|---|
| (none, 200) | transcript + reply |
| silence | no speech heard |
| upload_gap | cut out, try again |
| hermes_timeout | agent busy, retry |
| unauthorized | not provisioned |
| unknown_upload | session lost, retry (re-press recovers) |
| cancelled | ready, silent |
| stt_failed/too_long/bad_request | ! error |

## W6. Health telemetry

A: esp_timer every N s (default 30, NVS 5 to 3600) -> JSON
(uptime/heap/rssi/presses/mqtt/stt/hermes ms) -> MQTT publish, QoS 0.

| # | Path |
|---|---|---|
| W6a | Publish while connected |
| W6b | Publish while offline: dropped (fire-and-forget; relay sees LWT `offline`) |
| W6c | Timer create fails to health silent | logs only, device still talks |

## W7. MQTT presence

LWT `node/<id>/status` offline/online (retained QoS 1); state topic per
transition (retained QoS 1); telemetry QoS 0. Relay mirrors to stdout
when no broker. Device never subscribes: observability is one-way by
design. No gaps: reconnect is owned by esp-mqtt, LWT covers death.

## W8. OTA update (closed loop)

Relay serves manifest + images (`GET /v1/firmware/version`, `GET
/v1/firmware/{name}`); admin UI publishes with per-device allowlist.
Device polls each telemetry tick when `CONFIG_NODE_AUTO_UPDATE=y`
(default n: bench images never self-update). Stream hashed against
manifest SHA256, abort before `ota_end` on mismatch; slot confirmed at
boot (rollback on bad boot). Integrity built, MITM authenticity open
(LAN-only relay; see `docs/architecture.md`).

## W9. Provisioning (BLE transport)

Factory device (no NVS `wifi_ssid`) advertises `HERMES-<mac>` over BLE,
security-1 with POP = service name. Phone app / esp_prov delivers
SSID+pass; custom `node-cfg` endpoint delivers gateway URL + token + id.
Reboots into normal boot on success, 10-min timeout continues offline.
QEMU skips (no radio). Rotation without reflash: re-run provisioning
after NVS erase.
