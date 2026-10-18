/* app.js — entry point + status wiring (1:1 firmware main: boot/setup/loop).
 * Wires the status pill + source footer, runs the live-then-mock boot, assigns
 * the WX global (from data.js) and kicks the first render (ui.js). Loaded LAST
 * (6th of 6) so every other module is defined before boot() runs. */
"use strict";
const pill=document.getElementById('pill'), pillTxt=document.getElementById('pillTxt'), srcTxt=document.getElementById('srcTxt');
function stamp(s){ return s.toTimeString().slice(0,5); }
async function boot(){
  let wx=null,err=null;
  if(MODE!=='mock'){ try{ wx=await fetchLive(); }catch(e){ err=e; } }
  if(wx){ WX=wx; pill.className='pill '+(wx.source==='live'?'live':'mock');
    pillTxt.textContent=wx.source==='live'?('LIVE · '+stamp(wx.fetchedAt)+' local'):'MOCK · fallback';
    srcTxt.textContent=wx.source==='live'?('Open-Meteo · Bratislava ('+LAT+','+LON+') · '+stamp(wx.fetchedAt)):'baked-in mock · '+(wx.note||'');
    render();
  } else { WX=mockWx(); pill.className='pill '+(MODE==='live'?'err':'mock');
    pillTxt.textContent=MODE==='live'?'FETCH FAILED · showing mock':'MOCK · offline';
    srcTxt.textContent='offline — mock data ('+(err?err.message:'fetch skipped')+')'; render(); }
}
boot();
