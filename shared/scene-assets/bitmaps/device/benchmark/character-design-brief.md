# Main Character Design Brief

**Purpose:** A single, production-ready brief to generate a consistent original hero
("similar in appeal to Super Mario" — charming, readable, heroic silhouette — but not a
copy) plus 6–10 variants, on multi-angle character sheets, via ComfyUI Flux on m.tower1.
This brief is self-contained: a worker can produce consistent characters from it alone.

**Reference assets**
- Style/universe reference (hi-bit 16-bit pixel platformer scene):
  `C:/Users/m/Documents/desk-buddy/shared/scene-assets/bitmaps/device/benchmark/flux-dev/flux_03_txt2img_widescreen.png` (1344×768, 16:9)
- Sheet layout example (flat-vector 4-view turnaround on white):
  `C:/Users/m/Documents/desk-buddy/shared/scene-assets/bitmaps/device/benchmark/character-design-showing-multiple-angles-in-flat-style-art-vector.jpg` (1920×1047)

---

## 1. Style (fusion decision)

The two references disagree on rendering (scene = pixel art, sheet = flat vector). For a
**character sheet** the flat-vector treatment wins — it is the style of the layout we are
reproducing and is what makes consistent multi-angle generation achievable with a diffusion
model. So:

- **Rendering:** flat vector / cel-shaded. Solid fills, hard-edge shadow blocks only.
- **Line weight:** uniform thin near-black outline on every shape, constant stroke
  (no tapering, no pressure variation). Color `#1A1A1A`.
- **Shading:** simple two-tone (base + one darker shadow tone) per surface. Light from
  upper-right. No gradients, no airbrush, no soft brushes.
- **Palette:** bright, saturated, cheerful — warm character colors (orange/coral/teal)
  that pop against a clean background. Borrow the reference scene's saturated-platformer
  energy, not its pixelation.
- **Edges:** crisp, clean, scalable (sticker-like).

## 2. Character identity (canonical "anchor" design)

A young, cheerful **adventurer / courier**. Chunky and sturdy, confident and friendly.

- **Proportions:** ~4.5 heads tall (stylized hero — bigger head than a realistic 7-head
  figure, shorter than a 2.5-head chibi). Compact torso, wide stance, short sturdy limbs,
  mitten-like hands, big readable feet.
- **Silhouette (must read at small size):** large head under a **tilted cap**, a **short
  scarf** flaring behind for a sense of motion, broad shoulders, chunky boots. One glance
  = "heroic, on an adventure."
- **Hair:** messy warm-brown, a few strands poking out from under the cap.
- **Face:** large friendly eyes, small nose, cheerful confident grin. Simple, expressive,
  high readability.

### Outfit (specific)
| Item | Color (base) | Shadow |
|---|---|---|
| Tilted cap (signature) | Amber-orange `#F08A24` | `#C56A12` |
| Short flaring scarf (signature) | Coral `#E85D4A` | `#B83E30` |
| Jacket, cream cuffs | Teal `#2FA79B` | `#1F7A70` |
| Under-shirt | Cream `#F5E9D0` | — |
| Pants | Deep navy `#2C3E50` | `#1F2D3A` |
| Utility belt + small pouch | Amber-orange `#F08A24` | `#C56A12` |
| Boots (sturdy) | Brown `#6B4A2F` | `#4A331F` |
| Hair | Warm brown `#7A4E2D` | `#5A3820` |
| Skin | `#FAD7C3` | `#EAC4B0` |
| Outline | `#1A1A1A` | — |

### Accessories (signature anchors — keep on every variant)
- Tilted **orange cap**
- **Coral scarf** that flares behind (motion cue)
- **Amber utility belt** with one small pouch
- **Sturdy brown boots**

> Originality check: palette is teal/orange/coral — deliberately NOT Mario's red+blue or
> Luigi's green, while keeping the same bright, saturated, instantly-readable heroic feel.

## 3. Sheet layout requirements (from the sheet example)

- **Canvas:** landscape **16:9**, pure white `#FFFFFF` background. No gradients, texture,
  shadow, or scenery.
- **Views:** exactly **4 full-body views in one row**, left → right:
  1. Front  2. Left side profile  3. Back three-quarter  4. Full back
- **Alignment:** all four figures stand on the **same invisible baseline** (feet aligned
  near the bottom third); identical scale across views (same head height); heads near the
  top quarter. No vertical offset between views.
- **Spacing:** generous uniform margins (~6–8% each side); figures evenly spaced with
  consistent gaps; each figure ~20–22% of canvas width, centered in its column.
- **Pose:** relaxed standing in every view; clothing details (cap brim, scarf, belt pouch,
  cuffs, boot shape) preserved and consistent across all rotations.
- **No text/annotations:** no labels, arrows, measurements, swatches, or notes on the sheet.
- **Output:** PNG (or SVG).

## 4. Aspect ratio & resolution

- **Primary: 16:9 landscape** — e.g. **1344×768** (matches the style reference exactly) or
  **1536×864** / **1920×1080** (all fine; keep 16:9).
- For single hero concept tiles (not sheets), 1:1 or 4:3 is acceptable, but the
  **multi-angle sheet is always 16:9**.

## 5. Positive prompt (copy-paste)

```
flat vector character design sheet, character model sheet, full body, four views in a
single row (front, left side profile, back three-quarter, full back), clean uniform thin
black outlines, hard-edge cel shading, two-tone flat color, no gradients, bright saturated
cheerful colors, plain pure white background, evenly spaced, feet on one aligned baseline,
identical scale across all views, centered, generous margins, no text, no labels.
Character: young cheerful heroic adventurer, about 4.5 heads tall, large head, compact
sturdy body, wide stance, short limbs, big friendly eyes, cheerful confident grin, small
nose, messy warm-brown hair. Outfit: tilted orange cap, short coral scarf flaring behind,
teal jacket with cream cuffs, cream under-shirt, deep navy pants, amber utility belt with
small pouch, sturdy brown boots. Strong readable heroic silhouette, sticker style, game
hero character, crisp clean edges, vector art, high detail.
```

## 6. Negative prompt (copy-paste)

```
photorealistic, 3d render, realistic skin, gradient shading, soft airbrush, heavy or
varying line weight, sketchy pencil lines, messy, low detail, blurry, pixel art, pixelated,
extra fingers, extra limbs, extra arms, deformed, asymmetric, wrong proportions, text,
watermark, logo, signature, labels, annotations, letters, background scenery, environment,
complex background, clouds, city, multiple different characters, cropped, cut off,
low resolution, jpeg artifacts, noise, film grain
```

## 7. Consistency rules for the 6–10 variants

**KEEP FIXED (the shared style + anchors):**
flat vector, uniform thin black outline, two-tone hard-edge cel shading, pure white
background, 16:9 4-view sheet layout (front / left profile / back 3/4 / back), aligned
baseline, ~4.5-head heroic proportion, chunky readable silhouette, and the **signature
anchors: tilted cap + flaring scarf + utility belt + sturdy boots**.

**VARY (to get distinct candidates):**
cap color/style (cap, beanie, bucket hat, beret), hair color & style, jacket color,
scarf color, belt color, boots, and one small personality accessory (goggles, tool,
small backpack, bandana). Keep the overall bright-saturated heroic feel and the 4-anchor
silhouette so every candidate still reads as "the main hero."

Record for every candidate: full positive prompt, negative prompt, seed, resolution,
sampler/steps/CFG, and which variant it is.
