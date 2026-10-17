// Desk-buddy CrowPanel 5.79" e-paper: LIVE weather app (persistent).
//
// Keeps the panel live and re-fetches Open-Meteo every 30 minutes — every
// 1 hour during quiet hours (21:00–05:00 local). Full refresh each cycle,
// which doubles as a ghosting-clear. Two cities are
// configured (Bratislava default, Munich); pressing the side rotary switch
// toggles between them (choice persisted in flash). Each cit shows its own
// live 13h forecast, local clock and sunset.
//
// Layout (shared 1:1 with tools/sim_weather_ui.py, same coordinates):
//   left  : 4 condition icons + big current temp + city name
//   right : 13h temp curve with night band + data labels + hour axis
//   bottom: three badge outlines (top border only, black ink on paper)
//
// Input: rotary switch (the side switch) — UP IO6 / DOWN IO4 / PRESS IO5,
//   all active-low with pull-ups (Elecrow wiring). PRESS toggles cit;
//   any detent edge also triggers an immediate refresh.
//
// Serial protocol (for headless verification):
//   CAP -> streams the current framebuffer as base64 lines between
//          CAP_START and CAP_END (the app now stays awake; CAP is always live).

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <Preferences.h>
#include <net_config.h>
#include "EPD.h"
#include "statusimg.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "esp_random.h"
#include "le_dercainfo.h"  // LE_ANCHORS: Let's Encrypt trust roots as DER

#define EPD_PWR_PIN 7

// Input: the side rotary switch (active-low, pull-ups — Elecrow wiring)
#define BTN_PRESS  5   // rotary press / center -> toggle cit
#define BTN_UP     6   // rotary up
#define BTN_DOWN   4   // rotary down
#define REFRESH_MS       (30UL * 60UL * 1000UL)       // 30 minutes (daytime)
#define REFRESH_MS_NIGHT (1UL  * 60UL * 60UL * 1000UL) // 1 hour, quiet hours 21:00–05:00

uint8_t ImageBW[272 * 800 / 8];  // buffer: 800x272 (two cascaded SSD1683)

// Forward decl: the .ino preprocessor hoists function prototypes to the top,
// so any type used in a signature must be declared before those prototypes.
struct WxCity;

// NOTE: Paint_SetPixel(x,y,WHITE) SETS the buffer bit -> LIGHT on e-ink;
// Paint_SetPixel(x,y,BLK) CLEARS it -> dark ink. (EPD_ShowPicture maps a dark
// source pixel to buffer-clear, so direct drawing must use the same: INK=BLK.)
#define INK   BLACK
#define PAPER WHITE

// ---- layout constants (mirror tools/sim_weather_ui.py) --------------------
#define CX0   240
#define CX1   770
#define CY_T  16
#define CY_B  166
#define IX0   20
#define IGAP  52
#define ICY   52
#define CH_Y  214
#define CH_H  40

// ---- 5x7 pixel font (same glyphs as the render tools) ----------------------
// Order: 0-9, A-H, space, K-Q, R-W, Y, -, ., :, /, h, I, J
static const uint8_t GLYPHS[][7] = {
  /*0 '0'*/{0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110},
  /*1 '1'*/{0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110},
  /*2 '2'*/{0b01110,0b10001,0b00001,0b00110,0b01100,0b10000,0b11111},
  /*3 '3'*/{0b11110,0b00001,0b00001,0b01110,0b00001,0b00001,0b11110},
  /*4 '4'*/{0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010},
  /*5 '5'*/{0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110},
  /*6 '6'*/{0b00110,0b10000,0b10000,0b11110,0b10001,0b10001,0b01110},
  /*7 '7'*/{0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000},
  /*8 '8'*/{0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110},
  /*9 '9'*/{0b01110,0b10001,0b10001,0b01111,0b00001,0b00001,0b00001},
  /*10 'A'*/{0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001},
  /*11 'B'*/{0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110},
  /*12 'C'*/{0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110},
  /*13 'D'*/{0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110},
  /*14 'E'*/{0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111},
  /*15 'F'*/{0b11111,0b10000,0b11110,0b10000,0b10000,0b10000,0b10000},
  /*16 'G'*/{0b01110,0b10001,0b10000,0b10110,0b10001,0b10001,0b01111},
  /*17 'H'*/{0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001},
  /*18 ' '*/{0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b00000},
  /*19 'K'*/{0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001},
  /*20 'L'*/{0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111},
  /*21 'M'*/{0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001},
  /*22 'N'*/{0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001},
  /*23 'O'*/{0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110},
  /*24 'P'*/{0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000},
  /*25 'Q'*/{0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101},
  /*26 'R'*/{0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001},
  /*27 'S'*/{0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110},
  /*28 'T'*/{0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100},
  /*29 'U'*/{0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110},
  /*30 'V'*/{0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100},
  /*31 'W'*/{0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001},
  /*32 'Y'*/{0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100},
  /*33 '-'*/{0b00000,0b00000,0b00000,0b11111,0b00000,0b00000,0b00000},
  /*34 '.'*/{0b00000,0b00000,0b00000,0b00000,0b00000,0b00110,0b00110},
  /*35 ':'*/{0b00000,0b00110,0b00000,0b00000,0b00000,0b00110,0b00000},
  /*36 '/'*/{0b00000,0b00011,0b00110,0b01100,0b11000,0b00000,0b00000},
  /*37 'h'*/{0b10000,0b10000,0b10100,0b10010,0b10010,0b10010,0b10010},
  /*38 'I'*/{0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b11111},
  /*39 'J'*/{0b01111,0b00001,0b00001,0b00001,0b10001,0b01001,0b00110},
  /* v2 extras (match web/panel_v2.html indices 40-45) */
  /*40 '~'*/{0b01011,0b00010,0b00010,0b00010,0b00010,0b00010,0b01011},
  /*41 '°'*/{0b00110,0b01001,0b01001,0b00110,0b00000,0b00000,0b00000},
  /*42 '>'*/{0b01000,0b00100,0b00010,0b00001,0b00010,0b00100,0b01000},
  /*43 '_'*/{0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b11111},
  /*44 'm'*/{0b00000,0b00000,0b01110,0b10101,0b10101,0b10101,0b10101},
  /*45 's'*/{0b00000,0b00000,0b01110,0b10000,0b01110,0b00001,0b01110},
};
static int glyph_idx(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'H') return 10 + (c - 'A');
  if (c == ' ') return 18;
  if (c >= 'K' && c <= 'Q') return 19 + (c - 'K');
  if (c >= 'R' && c <= 'W') return 26 + (c - 'R');
  if (c == 'Y') return 32;
  if (c == '-') return 33;
  if (c == '.') return 34;
  if (c == ':') return 35;
  if (c == '/') return 36;
  if (c == 'h') return 37;   // countdown suffix ("5.5h")
  if (c == 'I') return 38;   // city names (BRATISLAVA)
  if (c == 'J') return 39;
  /* v2 extras (match web/panel_v2.html) */
  if (c == '~') return 40;
  if ((unsigned char)c == 0xB0) return 41;  // '°' single-byte (written as \xB0 in strings)
  if (c == '>') return 42;
  if (c == '_') return 43;
  if (c == 'm') return 44;
  if (c == 's') return 45;
  return -1;
}

// ---- drawing primitives -----------------------------------------------------
// The canvas is Paint_NewImage(800, 272, Rotation=180). Paint_SetPixel's
// rotate-180 case handles the physical mapping (including the 8-pixel cascade
// gap between the two 396-wide SSD1683s) internally — that is exactly what the
// proven-good EPD_ShowPicture path relies on (plain landscape x,y). We author
// in landscape mockup coordinates (mx,my) in [0..791]x[0..271] and pass them
// through untouched.
static inline void cv_set(int mx, int my, uint16_t c) {
  Paint_SetPixel(mx, my, c);
}

static int put_text(int x, int y, const char *s, uint16_t color, int sc) {
  int cx = x;
  for (; *s; s++) {
    int gi = glyph_idx(*s);
    if (gi < 0) {  // D17 guard: warn instead of silently dropping a glyph
      Serial.printf("font: missing glyph for 0x%02x\n", (unsigned)(unsigned char)*s);
      continue;
    }
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5; c++)
        if (GLYPHS[gi][r] & (1 << (4 - c))) {
          for (int dy = 0; dy < sc; dy++)
            for (int dx = 0; dx < sc; dx++)
              cv_set(cx + c * sc + dx, y + r * sc + dy, color);
        }
    cx += 6 * sc;
  }
  return cx;
}
static int text_w(const char *s, int sc) { return (int)strlen(s) * 6 * sc; }

static void fill_circle(int cx, int cy, int r, uint16_t color) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx * dx + dy * dy <= r * r)
        cv_set(cx + dx, cy + dy, color);
}

// thick line (Bresenham, width ~ 2*h+1)
static void thick_line(int x0, int y0, int x1, int y1, uint16_t color, int w) {
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

static void fill_rect(int x0, int y0, int x1, int y1, uint16_t color) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      cv_set(x, y, color);
}

// rounded filled rect (radius r)
static void round_rect_fill(int x0, int y0, int x1, int y1, int r, uint16_t color) {
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
static void icon_sun(int cx, int cy, int r) {
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
static void icon_moon(int cx, int cy, int r) {
  fill_circle(cx, cy, r, INK);
  fill_circle(cx + r / 2, cy - r / 3, r, PAPER);
}

static void icon_cloud(int cx, int cy) {
  fill_circle(cx - 8, cy, 8, INK);
  fill_circle(cx, cy - 6, 8, INK);
  fill_circle(cx + 9, cy, 8, INK);
  for (int y = cy + 2; y <= cy + 8; y++)
    for (int x = cx - 14; x <= cx + 15; x++) cv_set(x, y, INK);
}

static void icon_part(int cx, int cy) {
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
static void s_cloud(int cx, int cy) {
  fill_circle(cx - 4, cy, 3, INK);
  fill_circle(cx, cy - 2, 3, INK);
  fill_circle(cx + 4, cy, 3, INK);
  for (int y = cy + 1; y <= cy + 2; y++)
    for (int x = cx - 6; x <= cx + 6; x++) cv_set(x, y, INK);
}
static void s_sun(int cx, int cy) {
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
static void s_moon(int cx, int cy) {
  fill_circle(cx, cy, 3, INK);
  fill_circle(cx + 2, cy - 1, 3, PAPER);
}
static void s_part(int cx, int cy) {
  for (int y = cy - 5; y <= cy + 1; y++)
    for (int x = cx - 6; x <= cx; x++) {
      int d2 = (x - cx + 2)*(x - cx + 2) + (y - cy - 1)*(y - cy - 1);
      if (d2 >= 3 && d2 <= 8) cv_set(x, y, INK);
    }
  s_cloud(cx, cy + 2);
}
static void s_rain(int cx, int cy) {
  s_cloud(cx, cy - 2);
  for (int dx = -3; dx <= 3; dx += 3)
    for (int y = cy + 2; y <= cy + 4; y++) cv_set(cx + dx, y, INK);
}
static void s_snow(int cx, int cy) {
  thick_line(cx, cy - 4, cx, cy + 4, INK, 1);
  thick_line(cx - 4, cy - 2, cx + 4, cy + 2, INK, 1);
  thick_line(cx - 4, cy + 2, cx + 4, cy - 2, INK, 1);
}
static void s_bolt(int cx, int cy) {
  thick_line(cx + 2, cy - 4, cx - 1, cy, INK, 1);
  thick_line(cx - 1, cy, cx + 1, cy, INK, 1);
  thick_line(cx + 1, cy, cx - 2, cy + 4, INK, 1);
}
// WMO code (+ day/night) -> small icon. Code lists match the web exactly:
// bolt 95/96/99; snow 71/73/75/77/85/86; sun/moon 0; part 1/2; rain >=51; else cloud.
static void small_icon(int cx, int cy, int code, bool daytime) {
  if (code == 95 || code == 96 || code == 99) s_bolt(cx, cy);
  else if (code == 71 || code == 73 || code == 75 || code == 77 || code == 85 || code == 86) s_snow(cx, cy);
  else if (code == 0) (daytime ? s_sun : s_moon)(cx, cy);
  else if (code == 1 || code == 2) s_part(cx, cy);
  else if (code >= 51) s_rain(cx, cy);
  else s_cloud(cx, cy);
}

// ---- left-column info-row glyphs (tiny, sit on paper) -----------------------
// Umbrella/precip glyph. Returns the x after the glyph (unused at fixed call sites).
static int row_rain(int x, int y) {
  int ux = x, uy = y + 3;
  for (int a = 0; a <= 180; a += 6) {
    float rad = a * 3.14159265f / 180.0f;
    cv_set(ux + (int)floorf(-5 * cosf(rad)), uy - 1 - (int)floorf(4 * sinf(rad)), INK);
  }
  for (int yy = uy; yy <= uy + 5; yy++) cv_set(ux, yy, INK);
  cv_set(ux + 1, uy + 5, INK);
  return ux + 12;
}
static int row_wind(int x, int y) {
  for (int t = 0; t < 2; t++)
    for (int xx = x; xx <= x + 14; xx++) cv_set(xx, y + t, INK);
  for (int t = 0; t < 3; t++) {
    cv_set(x + 14 - t, y + 3 + t, INK);
    cv_set(x + 14 - t + 1, y + 3 + t, INK);
  }
  return x + 18;
}
static int row_therm(int x, int y) {
  for (int yy = y - 4; yy <= y + 4; yy++)
    for (int t = 0; t < 3; t++) cv_set(x + t, yy, INK);
  fill_circle(x + 1, y + 6, 3, INK);
  return x + 9;
}

// ---- cities -------------------------------------------------------------------
// Second+ cities are defined here (Bratislava comes from shared net_config.h).
struct WxCity { const char *name; const char *lat; const char *lon; };
static const WxCity CITIES[2] = {
  { "BRATISLAVA", WEATHER_LAT, WEATHER_LON },  // index 0 (default, from shared config)
  { "MUNICH",     "48.1351",  "11.5820"      },  // index 1
};
static const int N_CITIES = 2;

static int   cur_city = 0;
static bool  wifi_ok = false;
static uint32_t lastTick = 0;             // last completed refresh (for the refresh timer)

// ---- weather data ------------------------------------------------------------
// One forecast day for the 2-day strip (tomorrow + day-after).
struct WXDay {
  char date[11];      // "YYYY-MM-DD"
  int codes[24];      // hourly weather codes (drives the 8 per-3h clouds)
  float temps[24];    // hourly temps (drives the daylight lo-hi range)
  float rain;         // total precip mm (from hourly sum)
  int sr_h, sr_m, ss_h, ss_m;  // sunrise/sunset local (for daylight range)
  float lo, hi;       // computed daylight min/max temp
};
struct WX {
  float cur, feels, wind;
  int code;
  float temps[13];
  int codes[13];
  float winds[13];    // hourly wind speed m/s (daylight interval)
  float rain_total;
  int sunrise_hour;  // local hour 0..23, -1 if unknown
  int sunrise_min;   // local minute 0..59, 0 if unknown
  int sunset_hour;   // local hour 0..23, -1 if unknown
  int sunset_min;    // local minute 0..59, 0 if unknown
  int now_hour, now_min;
  int now_year, now_month, now_day;  // current date (moon phase)
  char day_date[3][11];   // 3-day daily dates "YYYY-MM-DD" (index 0 = today)
  WXDay days[2];      // next 2 days (strip)
  bool ok;
};
static WX wx;

// Refresh interval from the last fetched local time: every 1h during quiet
// hours (21:00–05:00), otherwise every 30 min. Falls back to the daytime
// interval when the time is unknown (before first fetch).
static uint32_t current_refresh_ms() {
  int h = wx.now_hour;
  return (h >= 21 || h < 5) ? REFRESH_MS_NIGHT : REFRESH_MS;
}

// Day/night from the local time-of-day window [sunrise, sunset]. The 13h
// forecast starts at "now" and runs forward, so its leading hours can cross
// into the next night — each slot is checked against the same window. With no
// sunrise/sunset data we fall back to a coarse hour heuristic.
static bool is_daytime(int h) {
  if (wx.now_hour < 0) return true;
  int cur = wx.now_hour * 60 + wx.now_min;
  int win = h * 60;  // minutes into "now"
  int t = (cur + win) % 1440;  // absolute local minutes
  if (wx.sunrise_hour >= 0 && wx.sunset_hour >= 0) {
    int sr = wx.sunrise_hour * 60 + wx.sunrise_min;
    int ss = wx.sunset_hour * 60 + wx.sunset_min;
    if (sr < ss) return (t >= sr && t < ss);
    return (t >= sr || t < ss);  // night straddles midnight
  }
  return (t >= 300 && t < 1020);  // ~05:00–17:00 fallback
}

// x-extent of one hour slot for the night band: the column between the
// midpoints of its neighbouring points, extended 3px at the plot edges.
// Adjacent dark slots share a boundary and the global (x+y)%2 dither, so
// they merge into one seamless band.
static void slot_range(int i, const int *px, int *x0, int *x1) {
  *x0 = (i == 0) ? px[0] - 3 : (px[i] + px[i - 1]) / 2;
  *x1 = (i == 12) ? px[12] + 3 : (px[i] + px[i + 1]) / 2;
}

// format a float as "N.N" / "NN.N" (max 5 chars + NUL); returns char count
static int fmt_num(char *buf, float v, int dec) {
  if (isnan(v)) { buf[0] = '?'; buf[1] = 0; return 1; }
  bool neg = v < 0;
  if (neg) v = -v;
  long iv = (long)(v * (dec ? 10 : 1) + 0.5f);
  long ip = iv / (dec ? 10 : 1);
  long fp = iv % (dec ? 10 : 1);
  int p = 0;
  if (neg) buf[p++] = '-';
  if (ip >= 100) buf[p++] = '0' + ip / 100 % 10;
  if (ip >= 10)  buf[p++] = '0' + ip / 10 % 10;
  buf[p++] = '0' + ip % 10;
  if (dec) { buf[p++] = '.'; buf[p++] = '0' + fp; }
  buf[p] = 0;
  return p;
}

// ---- date + daylight helpers (v2) -------------------------------------------
// Day of week: Tomohiko Sakamoto's algorithm. 0=Sunday .. 6=Saturday.
static int dow_sakamoto(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
// "YYYY-MM-DD" -> 0..6 (Sun..Sat)
static int date_dow(const char *dstr) {
  int y = (dstr[0]-'0')*1000 + (dstr[1]-'0')*100 + (dstr[2]-'0')*10 + (dstr[3]-'0');
  int m = (dstr[5]-'0')*10 + (dstr[6]-'0');
  int d = (dstr[8]-'0')*10 + (dstr[9]-'0');
  return dow_sakamoto(y, m, d);
}
static const char *DOW3[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

// min/max of hourly temps over daylight hours (sr/ss in fractional local hours;
// <0 = unknown -> 5-19h). Fallback: all hours, then 0. Used by the 2-day strip.
static void daylight_range(const float *temps, int n, float srH, float ssH, float *lo, float *hi) {
  *lo = NAN; *hi = NAN;
  for (int h = 0; h < n; h++) {
    bool isD = (srH < 0 || ssH < 0) ? (h >= 5 && h < 19)
                                    : (h >= (int)floorf(srH) && h < (int)ceilf(ssH));
    if (!isD) continue;
    float t = temps[h];
    if (isnan(t)) continue;
    if (isnan(*lo) || t < *lo) *lo = t;
    if (isnan(*hi) || t > *hi) *hi = t;
  }
  if (isnan(*lo)) {
    for (int h = 0; h < n; h++) {
      float t = temps[h];
      if (isnan(t)) continue;
      if (isnan(*lo) || t < *lo) *lo = t;
      if (isnan(*hi) || t > *hi) *hi = t;
    }
  }
  if (isnan(*lo)) { *lo = 0; *hi = 0; }
}

// ---- network: raw mbedTLS -----------------------------------------------------
static int le_rng(void *p, unsigned char *out, size_t len) {
  (void)p; esp_fill_random(out, len); return 0;
}

// GET https://WEATHER_HOST/path -> body (de-chunked). Returns bytes or -1.
static int http_get(const char *path, char *body_out, size_t cap) {
  char req[512];
  int ql = snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\n"
                "User-Agent: desk-buddy-s3\r\nConnection: close\r\n\r\n", path, WEATHER_HOST);
  if (ql < 0 || (size_t)ql >= sizeof(req)) return -1;
  Serial.printf("wx: GET https://%s%s\n", WEATHER_HOST, path);

  mbedtls_net_context net; mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
  mbedtls_x509_crt cacrt;
  mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
  mbedtls_x509_crt_init(&cacrt);

  int r = mbedtls_net_connect(&net, WEATHER_HOST, "443", MBEDTLS_NET_PROTO_TCP);
  if (r) { char b[80]; mbedtls_strerror(r, b, sizeof(b)); Serial.printf("wx: tcp FAIL (%s)\n", b); return -1; }

  mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, NULL);
  mbedtls_ssl_conf_rng(&conf, le_rng, NULL);
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  for (size_t i = 0; i < sizeof(LE_ANCHORS) / sizeof(LE_ANCHORS[0]); i++)
    mbedtls_x509_crt_parse_der(&cacrt, LE_ANCHORS[i].der, LE_ANCHORS[i].len);
  mbedtls_ssl_conf_ca_chain(&conf, &cacrt, NULL);
  mbedtls_ssl_setup(&ssl, &conf);
  mbedtls_ssl_set_hostname(&ssl, WEATHER_HOST);
  mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);

  int h = mbedtls_ssl_handshake(&ssl);
  if (h) { char b[160]; mbedtls_strerror(h, b, sizeof(b)); Serial.printf("wx: TLS FAIL r=%d (%s)\n", h, b); return -1; }
  Serial.printf("wx: TLS verified (flags 0x%08lx)\n", (unsigned long)mbedtls_ssl_get_verify_result(&ssl));

  if (mbedtls_ssl_write(&ssl, (const unsigned char *)req, (size_t)ql) < 0) {
    Serial.println("wx: request write FAIL");
    mbedtls_net_free(&net); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&cacrt);
    return -1;
  }

  static char raw[4200];
  int n = 0; uint32_t t0 = millis(); bool closed = false;
  while (!closed && millis() - t0 < 15000) {
    int rc = mbedtls_ssl_read(&ssl, (unsigned char *)raw + n, sizeof(raw) - 1 - n);
    if (rc > 0) { n += rc; continue; }
    if (rc == MBEDTLS_ERR_SSL_WANT_READ) { delay(10); continue; }
    if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == MBEDTLS_ERR_NET_CONN_RESET) { closed = true; break; }
    Serial.printf("wx: read FAIL r=0x%04x\n", (unsigned)rc); break;
  }
  raw[n] = 0;
  char hdr[32]; sscanf(raw, "%31s", hdr);
  int sc = 0; sscanf(raw, "%*s %d", &sc);
  Serial.printf("wx: %d bytes in %lums (%s %d)\n", n, millis() - t0, hdr, sc);

  mbedtls_ssl_close_notify(&ssl);
  mbedtls_net_free(&net); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&cacrt);

  char *p = strstr(raw, "\r\n\r\n");
  if (p) p += 4; else p = raw;
  // de-chunk
  char *o = body_out; char *src = p;
  while (*src) {
    if (isxdigit((unsigned char)src[0])) {
      unsigned long len = 0;
      while (isxdigit((unsigned char)*src)) {
        len = len * 16 + (isdigit((unsigned char)*src) ? *src - '0' : *src - 'a' + 10);
        src++;
      }
      if (*src == '\r') { src++; if (*src == '\n') src++; }
      if (len == 0) break;
      if ((size_t)(o - body_out) + len >= cap) break;
      memcpy(o, src, len); o += len; src += len;
      if (*src == '\r') src++;
      if (*src == '\n') src++;
    } else {
      if ((size_t)(o - body_out) + 1 >= cap) break;
      *o++ = *src++;
    }
  }
  *o = 0;
  return (int)(o - body_out);
}

static float num_after(const char *j, const char *key) {
  const char *p = strstr(j, key);
  if (!p) return NAN;
  while (*p && *p != ':') p++;
  if (!*p) return NAN;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  if (!(*p == '-' || (*p >= '0' && *p <= '9') || *p == '.')) return NAN;
  return (float)atof(p);
}

static int parse_num_array(const char *j, const char *key, float *out, int max) {
  const char *k = strstr(j, key);
  if (!k) return -1;
  const char *a = strchr(k, '[');
  const char *end = a ? strchr(a, ']') : NULL;
  if (!a || !end) return -1;
  const char *c = a + 1;
  int n = 0;
  while (c < end && n < max) {
    if (*c == '-' || (*c >= '0' && *c <= '9') || *c == '.') {
      float v = (float)atof(c);
      if (!isnan(v)) out[n++] = v;
      const char *e = c;
      while (e < end && (*e == '-' || (*e >= '0' && *e <= '9') || *e == '.')) e++;
      c = e + 1;
    } else c++;
  }
  return n;
}

// Nth quoted string in the array following `key` (e.g. key=\"time\":[, n=1 -> 2nd date)
static int json_str_array(const char *j, const char *key, int n, char *out, int cap) {
  const char *k = strstr(j, key);
  if (!k) return -1;
  const char *a = strchr(k, '[');
  const char *end = a ? strchr(a, ']') : NULL;
  if (!a || !end) return -1;
  int cnt = -1;
  const char *c = a;
  while (c < end) {
    if (*c == '"') {
      const char *s = c + 1;
      const char *e = strchr(s, '"');
      if (!e || e > end) break;
      cnt++;
      if (cnt == n) {
        int len = (int)(e - s);
        if (len >= cap) len = cap - 1;
        memcpy(out, s, len); out[len] = 0;
        return 0;
      }
      c = e + 1;
    } else c++;
  }
  return -1;
}

static int fetch_weather(const WxCity *cit) {
  static char body[4200];
  char path[512];
  snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%s&longitude=%s"
           "&current=temperature_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m"
           "&hourly=temperature_2m,weather_code,precipitation,wind_speed_10m&forecast_hours=13&timezone=auto"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max,sunrise,sunset&forecast_days=3",
           cit->lat, cit->lon);
  int bl = http_get(path, body, sizeof(body));
  if (bl < 0) return false;
  const char *cblk = strstr(body, "\"current\":{");
  wx.cur   = cblk ? num_after(cblk, "\"temperature_2m\"") : NAN;
  wx.feels = cblk ? num_after(cblk, "\"apparent_temperature\"") : NAN;
  wx.wind  = cblk ? num_after(cblk, "\"wind_speed_10m\"") : NAN;
  wx.code  = (int)(cblk ? num_after(cblk, "\"weather_code\"") : NAN);
  parse_num_array(body, "\"temperature_2m\":[", wx.temps, 13);
  parse_num_array(body, "\"weather_code\":[", (float *)wx.codes, 13);
  {   // hourly wind (km/h -> m/s)
    float w[13];
    int wn = parse_num_array(body, "\"wind_speed_10m\":[", w, 13);
    for (int i = 0; i < 13; i++) wx.winds[i] = (i < wn) ? w[i] / 3.6f : NAN;
  }
  const char *k = strstr(body, "\"precipitation\":[");
  if (k) {
    const char *a = strchr(k, '[');
    const char *end = a ? strchr(a, ']') : NULL;
    if (a && end) {
      const char *c = a + 1;
      float tot = 0;
      while (c < end) {
        if (*c == '-' || (*c >= '0' && *c <= '9') || *c == '.') {
          tot += (float)atof(c);
          const char *e = c;
          while (e < end && (*e == '-' || (*e >= '0' && *e <= '9') || *e == '.')) e++;
          c = e + 1;
        } else c++;
      }
      wx.rain_total = tot;
    }
  }
  // local clock: hourly.time[0] is the current local hour (timezone=auto)
  {
    const char *k = strstr(body, "\"hourly\":{\"time\":[\"");
    if (k) {
      const char *q = k + strlen("\"hourly\":{\"time\":[\"");
      if (q[10] == 'T') {
        int hh = (q[11] - '0') * 10 + (q[12] - '0');
        int mm = (q[14] - '0') * 10 + (q[15] - '0');
        if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
          wx.now_hour = hh; wx.now_min = mm;
        }
        // current date prefix "YYYY-MM-DD" (feeds day-of-week + moon phase)
        wx.now_year  = (q[0]-'0')*1000 + (q[1]-'0')*100 + (q[2]-'0')*10 + (q[3]-'0');
        wx.now_month = (q[5]-'0')*10 + (q[6]-'0');
        wx.now_day   = (q[8]-'0')*10 + (q[9]-'0');
        Serial.printf("wx: now=%d:%d date=%04d-%02d-%02d (from forecast)\n",
                      hh, mm, wx.now_year, wx.now_month, wx.now_day);
      }
    }
  }
  // 3-day daily: today's sunrise/sunset (replaces the old fetch_sunset request)
  // plus the two future dates + per-day sr/ss that the 2-day strip needs.
  const char *db = strstr(body, "\"daily\":{");
  if (db) {
    char ts[24];
    for (int di = 0; di < 3; di++) json_str_array(db, "\"time\":[", di, wx.day_date[di], 11);
    if (json_str_array(db, "\"sunrise\":[", 0, ts, sizeof ts) == 0) { int hh, mm; if (parse_hhmm(ts, &hh, &mm) == 0) { wx.sunrise_hour = hh; wx.sunrise_min = mm; } }
    if (json_str_array(db, "\"sunset\":[", 0, ts, sizeof ts) == 0)  { int hh, mm; if (parse_hhmm(ts, &hh, &mm) == 0) { wx.sunset_hour = hh; wx.sunset_min = mm; } }
    for (int di = 1; di < 3; di++) {
      memcpy(wx.days[di - 1].date, wx.day_date[di], 11);
      if (json_str_array(db, "\"sunrise\":[", di, ts, sizeof ts) == 0) { int hh, mm; if (parse_hhmm(ts, &hh, &mm) == 0) { wx.days[di - 1].sr_h = hh; wx.days[di - 1].sr_m = mm; } }
      if (json_str_array(db, "\"sunset\":[", di, ts, sizeof ts) == 0)  { int hh, mm; if (parse_hhmm(ts, &hh, &mm) == 0) { wx.days[di - 1].ss_h = hh; wx.days[di - 1].ss_m = mm; } }
    }
    Serial.printf("wx: daily %s / %s / %s\n", wx.day_date[0], wx.day_date[1], wx.day_date[2]);
    Serial.printf("wx: sunrise %d:%02d sunset %d:%02d\n",
                  wx.sunrise_hour, wx.sunrise_min, wx.sunset_hour, wx.sunset_min);
  }
  // Seed the graph's first point with the LIVE current temp, not the model's
  // first-hour forecast. Open-Meteo's current.temperature_2m (live obs) and
  // hourly[0] can disagree for the same "now" hour, which made the big number
  // and the curve's left edge contradict each other. Using the live value for
  // "now" makes the big temp, the curve start, and the min/max all agree.
  if (!isnan(wx.cur)) wx.temps[0] = wx.cur;
  wx.ok = !isnan(wx.cur) && !isnan(wx.temps[0]);
  float lo = NAN, hi = NAN;
  for (int i = 0; i < 13; i++) {
    if (isnan(wx.temps[i])) continue;
    if (isnan(lo) || wx.temps[i] < lo) lo = wx.temps[i];
    if (isnan(hi) || wx.temps[i] > hi) hi = wx.temps[i];
  }
  Serial.printf("wx: OK now=%.1f feels=%.1f wind=%.1f code=%d rain=%.1fmm range=%.1f..%.1f (13h)\n",
                wx.cur, wx.feels, wx.wind, wx.code, wx.rain_total, lo, hi);
  return wx.ok;
}

// Read the hourly "time" array -> per-entry local hour + "YYYY-MM-DD" date.
static int json_hour_array(const char *j, const char *key, int *hour, char (*date)[11], int max) {
  const char *k = strstr(j, key);
  if (!k) return -1;
  const char *a = strchr(k, '[');
  const char *end = a ? strchr(a, ']') : NULL;
  if (!a || !end) return -1;
  int n = 0;
  const char *c = a;
  while (c < end && n < max) {
    if (*c == '"') {
      const char *s = c + 1;
      const char *e = strchr(s, '"');
      if (!e || e > end) break;
      memcpy(date[n], s, 10); date[n][10] = 0;
      hour[n] = (s[10] == 'T') ? ((s[11] - '0') * 10 + (s[12] - '0')) : -1;
      n++;
      c = e + 1;
    } else c++;
  }
  return n;
}

// F3: 2-day hourly GET (start_date=day1 end_date=day2) -> wx.days[0..1].
// Fills per-hour codes/temps, sums rain, and computes the daylight lo/hi.
static bool fetch_two_days(const WxCity *cit) {
  static char body[4200];
  static char dates[48][11];
  static int   hours[48];
  static float tempv[48];
  static int   codev[48];
  static float precv[48];
  if (wx.days[0].date[0] == 0 || wx.days[1].date[0] == 0) return false;
  char path[512];
  snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%s&longitude=%s"
           "&hourly=temperature_2m,weather_code,precipitation"
           "&start_date=%s&end_date=%s&timezone=auto",
           cit->lat, cit->lon, wx.days[0].date, wx.days[1].date);
  int bl = http_get(path, body, sizeof(body));
  if (bl < 0) return false;
  int nt = json_hour_array(body, "\"time\":[", hours, dates, 48);
  int nn1 = parse_num_array(body, "\"temperature_2m\":[", tempv, 48);
  int nn2 = parse_num_array(body, "\"weather_code\":[", (float *)codev, 48);
  int nn3 = parse_num_array(body, "\"precipitation\":[", precv, 48);
  int n = nt;
  if (nn1 < n) n = nn1; if (nn2 < n) n = nn2; if (nn3 < n) n = nn3;
  for (int di = 0; di < 2; di++) {
    for (int hh = 0; hh < 24; hh++) { wx.days[di].codes[hh] = 0; wx.days[di].temps[hh] = NAN; }
    wx.days[di].rain = 0;
    for (int i = 0; i < n; i++) {
      if (hours[i] < 0 || hours[i] > 23) continue;
      if (strcmp(dates[i], wx.days[di].date) != 0) continue;
      int hh = hours[i];
      wx.days[di].codes[hh] = codev[i];
      wx.days[di].temps[hh] = tempv[i];
      wx.days[di].rain += precv[i];
    }
    float srH = (wx.days[di].sr_h >= 0) ? (wx.days[di].sr_h + wx.days[di].sr_m / 60.0f) : -1.0f;
    float ssH = (wx.days[di].ss_h >= 0) ? (wx.days[di].ss_h + wx.days[di].ss_m / 60.0f) : -1.0f;
    daylight_range(wx.days[di].temps, 24, srH, ssH, &wx.days[di].lo, &wx.days[di].hi);
    Serial.printf("wx: day%d %s rain=%.1f lo=%.1f hi=%.1f\n", di, wx.days[di].date,
                  wx.days[di].rain, wx.days[di].lo, wx.days[di].hi);
  }
  return true;
}

static int parse_hhmm(const char *q, int *hh, int *mm) {
  // q points at the first char of an ISO timestamp "YYYY-MM-DDTHH:MM:SS" —
  // the date starts with the year digit, the 'T' separator is at index 10.
  if (!q || q[10] != 'T') return -1;
  int h = (q[11] - '0') * 10 + (q[12] - '0');
  int m = (q[14] - '0') * 10 + (q[15] - '0');
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  *hh = h; *mm = m;
  return 0;
}

// (fetch_sunset removed — today's sunrise/sunset now come from the 3-day daily
//  inside fetch_weather, so it is no longer a separate network request.)

static int fetch_time() {
  // clock is parsed from the forecast body (hourly.time[0]) in fetch_weather;
  // this is a no-op kept for interface clarity.
  return wx.now_hour >= 0;
}

// ---- UI rendering (coords = tools/sim_weather_ui.py) --------------------------
// ---- moon phase (F10) --------------------------------------------------------
// Day number from a civil date (Howard Hinnant's days_from_civil).
static long days_from_civil(int y, int m, int d) {
  y -= (m <= 2);
  int era = (int)(((y >= 0) ? y : y - 399) / 400);
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (long)era * 146097L + (long)doe - 719468;
}
// Illuminated phase 0..1 (0=new, .5=full) from the current forecast date/time.
static float moon_phase() {
  if (wx.now_year < 1970) return 0.5f;
  const float synodic = 29.53058867f;
  const float ref = (float)days_from_civil(2000, 1, 6) + 18.2333f / 24.0f;  // known new moon
  float cur = (float)days_from_civil(wx.now_year, wx.now_month, wx.now_day)
              + (wx.now_hour + wx.now_min / 60.0f) / 24.0f;
  float p = fmodf(cur - ref, synodic) / synodic;
  if (p < 0) p += 1.0f;
  return p;
}
// Pixelate a moon disk at (cx,cy) radius r, phase 0..1 (0=new, .5=full).
static void draw_moon_phase(int cx, int cy, int r, float phase) {
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

// ---- rain-state classification (F12) ----------------------------------------
// A serial STATE override (F19) wins; otherwise severity priority:
// storm 95/96/99  >  snow 71/73/75/77/85/86  >  rain_total>=0.5  >  dry.
static const char *state_ov = nullptr;
static bool codes_has(const int *a, int n) {
  for (int i = 0; i < n; i++)
    for (int k = 0; k < 13; k++)
      if (wx.codes[k] == a[i]) return true;
  return false;
}
static const char *rain_state() {
  if (state_ov) return state_ov;
  static const int storm[3] = {95, 96, 99};
  static const int snow[6] = {71, 73, 75, 77, 85, 86};
  if (codes_has(storm, 3)) return "STORM";
  if (codes_has(snow, 6)) return "SNOW";
  if (wx.rain_total >= 0.5f) return "RAIN";
  return "DRY";
}

static void draw_temp_curve() {
  float lo = NAN, hi = NAN;
  for (int i = 0; i < 13; i++) {
    if (isnan(wx.temps[i])) continue;
    if (isnan(lo) || wx.temps[i] < lo) lo = wx.temps[i];
    if (isnan(hi) || wx.temps[i] > hi) hi = wx.temps[i];
  }
  if (isnan(lo)) return;
  // keep the points off the plot edges (visual only — labels show the raw values)
  float plo = lo - 1.0f, phi = hi + 1.0f;
  float range = phi - plo;
  if (range < 1.0f) range = 1.0f;

  int px[13], py[13];
  for (int i = 0; i < 13; i++) {
    px[i] = CX0 + i * (CX1 - CX0) / 12;
    py[i] = CY_T + (int)((phi - wx.temps[i]) * (CY_B - CY_T) / range + 0.5f);
  }

  // night band: one dithered column per dark slot (is_daytime), extending
  // down behind the hour-axis row (the axis labels inside it get white
  // knock-outs). Autumn days are shorter than the 12h window, so a window
  // can hold the tail of one night AND the head of the next — per-slot
  // columns handle any mix (prefix, suffix, or both).
  for (int i = 0; i < 13; i++) {
    if (is_daytime(i)) continue;
    int n0, n1;
    slot_range(i, px, &n0, &n1);
    for (int y = CY_T - 12; y <= CY_B + 24; y++)
      for (int x = n0; x <= n1; x++)
        if ((x + y) % 2 == 0) cv_set(x, y, INK);
  }

  // (sunset label removed — the plot now stretches up into the top strip it used)

  // curve
  for (int i = 0; i < 12; i++)
    thick_line(px[i], py[i], px[i + 1], py[i + 1], INK, 3);

  // markers: filled dot at "now" (hour 0, nudged to the column edge), plus
  // filled dots at hour 9 and 12
  fill_circle(px[0] + 4, py[0], 4, INK);
  fill_circle(px[9], py[9], 4, INK);
  fill_circle(px[12], py[12], 4, INK);

  // ---- data labels --------------------------------------------------------
  // The big number on the left is "now", so the curve needs no reference
  // label. Two plain VALUE plates, no words (the curvature explains the
  // rest):
  //   * the END of the window (index 12, 12h from now),
  //   * the OUTLIER — the interior point (1..11) farthest from the
  //     now->end baseline: the bulge that breaks the trend (midday peak,
  //     pre-dawn dip). Skipped when the bulge is under ~1 degree (a
  //     monotonic curve has nothing to call out) or when it would crowd
  //     the end label (within 2h of it).
  // Plates sit below their point when the point is in the upper half of
  // the plot, above when in the lower half — keeps them out of the curve's
  // way.
  const float nowT = wx.temps[0];
  const float endT = wx.temps[12];
  int outI = -1;
  float outDev = 1.0f;   // <~1 degree bulge isn't worth a label
  for (int k = 1; k <= 11; k++) {
    float base = nowT + (endT - nowT) * (k / 12.0f);
    float ad = fabsf(wx.temps[k] - base);
    if (ad > outDev) { outDev = ad; outI = k; }
  }
  char full[8];
  const int PH = 24;   // plate height
  const int midY = (CY_T + CY_B) / 2;
  {   // end-of-window plate (plain value)
    const int i = 12;
    int bn = fmt_num(full, wx.temps[i], 1);
    int pw = 6 + bn * 12 + 6 + 6;
    int x = px[i] - pw / 2;
    if (x < CX0 - 4) x = CX0 - 4;
    if (x > CX1 - pw + 4) x = CX1 - pw + 4;
    bool above = (py[i] > midY);
    int y = above ? (py[i] - PH - 12) : (py[i] + 12);
    if (y < 2) y = py[i] + 12;   // safety: never clip the top edge
    fill_rect(x, y, x + pw, y + PH, PAPER);
    put_text(x + 6, y + 4, full, INK, 2);
    fill_rect(x + 6 + bn * 12 + 2, y + 4, x + 6 + bn * 12 + 6, y + 10, INK);
    thick_line(x + pw / 2, above ? y + PH : y, x + pw / 2, above ? py[i] - 5 : py[i] + 5, INK, 2);
  }
  if (outI >= 0 && (12 - outI) >= 2) {   // outlier plate, skip if crowding the end label
    int bn = fmt_num(full, wx.temps[outI], 1);
    int pw = 6 + bn * 12 + 6 + 6;
    int x = px[outI] - pw / 2;
    if (x < CX0 - 4) x = CX0 - 4;
    if (x > CX1 - pw + 4) x = CX1 - pw + 4;
    bool above = (py[outI] > midY);
    int y = above ? (py[outI] - PH - 12) : (py[outI] + 12);
    if (y < 2) y = py[outI] + 12;   // safety: never clip the top edge
    fill_rect(x, y, x + pw, y + PH, PAPER);
    put_text(x + 6, y + 4, full, INK, 2);
    fill_rect(x + 6 + bn * 12 + 2, y + 4, x + 6 + bn * 12 + 6, y + 10, INK);
    thick_line(x + pw / 2, above ? y + PH : y, x + pw / 2, above ? py[outI] - 5 : py[outI] + 5, INK, 2);
  }

  // hour axis (12h window: every 3h — 0, 3, 6, 9, 12). Labels that land in
  // the night band get a white knock-out rect cut from the dither first
  // (black text on paper, same treatment as the value plates).
  char lab[4];
  if (wx.now_hour >= 0) {
    const int IDX[5] = {0, 3, 6, 9, 12};
    for (int k = 0; k < 5; k++) {
      int idx = IDX[k];
      int h = (wx.now_hour + idx) % 24;
      lab[0] = '0' + h / 10; lab[1] = '0' + h % 10; lab[2] = 0;
      int lx = px[idx] - 12;
      // the label center px[idx] sits inside slot idx's column, so it lands
      // on the band exactly when that slot is dark
      if (!is_daytime(idx)) fill_rect(lx - 10, 170, lx + 34, 194, PAPER);
      put_text(lx, 176, lab, INK, 2);
    }
  }

  // Day/night border countdowns ("how long till..."): at the day->night
  // border the time till the END of day (sunset), at the night->day border
  // the time till the START of day (sunrise). Shown only when the border
  // falls inside the 12h window and the sun data is known; drawn last so a
  // value plate crowding the top strip never eats them.
  if (wx.now_hour >= 0) {
    int trans[2], dt[2], night[2], nb = 0;
    if (wx.sunset_hour >= 0) {
      for (int i = 0; i < 12; i++)
        if (is_daytime(i) && !is_daytime(i + 1)) {
          trans[nb] = i;
          dt[nb] = (wx.sunset_hour * 60 + wx.sunset_min - wx.now_hour * 60 - wx.now_min + 1440) % 1440;
          night[nb] = true;
          nb++; break;
        }
    }
    if (wx.sunrise_hour >= 0) {
      for (int i = 0; i < 12; i++)
        if (!is_daytime(i) && is_daytime(i + 1)) {
          trans[nb] = i;
          dt[nb] = (wx.sunrise_hour * 60 + wx.sunrise_min - wx.now_hour * 60 - wx.now_min + 1440) % 1440;
          night[nb] = false;
          nb++; break;
        }
    }
    for (int b = 0; b < nb; b++) {
      int i = trans[b], d = dt[b];
      int r = ((d + 15) / 30) * 30;   // nearest half hour
      char s[6];
      if (r % 60 == 0) snprintf(s, sizeof s, "%dh", r / 60);
      else snprintf(s, sizeof s, "%d.5h", r / 60);
      int bx = (px[i] + px[i + 1]) / 2;   // dither border x
      int bw = strlen(s) * 12;            // scale-2 advance
      int lx = bx - bw / 2;
      if (lx < CX0 - 6) lx = CX0 - 6;
      if (lx + bw > CX1 + 6) lx = CX1 + 6 - bw;
      fill_rect(lx - 10, 0, lx + bw + 10, 20, PAPER);
      put_text(lx, 3, s, INK, 2);
      if (night[b]) {   // F16: moon-phase glyph after the "till night" countdown
        int mx = lx + bw + 15;
        if (mx + 7 > CX1 + 2) mx = CX1 - 5;
        fill_rect(mx - 9, 0, mx + 9, 20, PAPER);
        draw_moon_phase(mx, 10, 7, moon_phase());
      }
    }
  }
}

// ---- 2-day rain strip (F15) — bottom, x 240-774 (old badge slot) -----------
static void draw_two_day_strip() {
  const int SX = 240, SW = 534, SY = 212;   // flush with the curve's ink
  const int divX = SX + (SW >> 1);          // = 507, center of the strip
  const int GAP = 16;
  for (int d = 0; d < 2; d++) {
    const WXDay *day = &wx.days[d];
    if (day->date[0] == 0) continue;
    int bx = (d == 0) ? SX : divX + GAP;
    int rightEdge = (d == 0) ? divX - GAP : SX + SW;
    // row 1: DOW name (sc2) + 8 per-3h small clouds in a single row
    put_text(bx, SY + 14, DOW3[date_dow(day->date)], INK, 2);
    const int cY = SY + 20, PITCH = 27, cloudStart = rightEdge - 7 - 7 * PITCH;
    for (int k = 0; k < 8; k++) {
      int hh = k * 3;
      int code = (hh < 24 && day->codes[hh] >= 0) ? day->codes[hh] : 0;
      small_icon(cloudStart + k * PITCH, cY, code, true);
    }
    // row 2: rain amount (left) + daylight lo->hi °C (right-aligned)
    char buf[12];
    const char *rt;
    if (day->rain < 0.5f) rt = "DRY";
    else { int p = fmt_num(buf, day->rain, 1); buf[p] = 'M'; buf[p + 1] = 'M'; buf[p + 2] = 0; rt = buf; }
    put_text(bx, SY + 42, rt, INK, 1);
    char rng[16];
    int rp = fmt_num(rng, day->lo, 1);
    rng[rp++] = '-';
    rp += fmt_num(rng + rp, day->hi, 1);
    rng[rp++] = 0xB0; rng[rp++] = 'C'; rng[rp] = 0;
    put_text(rightEdge - strlen(rng) * 6, SY + 42, rng, INK, 1);
  }
}

static void draw_ui() {
  // icons: derived from current + 3h-step codes; clear sky shows sun by day,
  // moon by night (each slot is i*3h into "now")
  for (int i = 0; i < 4; i++) {
    int c = (i == 0) ? wx.code : wx.codes[i * 3];
    int x = IX0 + i * IGAP + 18;
    if (c == 0) { if (is_daytime(i * 3)) icon_sun(x, ICY, 11); else icon_moon(x, ICY, 11); }
    else if (c == 1 || c == 2) icon_part(x, ICY);
    else icon_cloud(x, ICY);
  }

  // top-left small city name removed — the name now lives under the big temp
  // (drawn after the big-temp block below)

  // left: big current temp (4x), centered in the left column
  char b[8];
  int cn = fmt_num(b, wx.cur, 1);
  {
    int tw = cn * 24;
    int nx = 14 + (210 - tw) / 2;
    if (nx < 14) nx = 14;
    put_text(nx, 96, b, INK, 4);
  }

  // left: current day label "DOW DD" (sc2), e.g. "SUN 18"
  {
    char dl[8];
    snprintf(dl, sizeof dl, "%s %02d",
             DOW3[dow_sakamoto(wx.now_year, wx.now_month, wx.now_day)], wx.now_day);
    int dlw = strlen(dl) * 12;
    int nx = 14 + (210 - dlw) / 2;
    if (nx < 14) nx = 14;
    put_text(nx, 136, dl, INK, 2);
  }
  // left: city name (sc1), centered under the day label
  {
    const char *nm = CITIES[cur_city].name;
    int nw = text_w(nm, 1);
    int nx = 14 + (210 - nw) / 2;
    if (nx < 14) nx = 14;
    put_text(nx, 156, nm, INK, 1);
  }

  // ---- left-column CURRENT INFO rows (sc2): rain state / wind / temp range ----
  // rain state: DRY / RAIN <amt>MM / SNOW <amt>MM / STORM (glyph + label)
  {
    const char *rs = rain_state();
    char amt[12];
    int ap = fmt_num(amt, wx.rain_total, 1);
    amt[ap] = 'M'; amt[ap + 1] = 'M'; amt[ap + 2] = 0;
    if (strcmp(rs, "STORM") == 0) {
      s_bolt(28, 254); put_text(42, 248, "STORM", INK, 2);
    } else if (strcmp(rs, "SNOW") == 0) {
      s_snow(26, 254); int x = put_text(42, 248, "SNOW ", INK, 2); put_text(x, 248, amt, INK, 2);
    } else if (strcmp(rs, "RAIN") == 0) {
      row_rain(25, 251); int x = put_text(42, 248, "RAIN ", INK, 2); put_text(x, 248, amt, INK, 2);
    } else {
      row_rain(25, 251); put_text(42, 248, "DRY", INK, 2);
    }
  }
  // daylight min/max of temp + wind (13h window, day hours; fallback: all)
  float lo = NAN, hi = NAN, wLo = NAN, wHi = NAN;
  for (int i = 0; i < 13; i++) {
    if (!is_daytime(i)) continue;
    if (!isnan(wx.temps[i])) { if (isnan(lo) || wx.temps[i] < lo) lo = wx.temps[i]; if (isnan(hi) || wx.temps[i] > hi) hi = wx.temps[i]; }
    if (!isnan(wx.winds[i])) { if (isnan(wLo) || wx.winds[i] < wLo) wLo = wx.winds[i]; if (isnan(wHi) || wx.winds[i] > wHi) wHi = wx.winds[i]; }
  }
  if (isnan(lo)) { for (int i = 0; i < 13; i++) { if (isnan(wx.temps[i])) continue; if (isnan(lo) || wx.temps[i] < lo) lo = wx.temps[i]; if (isnan(hi) || wx.temps[i] > hi) hi = wx.temps[i]; } }
  if (isnan(wLo)) { for (int i = 0; i < 13; i++) { if (isnan(wx.winds[i])) continue; if (isnan(wLo) || wx.winds[i] < wLo) wLo = wx.winds[i]; if (isnan(wHi) || wx.winds[i] > wHi) wHi = wx.winds[i]; } }
  if (isnan(lo)) { lo = 0; hi = 0; }
  if (isnan(wLo)) { wLo = 0; wHi = 0; }
  {   // wind interval (daylight, m/s)
    char ws[16];
    int p = fmt_num(ws, wLo, 1);
    ws[p++] = '-';
    p += fmt_num(ws + p, wHi, 1);
    ws[p++] = ' '; ws[p++] = 'm'; ws[p++] = 's'; ws[p] = 0;
    row_wind(15, 220);
    put_text(42, 216, ws, INK, 2);
  }
  {   // temp range (daylight, °C) — 0xB0 degree byte -> glyph 41
    char tr[16];
    int p = fmt_num(tr, lo, 1);
    tr[p++] = '-';
    p += fmt_num(tr + p, hi, 1);
    tr[p++] = 0xB0; tr[p++] = 'C'; tr[p] = 0;
    row_therm(26, 188);
    put_text(tr[0] == '1' ? 40 : 42, 184, tr, INK, 2);
  }

  // right: temp curve + labels + axis
  draw_temp_curve();

  // BOTTOM: 2-day rain strip (old badge slot, x 240-774)
  draw_two_day_strip();
}

// ---- capture (CAP command) ----------------------------------------------------
static void cap_b64(const uint8_t *data, int len) {
  static const char tb[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char line[2100];
  int p = 0;
  for (int i = 0; i < len; i += 3) {
    int v = (int)data[i] << 16 | (i + 1 < len ? (int)data[i + 1] << 8 : 0) | (i + 2 < len ? (int)data[i + 2] : 0);
    line[p++] = tb[(v >> 18) & 63];
    line[p++] = tb[(v >> 12) & 63];
    line[p++] = (i + 1 < len) ? tb[(v >> 6) & 63] : '=';
    line[p++] = (i + 2 < len) ? tb[v & 63] : '=';
    if (p >= 2000) { Serial.write(line, p); Serial.println(); p = 0; }
  }
  if (p) { Serial.write(line, p); Serial.println(); }
}

static void do_capture() {
  Serial.println("CAP_START");
  cap_b64(ImageBW, 272 * 100);
  Serial.println("CAP_END");
}

// ---- boot ----------------------------------------------------------------------
static bool wifi_up() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("deskbuddy-s3");
  WiFi.begin(WIFI_SSID, WIFI_PSWD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(100);
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.printf("net: wifi up ssid=%s ip=%s rssi=%d\n", WIFI_SSID,
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

static void ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) { wifi_ok = true; return; }
  wifi_ok = wifi_up();
}

// Fetch live data for the current cit into the global wx (full refresh on fail).
static bool refresh_data() {
  ensure_wifi();
  if (!wifi_ok) return false;
  bool ok = fetch_weather(&CITIES[cur_city]);
  if (ok) { fetch_two_days(&CITIES[cur_city]); fetch_time(); }
  return ok;
}

// Redraw the screen from the framebuffer we already have (drawn by caller).
static void do_full_refresh() {
  EPD_GPIOInit();
  EPD_FastMode1Init();
  EPD_Update();              // full refresh (0xF7) — clears ghosting on every cycle
  Serial.println("panel: full refresh");
}

// One draw+update cycle: clear, render (weather or status), full refresh.
static void render_and_show(bool ok) {
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  if (ok) { draw_ui(); Serial.println("ui: live weather drawn"); }
  else { EPD_ShowPicture(0, 0, 792, 272, gImage_status, WHITE); Serial.println("ui: status bitmap"); }
  EPD_Display(ImageBW);
  do_full_refresh();
  lastTick = millis();
  Serial.printf("panel up: %s %s shown (wifi=%d)\n",
                CITIES[cur_city].name, ok ? "live" : "status", (int)wifi_ok);
}

// Switch to the next city, persist, re-fetch, redraw. Shared by the side
// rotary switch and the serial 'CITY' command (used for headless verify).
static void toggle_city(const char *src) {
  cur_city = (cur_city + 1) % N_CITIES;
  Preferences prefs; prefs.begin("wx", false); prefs.putInt("city", cur_city); prefs.end();
  Serial.printf("%s: toggle -> %s (re-fetching)\n", src, CITIES[cur_city].name);
  bool ok = refresh_data();
  render_and_show(ok);
}

// Handle a serial line (CAL/CAP/CITY still work while the app stays awake).
// Non-blocking: accumulates bytes until a newline, so a bare command without
// a trailing newline can't wedge the loop.
static void poll_serial() {
  static char buf[96];
  static int blen = 0;
  int mockH, mockM;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (blen > 0) {
        buf[blen] = 0;
        if (strstr(buf, "CAL")) {
          Serial.print("cal: now=");
          Serial.print(wx.now_hour); Serial.print(":");
          if (wx.now_min < 10) Serial.print('0');
          Serial.print(wx.now_min);
          if (wx.sunset_hour >= 0) { Serial.print(" sunset="); Serial.print(wx.sunset_hour); }
          Serial.println();
        } else if (strstr(buf, "CAP")) {
          do_capture();
        } else if (strstr(buf, "CITY")) {
          toggle_city("ser");   // headless toggle test (mirrors the side switch)
        } else if (!strncmp(buf, "STATE", 5)) {
          // test aid: override the rain-state classification to preview a state
          const char *st = (buf[5] == ' ') ? buf + 6 : buf + 5;
          if (!strcmp(st, "RESET")) state_ov = nullptr;
          else if (!strcmp(st, "DRY")) state_ov = "DRY";
          else if (!strcmp(st, "RAIN")) state_ov = "RAIN";
          else if (!strcmp(st, "SNOW")) state_ov = "SNOW";
          else if (!strcmp(st, "STORM")) state_ov = "STORM";
          else { Serial.println("state: use DRY/RAIN/SNOW/STORM/RESET"); blen = 0; return; }
          render_and_show(true);
          Serial.printf("state: override -> %s\n", state_ov ? state_ov : "(none)");
        } else if (sscanf(buf, "MOCK %d:%d", &mockH, &mockM) == 2 &&
                   mockH >= 0 && mockH < 24 && mockM >= 0 && mockM < 60) {
          // test aid: redraw the live data as if "now" were hh:mm (exercises
          // time-dependent UI — night band, sunset countdown — without waiting)
          wx.now_hour = mockH; wx.now_min = mockM;
          render_and_show(true);
          Serial.printf("mock: redrawn as %02d:%02d\n", mockH, mockM);
        }
        blen = 0;
      }
    } else if (blen < sizeof(buf) - 1) {
      buf[blen++] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("== desk-buddy 5.79 e-paper: weather (persistent) ==");

  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, HIGH);
  delay(100);
  Serial.println("panel power: on");

  pinMode(BTN_PRESS, INPUT_PULLUP);
  pinMode(BTN_UP,    INPUT_PULLUP);
  pinMode(BTN_DOWN,  INPUT_PULLUP);

  Preferences prefs;
  prefs.begin("wx", false);
  int saved = prefs.getInt("city", 0);
  if (saved < 0 || saved >= N_CITIES) saved = 0;
  cur_city = saved;
  Serial.printf("city: %s (index %d)\n", CITIES[cur_city].name, cur_city);

  memset(&wx, 0, sizeof(wx));
  for (int i = 0; i < 13; i++) wx.temps[i] = NAN;
  wx.sunrise_hour = -1;
  wx.sunset_hour = -1;
  wx.now_hour = -1;
  wx.now_min = 0;

  // first fetch
  bool ok = refresh_data();
  if (!ok) Serial.println("wx: FETCH FAILED -> status page");

  EPD_GPIOInit();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  Serial.print("init: ");
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_Update();
  Serial.println("blank full refresh done");

  render_and_show(ok);
}

void loop() {
  poll_serial();

  // button debounce: simple rising-edge detect on the (active-low) rotary pins
  static bool lastPress = false, lastUp = false, lastDown = false;
  bool p = !digitalRead(BTN_PRESS);
  bool u = !digitalRead(BTN_UP);
  bool d = !digitalRead(BTN_DOWN);
  if (p && !lastPress) { toggle_city("btn"); }              // rotary pressed: toggle city
  if ((u && !lastUp) || (d && !lastDown)) {                 // detent edge: immediate refresh
    Serial.println("btn: detent -> refresh");
    bool ok = refresh_data();
    render_and_show(ok);
  }
  lastPress = p; lastUp = u; lastDown = d;

  if (!wifi_ok) ensure_wifi();             // opportunistic reconnect
  uint32_t interval = current_refresh_ms();
  if (millis() - lastTick >= interval) {  // periodic refresh (30 min, 1 h at night)
    Serial.printf("tick: %s refresh\n", interval == REFRESH_MS_NIGHT ? "1h night" : "30-min");
    bool ok = refresh_data();
    render_and_show(ok);
    lastTick = millis();
  }
  delay(40);
}
