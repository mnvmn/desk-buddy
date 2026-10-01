# Token card (data card) — defect log

**All defects resolved.** The 5 visual defects found 2026-09-30 by comparing
the live ESP32-C6 token card against the widget goldens were fixed, rebuilt,
flashed to COM3, and re-captured + pixel-verified (all clean). Per the
"remove when confirmed fixed" workflow the open entries are removed below;
this page keeps the method + a short provenance note.

Device: ESP32-C6 480×480 AMOLED token card (two rows, one per server).
Method: live capture via `firmware/tools/cap_card.py <idle|working> out.png`
(`CAR` → data card, `CAP` → 480×480, CCW-unrotated to upright), then
`numpy` band-scan pixel probes + zoomed vision review against
`widget/tests/snapshots/{generating,idle}.png`.

Scope note (per `wiki/shared/parity/README.md`): general font/render and
row-structure differences between widget (egui) and device (LVGL) are **by
design** — parity is at the data level. Only concrete rendering errors are
defects.

## Resolved 2026-09-30 (fixed, flashed COM3, verified clean)

1. **Model name clipped at right edge** → `l_model` right-aligned
   (`LV_TEXT_ALIGN_RIGHT`, full row width); long names now truncate the
   *start* so the `.gguf` suffix always survives. Verified: model line ends at
   x=439 (was 461, at the border), fully visible.
2. **Middle-dot `·` tofu in footnote** → ASCII `|` (built-in Montserrat is
   ASCII-only, no middle-dot glyph). Verified: pipe, no box.
3. **Multiplication `×` tofu in slots chip** → ASCII `x`. Verified: `x2 slots`,
   no box.
4. **`NN total` left-packed** → split into its own right-anchored `l_total`
   label on the footer row (matches widget footer-right). Verified: `2M total`
   at the right edge, host at left.
5. **Capacity-line field order** → now `slots · ctx · mem% · ftype`, matching
   the widget's left-to-right order (was `ctx · mem% · ftype · slots`).
   Verified: `x2 slots 1k/100k 0% Q6_K`.

Full card re-capture after all fixes: clean — no clipping, overlap, tofu, or
collisions; model right-aligned and fully visible.

## Verification method (for future runs)
One call, no vision needed:
```
uv run --with pyserial --with pillow --with numpy \
  python esp32c6_480x480/firmware/tools/verify_card.py [--port COM3] [--out out.png]
```
Captures the live working card (reuse `cap_card`'s CAR/CAP/unrotate), then
pixel-checks the four regressions: model line not reaching x≥460 (clip),
capacity line leading with the slots chip, no tofu-box separators (hollow
dense squares), and the `NN total` footer right-aligned (ink to x≥420). Exits
0 = CLEAN, 1 = defect present — so it runs unattended after a flash. Add
`--no-gen` to check the idle card, or `--from <png>` to re-score an existing
frame (fast, no capture). If it reports a regression, capture a zoomed crop
and re-add it here as an open defect.
