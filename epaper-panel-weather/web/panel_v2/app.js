/* app.js — entry point + status wiring (1:1 firmware main: boot/setup/loop).
 * Wires the status pill + source footer, runs the live-then-mock boot, assigns
 * the WX global (from data.js) and kicks the first render (ui.js). Loaded LAST
 * (6th of 6) so every other module is defined before boot() runs. */
"use strict";
const pill=document.getElementById('pill'), pillTxt=document.getElementById('pillTxt'), srcTxt=document.getElementById('srcTxt');
const panelEl=document.getElementById('panel');
function stamp(s){ return s.toTimeString().slice(0,5); }

/* PAGE: 0=weather, 1=travel. Boot into travel with ?page=travel. */
let PAGE=(params.get('page')==='travel')?1:0;
const STATUS={ wx:null, tv:null };   // per-view {pill, txt, src}, filled at boot

function setPill(cls,txt,src){ pill.className='pill '+cls; pillTxt.textContent=txt; srcTxt.textContent=src; }
function showStatus(){ const s=STATUS[PAGE===1?'tv':'wx']; if(s) setPill(s.pill,s.txt,s.src); }

/* render the ACTIVE view into the shared buffer (reset -> draw -> blit) */
function renderPage(){
  reset();
  if(PAGE===1){ if(TRAVEL) drawTravel(); } else { if(WX) drawUI(); }
  blit();
}

async function loadWeather(){
  let wx=null, err=null;
  if(MODE!=='mock'){ try{ wx=await fetchLive(); }catch(e){ err=e; } }
  if(wx){ WX=wx;
    STATUS.wx = wx.source==='live'
      ? { pill:'live', txt:'LIVE · '+stamp(wx.fetchedAt)+' · WEATHER', src:'Open-Meteo · Bratislava ('+LAT+','+LON+') · '+stamp(wx.fetchedAt) }
      : { pill:'mock', txt:'MOCK · fallback · WEATHER', src:'baked-in mock · '+(wx.note||'') };
  } else { WX=mockWx();
    STATUS.wx = { pill:MODE==='live'?'err':'mock', txt:(MODE==='live'?'FETCH FAILED · showing mock':'MOCK · offline')+' · WEATHER',
      src:'offline — mock data ('+(err?err.message:'fetch skipped')+')' };
  }
}

async function loadTravel(){
  let tv=null, err=null;
  if(MODE!=='mock'){ try{ tv=await fetchTravelLive(); }catch(e){ err=e; } }
  if(tv){ TRAVEL=tv;
    STATUS.tv = tv.source==='live'
      ? { pill:'live', txt:'LIVE · '+stamp(tv.fetchedAt)+' · TRAVEL', src:'imhd + GTFS · '+tv.stop+' · '+stamp(tv.fetchedAt) }
      : { pill:'mock', txt:'MOCK · fallback · TRAVEL', src:'baked-in mock · '+(tv.note||'') };
  } else { TRAVEL=mockTravel();
    STATUS.tv = { pill:MODE==='live'?'err':'mock', txt:(MODE==='live'?'FETCH FAILED · showing mock':'MOCK · offline')+' · TRAVEL',
      src:'offline — mock data ('+(err?err.message:'fetch skipped')+')' };
  }
}

async function boot(){
  await Promise.all([loadWeather(), loadTravel()]);
  showStatus();
  renderPage();
}

/* click / tap the panel to toggle WEATHER <-> TRAVEL (firmware: rotary press) */
panelEl.addEventListener('click', function(){
  PAGE=1-PAGE;
  showStatus();
  renderPage();
});

boot();
