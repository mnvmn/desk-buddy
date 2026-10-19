/* app.js — entry point + status wiring (mirrors weather panel_v2 app.js).
 * Wires the status pill + source footer, runs live-then-mock boot, assigns the
 * TRAVEL global (ui.js) and kicks the first render. Loaded LAST (6th of 6). */
"use strict";
const pill=document.getElementById('pill'), pillTxt=document.getElementById('pillTxt'), srcTxt=document.getElementById('srcTxt');
function stamp(s){ return s.toTimeString().slice(0,5); }
async function boot(){
  let tv=null, err=null;
  if(MODE!=='mock'){ try{ tv=await fetchTravelLive(); }catch(e){ err=e; } }
  if(tv){ TRAVEL=tv; pill.className='pill '+(tv.source==='live'?'live':'mock');
    pillTxt.textContent = tv.source==='live'?('LIVE · '+stamp(tv.fetchedAt)+' local'):'MOCK · fallback';
    srcTxt.textContent = tv.source==='live'?('transit daemon · '+TRAVEL_URL+' · '+stamp(tv.fetchedAt)):'baked next-hour mock · '+(tv.note||'');
    render();
  } else { TRAVEL=mockTravel(); pill.className='pill '+(MODE==='live'?'err':'mock');
    pillTxt.textContent = MODE==='live'?'FETCH FAILED · showing mock':'MOCK · offline';
    srcTxt.textContent = 'offline — baked next-hour for Karpatská ('+(err?err.message:'fetch skipped')+')'; render(); }
}
boot();
