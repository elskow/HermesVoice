# Voice Node

ESP32 firmware base: WiFi + MQTT + OTA + NVS config + health telemetry.
Framework: **ESP-IDF v5.x** (CMake-based). Ninja is the generator IDF uses under the hood.
Bazel is intentionally NOT used, see `docs/build-system.md` for why.

## Quick start

```bash
# 1. Install ESP-IDF v5.1+ and export it
. $HOME/esp/esp-idf/export.sh

# 2. Configure (WiFi/MQTT via menuconfig or sdkconfig.defaults)
idf.py menuconfig        # -> Component config -> Voice Node

# 3. Build / flash / monitor
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

See [docs/quickstart.md](docs/quickstart.md) for full setup (Linux/macOS/Windows + Docker).

## Layout

```
CMakeLists.txt            # top-level project (thin: project() + version)
sdkconfig.defaults        # sane dev defaults (NOT secrets)
partitions.csv            # factory + 2x OTA slots + NVS
main/                     # app entry: wiring only, no business logic
components/               # one folder per bounded context (own CMakeLists.txt)
  node_wifi/  node_mqtt/  node_config/  node_ota/  node_ptt/  node_health/
docs/                     # ADRs: build-system, wifi, provisioning, ota, testing
tools/                    # flash/monitor helpers, mqtt smoke test, gen_version.py
tests/host/               # host-runnable unit tests (no hardware needed)
.github/workflows/        # CI: build + host tests
```

## Rules

1. `main/` only wires components together, no logic there.
2. Each component has `include/<name>.h` (public API) + `src/*.c` (private impl).
3. No secrets in git. WiFi/MQTT creds go to NVS or env, never `sdkconfig.defaults`.
4. Every component returns `esp_err_t` and logs via `NODE_LOG*` (see `node_log`).
5. See `docs/` ADRs before adding a dependency or changing partitions/OTA.

## Config

Kconfig options (`idf.py menuconfig -> Voice Node`):
`NODE_WIFI_SSID`, `NODE_WIFI_PASS`, `NODE_MQTT_URI`, `NODE_DEVICE_ID`,
`NODE_TELEMETRY_INTERVAL_S`, `NODE_PTT_GPIO`, `NODE_PTT_ACTIVE_LOW`.

Production: provision via NVS (`node_config`). Kconfig values are fallback defaults only.
