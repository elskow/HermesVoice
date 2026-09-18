# Architecture: Hermes Voice in one page

One PTT press turns into an agent reply on a 128x64 OLED. Five contracts
hold the shape; everything else is implementation.

```
 press                                 reply text
   |                                        |
[ESP32 node] --raw PCM + X-*--> [relay :8081] --input--> [Hermes :8642]
   |        <--plain text--------      |
   v                                   v
 OLED + LED                      STT provider + MQTT turns
```

## Ownership (who does what)

- **Device** (`firmware/`): PTT, I2S capture, chunked upload, OLED/LED,
  health telemetry. Holds an **opaque session**: `device_id -> session_id`
  is cached server-side; the device never sees agent state.
- **Relay** (`relay/`, module `hermes-voice`): owns STT, Hermes calls, MQTT
  observability, admin UI. The relay never stores turns; it emits them.
- **Hermes gateway** (Syntesa, `:8642`): the agent harness. Ours by
  dependency, not by code: `internal/hermes` is a thin HTTP client.

## The five contracts

1. **Voice HTTP**: device posts raw PCM (`audio/l16;rate=16000`), gets
   plain text + `X-*` headers back. No JSON on the device (saves the
   parser, keeps `curl` debuggability); JSON exists server-side only.
   (`docs/voice-contract*.md`, `relay/CONTRACT.md` generated.)
2. **Chunk sessions**: `POST /v1/chunks/start|{id}?seq|{id}/finish` for
   clips over one shot. Relay reassembles; no persistent connection.
3. **MQTT topics** (`node/<id>/`): `state` (retained), `prompt`,
   `response`, `turns`. One-way observability: the device never
   subscribes. Empty broker URL = topics log to stdout.
4. **Turn envelope**: one JSON record per completed turn on
   `node/<id>/turns` — device, session, request ID, transcript, reply,
   verdict (`ok|silence|stt_failed|hermes_timeout|cancelled`), both
   latencies, PCM bytes, timestamp. The boundary record: any future
   dashboard/eval/archiver subscribes here.
5. **`.env` file as config**: `relay/.env` is the one and only store
   (gitignored, `0600`). Process env wins, file fills the rest. The
   setup UI is an editor for that file, not a system of record.

## Request path (happy path as call graph)

```
handleVoice/handleChunkFinish
  -> transcribeAndPrompt (request ID threaded via ctx)
    -> STT.Transcribe (swappable: stub|deepgram|deepgram-stream|whisper)
    -> hermes session (create once per device, reuse)
    -> hermes Prompt
    -> PublishTurn(ok) + plain-text reply
```

Every failure emits a turn with its verdict and an `X-Error` header
(`silence`, `upload_gap`, `stt_failed`, `hermes_timeout`, `cancelled`).
Failures are values on the wire, not missing responses.

## Update safety model

- **Integrity** (built): streamed SHA256 vs manifest; mismatch aborts
  before `ota_end`, slot never commits. Kills corrupt-flash bricks.
- **Authenticity** (open): manifest + image share the LAN HTTP channel; a
  MITM can rewrite both. TLS pinning needs a relay cert story (no CA on
  the LAN today). Acceptable while the relay is localhost/LAN-only;
  revisit before any internet-facing deployment.
- **Rollback** (built): app confirms its slot at boot; bad boots revert.
- **Rollout** (built): manifest `devices` allowlist, empty = fleet.

## What is deliberately NOT built

- No JSON parsing on device, no WebRTC/RTC, no BLE provisioning
  transport (W9), no OTA trigger (W8): USB flash covers development.
- No secret store beyond `.env`: any web-writable credential DB needs
  its own secrets (a circle terminating at a file anyway).
- No `internal/` exports: the admin surface never leaks into imports.

Deeper flows: `docs/device-workflows.md` (W1-W9). Method: `DESIGN_THINKING.md`.

## Resource baseline (2026-09-18, post-BLE, post-16KB diet)

- Flash image `voice-node.bin`: 1.27 MB of 1.81 MB app partition (~71%).
  CI enforces fit via `check_sizes.py`.
- Static RAM (data+bss): ~331 KB. Audio owns 48 KB (2x16KB halves + 16KB
  chunk, 15%). BLE (NimBLE) + BT controller reservation fit with margin;
  Bluedroid overflowed DRAM by 53 KB (measured, rejected).
- QEMU heap note: static halves need no heap; the old 320 KB malloc guard
  is gone. QEMU asserts boot only.
