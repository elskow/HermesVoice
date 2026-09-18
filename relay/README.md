# Hermes Voice relay

Thin Go glue: PTT devices -> STT provider -> Hermes agent harness.
Device speaks no JSON (raw PCM up, text + X-* headers down).

## Run

```bash
go run ./cmd/hermes-voice           # :8081, env from relay/.env (godotenv)
make dev-relay                 # live reload on save (needs air installed)
curl -X POST localhost:8081/v1/voice \
  -H 'X-Device-ID: node-01' \
  -H 'Content-Type: audio/l16;rate=16000;channels=1' \
  --data-binary @sample.pcm -v
```

`MQTT_BROKER_URL` empty = topics log to stdout. Set it to publish live.
`HERMES_BASE_URL` is required (fail fast when unset).

## Swap STT

```bash
STT_PROVIDER=deepgram STT_DEEPGRAM_API_KEY=... go run ./cmd/hermes-voice
STT_PROVIDER=whisper STT_WHISPER_URL=http://localhost:8000 go run ./cmd/hermes-voice
```

New vendor = one file in `internal/stt/` + factory case. Handler untouched.

## Setup UI

`http://127.0.0.1:8090/setup` (localhost only, never LAN): edit `relay/.env`,
test each provider inline, save (atomic rewrite, then restart relay).
`http://127.0.0.1:8090/status`: uptime, per-verdict counters, last 20 turns
(auto-refreshes every 5 s).
Regenerate templates after editing `internal/setup/page.templ`:

```bash
go run github.com/a-h/templ/cmd/templ@latest generate ./internal/setup/
```
