# Relay API contract (generated - do not edit by hand)

Regenerate: `go generate ./...` from `relay/`. Source of truth is
route registrations + `errReply` call sites in `internal/server/`.

Device speaks no JSON: raw PCM up, text + `X-*` headers down.

## Endpoints

| Method | Path | Handler |
|---|---|---|
| POST | `/v1/voice` | handleVoice |
| POST | `/v1/chunks/start` | handleChunkStart |
| POST | `/v1/chunks/{id}` | handleChunkAppend |
| POST | `/v1/chunks/{id}/finish` | handleChunkFinish |
| POST | `/v1/cancel` | handleCancel |
| GET | `/v1/firmware/version` | handleFirmwareVersion |
| GET | `/v1/firmware/{name}` | handleFirmwareBinary |
| GET | `/healthz` |  |

## X-Error vocabulary

| HTTP | `X-Error` | Message |
|---|---|---|
| BadRequest | `bad_request` | missing upload id or seq |
| Conflict | `cancelled` | superseded by newer request |
| BadGateway | `hermes_timeout` | agent unavailable, try again |
| OK | `silence` | empty audio |
| BadGateway | `stt_failed` | (dynamic, see handler) |
| BadRequest | `too_long` | audio too large (max 30 s) |
| Unauthorized | `unauthorized` | bad device id or token |
| NotFound | `unknown_upload` | unknown upload, restart session |
| BadRequest | `upload_gap` | (dynamic, see handler) |

## Response headers (success)

`X-Session-ID`, `X-Transcript` (200 chars), `X-STT-MS`, `X-Hermes-MS`,
`X-Upload-ID` (chunk start). Request headers: `X-Device-ID` (required),
`X-Request-ID` (optional inbound, always echoed back).
`Authorization: Bearer <token>` (when `DEVICE_TOKENS` set),
`Content-Type: audio/l16;rate=16000;channels=1`.
