// Host pixel-verification harness for the shared ERROR page — the 1:1 port of
// web/panel_v2/error_ui.js. Compiles the REAL panel_error.cpp against the stub
// 800x272 1bpp canvas, renders draw_error() with the weather-data error strings
// (the same state as error_preview.html?state=weather-data), and dumps the ink
// grid to host_ink_error.txt ('1'=ink '0'=paper). Diff against the web's PXL
// grid (same encoding) to prove pixel-identity of the port.
//
// Build:  g++ -std=c++17 -O2 -I tools/host -I fw/weather_test \
//              tools/host_render_error.cpp -o tools/host_render_error
#define HOST_BUILD
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>

#include "panel_canvas.h"
#include "panel_font.h"
#include "panel_draw.h"
#include "panel_error.h"

// ---- globals + stubs (same as the other host harnesses) --------------------
uint8_t ImageBW[EPD_H * EPD_W / 8];
HostSerial Serial;
int HostSerial::printf(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  int n = vfprintf(stderr, fmt, ap);
  va_end(ap);
  return n;
}
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

// ---- compile the REAL error page into this TU ------------------------------
#include "panel_error.cpp"

int main() {
  Paint_Clear(WHITE);
  draw_error("WEATHER", "NO WEATHER DATA", "RETRYING");   // weather-data error state

  const char *out = "host_ink_error.txt";
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
  printf("host_ink_error.txt written (%dx%d)\n", EPD_W, EPD_H);
  return 0;
}
