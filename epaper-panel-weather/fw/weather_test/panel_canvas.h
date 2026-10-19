// Shared canvas: framebuffer, pixel setter, layout constants, pins,
// refresh timing. All other panel_* modules include this first.
#pragma once
#include <Arduino.h>
#include <math.h>
#include "EPD.h"

// NOTE: Paint_SetPixel(x,y,WHITE) SETS the buffer bit -> LIGHT on e-ink;
// Paint_SetPixel(x,y,BLK) CLEARS it -> dark ink. (EPD_ShowPicture maps a dark
// source pixel to buffer-clear, so direct drawing must use the same: INK=BLK.)
#define INK   BLACK
#define PAPER WHITE

// Framebuffer: 800x272 (two cascaded SSD1683s) - defined in panel_state.cpp.
extern uint8_t ImageBW[272 * 800 / 8];

#define EPD_PWR_PIN 7

// Input: the side rotary switch (active-low, pull-ups — Elecrow wiring)
#define BTN_PRESS  5   // rotary press / center -> toggle PAGE (weather <-> travel)
#define BTN_UP     6   // rotary up
#define BTN_DOWN   4   // rotary down
#define REFRESH_MS       (30UL * 60UL * 1000UL)       // 30 minutes (daytime)
#define REFRESH_MS_NIGHT (1UL  * 60UL * 60UL * 1000UL) // 1 hour, quiet hours 21:00–05:00
#define TRAVEL_REFRESH_MS (5UL * 60UL * 1000UL)        // 5 min (travel page; buses move)

// ---- layout constants (mirror tools/sim_weather_ui.py) --------------------
#define CX0   240
#define CX1   770
#define CY_T  16
#define CY_B  166
#define IX0   20
#define IGAP  52
#define ICY   236   // left icon row: optical center line, bottom of the left column
#define CH_Y  214
#define CH_H  40

// ---- drawing primitives -----------------------------------------------------
// The canvas is Paint_NewImage(800, 272, Rotation=180). Paint_SetPixel's
// rotate-180 case handles the physical mapping (including the 8-pixel cascade
// gap between the two 396-wide SSD1683s) internally — that is exactly what the
// proven-good EPD_ShowPicture path relies on (plain landscape x,y). We author
// in landscape mockup coordinates (mx,my) in [0..791]x[0..271] and pass them
// through untouched.
// Bounds-guarded: an out-of-range pixel is dropped, never written. The real
// Paint_SetPixel (EDP) has no bounds check, so an OOB coordinate would corrupt
// memory adjacent to the framebuffer (crash). The host stub clamps the same way,
// keeping host pixel-diffs honest.
static inline void cv_set(int mx, int my, uint16_t c) {
  if (mx < 0 || my < 0 || mx >= EPD_W || my >= EPD_H) return;
  Paint_SetPixel(mx, my, c);
}
