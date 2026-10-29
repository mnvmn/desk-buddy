// Host-only stub shadowing the EDP library's EPD.h for the pixel harness.
// Provides the paint/display entry points the drawing path uses, backed by the
// same 1bpp ImageBW buffer the panel drives: bit set (WHITE) = paper/light,
// bit clear (BLACK) = ink — matching the SSD1683 mapping (Paint_SetPixel(x,y,
// WHITE) SETS the bit -> light; BLK clears it -> dark ink).
#pragma once
#include <stdint.h>

#define BLACK 0
#define WHITE 1
#define EPD_W 800
#define EPD_H 272
#define Rotation 180

extern uint8_t ImageBW[EPD_H * EPD_W / 8];

void Paint_SetPixel(int x, int y, uint16_t c);
void Paint_NewImage(uint8_t *buf, int w, int h, int rot, uint16_t color);
void Paint_Clear(uint16_t color);
void EPD_GPIOInit(void);
void EPD_FastMode1Init(void);
void EPD_Update(void);
void EPD_Display(uint8_t *buf);
void EPD_Display_Clear(void);
void EPD_ShowPicture(int x, int y, int w, int h, const uint8_t *img, uint16_t color);

// Minimal Serial: the only Arduino object the drawing path touches is
// Serial.printf (panel_font.h's missing-glyph guard).
struct HostSerial { int printf(const char *fmt, ...); };
extern HostSerial Serial;
