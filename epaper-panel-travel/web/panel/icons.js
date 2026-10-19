/* icons.js — travel glyph/icon drawing (same style as weather panel_v2 icons.js:
 * procedural pixel math via the canvas.js primitives, INK on PAPER, 1-bit-safe).
 * busIcon  = side-view bus (line headers), liveTick = 3px "real-time" square,
 * clockIcon = tiny clock (header), delayChip helper lives in ui.js.
 * Load order: #3 of 6. */
"use strict";

/* Side-view bus, ~26w x 20h, centred at (cx,cy). body = main colour, win =
 * the knocked-out window/tyre colour. Solid 1-bit: filled body, a window band
 * (win) with thin mullions (body), two wheels (body) with a 1px win gap under
 * the body so they don't merge into the body. Default ink-on-paper; pass
 * (PAPER, INK) to draw a bus inside a filled ink box. */
function busIcon(cx,cy, body, win){ body=body||INK; win=win||PAPER;
  const x0=cx-12, x1=cx+12, y0=cy-7, y1=cy+6;
  fillRect(x0,y0,x1,y1,body);
  setPx(x0,y0,win); setPx(x1,y0,win); setPx(x0,y1,win); setPx(x1,y1,win);
  setPx(x0+1,y0,win); setPx(x1-1,y0,win);
  fillRect(x0+2, y0+2, x1-2, y0+5, win);
  for(let yy=y0+2; yy<=y0+5; yy++){ setPx(cx-4,yy,body); setPx(cx+3,yy,body); }
  fillRect(x0, y1, x1, y1+1, win);
  fillCircle(cx-7, y1+3, 3, body); fillCircle(cx+7, y1+3, 3, body);
  fillCircle(cx-7, y1+3, 1, win); fillCircle(cx+7, y1+3, 1, win);
}

/* 3x3 filled square at (x,y) — marks a departure the live feed tracks. */
function liveTick(x,y){ fillRect(x,y,x+2,y+2,INK); }

/* hollow 3x3 square — marks a scheduled (not-yet-live) departure. */
function schedTick(x,y){
  for(let yy=y;yy<=y+2;yy++)for(let xx=x;xx<=x+2;xx++)
    if(xx===x||xx===x+2||yy===y||yy===y+2) setPx(xx,yy,INK);
}

/* tiny filled up-triangle — marks a delayed (late) departure. */
function lateTick(x,y){ for(let yy=0;yy<4;yy++) for(let xx=0;xx<=yy;xx++) setPx(x+yy, y+3-yy, INK); }

/* Tiny clock, radius r, centred at (cx,cy): ring + two hands. */
function clockIcon(cx,cy,r){
  for(let y=cy-r;y<=cy+r;y++)for(let x=cx-r;x<=cx+r;x++){
    const d2=(x-cx)*(x-cx)+(y-cy)*(y-cy); if(d2>=(r-1)*(r-1)&&d2<=(r+1)*(r+1)) setPx(x,y,INK); }
  thickLine(cx,cy,cx,cy-r+2,INK,1); thickLine(cx,cy,cx+r-2,cy,INK,1);
  setPx(cx,cy,INK);
}
