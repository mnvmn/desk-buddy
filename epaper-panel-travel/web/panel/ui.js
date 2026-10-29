/* ui.js — travel layout + composition (mirrors weather panel_v2 ui.js).
 * 800x272 1-bit, two zones: a full-width HEADER (stop + day + time) and a
 * body split into THREE equal LINE columns (32 | 49 | 44), each with a header
 * (big number + destination + nástupiste) and the next-hour departure rows.
 * Reads INK/PAPER + primitives (canvas.js), putText (font.js), the travel
 * icons (icons.js), and the TRAVEL global (assigned by app.js) + the data.js
 * helpers. Load order: #4 of 6. */
"use strict";
let TRAVEL=null;

/* ---- geometry ---- */
const COLS=[
  {x0:8,   x1:258},      // line 32
  {x0:274, x1:524},      // line 49
  {x0:540, x1:790},      // line 44
];
const DIVX=[266, 532];           // vertical dividers
const HEAD_Y=38;                 // header divider line
const BODY_T=46, BODY_B=258;
const ROW0=102, ROWP=38;         // first-row y + row pitch

function dayLabel(dstr){ const DOW=['SUN','MON','TUE','WED','THU','FRI','SAT'];
  const dd=new Date(dstr+'T12:00:00'); return DOW[dd.getDay()]+' '+String(dd.getDate()).padStart(2,'0'); }
function secAhead(dep, nowSec){ return hhmmToSec(dep.t)+dep.d - nowSec; }
function minsTxt(dep, nowSec){ return "IN "+Math.max(0,Math.round(secAhead(dep,nowSec)/60))+"M"; }

/* ---- header (full width) ---- */
function drawHeader(){
  busIcon(24,20); putText(46,13,TRAVEL.stop,INK,2);
  const rt=dayLabel(TRAVEL.day)+" "+TRAVEL.now;
  putText(774-rt.length*12,13,rt,INK,2); clockIcon(784,20,6);
  thickLine(8,HEAD_Y,792,HEAD_Y,INK,1);
}

/* ---- one line column ---- */
function drawLineCol(col, L){
  const x0=col.x0, x1=col.x1;
  putText(x0,50,L.line,INK,4);
  putText(x0+56,58,L.dest,INK,2);
  const pl="N "+(L.plat||""); putText(x1-pl.length*6,58,pl,INK,1);
  thickLine(x0,88,x1,88,INK,1);
  for(let i=0;i<L.dep.length;i++){ const dep=L.dep[i]; drawRow(x0,x1,ROW0+i*ROWP,dep,i===0); }
}
function drawRow(x0,x1,y,dep,isNext){
  if(isNext) putText(x0,y+3,">",INK,3);
  putText(x0+(isNext?20:2),y+2,dep.t,INK,3);
  const inT=minsTxt(dep, TRAVEL_NOW_SEC); const inX=x1-2-inT.length*6;
  putText(inX,y+5,inT,INK,1);
  const tx=inX-9;
  if(dep.d>=45) lateTick(tx,y+4); else if(dep.live) liveTick(tx,y+3); else schedTick(tx,y+3);
}

/* ---- compose ---- */
function drawUI(){
  const nowSec=hhmmToSec(TRAVEL.now); TRAVEL_NOW_SEC=nowSec;
  drawHeader();
  for(let i=0;i<COLS.length && i<TRAVEL.lines.length;i++) drawLineCol(COLS[i], TRAVEL.lines[i]);
  for(const dx of DIVX) thickLine(dx,BODY_T,dx,BODY_B,INK,1);
}
function blit(){ ctx.putImageData(buf,0,0); }
function render(){ reset(); drawUI(); blit(); }
let TRAVEL_NOW_SEC=0;
