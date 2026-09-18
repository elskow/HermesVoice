# Quickstart

## Prereqs

- ESP-IDF v6.1 installed at `~/.espressif/v6.1` (Makefile wires the env)
- ESP32 devkit + USB cable (silicon loop only)
- Homebrew gcc for the linux sim: `brew install gcc`

## Configure

```bash
$(WITH_IDF) idf.py -C firmware -B firmware/build menuconfig   # same env make uses
# navigate to: Component config -> Voice Node
# set WiFi SSID/pass, MQTT URI, device ID
```

For production, leave Kconfig as fallback and provision real credentials
into NVS (see `docs/provisioning.md`).

## Build / flash / monitor

```bash
make firmware              # ESP32 image in firmware/build/
make flash PORT=/dev/cu.usbserial-*
```

## Local loop (no hardware, no flash)

```bash
make test        # host unit + go vet/test + linux sim e2e + gofmt
./tools/dev.py   # relay + linux sim vs real Hermes, ~30s
```

## Watch MQTT traffic

Relay logs `[mqtt] node/<id>/...` lines in `./tools/dev.py` output already.
For a live broker, point mosquitto at the relay and subscribe `node/#`.

## Docker alternative

```bash
docker run --rm -v $PWD:/project -w /project \
  --device=/dev/ttyUSB0 espressif/idf:v6.1 \
  /bin/bash -c ". /opt/esp/idf/export.sh && idf.py build"
```

## Next steps

- `docs/provisioning.md` before giving hardware to anyone else
- `docs/ota.md` before first release (rollback + cert)
- `docs/testing.md` for the host-test pattern
