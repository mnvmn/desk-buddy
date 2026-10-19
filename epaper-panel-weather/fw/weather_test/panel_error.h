// Shared ERROR page (weather + travel), drawn when a page's data is
// unavailable. Calm + centered: a warning triangle with "!", the source name
// (scale 4), the error message (scale 4), and a footer (scale 2).
// 1:1 port of web/panel_v2/error_ui.js (800x272, 1-bit, uppercase, no
// diacritics — the firmware font has no X/Z either, and none of these strings
// need them). Pure canvas: no network, no app state beyond the framebuffer.
#pragma once
#include "panel_canvas.h"

// Draw the error page: warning triangle up top, then the source name (scale 4),
// the error message (scale 4), and an optional footer (scale 2), each centered
// on the 800px canvas. Call after Paint_Clear, before EPD_Display.
void draw_error(const char *srcName, const char *msg, const char *footer);
