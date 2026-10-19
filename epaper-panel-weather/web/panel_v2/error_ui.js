/* error_ui.js — shared ERROR page (weather + travel), drawn when a page's data
 * is unavailable. Calm + centered: a warning triangle with "!", the source name
 * (scale 4), the error message (scale 4), and a footer (scale 2). 800x272 1-bit.
 * Public entry: drawError(srcName, msg, footer) — call after reset(), before the
 * canvas blit. Uses putText (font.js) + thickLine/fillRect + INK/W (canvas.js).
 * Load order: after font.js. Uppercase, no diacritics (font constraint). */
"use strict";

/* center a string of `len` chars at `scale` on the 800px canvas; char advance ~6px */
function errCx(str, scale){ return Math.max(0, Math.round((W - str.length * 6 * scale) / 2)); }

function drawError(srcName, msg, footer){
  // warning triangle (outlined, thick) + "!" centered near the top
  thickLine(400, 26, 352, 92, INK, 4);
  thickLine(400, 26, 448, 92, INK, 4);
  thickLine(352, 92, 448, 92, INK, 4);
  thickLine(400, 46, 400, 72, INK, 5);   // "!" bar
  fillRect(395, 80, 405, 90, INK);       // "!" dot
  // source name (scale 4)
  putText(errCx(srcName, 4), 106, srcName, INK, 4);
  // error message (scale 4)
  putText(errCx(msg, 4), 152, msg, INK, 4);
  // footer (scale 2)
  if (footer) putText(errCx(footer, 2), 210, footer, INK, 2);
}
