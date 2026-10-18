// Weather data model: cities + forecast structs.
#pragma once
#include "panel_canvas.h"

struct WxCity { const char *name; const char *lat; const char *lon; };
// Second+ cities are defined in panel_state.cpp (Bratislava comes from the
// shared net_config.h).
extern const WxCity CITIES[2];
static const int N_CITIES = 2;

// One forecast day for the 2-day strip (tomorrow + day-after).
struct WXDay {
  char date[11];      // "YYYY-MM-DD"
  int codes[24];      // hourly weather codes (drives the 4 per-6h strip icons)
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
  int codes24[24];    // hourly WMO codes from now (v2.1: drives the 4 left-column parts)
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

extern WX wx;   // defined in panel_state.cpp
