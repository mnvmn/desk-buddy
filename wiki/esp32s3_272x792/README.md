# ESP32-S3 E-Paper Panel (implemented — bring-up)

> Status: **bring-up done.** The Elecrow CrowPanel 5.79" e-paper HMI
> (272×792, 1-bit, ESP32-S3) powers up, renders pre-rendered 1bpp bitmaps,
> joins Wi-Fi, and does a live TLS-verified weather fetch (Open-Meteo),
> showing the weather page (net-status page as offline fallback).
> Current firmware: `fw/test_sketch` (weather test page).
> Device facts + flash recipe:
> [../../esp32s3_272x792/README.md](../../esp32s3_272x792/README.md).
> Shared docs: [../shared/](../shared/); sibling device (C6 AMOLED):
> [../esp32c6_480x480/](../esp32c6_480x480/).

## Documents

- [bringup.md](bringup.md) — flash + verify results with serial ground
  truth (blank test image → 12h weather mock → net-status page → live
  HTTPS weather), EDP library notes, panel geometry gotchas, 1-bit
  rendering pitfalls
- [defects.md](defects.md) — the 12h weather mock defect log D1–D16
  (all fixed & verified: what vanishes on 1-bit e-ink and why)

## Key gotchas (details in the docs)

- **Panel geometry**: two cascaded SSD1683s with an 8-column dead zone at
  the splice — full-screen buffers are 800×272; image data is 792×272
  (mod-8 rows), 1bpp row-major MSB-first.
- **1-bit only**: any color with luminance > 128 vanishes (dither grays
  with a checkerboard); draw icons on dark fills in the light color
  `(235,238,242)`.
- **TLS**: this Arduino core's PEM parser rejects the Let's Encrypt
  chain — feed DER anchors (`le_dercainfo.h`); chunked responses must be
  de-chunked before parsing.
- **Flashing**: COM4; after `arduino-cli upload` the board sits in
  download mode — exit with the DTR low / RTS pulse, then look for the
  `panel up:` serial line.
