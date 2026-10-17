/* ui.js — layout + composition (1:1 firmware `panel_ui` + `draw_ui`).
 * Layout constants, the WX global (assigned by app.js boot), the daylight +
 * rain-state helpers, the 13h temp curve, the left-column + 2-day-strip drawUI,
 * and the render entry (reset -> drawUI -> blit). Reads INK/PAPER + primitives
 * from canvas.js, font.js, and the icon fns from icons.js; reads LAT/LON/
 * RANGE_SEP/cityName/STATE_OV from data.js at draw time. Load order: #4 of 6. */
"use strict";

/* ---- layout ---- */
const CX0=240, CX1=770, CY_T=16, CY_B=166;
const IX0=20, IGAP=52, ICY=52;
let WX=null;

function isDaytime(h){ if(!WX||WX.now_hour<0) return true; const cur=WX.now_hour*60+WX.now_min;
  const t=(cur+h*60)%1440;
  if(WX.sunrise_hour>=0&&WX.sunset_hour>=0){ const sr=WX.sunrise_hour*60+WX.sunrise_min, ss=WX.sunset_hour*60+WX.sunset_min;
    return sr<ss ? (t>=sr&&t<ss) : (t>=sr||t<ss); } return t>=300&&t<1020; }
function slotRange(i,pxA,out){ out.x0=i===0?Math.max(CX0,pxA[0]-3):Math.floor((pxA[i]+pxA[i-1])/2); out.x1=i===12?pxA[12]+3:Math.floor((pxA[i]+pxA[i+1])/2); }

/* classify the 13h window: DRY / RAIN / SNOW / STORM (severity priority) */
function rainState(){
  if(STATE_OV) return STATE_OV;
  const codes=WX.codes||[]; const has=s=>s.some(c=>codes.indexOf(c)>=0);
  if(has([95,96,99])) return 'STORM';
  if(has([71,73,75,77,85,86])) return 'SNOW';
  if((WX.rain_total||0)>=0.5) return 'RAIN';
  return 'DRY';
}

function drawTempCurve(){
  const T=WX.temps; let lo=Infinity,hi=-Infinity;
  for(let i=0;i<13;i++){ if(!isFinite(T[i])) continue; if(T[i]<lo)lo=T[i]; if(T[i]>hi)hi=T[i]; }
  if(!isFinite(lo)) return;
  const plo=lo-1,phi=hi+1; let range=phi-plo; if(range<1)range=1;
  const px=new Array(13),py=new Array(13);
  for(let i=0;i<13;i++){ px[i]=CX0+Math.floor(i*(CX1-CX0)/12); py[i]=CY_T+Math.round((phi-T[i])*(CY_B-CY_T)/range); }
  const sl={x0:0,x1:0};
  for(let i=0;i<13;i++){ if(isDaytime(i)) continue; slotRange(i,px,sl);
    for(let y=CY_T-12;y<=CY_B+24;y++) for(let x=sl.x0;x<=sl.x1;x++) if(((x+y)&1)===0) setPx(x,y,INK); }
  for(let i=0;i<12;i++) thickLine(px[i],py[i],px[i+1],py[i+1],INK,3);
  fillCircle(px[0]+4, py[0], 4, INK);
  fillCircle(px[9],py[9],4,INK); fillCircle(px[12],py[12],4,INK);
  const nowT=T[0],endT=T[12]; let outI=-1,outDev=1.0;
  for(let k=1;k<=11;k++){ const base=nowT+(endT-nowT)*(k/12); const ad=Math.abs(T[k]-base); if(ad>outDev){outDev=ad;outI=k;} }
  const PH=24,midY=(CY_T+CY_B)>>1;
  function plate(i,tv){ const full=fmtNum(tv,1),bn=full.length; const pw=6+bn*12+6+6; let x=px[i]-Math.floor(pw/2);
    if(x<CX0)x=CX0; if(x>CX1-pw+4)x=CX1-pw+4; const above=py[i]>midY; let y=above?(py[i]-PH-12):(py[i]+12); if(y<2)y=py[i]+12;
    fillRect(x,y,x+pw,y+PH,PAPER); putText(x+6,y+4,full,INK,2); fillRect(x+6+bn*12+2,y+4,x+6+bn*12+6,y+10,INK);
    thickLine(x+Math.floor(pw/2),above?y+PH:y,x+Math.floor(pw/2),above?py[i]-5:py[i]+5,INK,2); }
  plate(12,T[12]); if(outI>=0&&(12-outI)>=2) plate(outI,T[outI]);
  if(WX.now_hour>=0){ const IDX=[0,3,6,9,12];
    for(let k=0;k<5;k++){ const idx=IDX[k]; const h=(WX.now_hour+idx)%24; const lab=('0'+h).slice(-2); let lx=px[idx]-12; if(lx<CX0)lx=CX0; if(lx+24>CX1)lx=CX1-24;
      if(!isDaytime(idx)) fillRect(lx-10,170,lx+34,194,PAPER); putText(lx,176,lab,INK,2); } }
  if(WX.now_hour>=0){ const trans=[],dt=[],night=[];
    if(WX.sunset_hour>=0){ for(let i=0;i<12;i++) if(isDaytime(i)&&!isDaytime(i+1)){ trans.push(i); dt.push((WX.sunset_hour*60+WX.sunset_min-WX.now_hour*60-WX.now_min+1440)%1440); night.push(true); break; } }
    if(WX.sunrise_hour>=0){ for(let i=0;i<12;i++) if(!isDaytime(i)&&isDaytime(i+1)){ trans.push(i); dt.push((WX.sunrise_hour*60+WX.sunrise_min-WX.now_hour*60-WX.now_min+1440)%1440); night.push(false); break; } }
    for(let b=0;b<trans.length;b++){ const i=trans[b],d=dt[b]; const r=Math.floor((d+15)/30)*30;
      const s=(r%60===0)?(r/60)+'h':(Math.floor(r/60)+'.5h'); const bx=Math.floor((px[i]+px[i+1])/2); const bw=s.length*12;
      let lx=bx-Math.floor(bw/2); if(lx<CX0-6)lx=CX0-6; if(lx+bw>CX1+6)lx=CX1+6-bw; fillRect(lx-10,0,lx+bw+10,20,PAPER); putText(lx,3,s,INK,2);
      if(night[b]){ let mx=lx+bw+15; if(mx+7>CX1+2)mx=CX1-5; fillRect(mx-9,0,mx+9,20,PAPER); drawMoonPhase(mx,10,7,moonPhase()); } } }
}

function drawUI(){
  // left: 4 condition icons (current +3/+6/+9h)
  for(let i=0;i<4;i++){ const c=(i===0)?WX.code:WX.codes[i*3]; const x=IX0+i*IGAP+18;
    if(c===0){ (isDaytime(i*3)?iconSun:iconMoon)(x,ICY,11); }
    else if(c===1||c===2) iconPart(x,ICY); else iconCloud(x,ICY); }
  // left: big current temp (sc4) centered in left column
  const b=fmtNum(WX.cur,1); let nx=14+Math.floor((210-b.length*24)/2); if(nx<14)nx=14; putText(nx,96,b,INK,4);
  // left: current day (sc2) on top, city (sc1, smaller) below
  const dl=dayLabel(new Date().toISOString().slice(0,10));
  nx=14+Math.floor((210-dl.length*12)/2); if(nx<14)nx=14; putText(nx,136,dl,INK,2);
  const nm=cityName(); nx=14+Math.floor((210-nm.length*6)/2); if(nx<14)nx=14; putText(nx,156,nm,INK,1);

  // left: CURRENT INFO rows (sc2) — glyphs right-aligned to x=30, text column at x=42
  //  rain state: DRY / RAIN / SNOW / STORM (glyph + label, no "12H")
  const rs=rainState(); const amt=fmtNum(WX.rain_total,1)+'MM';
  if(rs==='STORM'){ sBolt(28,254); putText(42,248,'STORM',INK,2); }
  else if(rs==='SNOW'){ sSnow(26,254); putText(putText(42,248,'SNOW ',INK,2),248,amt,INK,2); }
  else if(rs==='RAIN'){ rowRain(25,251); putText(putText(42,248,'RAIN ',INK,2),248,amt,INK,2); }
  else { rowRain(25,251); putText(42,248,'DRY',INK,2); }
  //  daylight min/max of temp + wind (13h window, day hours only; fallback: all hours)
  let lo=Infinity,hi=-Infinity,wLo=Infinity,wHi=-Infinity;
  for(let i=0;i<13;i++){ if(!isDaytime(i)) continue;
    const t=WX.temps[i]; if(isFinite(t)){ if(t<lo)lo=t; if(t>hi)hi=t; }
    const w=WX.winds?WX.winds[i]:null; if(isFinite(w)){ if(w<wLo)wLo=w; if(w>wHi)wHi=w; } }
  if(!isFinite(lo)){ for(let i=0;i<13;i++){ const t=WX.temps[i]; if(!isFinite(t))continue; if(t<lo)lo=t; if(t>hi)hi=t; } }
  if(!isFinite(wLo)){ for(let i=0;i<13;i++){ const w=WX.winds?WX.winds[i]:null; if(isFinite(w)){ if(w<wLo)wLo=w; if(w>wHi)wHi=w; } } }
  if(!isFinite(lo)){ lo=0; hi=0; } if(!isFinite(wLo)){ wLo=0; wHi=0; }
  //  wind interval (daylight, m/s)
  rowWind(15,220); putText(42,216,fmtNum(wLo,1)+RANGE_SEP+fmtNum(wHi,1)+' m/s',INK,2);
  //  temp range (daylight, °C)
  rowTherm(26,188); const rng=fmtNum(lo,1)+RANGE_SEP+fmtNum(hi,1)+'°C'; putText((rng[0]==='1'?40:42),184,rng,INK,2);

  // right: 13h curve
  drawTempCurve();

  // BOTTOM: 2-day rain strip (old badge slot, x 240-786)
  drawTwoDayStrip();
}

function dayLabel(dstr){ // "2026-10-18" -> "SUN 18"
  const DOW=['SUN','MON','TUE','WED','THU','FRI','SAT'];
  const dd=new Date(dstr+'T12:00:00');
  return DOW[dd.getDay()]+' '+String(dd.getDate()).padStart(2,'0');
}
function dayDow(dstr){ // "2026-10-18" -> "SUN"
  return ['SUN','MON','TUE','WED','THU','FRI','SAT'][new Date(dstr+'T12:00:00').getDay()];
}
function drawTwoDayStrip(){
  const SX=240, SW=534, SY=212;   // flush with the curve's ink (239..774 — dot edges, not centers)
  const divX=SX+(SW>>1);   // = 507, center of the strip
  const days=WX.days; // [{date, codes[24], rain, pprob, lo, hi}]  (2 entries: tomorrow, day-after)
  const GAP=16;   // half the middle gap (cell padding removed -> wider gap, edges flush with graph)
  for(let d=0;d<2;d++){
    const day=days[d]; if(!day) continue;
    const bx=(d===0)?SX:divX+GAP;                 // left of this day-block (flush, no left padding)
    const rightEdge=(d===0)?divX-GAP:SX+SW;       // right edge (flush, no right padding)
    // row 1: day-of-week name (sc2) + 4 per-6h medium icons in a single row
    putText(bx, SY+14, dayDow(day.date), INK, 2);
    const cY=SY+20, PITCH=52, iconStart=rightEdge-14-3*PITCH;
    for(let k=0;k<4;k++){ const hh=k*6; const code=(day.codes[hh]!=null)?day.codes[hh]:0;
      stripIcon(iconStart+k*PITCH, cY, code); }
    // row 2: rain amount (left) + daylight lo->hi °C (right-aligned)
    const rt=day.rain<0.5?'DRY':fmtNum(day.rain,1)+'MM';
    putText(bx, SY+42, rt, INK, 1);
    const rng=fmtNum(day.lo,1)+RANGE_SEP+fmtNum(day.hi,1)+'°C';
    putText(rightEdge-rng.length*6, SY+42, rng, INK, 1);
  }
}

function blit(){ ctx.putImageData(buf,0,0); }
function render(){ reset(); drawUI(); blit(); }
