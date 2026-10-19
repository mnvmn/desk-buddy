/* travel_data.js — TRAVEL data source (the travel half of the unified panel;
 * the weather source is data.js). TRAVEL global (assigned by app.js boot):
 * { stop, platform, now:"HH:MM", day:"YYYY-MM-DD",
 *   lines:[{line,dest,plat,dep:[{t,d,live}]}], source, fetchedAt }.
 * A departure: t="HH:MM" (scheduled), d=live−scheduled in seconds (0 on-time),
 * live=true when the imhd feed tracks the real vehicle.
 *   MOCK  = a baked next-hour for Karpatská (32+49+44), real 2026-10-18 15:35 data.
 *   LIVE  = fetch a small merged JSON from a local host daemon
 *           (default http://127.0.0.1:8341/next-hour.json, ?url= to override).
 * Reuses the shared MODE/params from the weather data.js (which loads first);
 * defines only the travel bits. Load order: #8 of 9. */
"use strict";
const STOP="KARPATSKA", PLATFORM="313";
const TRAVEL_URL=params.get('url')||'http://127.0.0.1:8341/next-hour.json';

function hhmmToSec(s){ const h=parseInt(s.slice(0,2),10), m=parseInt(s.slice(3,5),10); return h*3600+m*60; }

/* ---- MOCK: baked next-hour for Karpatská (real 2026-10-18 15:35 data) ---- */
function mockTravel(){
  return {
    stop:STOP, platform:PLATFORM, now:"15:35", day:"2026-10-18",
    lines:[
      { line:"32", dest:"DLHE DIELY", plat:"313", dep:[
        { t:"15:40", d:0,  live:true  },
        { t:"15:55", d:0,  live:true  },
        { t:"16:10", d:0,  live:false },
        { t:"16:25", d:0,  live:false } ] },
      { line:"49", dest:"KRAMARE NUSCH", plat:"313", dep:[
        { t:"15:48", d:0,  live:true  },
        { t:"16:03", d:0,  live:true  },
        { t:"16:18", d:0,  live:true  },
        { t:"16:33", d:0,  live:false } ] },
      { line:"44", dest:"KOLIBA", plat:"315", dep:[
        { t:"15:39", d:6,  live:true  },
        { t:"15:49", d:1,  live:true  },
        { t:"16:04", d:0,  live:false },
        { t:"16:19", d:0,  live:false } ] }
    ],
    source:'mock', fetchedAt:null, note:'baked next-hour (offline)'
  };
}

/* ---- LIVE: fetch the merged JSON from the host daemon ------------------ */
async function fetchTravelLive(){
  const ctl=new AbortController(); const to=setTimeout(()=>ctl.abort(),2500);
  try{
    const r=await fetch(TRAVEL_URL,{signal:ctl.signal}); if(!r.ok) throw new Error('HTTP '+r.status);
    const j=await r.json();
    if(!j||!Array.isArray(j.lines)) throw new Error('bad payload');
    const tv={ stop:j.stop||STOP, platform:j.platform||PLATFORM,
      now:j.now, day:j.day||new Date().toISOString().slice(0,10),
      lines:j.lines.map(L=>({ line:String(L.line), dest:L.dest, plat:String(L.plat||PLATFORM),
        dep:(L.dep||L.departures||[]).map(d=>({ t:d.t, d:(d.d!=null?d.d:0), live:!!d.live })) })),
      source:'live', fetchedAt:new Date() };
    if(!tv.now||tv.lines.length===0) throw new Error('incomplete');
    return tv;
  } finally { clearTimeout(to); }
}
