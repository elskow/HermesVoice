# Hardware BOM (besides the ESP32 classic)

Derived from the firmware pin map (`main/Kconfig.projbuild` defaults) and
drivers. ESP32 classic DevKit assumed (no PSRAM, no 5 GHz).

## Must buy (device does not work without)

| # | Part | Why this exact one | Wiring |
|---|---|---|---|
| 1 | INMP441 I2S mic module | Firmware speaks I2S Philips-standard, 16-bit, mono, 16 kHz (`node_audio.c`). The INMP441 speaks exactly this. Not an analog mic (no ADC path in code), not a PDM mic (wrong slot format) | WS to 25, SCK to 26, SD to 32, VDD to 3V3, GND to GND, L/R to GND (left channel = mono) |
| 2 | 0.96" SSD1306 OLED, 128x64, I2C, addr 0x3C | Backend is `esp_lcd_new_panel_ssd1306`, address hardcoded `0x3C` (`ssd1306_backend.c`). 4-pin I2C version, not SPI | SDA to 21, SCL to 22, VCC to 3V3, GND to GND |
| 3 | Tactile pushbutton 6x6 mm | PTT is GPIO 4, active-low, internal pull-up (`node_ptt.c`). Any momentary NO button works | One leg to GPIO4, other to GND. A 100 nF cap across it helps the 200 ms debounce in noisy rooms |
| 4 | LED + 220 ohm resistor (or LED module) | Status LED on GPIO 2, driven high = on. GPIO2 is a strapping pin: the LED load is fine, but do not hold buttons on it at reset | Anode via 220 ohm to GPIO2, cathode to GND |

## Bench infra (probably owned already)

| # | Part | Why |
|---|---|---|
| 5 | Micro-USB data cable | Flash + monitor at 460800 baud. Charge-only cables are the top fake "dead board" cause |
| 6 | Breadboard + about 20 jumper wires | Mic 5 + OLED 4 + button 2 + LED 2 = 13 connections minimum |
| 7 | 2.4 GHz WiFi AP (phone hotspot works) | ESP32 classic has no 5 GHz radio. SSID/pass via menuconfig or NVS (`NodeDev`/empty defaults) |

## Deliberately excluded

- SH1106 1.3" OLED: driver is SSD1306-only (`node_display.c` flags the swap). Buy later only with the driver rewrite.
- WROVER / PSRAM board: the 10 s / 320 KB cap fits plain DRAM by design. Only for 30 s clips.
- Speaker / DAC: reply path is display-only, no audio-out code exists.
- Level shifters: everything here is 3V3-native.

## First power checklist (maps to W1 in `device-workflows.md`)

1. Flash, monitor, expect `voice-node ready` + `ready` screen with `node-01` footer.
2. `no wifi, offline` on screen means W1d working: fix SSID/pass, not hardware.
3. Hold button about 2 s: `* recording` + LED on. Release: `> uploading`, then transcript + reply.
