// TRAVEL view: data model + rendering (the travel half of the unified panel;
// the weather half is panel_data.h / panel_ui.*). Data for Karpatská (lines
// 32/49/44) is merged from the imhd.sk live feed + IDS BK GTFS schedule by a
// local host daemon (tools/serve_next_hour.py) and served as a small JSON doc.
// draw_travel() renders a full-width HEADER (stop + day + time) over THREE
// equal LINE columns — a verbatim port of web/panel_v2/travel_ui.js.
//
// The draw path is pure canvas (no network): it compiles on the host
// pixel-harness (tools/host_render_travel.cpp) via #ifndef HOST_BUILD around
// the fetch only.
#pragma once
#include "panel_canvas.h"
#include "panel_font.h"
#include "panel_draw.h"

#define TRAVEL_MAX_LINES 3
#define TRAVEL_MAX_DEP   6

// The travel feed is imhd.sk itself (no LAN daemon): the device does the
// Socket.IO v4 long-poll handshake over HTTPS and parses the 'tabs' snapshot
// directly. imhd gives LIVE departures only (the GTFS schedule is 48 MB, too
// big on-device), so the panel shows the live window (~3-4 departures/line).
#define IMHD_STOP_ID 129   // imhd.sk stop id for Karpatská (Bratislava)

// One departure: t="HH:MM" (scheduled), d = live−scheduled in seconds
// (0 on-time), live = the imhd feed is tracking the real vehicle.
struct TravelDep  { char t[6]; int d; bool live; };
// One line column: number, destination (folded, uppercase), nástupiste, deps.
struct TravelLine { char line[4]; char dest[16]; char plat[6]; int ndep; TravelDep dep[TRAVEL_MAX_DEP]; };

struct travel_t {
  char stop[12];      // "KARPATSKA"
  char platform[8];   // "313"
  char now[6];        // "15:35"
  char day[12];       // "2026-10-18"
  int nlines;
  TravelLine lines[TRAVEL_MAX_LINES];
  bool ok;
};

extern travel_t travel;              // defined in panel_state.cpp

void draw_travel(const travel_t *tv);   // render the three line columns (web travel_ui.js port)
void travel_now_mock(travel_t *tv);     // baked 2026-10-18 15:35 fallback (web travel_data.js MOCK)
// Pure imhd 'tabs' parse (no network, host-testable): filter the Karpatská
// targets by line + destination keyword + window, dedupe by (line, time), and
// convert the ms epochs to local HH:MM. now_epoch is time(NULL) (NTP-synced).
bool travel_parse_tabs(const char *tabs, long now_epoch, travel_t *tv);

#ifndef HOST_BUILD
// The live feed: do the imhd.sk Socket.IO long-poll handshake (open -> connect
// -> tabStart -> read the 'tabs' snapshot) over HTTPS, then travel_parse_tabs().
// Blocks up to ~30 s. Returns tv->ok (false on any network/handshake/parse fail).
bool fetch_travel(travel_t *tv);
#endif
