/* canvas.js — pixel buffer + low-level primitives (1:1 firmware `panel_canvas`).
 * 800x272 1-bit e-ink buffer held as an RGBA32 (LE) Uint32Array over an
 * ImageData; PAPER/INK constants; reset + setPx/fillRect/thickLine/fillCircle.
 * Every other module draws through these — nothing else touches PXL directly.
 * Load order: this is script #1 of 6 (see panel_v2.html). */
"use strict";
const W=800, H=272;
const cv  = document.getElementById('panel');
const ctx = cv.getContext('2d');
const buf = ctx.createImageData(W,H);
const PXL = new Uint32Array(buf.data.buffer);
const PAPER = (0xff<<24)|(0xdf<<16)|(0xe7<<8)|0xe9;
const INK   = (0xff<<24)|(0x1c<<16)|(0x19<<8)|0x18;
function reset(){ PXL.fill(PAPER); }
function setPx(x,y,c){ if(x<0||y<0||x>=W||y>=H) return; PXL[y*W+x]=c; }
function fillRect(x0,y0,x1,y1,c){
  if(x1<x0){const t=x0;x0=x1;x1=t;} if(y1<y0){const t=y0;y0=y1;y1=t;}
  const xa=Math.max(0,x0),ya=Math.max(0,y0),xb=Math.min(W-1,x1),yb=Math.min(H-1,y1);
  for(let y=ya;y<=yb;y++){ const row=y*W; for(let x=xa;x<=xb;x++) PXL[row+x]=c; }
}
function thickLine(x0,y0,x1,y1,c,w){
  let dx=Math.abs(x1-x0),dy=-Math.abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,e=dx+dy,h=w>>1;
  for(;;){ for(let t=-h;t<=h;t++){ setPx(x0+t,y0,c); setPx(x0,y0+t,c); }
    if(x0===x1&&y0===y1) break; let e2=2*e;
    if(e2>=dy){e+=dy;x0+=sx;} if(e2<=dx){e+=dx;y0+=sy;} }
}
function fillCircle(cx,cy,r,c){ for(let dy=-r;dy<=r;dy++)for(let dx=-r;dx<=r;dx++) if(dx*dx+dy*dy<=r*r) setPx(cx+dx,cy+dy,c); }
