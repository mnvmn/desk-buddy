// Shared ERROR page — 1:1 port of web/panel_v2/error_ui.js. Same coordinates,
// same width values, same centering as the web (thick_line/fill_rect/put_text
// are the byte-identical firmware twins of canvas.js/font.js), so a host render
// of this is pixel-identical to the web error_preview.html.
#include "panel_error.h"
#include "panel_font.h"   // put_text
#include "panel_draw.h"   // thick_line, fill_rect
#include <string.h>

// Center a string of len chars at scale on the 800px canvas (char advance ~6px).
// 800 - len*6*scale is always even, so integer division == the web's Math.round.
static int err_cx(const char *str, int scale) {
  int cx = (800 - (int)strlen(str) * 6 * scale) / 2;
  return cx < 0 ? 0 : cx;
}

void draw_error(const char *srcName, const char *msg, const char *footer) {
  // warning triangle (outlined, thick) + "!" centered near the top
  thick_line(400, 26, 352, 92, INK, 4);
  thick_line(400, 26, 448, 92, INK, 4);
  thick_line(352, 92, 448, 92, INK, 4);
  thick_line(400, 46, 400, 72, INK, 5);   // "!" bar
  fill_rect(395, 80, 405, 90, INK);       // "!" dot
  // source name (scale 4)
  put_text(err_cx(srcName, 4), 106, srcName, INK, 4);
  // error message (scale 4)
  put_text(err_cx(msg, 4), 152, msg, INK, 4);
  // footer (scale 2)
  if (footer) put_text(err_cx(footer, 2), 210, footer, INK, 2);
}
