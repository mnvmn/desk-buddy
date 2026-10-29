/* travel_ui.js — TRAVEL view: layout + composition (the travel half of the
 * unified panel; the weather view is ui.js). 800x272 1-bit. A full-width HEADER
 * (stop + day + time) over THREE equal LINE columns (32 | 49 | 44), each with a
 * header (big number + destination + nástupiste) and the next-hour departure
 * rows. Reads INK/PAPER + primitives (canvas.js), putText/fmtNum (font.js), the
 * travel icons (travel_icons.js), dayLabel (ui.js) + hhmmToSec (travel_data.js),
 * and the TRAVEL global (assigned by app.js). Public entry: drawTravel() —
 * app.js renderPage() calls it when PAGE===travel (after reset(), before blit()).
 * Renamed from the standalone project's drawUI/render/blit (which collide with
 * the weather ui.js globals); internal draw helpers are tv*-prefixed.
 * Load order: #7 of 9. */
"use strict";
let TRAVEL=null;
let TRAVEL_NOW_SEC=0;

/* ---- geometry ---- */
const TV_COLS=[
  {x0:8,   x1:258},      // line 32
  {x0:274, x1:524},      // line 49
  {x0:540, x1:790},      // line 44
];
const TV_DIVX=[266, 532];          // vertical dividers
const TV_HEAD_Y=38;                // header divider line
const TV_BODY_T=46, TV_BODY_B=258;
const TV_ROW0=102, TV_ROWP=38;     // first-row y + row pitch

function secAhead(dep, nowSec){ return hhmmToSec(dep.t)+dep.d - nowSec; }
function minsTxt(dep, nowSec){ return "IN "+Math.max(0,Math.round(secAhead(dep,nowSec)/60))+"M"; }

/* ---- header (full width) ---- */
function tvDrawHeader(){
  busIcon(24,20); putText(46,13,TRAVEL.stop,INK,2);
  const rt=dayLabel(TRAVEL.day)+" "+TRAVEL.now;
  putText(774-rt.length*12,13,rt,INK,2); clockIcon(784,20,6);
  thickLine(8,TV_HEAD_Y,792,TV_HEAD_Y,INK,1);
}

/* ---- one line column ---- */
function tvDrawLineCol(col, L){
  const x0=col.x0, x1=col.x1;
  putText(x0,50,L.line,INK,4);
  putText(x0+56,58,L.dest,INK,2);
  const pl="N "+(L.plat||""); putText(x1-pl.length*6,58,pl,INK,1);
  thickLine(x0,88,x1,88,INK,1);
  for(let i=0;i<L.dep.length;i++){ const dep=L.dep[i]; tvDrawRow(x0,x1,TV_ROW0+i*TV_ROWP,dep); }
}
function tvDrawRow(x0,x1,y,dep){
  putText(x0+2,y+2,dep.t,INK,3);
  const inT=minsTxt(dep, TRAVEL_NOW_SEC); const inX=x1-2-inT.length*6;
  putText(inX,y+5,inT,INK,1);
  const tx=inX-9;
  if(dep.d>=45) lateTick(tx,y+4); else if(dep.live) liveTick(tx,y+3); else schedTick(tx,y+3);
}

/* ---- compose (called by app.js renderPage when PAGE===travel) ---- */
function drawTravel(){
  const nowSec=hhmmToSec(TRAVEL.now); TRAVEL_NOW_SEC=nowSec;
  tvDrawHeader();
  for(let i=0;i<TV_COLS.length && i<TRAVEL.lines.length;i++) tvDrawLineCol(TV_COLS[i], TRAVEL.lines[i]);
  for(const dx of TV_DIVX) thickLine(dx,TV_BODY_T,dx,TV_BODY_B,INK,1);
}
