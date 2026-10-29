/* font.js — 5x7 bitmap font (1:1 firmware `panel_font`).
 * GLYPHS[] rows: bit4 = leftmost. Firmware glyphs 0-39 + v2 extras
 * 40 '~', 41 '°', 42 '>', 43 '_', 44 'm', 45 's' (match fw/weather_test
 * GLYPHS[] indices 40-45). putText() advances 6 px/scale per char, skips
 * unknown glyphs (gi<0) just like firmware put_text. Load order: #2 of 6. */
"use strict";
const GLYPHS=[
 [14,17,19,21,25,17,14],[4,12,4,4,4,4,14],[14,17,1,6,12,16,31],[30,1,1,14,1,1,30],
 [2,6,10,18,31,2,2],[31,16,30,1,1,17,14],[6,16,16,30,17,17,14],[31,1,2,4,8,8,8],
 [14,17,17,14,17,17,14],[14,17,17,15,1,1,1],[14,17,17,31,17,17,17],[30,17,17,30,17,17,30],
 [14,17,16,16,16,17,14],[30,17,17,17,17,17,30],[31,16,16,30,16,16,31],[31,16,30,16,16,16,16],
 [14,17,16,22,17,17,15],[17,17,17,31,17,17,17],[0,0,0,0,0,0,0],[17,18,20,24,20,18,17],
 [16,16,16,16,16,16,31],[17,27,21,21,17,17,17],[17,25,21,19,17,17,17],[14,17,17,17,17,17,14],
 [30,17,17,30,16,16,16],[14,17,17,17,21,18,13],[30,17,17,30,20,18,17],[15,16,16,14,1,1,30],
 [31,4,4,4,4,4,4],[17,17,17,17,17,17,14],[17,17,17,17,17,10,4],[17,17,17,21,21,27,17],
 [17,17,10,4,4,4,4],[0,0,0,31,0,0,0],[0,0,0,0,0,6,6],[0,6,0,0,0,6,0],[0,3,6,12,24,0,0],
 [16,16,20,18,18,18,18],[31,4,4,4,4,4,31],[15,1,1,1,17,9,6],[11,2,2,2,2,2,11],[6,9,9,6,0,0,0],
 [8,4,2,1,2,4,8],[0,0,0,0,0,0,31],[0,0,14,21,21,21,21],[0,0,14,16,14,1,14]
];
function glyphIdx(ch){ const c=ch.charCodeAt(0);
  if(c>=48&&c<=57) return c-48; if(c>=65&&c<=72) return 10+(c-65); if(c===32) return 18;
  if(c>=75&&c<=81) return 19+(c-75); if(c>=82&&c<=87) return 26+(c-82); if(c===89) return 32;
  if(c===45) return 33; if(c===46) return 34; if(c===58) return 35; if(c===47) return 36;
  if(c===104) return 37; if(c===73) return 38; if(c===74) return 39; if(c===126) return 40;
  if(c===176) return 41; if(c===62) return 42; if(c===95) return 43; if(c===109) return 44; if(c===115) return 45; return -1; }
function putText(x,y,s,c,sc){ let cx=x;
  for(let i=0;i<s.length;i++){ const gi=glyphIdx(s[i]); if(gi<0) continue;
    for(let r=0;r<7;r++){ const row=GLYPHS[gi][r]; for(let col=0;col<5;col++)
      if(row&(1<<(4-col))) for(let dy=0;dy<sc;dy++)for(let dx=0;dx<sc;dx++) setPx(cx+col*sc+dx,y+r*sc+dy,c); }
    cx+=6*sc; } return cx; }
function fmtNum(v,dec){ if(!isFinite(v)) return '?'; dec=dec||1; const f=dec?10:1;
  let neg=v<0; if(neg) v=-v; const iv=Math.round(v*f), ip=Math.floor(iv/f), fp=iv%10; let s='';
  if(neg) s='-'; if(ip>=100) s+=(ip/100%10|0); if(ip>=10) s+=(ip/10%10|0); s+=(ip%10);
  if(dec) s+='.'+fp; return s; }
