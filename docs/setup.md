# Setup: clone to first green run

Start here. `make doctor` is the source of truth; this page explains what
each line means and where keys come from.

```bash
git clone <repo> && cd made-detection
make doctor     # checks every prerequisite, prints install for misses
```

## Prerequisites

| Tool | Version | Install | Verify |
|---|---|---|---|
| Go | >= 1.25 (`relay/go.mod`) | https://go.dev/dl | `go version` |
| ESP-IDF | 6.1 at `~/.espressif/v6.1` | https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/get-started/ | `make doctor` |
| xtensa esp32 toolchain | esp-15.2.0 (bundled with IDF install) | comes with IDF | `make doctor` |
| gcc (macOS only) | gcc-16, for the linux sim (Apple clang rejects IDF mbedtls flags) | `brew install gcc` | `gcc-16 --version` |
| Docker | any recent, for the compose stack | https://docs.docker.com/get-docker | `docker ps` |
| qemu xtensa (optional) | only for `make test-qemu` (~3 min) | `idf_tools.py install qemu-xtensa` | `make doctor` |
| templ (optional) | only to edit `*.templ` UI files | `go install github.com/a-h/templ/cmd/templ@latest` | `templ version` |
| air (optional) | only for `make watch` live reload | `go install github.com/air-verse/air@latest` | `air -v` |

No devcontainer: ESP-IDF + USB flashing through Docker is worse than the
native toolchain. Install natively, let `doctor` confirm.

## Keys

```bash
cp relay/.env.example relay/.env   # then fill the two keys below
```

| `.env` line | Where to get it | Verified by |
|---|---|---|
| `HERMES_API_KEY` | your Hermes gateway: set `API_SERVER_KEY` in the shell (or root `.env`) before `docker compose up gateway`, then paste the **same value** here (`compose.yaml` documents the sync) | setup UI test button, or `curl -H "Authorization: Bearer $KEY" localhost:8642/v1/models` |
| `STT_DEEPGRAM_API_KEY` | https://console.deepgram.com (only when `STT_PROVIDER=deepgram`) | setup UI test button (rejects bad keys with 401) |

No keys needed for the first run: `STT_PROVIDER=stub` (default) returns a
canned transcript, so `./tools/dev.py` goes green with zero secrets.

## First green runs (in order)

```bash
make doctor      # all green (qemu line may MISS: optional)
make test        # firmware host tests + go tests + linux sim e2e (~30s)
./tools/dev.py   # relay + linux sim vs real Hermes: expect "heard:"
```

Then open the admin UI: `go run ./relay/cmd/hermes-voice` and visit
`http://127.0.0.1:8090/setup` (localhost only). Empty `.env` shows every
field unset; fill keys, test each provider inline, save, restart relay.

## Loops (which command for which change)

| I changed | Run | Green looks like |
|---|---|---|
| relay Go (`relay/`) | `make test-relay` | `ok hermes-voice/...` |
| relay + sim e2e | `./tools/dev.py` | `DEV STACK GREEN (expect: heard:)` |
| firmware C, no silicon | `make test-fw` (host) + `make test-linux` (sim) | host pass + dev.py green |
| display layout | host snapshot tests (`make test-fw`) | `[display]` lines pass |
| admin UI (`*.templ`) | `templ generate` + `go run ./relay/cmd/hermes-voice`, open `:8090` | page renders |
| ESP32 image | `make firmware` | `voice-node.bin` in `firmware/build/` |
| full stack | `docker compose up` | relay + mosquitto + gateway healthy |

Next: `docs/architecture.md` for why the system is shaped this way.
