// Host pixel-verification harness for the v2.1 weather panel port.
//
// Compiles the REAL panel_fetch.cpp (pure helpers + block_code/left_part) and
// REAL panel_ui.cpp + panel_draw.h against a stubbed 800x272 1bpp canvas, feeds
// the web mock's EXACT baked data (web/panel_v2/data.js mockWx, now=12:30),
// renders draw_ui(), and dumps the ink grid to host_ink.txt (one row/line,
// '1'=ink '0'=paper). Diff against the web's PXL grid (same encoding) to prove
// pixel-identity of the ported icon rows.
//
// Build:  g++ -std=c++17 -O2 -I tools/host -I fw/weather_test \
//              tools/host_render.cpp -o tools/host_render
#define HOST_BUILD
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>

#include "panel_canvas.h"
#include "panel_fetch.h"
#include "panel_font.h"
#include "panel_draw.h"
#include "panel_state.h"
#include "panel_ui.h"

// ---- globals normally defined in panel_state.cpp ---------------------------
uint8_t ImageBW[EPD_H * EPD_W / 8];
WX wx;
int cur_city = 0;
bool wifi_ok = false;
uint32_t lastTick = 0;
const WxCity CITIES[1] = {
  {"BRATISLAVA", "48.1476", "17.1067"},
};
HostSerial Serial;
int HostSerial::printf(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  int n = vfprintf(stderr, fmt, ap);
  va_end(ap);
  return n;
}

// ---- paint/display stubs (only Paint_SetPixel is on the drawing path) -------
void Paint_SetPixel(int x, int y, uint16_t c) {
  if (x < 0 || y < 0 || x >= EPD_W || y >= EPD_H) return;
  int off = y * 100 + (x >> 3);
  int bit = 0x80 >> (x & 7);
  if (c == WHITE) ImageBW[off] |= (uint8_t)bit;
  else            ImageBW[off] &= (uint8_t)~bit;
}
void Paint_NewImage(uint8_t *, int, int, int, uint16_t) {}
void Paint_Clear(uint16_t c) { memset(ImageBW, (c == WHITE) ? 0xFF : 0x00, sizeof ImageBW); }
void EPD_GPIOInit(void) {}
void EPD_FastMode1Init(void) {}
void EPD_Update(void) {}
void EPD_Display(uint8_t *) {}
void EPD_Display_Clear(void) {}
void EPD_ShowPicture(int, int, int, int, const uint8_t *, uint16_t) {}

// ---- compile the REAL firmware fetch + draw + UI code into this TU ---------
#include "panel_fetch.cpp"
#include "panel_ui.cpp"

// ---- mock helpers (web/panel_v2/data.js, verbatim) --------------------------
// mockDayTemps(mid, amp): t[h] = round((mid + amp*cos((h-14)/24*2pi))*10)/10
static void mock_day_temps(float *t, float mid, float amp) {
  const float PI = 3.14159265358979f;
  for (int h = 0; h < 24; h++) {
    float v = mid + amp * cosf((h - 14) / 24.0f * 2.0f * PI);
    t[h] = (float)((long)(v * 10 + (v >= 0 ? 0.5f : -0.5f)) / 10.0f);
  }
}
// daylight lo/hi with the web's fixed Bratislava window (srH=7.25, ssH=17.95)
static void mock_daylight(const float *t, float *lo, float *hi) {
  *lo = 1e9f; *hi = -1e9f;
  for (int h = 0; h < 24; h++) {
    if (!(h >= 7 && h < 18)) continue;  // floor(7.25)=7 .. ceil(17.95)-1=17
    if (isnan(t[h])) continue;
    if (t[h] < *lo) *lo = t[h];
    if (t[h] > *hi) *hi = t[h];
  }
}

int main() {
  // ---- current (now=12:30), exactly as web/panel_v2/data.js mockWx ----------
  wx.cur = 15.6f; wx.feels = 12.4f; wx.wind = 9.7f; wx.code = 3;
  const float temps[13] = {15.0f,16.2f,17.0f,17.5f,17.4f,16.9f,15.6f,14.4f,13.3f,13.0f,13.2f,14.1f,14.0f};
  const int codes24[24] = {3,3,3,3,1,1,0,0,0,0,1,2,3,3,2,1,1,1,1,1,1,1,1,1};
  const float winds[13] = {2.1f,2.4f,2.8f,3.1f,3.4f,3.0f,2.6f,2.2f,1.9f,2.3f,2.7f,2.5f,2.3f};
  for (int i = 0; i < 13; i++) { wx.temps[i] = temps[i]; wx.codes[i] = codes24[i]; wx.winds[i] = winds[i]; }
  for (int i = 0; i < 24; i++) wx.codes24[i] = codes24[i];
  // NOTE: the mock does NOT override temps[0]=cur (that's live-path only), so
  // temps[0]=15.0 while cur=15.6 — reproduce that exactly.
  wx.rain_total = 0.0f;
  wx.now_hour = 12; wx.now_min = 30;
  wx.sunrise_hour = 7; wx.sunrise_min = 15;
  wx.sunset_hour  = 17; wx.sunset_min  = 57;
  wx.now_year = 2026; wx.now_month = 10; wx.now_day = 18;  // today (UTC) — must match the web mock's wall-clock date
  wx.ok = true;

  // ---- day 0 = tomorrow (2026-10-19) ---------------------------------------
  {
    WXDay *d = &wx.days[0];
    strcpy(d->date, "2026-10-19");
    const int c[24] = {3,3,3,3,3,3,3,1,1,2,0,0,0,0,1,0,0,0,0,0,0,0,0,0};
    for (int i = 0; i < 24; i++) d->codes[i] = c[i];
    mock_day_temps(d->temps, 14, 6);
    d->rain = 0.0f;
    mock_daylight(d->temps, &d->lo, &d->hi);
  }
  // ---- day 1 = day-after (2026-10-20) --------------------------------------
  {
    WXDay *d = &wx.days[1];
    strcpy(d->date, "2026-10-20");
    const int c[24] = {0,0,0,1,3,3,2,1,1,1,1,0,0,0,0,1,2,3,3,2,1,1,1,1};
    for (int i = 0; i < 24; i++) d->codes[i] = c[i];
    mock_day_temps(d->temps, 10, 5);
    d->rain = 0.4f;
    mock_daylight(d->temps, &d->lo, &d->hi);
  }

  // ---- render ---------------------------------------------------------------
  // Real rain state (no override): both web `rainState()` and firmware
  // `rain_state()` return "DRY" for dry conditions — they agree. (An earlier
  // pin of state_ov="CLEAR" was a harness artifact, not a real diff.)
  state_ov = nullptr;
  Paint_Clear(WHITE);
  draw_ui();

  // ---- dump ink grid: '1'=ink (bit clear), '0'=paper (bit set) -------------
  const char *out = "host_ink.txt";
  FILE *f = fopen(out, "w");
  if (!f) { fprintf(stderr, "cannot open %s\n", out); return 1; }
  for (int y = 0; y < EPD_H; y++) {
    for (int x = 0; x < EPD_W; x++) {
      int off = y * 100 + (x >> 3);
      int bit = 0x80 >> (x & 7);
      fputc((ImageBW[off] & bit) ? '0' : '1', f);
    }
    fputc('\n', f);
  }
  fclose(f);

  // ---- diagnostics ----------------------------------------------------------
  float ph = moon_phase();
  printf("host_ink.txt written (%dx%d)\n", EPD_W, EPD_H);
  printf("MOON_PHASE=%.10f\n", ph);
  {
    int R = 24 - (wx.now_hour >= 0 ? wx.now_hour : 0); if (R < 1) R = 1;
    printf("left  block-codes:");
    for (int k = 0; k < 4; k++) { int a, b; left_part(k, R, &a, &b); printf(" %d", block_code(wx.codes24, a, b - a)); }
    printf("\nday0 block-codes:");
    for (int k = 0; k < 4; k++) printf(" %d", block_code(wx.days[0].codes, k * 6, 6));
    printf("\nday1 block-codes:");
    for (int k = 0; k < 4; k++) printf(" %d", block_code(wx.days[1].codes, k * 6, 6));
    printf("day0 lo/hi: %.1f / %.1f   day1 lo/hi: %.1f / %.1f\n",
      wx.days[0].lo, wx.days[0].hi, wx.days[1].lo, wx.days[1].hi);
    const char *rs = rain_state();
    printf("rain_state: %s  (codes[0..5]=%d %d %d %d %d %d, rain_total=%.2f)\n",
      rs, wx.codes[0], wx.codes[1], wx.codes[2], wx.codes[3], wx.codes[4], wx.codes[5], wx.rain_total);
  }
  return 0;
}
