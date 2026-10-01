# esp32s3_272x792 — CrowPanel 5.79" E-Paper HMI (Elecrow)

Desk-buddy target device folder for the Elecrow **CrowPanel ESP32 5.79" e-paper HMI display**
(272×792, black & white, ESP32-S3).

## Device overview

| Item | Value |
| --- | --- |
| Size | 5.79 inch |
| MCU | ESP32-S3-WROOM-1-N8R8, up to 240 MHz |
| Flash | 8 MB |
| PSRAM | 8 MB (octal) |
| Display driver | 2 × SSD1683 |
| Resolution | 272 (H) × 792 (L) px, pixel pitch 0.1755 mm |
| Panel | AM electrophoretic, B/W, full viewing angle |
| Display interface | 3-/4-wire SPI (default 4-wire) |
| Board interfaces | UART0 ×1, BAT ×1 (SH1.0-2P), GPIO ×1 (2×10), TF card slot ×1 |
| Buttons | Rotary (dial) switch ×1, Menu ×1, Back ×1, RESET ×1, BOOT ×1 |
| Refresh | Partial refresh (fast mode) + full/4-gray refresh |
| Operating voltage | 2.2–3.7 V (3.7 V LiPo on BAT connector) |
| Active area | 47.74 (H) × 139.00 (L) mm |
| Environments | Arduino IDE, ESP-IDF, MicroPython |

E-ink: reflection-mode, no backlight; content persists when powered off.

## EPD SPI pin map (from Elecrow driver, v1.0)

| Signal | GPIO |
| --- | --- |
| SCK | 12 |
| MOSI | 11 |
| RES | 47 |
| DC | 46 |
| CS | 45 |
| BUSY | 48 |

## Connection / flashing (this machine)

- USB serial appears as **COM4** (CH340, VID 1A86 PID 7523). COM3 is an unrelated
  "Unknown USB Serial Device" (303A:1001) — do not flash there.
- `arduino-cli compile` (needs the shared config include + the vendored EDP
  lib):
  ```
  ROOT="C:/Users/m/Documents/desk-buddy/esp32s3_272x792"
  REPO="C:/Users/m/Documents/desk-buddy"
  INC="--build-property compiler.c.extra_flags=-I$REPO/shared --build-property compiler.SS.extra_flags=-I$REPO/shared --build-property compiler.cpp.extra_flags=-I$REPO/shared"
  arduino-cli compile -b "esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=8M" --library "$ROOT/lib/EDP" $INC "$ROOT/fw/test_sketch"
  ```
  (or just `tools/flash_test.sh`, which does compile+upload).

```
.venv/Scripts/python.exe -m esptool --port COM4 --baud 115200 <cmd>
```

- `--baud 115200` is the safe default; 460800 works too.
- Hold **BOOT** while plugging in / pressing RESET to enter download mode if
  auto-reset fails.
- Verify: `esptool ... flash_id` → ESP32-S3 QFN56 rev v0.2, 8MB PSRAM,
  MAC 44:1b:f6:92:8e:e0.

## Layout

- `fw/` — firmware sources / images (test images land here)
- `tools/` — helper scripts (render, flash)
- wiki: [`wiki/esp32s3_272x792/`](../wiki/esp32s3_272x792/) — bring-up
  results, gotchas, weather-mock defect log

## References

- Wiki: https://www.elecrow.com/wiki/CrowPanel_ESP32_E-paper_5.79-inch_HMI_Display.html
- Arduino tutorial: https://www.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_5.79inch_Arduino_Tutorial.html
- GitHub (drivers, factory firmware, examples): https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792
- Product page: https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html
