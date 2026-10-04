# Bring-up test results (2026-09-26)

## Persistent live weather app + city toggle — PASS (2026-09-27)

- `fw/weather_test` is now the **persistent** weather app (no more deep sleep):
  it stays awake and **re-fetches Open-Meteo every 10 minutes** with a full
  refresh each cycle (doubles as a ghosting-clear). Two cities are configured —
  **Bratislava** (default, from `shared/net.env`) and **Munich** (48.1351,
  11.5820) — and the side **rotary switch** toggles between them. The choice is
  persisted in flash (`Preferences`), so a reboot resumes on the last city.
- **Layout (2026-09-27 redesign, matches HTML mock `esp32_weather_mockup_v4`):**
  1-bit e-ink, black ink on paper throughout. Left: 4 condition icons + big
  current temp (no degree ring — no ° glyph) + city name centered under it.
  Right: 13h curve + dithered night band + two plain value plates (end of
  window + biggest bulge off the now→end baseline). **Bottom row: three badges
  with a TOP BORDER ONLY (2px ink rule, no fill), black ink on paper** —
  umbrella + DRY 12H/RAIN, wind arrows + M/S (km/h / 3.6), thermometer + lo→hi.
  **Night hour-axis labels get white knock-out rects cut from the dither**
  (the night band extends down to y=CY_B+29 to cover the axis row; each label
  whose center falls inside the band is backed by a 28x18 PAPER rect).
- **Condition icons map WMO code → glyph; clear sky is day/night aware.**
  Code 0 (clear) → `icon_sun` during the day, `icon_moon` (a crescent — full
  ink disc knocked out by an offset paper disc) at night; 1/2 (partly cloudy)
  → `icon_part`; everything else → `icon_cloud`. Day/night comes from
  `is_daytime(h)`, which places forecast slot `h` (hours into "now") against
  the live `[sunrise, sunset]` window fetched from Open-Meteo (same
  `daily=sunrise,sunset` request, timezone=auto), wrapping across midnight;
  if that data is missing it falls back to a coarse ~05:00–17:00 heuristic.
- **Input mapping** (side rotary switch, active-low + pull-ups, per Elecrow
  wiring): PRESS/center = `IO5` → toggle city; UP = `IO6`, DOWN = `IO4` → any
  detent edge triggers an immediate refresh. (Debounce: simple rising-edge
  detect.)
- Each city shows its own live 13h forecast and sunset; the city name sits
  centered under the big temp.
- Boot + toggle verified over serial:
```
city: BRATISLAVA (index 0)
net: wifi up ... ip=192.168.5.253
wx: OK now=10.2 feels=8.1 wind=8.4 code=3 rain=0.0mm range=10.2..21.5 (13h)
panel up: BRATISLAVA live shown (wifi=1)
ser: toggle -> MUNICH (re-fetching)
wx: GET https://api.open-meteo.com/... latitude=48.1351&longitude=11.5820 ...
wx: OK now=8.4 feels=7.0 wind=3.6 code=1 ... sunset hour=19
panel up: MUNICH live shown (wifi=1)
```
- **Serial commands** (the app stays awake, so these are always live):
  `CAL` → prints now/sunset; `CAP` → streams the framebuffer (base64 between
  `CAP_START`/`CAP_END`); `CITY` → toggle city (mirrors the switch, used for
  headless verify).
- If Wi-Fi drops it opportunistically reconnects in the loop; a failed fetch
  shows the net-status bitmap until the next tick.
- Build gotcha: the Arduino `.ino`→`.cpp` preprocessor **hoists function
  prototypes to the top** of the file, so any custom type used in a signature
  (e.g. `struct WxCity`) must be **forward-declared near the top** or the
  hoisted prototypes fail with `'type' does not name a type`.
- `shared/net_config.h` is copied into `fw/weather_test/` for the build and is
  gitignored (`**/net_config.h`).
- Flash size ~1027 KB (78%).

## Live weather REST over HTTPS (Open-Meteo) — PASS (2026-09-26)

- `fw/test_sketch` now makes a **real TLS-verified HTTPS GET** to
  `WEATHER_HOST` (`api.open-meteo.com`) from `shared/net.env`, parses the
  current temp / feels-like / wind / weather code + 13h hourly temp range
  (raw string parsing — no JSON lib vendored in this build), prints it, then
  shows the weather bitmap and deep sleeps. Serial ground truth:

```
net: wifi up ssid=079U ip=192.168.5.253 rssi=-58 mac=44:1B:F6:92:8E:E0
wx: GET https://api.open-meteo.com/v1/forecast?latitude=48.1476&longitude=17.1067&...
wx: TLS verified (flags 0x00000000)
wx: 1167 bytes in 37ms (HTTP/1.1 200)
wx: OK now=14.9 feels=13.6 wind=5.3km/h code=0 range=10.5..14.9 (13h)
panel up: weather shown (wifi=1)
```

- **No API key needed** — Open-Meteo free tier is keyless.
- **Why raw mbedTLS + DER (not `WiFiClientSecure`):** this board's prebuilt
  `esp32-arduino-libs` (`idf-release_v5.5`, platform 3.3.3) has a **PEM parser
  that rejects the Let's Encrypt chain** — `mbedtls_x509_crt_parse` returns
  `MBEDTLS_ERR_X509_BAD_INPUT_DATA` on byte-correct PEM (host openssl parses
  the same bytes fine). Its **DER parser works**, so the firmware drives
  mbedTLS directly and feeds **DER** trust anchors (`le_dercainfo.h`, from
  `tools/gen_le_dercainfo.py`). Verified: handshake OK, `verify flags 0x00000000`
  (clean), leaf ← ISRG X1 chain. Proven first in the `fw/certdump` diagnostic.
- Response is `Transfer-Encoding: chunked`; the firmware **de-chunks** the
  body before parsing (chunk-size hex lines are interleaved in the JSON and
  span chunk boundaries).
- JSON gotcha: `current_units` block precedes `current` and repeats the same
  key names as *strings* — parsing must anchor to `"current":{"`, and the
  hourly `time` array (ISO-8601, contains hex chars) must be skipped by
  anchoring on `"temperature_2m":[`.
- Flash size ~1019 KB (77%).

## Net status page flash (WiFi) — PASS (2026-09-26)

- `fw/test_sketch` is now the **weather test** firmware: connects Wi-Fi with
  the shared config (`shared/net.env` → `shared/net_config.h`, same creds the
  C6 uses), does the live weather fetch, shows the weather bitmap (or the
  net-status fallback bitmap `statusimg.h` from
  `tools/render_status_page.py` if Wi-Fi is down), deep sleeps. Serial ground
  truth:

```
== desk-buddy 5.79 e-paper: weather test ==
panel power: on
net: wifi up ssid=079U ip=192.168.5.253 rssi=-58 mac=44:1B:F6:92:8E:E0
init: blank full refresh done
wx: TLS verified (flags 0x00000000)
wx: OK now=14.9 feels=13.6 wind=5.3km/h code=0 range=10.5..14.9 (13h)
panel up: weather shown (wifi=1)
panel: deep sleep
```

- **WiFi association works** (20 s budget, rssi -58). The old `LAN_HOST:LAN_PORT`
  (llama-server) probe was removed — this device's only remote dependency is
  the weather API. (Historical note: an earlier build probed 192.168.4.75 and
  reported it unreachable because the S3 landed on 192.168.5.0/24 — no longer
  relevant to this firmware.)
- After `upload`, the board is left in download mode (`waiting for
  download`): exit with the DTR/RTS pulse (DTR low, RTS 50 ms pulse) and the
  boot log above appears.
- Flash size ~922 KB (70% of the 1.3 MB app partition).

## 12h weather mock flash — PASS

- Image: `fw/test_sketch/weatherimg.h` (generated by `tools/render_weather.py`,
  792×272 1bpp) — 12 h weather mock layout: 4 condition icons, big current
  temp + city name, temp curve with dithered night band (sunset→end), 5-point
  hour axis (every 3 h, e.g. 11/14/17/20/23), plain value plates at the far
  end of the curve and at the biggest bulge off the now→end baseline,
  three chips (DRY 13H · 10.9 KM/H · 15.5~21.4).
- Boot log (clean power-on reset):

```
== desk-buddy 5.79 e-paper test ==
panel power: on
init: blank full refresh done
panel up: 12h weather mock shown
panel: deep sleep
```

## Flash + verify — PASS

- Chip: ESP32-S3 QFN56 rev v0.2, 8MB PSRAM, MAC 44:1b:f6:92:8e:e0
- Port: COM4 (CH340)
- Flash size used: 8M (FQBN `esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=8M`)
- Image: `fw/test_sketch` — blank full refresh → full-screen custom 272×792 bitmap → deep sleep
- Boot log (clean power-on reset):

```
ESP-ROM:esp32s3-20210327
rst:0x1 (POWERON),boot:0xb (SPI_FAST_FLASH_BOOT)
mode:DIO, clock div:1
== desk-buddy 5.79 e-paper test ==
panel power: on
init: blank full refresh done
panel up: test image shown
panel: deep sleep
```

Sketch size: 345,691 bytes (26% of 1,310,720). Flash verified by hash,
boot confirmed by serial (reset via DTR/RTS toggle, then 25 s read).

## EDP library notes (vendor lib, adapted for arduino-cli)

- Source: Elecrow repo `example/arduino/Examples/5.79_Global_refresh/`
  (EPD.cpp/h, EPD_Init.cpp/h, spi.cpp/h, EPDfont.h) vendored into
  `lib/EDP/src/`. The example folder is flat (not an Arduino "library"), so it
  doesn't build as-is; it now lives as a proper library (`library.properties`,
  `src/` layout).
- EPDfont.h (font bitmaps) is needed by EPD.cpp even when unused — don't drop it.
- Library must be passed per-compile: `arduino-cli compile --library <path>/lib/EDP`
  (it isn't in the user sketchbook).
- `library.properties` must be LF-only; CRLF makes arduino-cli fail to parse
  ("invalid line format, should be 'key=value'").

## Panel geometry gotcha

- Two cascaded SSD1683s (each 400×300) drive the panel; there is an **8-column
  dead zone** at the splice, so:
  - full-screen via `EPD_Display(ImageBW)` → buffer/canvas must be **800×272**
    (`EPD_W`=800, `EPD_H`=272);
  - full-screen via `EPD_ShowPicture(x, y, 792, 272, ...)` → image data is
    **792×272** (mod-8 rows), the lib adds the offset internally.
- `Rotation` is defined as 180 in EPD.h — vendor examples render upside-down
  content assuming that; testimg.h was authored for the default orientation.
- Image format: 1bpp, row-major, MSB-first, row width multiple of 8.
- E-ink is 1-bit: render tool must use true black/white (no grays) —
  `render_weather.py` thresholds at <128 = ink. Preview files:
  `weather_preview.png` (color) and `weather_1bpp.png` (exact panel output).
- Pin 7 = display power (LDO enable), must be driven HIGH before EPD init
  (vendor examples do this first in setup()).

## Build gotcha (arduino-cli on this box)

- `tools/flash_test.sh` fails with `C:\c\Users\...` — MSYS path mangling of the
  script's `$(dirname)` output. Run arduino-cli directly with a native path:
  `ROOT="C:/Users/m/Documents/desk-buddy/esp32s3_272x792"; arduino-cli compile -b "<FQBN>" --library "$ROOT/lib/EDP" "$ROOT/fw/test_sketch"` then
  `arduino-cli upload -p COM4 -b "<FQBN>" "$ROOT/fw/test_sketch"`.
- Serial verification: if the board is in `waiting for download`, an RTS-only
  pulse with DTR held LOW exits it (DTR high = DIO0 held = download boot):
  `DTR=0; RTS=1 (50ms); RTS=0` → boot log → `panel up:`, then deep sleep.
- After `arduino-cli upload` the board is often left in download mode — send
  that pulse before expecting the app boot log.
- E-ink render pitfalls (see `defects.md`, D1–D16): any color with
  luminance > 128 vanishes on the 1-bit panel (grays must be checkerboard
  dithered); antialiased line edges break when thresholded — draw curves as
  pure black; `put_text()` fails hard on unknown glyphs (`/` and space were
  both missing once → silent "8 KM H"). Dark filled shapes (the bottom chips,
  fill `(23,25,30)`→ink) make anything drawn on them with `INK` invisible —
  icons on dark backgrounds must use the light text color (`CTXT`,
  `(235,238,242)`); data labels over the dither need a white plate + leader
  line to read as attached to their point.
