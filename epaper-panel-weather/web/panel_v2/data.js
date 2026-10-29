/* data.js — weather data source (1:1 firmware `panel_fetch`).
 * Bratislava Open-Meteo live fetch (current + 13h curve + 2-day hourly range),
 * the baked-in MOCK fallback, and the URL test-params:
 *   ?mode=auto|live|mock  (auto tries live then falls back to mock)
 *   &state=DRY|RAIN|SNOW|STORM  (force the rain-state label/icon for previews)
 *   &range=slash|tilde|underscore  (range-separator candidates; default '-')
 * Defines MODE / STATE_OV / RANGE_SEP / LAT / LON / cityName, read by ui.js at
 * draw time and by app.js at boot. Load order: #5 of 6. */
"use strict";
const LAT='48.1476', LON='17.1067';
function cityName(){ return 'BRATISLAVA'; }
const params=new URLSearchParams(location.search);
const MODE=params.get('mode')||'auto';
const STATE_OV=params.get('state');   // test override: DRY|RAIN|SNOW|STORM
const RANGE_SEP=(()=>{const r=params.get('range');return r==='slash'?'/':r==='tilde'?'~':r==='underscore'?'_':'-';})();   // default '-'; ?range=slash|tilde|underscore to compare

function pad2(n){ return String(n).padStart(2,'0'); }
function addDays(dstr,n){ const d=new Date(dstr+'T12:00:00'); d.setDate(d.getDate()+n);
  return d.getFullYear()+'-'+pad2(d.getMonth()+1)+'-'+pad2(d.getDate()); }

/* min/max of hourly temps over daylight hours (srH/ssH in hours; <0 = unknown -> 5-19h) */
function daylightRange(temps, srH, ssH){
  let lo=Infinity,hi=-Infinity;
  for(let h=0;h<24;h++){ const isD=(srH<0||ssH<0)?(h>=5&&h<19):(h>=Math.floor(srH)&&h<Math.ceil(ssH));
    if(!isD) continue; const t=temps[h]; if(t==null||!isFinite(t)) continue; if(t<lo)lo=t; if(t>hi)hi=t; }
  if(!isFinite(lo)) for(let h=0;h<24;h++){ const t=temps[h]; if(t==null||!isFinite(t))continue; if(t<lo)lo=t; if(t>hi)hi=t; }
  if(!isFinite(lo)) return {lo:0,hi:0};
  return {lo,hi};
}
/* procedural diurnal temp curve (mock): peak ~14:00 */
function mockDayTemps(mid, amp){ const t=new Array(24);
  for(let h=0;h<24;h++) t[h]=Math.round((mid+amp*Math.cos((h-14)/24*2*Math.PI))*10)/10; return t; }
function mockWx(){
  const srH=7.25, ssH=17.95; // Bratislava late-Oct sunrise/sunset (approx, hours)
  const d0={ date:addDays('2026-10-18',1), code:3,
    codes:[3,3,3,3,3,3,3,1,1,2,0,0,0,0,1,0,0,0,0,0,0,0,0,0],
    temps:mockDayTemps(14,6), rain:0.0, pprob:0, lo:0, hi:0 };
  const d1={ date:addDays('2026-10-18',2), code:1,
    codes:[0,0,0,1,3,3,2,1,1,1,1,0,0,0,0,1,2,3,3,2,1,1,1,1],
    temps:mockDayTemps(10,5), rain:0.4, pprob:10, lo:0, hi:0 };
  const r0=daylightRange(d0.temps,srH,ssH); d0.lo=r0.lo; d0.hi=r0.hi;
  const r1=daylightRange(d1.temps,srH,ssH); d1.lo=r1.lo; d1.hi=r1.hi;
  const wx={
    cur:15.6, feels:12.4, wind:9.7, code:3,
    temps:[15.0,16.2,17.0,17.5,17.4,16.9,15.6,14.4,13.3,13.0,13.2,14.1,14.0],
    // 24h codes from now_hour (12:00) — left column reads 4 equal parts now→midnight
    codes24:[3,3,3,3,1,1,0,0,0,0,1,2,3,3,2,1,1,1,1,1,1,1,1,1],
    winds:[2.1,2.4,2.8,3.1,3.4,3.0,2.6,2.2,1.9,2.3,2.7,2.5,2.3],
    rain_total:0.0, now_hour:12, now_min:30,
    sunrise_hour:7, sunrise_min:15, sunset_hour:17, sunset_min:57,
    days:[d0,d1],
    source:'mock', fetchedAt:null, note:'offline / fallback'
  };
  wx.codes=wx.codes24.slice(0,13); // 13h window (curve/rainState/rows)
  return wx;
}
function parseHHMM(s){ if(!s||s[10]!=='T') return null; const h=parseInt(s.slice(11,13),10),m=parseInt(s.slice(14,16),10);
  if(h<0||h>23||m<0||m>59) return null; return {h,m}; }

async function fetchLive(){
  const ctl=new AbortController(); const to=setTimeout(()=>ctl.abort(),9000);
  try{
    // GET 1: current + 24h (curve uses the first 13h; left column needs now→midnight)
    // + 3-day daily headlines (incl. sunrise/sunset)
    const url1='https://api.open-meteo.com/v1/forecast?latitude='+LAT+'&longitude='+LON
      +'&current=temperature_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m'
      +'&hourly=temperature_2m,weather_code,precipitation,wind_speed_10m&forecast_hours=24&timezone=auto'
      +'&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max,sunrise,sunset&forecast_days=3';
    const r1=await fetch(url1,{signal:ctl.signal}); if(!r1.ok) throw new Error('HTTP '+r1.status);
    const j=await r1.json();
    const cur=j.current, hour=j.hourly, day=j.daily;
    const wx={ cur:cur.temperature_2m, feels:cur.apparent_temperature, wind:cur.wind_speed_10m, code:cur.weather_code,
      temps:hour.temperature_2m.slice(0,13), codes:hour.weather_code.slice(0,13),
      codes24:hour.weather_code.slice(0,24),
      winds:hour.wind_speed_10m.slice(0,13).map(v=> (v==null||!isFinite(v))?null:v/3.6),
      rain_total:hour.precipitation.slice(0,13).reduce((a,v)=>a+(v||0),0),
      now_hour:parseInt(hour.time[0].slice(11,13),10), now_min:parseInt(hour.time[0].slice(14,16),10),
      sunrise_hour:-1,sunrise_min:0,sunset_hour:-1,sunset_min:0, days:[], source:'live', fetchedAt:new Date() };
    const sr=parseHHMM(day.sunrise[0]); if(sr){wx.sunrise_hour=sr.h;wx.sunrise_min=sr.m;}
    const ss=parseHHMM(day.sunset[0]);  if(ss){wx.sunset_hour=ss.h;wx.sunset_min=ss.m;}
    wx.temps[0]=wx.cur;
    if(!isFinite(wx.cur)||!isFinite(wx.temps[0])) throw new Error('bad payload');

    // GET 2: 2-day date-range hourly (tomorrow + day-after): temp, code, precip
    const d1=day.time[1], d2=day.time[2];
    const url2='https://api.open-meteo.com/v1/forecast?latitude='+LAT+'&longitude='+LON
      +'&hourly=temperature_2m,weather_code,precipitation&start_date='+d1+'&end_date='+d2+'&timezone=auto';
    const r2=await fetch(url2,{signal:ctl.signal}); if(!r2.ok) throw new Error('HTTP '+r2.status);
    const j2=await r2.json(); const h2=j2.hourly; const t2=h2.time;
    for(let di=0; di<2; di++){ const dd=(di===0)?d1:d2;
      const codes=new Array(24).fill(null); const temps=new Array(24).fill(null); let rain=0;
      for(let i=0;i<t2.length;i++){ if(t2[i].slice(0,10)===dd){ const hh=parseInt(t2[i].slice(11,13),10);
        codes[hh]=(h2.weather_code[i]||0); temps[hh]=h2.temperature_2m[i]; rain+=(h2.precipitation[i]||0); } }
      const sr=parseHHMM(day.sunrise[1+di]), ss=parseHHMM(day.sunset[1+di]);
      const dr=daylightRange(temps, sr?(sr.h+sr.m/60):-1, ss?(ss.h+ss.m/60):-1);
      wx.days.push({ date:dd, code:(day.weather_code[1+di]||0), codes, temps, rain:rain,
        pprob:day.precipitation_probability_max?day.precipitation_probability_max[1+di]:0,
        lo:dr.lo, hi:dr.hi });
    }
    return wx;
  } finally { clearTimeout(to); }
}
