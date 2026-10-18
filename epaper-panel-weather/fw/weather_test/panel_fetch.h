// Open-Meteo fetch + JSON mini-parser + the pure date/weather helpers the
// UI and the refresh loop build on.
#pragma once
#include "panel_data.h"

// ---- network fetch (fills the global wx) -------------------------------------
int  fetch_weather(const WxCity *cit);   // current + 13h hourly + 3-day daily
bool fetch_two_days(const WxCity *cit);  // 24h x 2 days hourly (strip)
int  fetch_time();                       // no-op kept for interface parity

// ---- formatting + time helpers -------------------------------------------------
int  fmt_num(char *buf, float v, int dec);
bool is_daytime(int h);
void slot_range(int i, const int *px, int *x0, int *x1);
uint32_t current_refresh_ms();

// ---- date helpers (v2) -----------------------------------------------------------
int  dow_sakamoto(int y, int m, int d);
int  date_dow(const char *dstr);
float moon_phase();
extern const char *DOW3[7];   // "SUN".."SAT" (panel_fetch.cpp)

// ---- rain-state classification (F12) ---------------------------------------------
const char *rain_state();
extern const char *state_ov;   // serial STATE override; nullptr = live

// ---- v2.1 block aggregation (1:1 web ui.js blockCode / leftPart) -------------------
// Dominant WMO code of an n-hour window: any storm(95/96/99) > any snow(71/73/75/77/85/86)
// > any rain(51-67/80-82) > most-frequent dry code, dry ties to the more overcast
// (3/45/48 > 1/2 > 0). Not a point sample — every hour in the window is considered.
int  block_code(const int *codes, int start, int n);
// [a,b) = index range of part k (0..3) of the 4 EQUAL parts from now to midnight,
// R = whole hours left (24-now_hour, >=1); min 1 hour per part when R<4 (may overlap).
void left_part(int k, int R, int *a, int *b);
