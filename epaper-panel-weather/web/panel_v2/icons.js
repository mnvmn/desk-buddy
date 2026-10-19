/* icons.js — ALL glyph/icon drawing (1:1 firmware `panel_draw`).
 * Large condition icons (sun/moon/cloud/part) for the left column, medium ~26px
 * icons (cloud/sun/part/rain/snow/bolt) for the 2-day strip (4 per day, one per
 * 6h block), the moon-phase glyph, the code->icon selector (stripIcon), the
 * small sSnow/sBolt left-column state-row glyphs, and the tiny left-column
 * info-row glyphs (rain/wind/therm). Draws only via the canvas.js primitives
 * + INK/PAPER. Load order: #3 of 6. */
"use strict";
const DX8=[1,1,0,-1,-1,-1,0,1], DY8=[0,1,1,1,0,-1,-1,-1];

/* ---- large icons (verbatim from firmware) ---- */
function iconSun(cx,cy,r){ for(let y=cy-r;y<=cy+r;y++)for(let x=cx-r;x<=cx+r;x++){
    const d2=(x-cx)*(x-cx)+(y-cy)*(y-cy); if(d2>=(r-2)*(r-2)) setPx(x,y,INK); }
  for(let i=0;i<8;i++){ const ux=DX8[i],uy=DY8[i]; let x0=cx+ux*(r+3),y0=cy+uy*(r+3);
    let x1=cx+ux*(r+8),y1=cy+uy*(r+8); if(ux!==0&&uy!==0){x1=cx+(ux<0?-1:1)*(r+6);y1=cy+(uy<0?-1:1)*(r+6);}
    thickLine(x0,y0,x1,y1,INK,2); } }
function iconMoon(cx,cy,r){ fillCircle(cx,cy,r,INK); fillCircle(cx+(r/2|0),cy-(r/3|0),r,PAPER); }
function iconCloud(cx,cy){ fillCircle(cx-8,cy,8,INK); fillCircle(cx,cy-6,8,INK); fillCircle(cx+9,cy,8,INK);
  for(let y=cy+2;y<=cy+8;y++)for(let x=cx-14;x<=cx+15;x++) setPx(x,y,INK); }
function iconPart(cx,cy){ const sx=cx-4,sy=cy-4;
  for(let y=sy-11;y<=sy+11;y++)for(let x=sx-11;x<=sx+11;x++){ const d2=(x-sx)*(x-sx)+(y-sy)*(y-sy); if(d2>=77&&d2<=116) setPx(x,y,INK); }
  for(let i=0;i<8;i++){ const ux=DX8[i],uy=DY8[i]; let x0=sx+ux*11,y0=sy+uy*11; let x1=sx+ux*16,y1=sy+uy*16;
    if(ux!==0&&uy!==0){x1=sx+(ux<0?-1:1)*14;y1=sy+(uy<0?-1:1)*14;} thickLine(x0,y0,x1,y1,INK,2); }
  iconCloud(cx,cy+8); }

/* ---- medium icons for the 2-day strip (~26px, code->icon; day variants) ---- */
function mCloud(cx,cy){ fillCircle(cx-7,cy,6,INK); fillCircle(cx,cy-6,6,INK); fillCircle(cx+7,cy,6,INK);
  for(let y=cy+1;y<=cy+6;y++)for(let x=cx-11;x<=cx+11;x++) setPx(x,y,INK); }
function mSun(cx,cy){ for(let y=cy-6;y<=cy+6;y++)for(let x=cx-6;x<=cx+6;x++){
    const d2=(x-cx)*(x-cx)+(y-cy)*(y-cy); if(d2>=16) setPx(x,y,INK); }
  for(let i=0;i<8;i++){ const ux=DX8[i],uy=DY8[i]; let x0=cx+ux*8,y0=cy+uy*8;
    let x1=cx+ux*12,y1=cy+uy*12; if(ux!==0&&uy!==0){ x1=cx+(ux<0?-1:1)*11; y1=cy+(uy<0?-1:1)*11; }
    thickLine(x0,y0,x1,y1,INK,1); } }
function mPart(cx,cy){ // sun arc + rays peeking from the cloud's upper-left
  // (1-bit: both are INK, so only the visible side of the sun is drawn — the
  // cloud is painted last and the rest of the ring would poke through)
  for(let y=cy-11;y<=cy+2;y++)for(let x=cx-11;x<=cx+2;x++){
    const d2=(x-cx+3)*(x-cx+3)+(y-cy-5)*(y-cy-5); if(d2>=20&&d2<=42) setPx(x,y,INK); }
  for(const i of [4,5,6,7]){ const ux=DX8[i],uy=DY8[i];
    thickLine(cx-3+ux*8,cy-5+uy*8,cx-3+ux*12,cy-5+uy*12,INK,1); }
  mCloud(cx+3,cy+4); }
function mRain(cx,cy){ mCloud(cx,cy-4);
  for(const dx of [-5,0,5]) for(let y=cy+5;y<=cy+8;y++) for(let t=0;t<2;t++) setPx(cx+dx+t,y,INK); }
function mSnow(cx,cy){ thickLine(cx,cy-9,cx,cy+9,INK,1); thickLine(cx-8,cy-5,cx+8,cy+5,INK,1); thickLine(cx-8,cy+5,cx+8,cy-5,INK,1); }
function mBolt(cx,cy){ thickLine(cx+4,cy-9,cx-2,cy-2,INK,2); thickLine(cx-2,cy-2,cx+2,cy-1,INK,2); thickLine(cx+2,cy-1,cx-4,cy+9,INK,2); }
/* small state-row glyphs (left column SNOW/STORM rows) */
function sSnow(cx,cy){ thickLine(cx,cy-4,cx,cy+4,INK,1); thickLine(cx-4,cy-2,cx+4,cy+2,INK,1); thickLine(cx-4,cy+2,cx+4,cy-2,INK,1); }
function sBolt(cx,cy){ thickLine(cx+2,cy-4,cx-1,cy,INK,1); thickLine(cx-1,cy,cx+1,cy,INK,1); thickLine(cx+1,cy,cx-2,cy+4,INK,1); }
// Moon phase: 0=new, 0.25=first quarter, 0.5=full, 0.75=last quarter, 1=new.
function moonPhase(){
  const synodic=29.53058867; // days
  const ref=Date.UTC(2000,0,6,18,14)/86400000; // 2000-01-06 18:14 UTC (known new moon)
  let p=(((Date.now()/86400000)-ref)%synodic)/synodic; if(p<0)p+=1;
  return p;
}
// Draw a moon-phase glyph (lit=INK, dark=PAPER) centred at (cx,cy), radius r.
function drawMoonPhase(cx,cy,r,phase){
  const waxing=phase<0.5;
  const termX=Math.cos(phase*2*Math.PI); // -1..1 terminator x (normalised)
  for(let y=cy-r;y<=cy+r;y++)for(let x=cx-r;x<=cx+r;x++){
    const dx=x-cx,dy=y-cy,d2=dx*dx+dy*dy;
    if(d2>r*r) continue; // outside disc -> stays PAPER
    const nx=dx/r;
    const lit=waxing? nx>termX : nx<-termX;
    setPx(x,y,lit?INK:PAPER);
  }
  for(let y=cy-r-1;y<=cy+r+1;y++)for(let x=cx-r-1;x<=cx+r+1;x++){
    const dx=x-cx,dy=y-cy,d2=dx*dx+dy*dy;
    if(d2>(r+1)*(r+1)||d2<(r-1)*(r-1)) continue;
    setPx(x,y,INK);
  }
}
function stripIcon(cx,cy,code){
  // Optical alignment on one vertical axis (cy): cloud-body glyphs (mCloud,
  // mPart) are shifted so their cloud CENTRES land on cy — mPart draws its cloud
  // 4px below its anchor, so it needs -1 (not +3) to match a bare mCloud (+3,
  // whose body centre is 3px above its anchor). Symmetric glyphs (mSun/mSnow/
  // mBolt) sit on cy directly; mRain keeps +4 so its cloud+drop mass centres on cy.
  if(code===95||code===96||code===99) mBolt(cx,cy);
  else if(code===71||code===73||code===75||code===77||code===85||code===86) mSnow(cx,cy);
  else if(code===0) mSun(cx,cy);
  else if(code===1||code===2) mPart(cx,cy-1);
  else if(code>=51||code>=71) mRain(cx,cy+4);
  else mCloud(cx,cy+3);
}

/* small left-column info row: tiny glyph + text (sc1) */
function rowRain(x,y){ // umbrella/precip glyph
  const ux=x,uy=y+3;
  for(let a=0;a<=180;a+=6){ const rad=a*Math.PI/180; setPx(ux+Math.floor(-5*Math.cos(rad)),uy-1-Math.floor(4*Math.sin(rad)),INK); }
  for(let yy=uy;yy<=uy+5;yy++) setPx(ux,yy,INK); setPx(ux+1,uy+5,INK);
  return ux+12;
}
function rowWind(x,y){ for(let t=0;t<2;t++)for(let xx=x;xx<=x+14;xx++) setPx(xx,y+t,INK);
  for(let t=0;t<3;t++){ setPx(x+14-t,y+3+t,INK); setPx(x+14-t+1,y+3+t,INK); } return x+18; }
function rowTherm(x,y){ for(let yy=y-4;yy<=y+4;yy++)for(let t=0;t<3;t++) setPx(x+t,yy,INK); fillCircle(x+1,y+6,3,INK); return x+9; }
