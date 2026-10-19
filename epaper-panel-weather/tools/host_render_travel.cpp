// Host pixel-verification harness for the TRAVEL panel port (the travel half
// of the unified panel; host_render.cpp is the weather half).
//
// Compiles the REAL panel_fetch.cpp (date helpers: date_dow / DOW3) and REAL
// panel_travel.cpp against a stubbed 800x272 1bpp canvas, feeds the web mock's
// EXACT baked data (web/panel_v2/travel_data.js MOCK, now=15:35, 2026-10-18),
// renders draw_travel(), and dumps the ink grid to host_ink_travel.txt (one
// row/line, '1'=ink '0'=paper). Diff against the web's travel PXL grid (same
// encoding) to prove pixel-identity of the ported icons + layout.
//
// Build:  g++ -std=c++17 -O2 -I tools/host -I fw/weather_test \
//              tools/host_render_travel.cpp -o tools/host_render_travel
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
#include "panel_travel.h"

// ---- globals normally defined in panel_state.cpp ---------------------------
uint8_t ImageBW[EPD_H * EPD_W / 8];
WX wx;                      // referenced by panel_fetch.cpp helpers
travel_t travel;
int cur_city = 0;
int cur_page = PAGE_TRAVEL;
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

// ---- compile the REAL firmware date helpers + travel code into this TU ------
#include "panel_fetch.cpp"
#include "panel_travel.cpp"

int main() {
  // ---- travel: exactly web/panel_v2/travel_data.js MOCK ---------------------
  travel_now_mock(&travel);

  Paint_Clear(WHITE);
  draw_travel(&travel);

  // ---- dump ink grid: '1'=ink (bit clear), '0'=paper (bit set) --------------
  const char *out = "host_ink_travel.txt";
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

  printf("host_ink_travel.txt written (%dx%d, %d lines, now=%s day=%s)\n",
         EPD_W, EPD_H, travel.nlines, travel.now, travel.day);
  return 0;
}
