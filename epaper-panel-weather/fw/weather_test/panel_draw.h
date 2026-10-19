// Drawing primitives + weather icons + row glyphs + moon phase.
// Pure canvas operations: no network, no app state beyond the framebuffer.
#pragma once
#include "panel_canvas.h"

static inline void fill_circle(int cx, int cy, int r, uint16_t color) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx * dx + dy * dy <= r * r)
        cv_set(cx + dx, cy + dy, color);
}

// thick line (Bresenham, width ~ 2*h+1)
static inline void thick_line(int x0, int y0, int x1, int y1, uint16_t color, int w) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int e = dx + dy, h = w / 2;
  for (;;) {
    for (int t = -h; t <= h; t++) { cv_set(x0 + t, y0, color); cv_set(x0, y0 + t, color); }
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += sx; }
    if (e2 <= dx) { e += dx; y0 += sy; }
  }
}

static inline void fill_rect(int x0, int y0, int x1, int y1, uint16_t color) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      cv_set(x, y, color);
}

// rounded filled rect (radius r)
static inline void round_rect_fill(int x0, int y0, int x1, int y1, int r, uint16_t color) {
  for (int y = y0; y <= y1; y++) {
    int a = x0, b = x1;
    if (y < y0 + r) {
      int dy = y0 + r - y - 1;
      int dx = (int)sqrtf((float)(r * r - dy * dy));
      if (dx < 0) dx = 0;
      a = x0 + r - dx;
    } else if (y > y1 - r) {
      int dy = y - (y1 - r) + 1;
      int dx = (int)sqrtf((float)(r * r - dy * dy));
      if (dx < 0) dx = 0;
      b = x1 - r + dx;
    }
    for (int x = a; x <= b; x++) cv_set(x, y, color);
  }
}

// ---- weather icons ----------------------------------------------------------
static inline void icon_sun(int cx, int cy, int r) {
  for (int y = cy - r; y <= cy + r; y++)
    for (int x = cx - r; x <= cx + r; x++) {
      int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
      if (d2 >= (r - 2) * (r - 2)) cv_set(x, y, INK);
    }
  const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
  const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  for (int i = 0; i < 8; i++) {
    int ux = dx8[i], uy = dy8[i];
    int x0 = cx + ux * (r + 3), y0 = cy + uy * (r + 3);
    int x1 = cx + ux * (r + 8), y1 = cy + uy * (r + 8);
    if (ux != 0 && uy != 0) { x1 = cx + (ux < 0 ? -1 : 1) * (r + 6); y1 = cy + (uy < 0 ? -1 : 1) * (r + 6); }
    thick_line(x0, y0, x1, y1, INK, 2);
  }
}

// Crescent moon: a full ink disc knocked out by an offset paper disc (the
// simulator's two-circle trick). The cut is biased to the lower-right so the
// lit crescent opens to the upper-left.
static inline void icon_moon(int cx, int cy, int r) {
  fill_circle(cx, cy, r, INK);
  fill_circle(cx + r / 2, cy - r / 3, r, PAPER);
}

static inline void icon_cloud(int cx, int cy) {
  fill_circle(cx - 8, cy, 8, INK);
  fill_circle(cx, cy - 6, 8, INK);
  fill_circle(cx + 9, cy, 8, INK);
  for (int y = cy + 2; y <= cy + 8; y++)
    for (int x = cx - 14; x <= cx + 15; x++) cv_set(x, y, INK);
}

static inline void icon_part(int cx, int cy) {
  int sx = cx - 4, sy = cy - 4;
  for (int y = sy - 11; y <= sy + 11; y++)
    for (int x = sx - 11; x <= sx + 11; x++) {
      int d2 = (x - sx) * (x - sx) + (y - sy) * (y - sy);
      if (d2 >= 77 && d2 <= 116) cv_set(x, y, INK);
    }
  const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
  const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  for (int i = 0; i < 8; i++) {
    int ux = dx8[i], uy = dy8[i];
    int x0 = sx + ux * 11, y0 = sy + uy * 11;
    int x1 = sx + ux * 16, y1 = sy + uy * 16;
    if (ux != 0 && uy != 0) { x1 = sx + (ux < 0 ? -1 : 1) * 14; y1 = sy + (uy < 0 ? -1 : 1) * 14; }
    thick_line(x0, y0, x1, y1, INK, 2);
  }
  icon_cloud(cx, cy + 8);
}

// ---- small strip icons (~9px, 2-day clouds row) — verbatim from icons.js ----
static inline void s_cloud(int cx, int cy) {
  fill_circle(cx - 4, cy, 3, INK);
  fill_circle(cx, cy - 2, 3, INK);
  fill_circle(cx + 4, cy, 3, INK);
  for (int y = cy + 1; y <= cy + 2; y++)
    for (int x = cx - 6; x <= cx + 6; x++) cv_set(x, y, INK);
}
static inline void s_sun(int cx, int cy) {
  for (int y = cy - 3; y <= cy + 3; y++)
    for (int x = cx - 3; x <= cx + 3; x++) {
      int d2 = (x - cx)*(x - cx) + (y - cy)*(y - cy);
      if (d2 >= 4 && d2 <= 9) cv_set(x, y, INK);
    }
  const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
  const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  for (int i = 0; i < 8; i++)
    thick_line(cx + dx8[i]*4, cy + dy8[i]*4, cx + dx8[i]*6, cy + dy8[i]*6, INK, 1);
}
static inline void s_moon(int cx, int cy) {
  fill_circle(cx, cy, 3, INK);
  fill_circle(cx + 2, cy - 1, 3, PAPER);
}
static inline void s_part(int cx, int cy) {
  for (int y = cy - 5; y <= cy + 1; y++)
    for (int x = cx - 6; x <= cx; x++) {
      int d2 = (x - cx + 2)*(x - cx + 2) + (y - cy - 1)*(y - cy - 1);
      if (d2 >= 3 && d2 <= 8) cv_set(x, y, INK);
    }
  s_cloud(cx, cy + 2);
}
static inline void s_rain(int cx, int cy) {
  s_cloud(cx, cy - 2);
  for (int dx = -3; dx <= 3; dx += 3)
    for (int y = cy + 2; y <= cy + 4; y++) cv_set(cx + dx, y, INK);
}
static inline void s_snow(int cx, int cy) {
  thick_line(cx, cy - 4, cx, cy + 4, INK, 1);
  thick_line(cx - 4, cy - 2, cx + 4, cy + 2, INK, 1);
  thick_line(cx - 4, cy + 2, cx + 4, cy - 2, INK, 1);
}
static inline void s_bolt(int cx, int cy) {
  thick_line(cx + 2, cy - 4, cx - 1, cy, INK, 1);
  thick_line(cx - 1, cy, cx + 1, cy, INK, 1);
  thick_line(cx + 1, cy, cx - 2, cy + 4, INK, 1);
}
// WMO code (+ day/night) -> small icon. Code lists match the web exactly:
// bolt 95/96/99; snow 71/73/75/77/85/86; sun/moon 0; part 1/2; rain >=51; else cloud.
static inline void small_icon(int cx, int cy, int code, bool daytime) {
  if (code == 95 || code == 96 || code == 99) s_bolt(cx, cy);
  else if (code == 71 || code == 73 || code == 75 || code == 77 || code == 85 || code == 86) s_snow(cx, cy);
  else if (code == 0) (daytime ? s_sun : s_moon)(cx, cy);
  else if (code == 1 || code == 2) s_part(cx, cy);
  else if (code >= 51) s_rain(cx, cy);
  else s_cloud(cx, cy);
}

// ---- medium icons (~26px) for the v2.1 condition rows — verbatim from icons.js ----
// One cloud/sun/part/rain/snow/bolt glyph used by BOTH the left column (4 parts
// from now->midnight) and the 2-day strip (one per 6h block), via strip_icon.
static inline void m_cloud(int cx, int cy) {
  fill_circle(cx - 7, cy, 6, INK);
  fill_circle(cx, cy - 6, 6, INK);
  fill_circle(cx + 7, cy, 6, INK);
  for (int y = cy + 1; y <= cy + 6; y++)
    for (int x = cx - 11; x <= cx + 11; x++) cv_set(x, y, INK);
}
static inline void m_sun(int cx, int cy) {
  for (int y = cy - 6; y <= cy + 6; y++)
    for (int x = cx - 6; x <= cx + 6; x++) {
      int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
      if (d2 >= 16) cv_set(x, y, INK);
    }
  const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
  const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  for (int i = 0; i < 8; i++) {
    int ux = dx8[i], uy = dy8[i];
    int x0 = cx + ux * 8, y0 = cy + uy * 8;
    int x1 = cx + ux * 12, y1 = cy + uy * 12;
    if (ux != 0 && uy != 0) { x1 = cx + (ux < 0 ? -1 : 1) * 11; y1 = cy + (uy < 0 ? -1 : 1) * 11; }
    thick_line(x0, y0, x1, y1, INK, 1);
  }
}
static inline void m_part(int cx, int cy) {
  // sun arc + rays peeking from the cloud's upper-left (cloud painted last)
  for (int y = cy - 11; y <= cy + 2; y++)
    for (int x = cx - 11; x <= cx + 2; x++) {
      int d2 = (x - cx + 3) * (x - cx + 3) + (y - cy - 5) * (y - cy - 5);
      if (d2 >= 20 && d2 <= 42) cv_set(x, y, INK);
    }
  const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
  const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  for (int i = 4; i < 8; i++) {
    int ux = dx8[i], uy = dy8[i];
    thick_line(cx - 3 + ux * 8, cy - 5 + uy * 8, cx - 3 + ux * 12, cy - 5 + uy * 12, INK, 1);
  }
  m_cloud(cx + 3, cy + 4);
}
static inline void m_rain(int cx, int cy) {
  m_cloud(cx, cy - 4);
  const int dxs[3] = {-5, 0, 5};
  for (int d = 0; d < 3; d++)
    for (int y = cy + 5; y <= cy + 8; y++)
      for (int t = 0; t < 2; t++) cv_set(cx + dxs[d] + t, y, INK);
}
static inline void m_snow(int cx, int cy) {
  thick_line(cx, cy - 9, cx, cy + 9, INK, 1);
  thick_line(cx - 8, cy - 5, cx + 8, cy + 5, INK, 1);
  thick_line(cx - 8, cy + 5, cx + 8, cy - 5, INK, 1);
}
static inline void m_bolt(int cx, int cy) {
  thick_line(cx + 4, cy - 9, cx - 2, cy - 2, INK, 2);
  thick_line(cx - 2, cy - 2, cx + 2, cy - 1, INK, 2);
  thick_line(cx + 2, cy - 1, cx - 4, cy + 9, INK, 2);
}
// WMO code -> medium icon. Code list matches the web stripIcon EXACTLY:
// bolt 95/96/99; snow 71/73/75/77/85/86; sun 0; part 1/2; rain >=51; else cloud.
// (No day/night split — the 1-bit panel always shows the sun glyph for code 0.)
static inline void strip_icon(int cx, int cy, int code) {
  // Optical alignment on one vertical axis (cy): cloud-body glyphs (m_cloud,
  // m_part) are shifted so their cloud CENTRES land on cy — m_part draws its cloud
  // 4px below its anchor, so it needs -1 (not +3) to match a bare m_cloud (+3,
  // whose body centre is 3px above its anchor). Symmetric glyphs (m_sun/m_snow/
  // m_bolt) sit on cy directly; m_rain keeps +4 so its cloud+drop mass centres on cy.
  if (code == 95 || code == 96 || code == 99) m_bolt(cx, cy);
  else if (code == 71 || code == 73 || code == 75 || code == 77 || code == 85 || code == 86) m_snow(cx, cy);
  else if (code == 0) m_sun(cx, cy);
  else if (code == 1 || code == 2) m_part(cx, cy - 1);
  else if (code >= 51 || code >= 71) m_rain(cx, cy + 4);
  else m_cloud(cx, cy + 3);
}

// ---- left-column info-row glyphs (tiny, sit on paper) -----------------------
// Umbrella/precip glyph. Returns the x after the glyph (unused at fixed call sites).
static inline int row_rain(int x, int y) {
  int ux = x, uy = y + 3;
  for (int a = 0; a <= 180; a += 6) {
    float rad = a * 3.14159265f / 180.0f;
    cv_set(ux + (int)floorf(-5 * cosf(rad)), uy - 1 - (int)floorf(4 * sinf(rad)), INK);
  }
  for (int yy = uy; yy <= uy + 5; yy++) cv_set(ux, yy, INK);
  cv_set(ux + 1, uy + 5, INK);
  return ux + 12;
}
static inline int row_wind(int x, int y) {
  for (int t = 0; t < 2; t++)
    for (int xx = x; xx <= x + 14; xx++) cv_set(xx, y + t, INK);
  for (int t = 0; t < 3; t++) {
    cv_set(x + 14 - t, y + 3 + t, INK);
    cv_set(x + 14 - t + 1, y + 3 + t, INK);
  }
  return x + 18;
}
static inline int row_therm(int x, int y) {
  for (int yy = y - 4; yy <= y + 4; yy++)
    for (int t = 0; t < 3; t++) cv_set(x + t, yy, INK);
  fill_circle(x + 1, y + 6, 3, INK);
  return x + 9;
}
// Pixelate a moon disk at (cx,cy) radius r, phase 0..1 (0=new, .5=full).
static inline void draw_moon_phase(int cx, int cy, int r, float phase) {
  bool waxing = phase < 0.5f;
  float termX = cosf(phase * 2.0f * 3.14159265f);
  for (int y = cy - r; y <= cy + r; y++)
    for (int x = cx - r; x <= cx + r; x++) {
      int dx = x - cx, dy = y - cy;
      int d2 = dx * dx + dy * dy;
      if (d2 > r * r) continue;
      float nx = (float)dx / r;
      bool lit = waxing ? (nx > termX) : (nx < -termX);
      cv_set(x, y, lit ? INK : PAPER);
    }
  for (int y = cy - r - 1; y <= cy + r + 1; y++)
    for (int x = cx - r - 1; x <= cx + r + 1; x++) {
      int dx = x - cx, dy = y - cy;
      int d2 = dx * dx + dy * dy;
      if (d2 > (r + 1) * (r + 1) || d2 < (r - 1) * (r - 1)) continue;
      cv_set(x, y, INK);
    }
}
