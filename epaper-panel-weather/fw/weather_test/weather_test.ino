// Desk-buddy CrowPanel 5.79" e-paper: LIVE weather app (persistent).
//
// Two PAGES share the panel, toggled by the side rotary switch (choice
// persisted in flash): WEATHER (Bratislava, Open-Meteo live forecast + clock
// + sunset, re-fetched every 30 min / 1 h during quiet hours 21:00-05:00) and
// TRAVEL (Karpatská bus departures, lines 32/49/44 — merged from the imhd.sk
// live feed + IDS BK GTFS schedule by a host daemon, re-fetched every 5 min).
// Full refresh each cycle, which doubles as a ghosting-clear.
//
// Input: rotary switch (the side switch) - UP IO6 / DOWN IO4 / PRESS IO5,
//   all active-low with pull-ups (Elecrow wiring). Turning UP/DOWN toggles the
//   PAGE (no press needed); pressing the knob force-refreshes the current page.
//
// Serial protocol (for headless verification):
//   CAP -> streams the current framebuffer as base64 lines between
//          CAP_START and CAP_END (the app now stays awake; CAP is always live).
//   CAL -> print now/sunset hours
//   PAGE -> toggle PAGE (weather <-> travel) + re-fetch (mirrors the side switch)
//   STATE DRY|RAIN|SNOW|STORM|RESET -> override the rain-state row (weather page)
//   MOCK hh:mm -> redraw the weather page as if now were hh:mm
//
// Module map (all in this directory):
//   panel_canvas.h   shared canvas: framebuffer, pixel setter, layout, pins
//   panel_font.h     5x7 font table + text renderer
//   panel_draw.h     primitives + weather icons + row glyphs + moon phase
//   panel_data.h     weather data model (WxCity / WXDay / WX)
//   panel_net.h/.cpp raw mbedTLS HTTPS GET
//   panel_fetch.h/.cpp Open-Meteo fetch + JSON parsing + date/weather helpers
//   panel_ui.h/.cpp  weather-page rendering (curve, 2-day strip, full UI)
//   panel_travel.h/.cpp travel page: data model, rendering, JSON parse + fetch
//   panel_state.h/.cpp globals + wifi / refresh / render / capture + page toggle
//   weather_test.ino serial commands + setup()/loop() (control flow only)

#include <Preferences.h>
#include "panel_canvas.h"
#include "panel_data.h"
#include "panel_fetch.h"
#include "panel_state.h"

// Handle a serial line (CAL/CAP/CITY still work while the app stays awake).
// Non-blocking: accumulates bytes until a newline, so a bare command without
// a trailing newline can't wedge the loop.
static void poll_serial() {
  static char buf[96];
  static int blen = 0;
  int mockH, mockM;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (blen > 0) {
        buf[blen] = 0;
        if (strstr(buf, "CAL")) {
          Serial.print("cal: now=");
          Serial.print(wx.now_hour); Serial.print(":");
          if (wx.now_min < 10) Serial.print('0');
          Serial.print(wx.now_min);
          if (wx.sunset_hour >= 0) { Serial.print(" sunset="); Serial.print(wx.sunset_hour); }
          Serial.println();
        } else if (strstr(buf, "CAP")) {
          do_capture();
        } else if (strstr(buf, "PAGE")) {
          toggle_page("ser");   // headless page toggle test (mirrors the side switch)
        } else if (!strncmp(buf, "STATE", 5)) {
          // test aid: override the rain-state classification to preview a state
          const char *st = (buf[5] == ' ') ? buf + 6 : buf + 5;
          if (!strcmp(st, "RESET")) state_ov = nullptr;
          else if (!strcmp(st, "DRY")) state_ov = "DRY";
          else if (!strcmp(st, "RAIN")) state_ov = "RAIN";
          else if (!strcmp(st, "SNOW")) state_ov = "SNOW";
          else if (!strcmp(st, "STORM")) state_ov = "STORM";
          else { Serial.println("state: use DRY/RAIN/SNOW/STORM/RESET"); blen = 0; return; }
          render_and_show(true);
          Serial.printf("state: override -> %s\n", state_ov ? state_ov : "(none)");
        } else if (sscanf(buf, "MOCK %d:%d", &mockH, &mockM) == 2 &&
                   mockH >= 0 && mockH < 24 && mockM >= 0 && mockM < 60) {
          // test aid: redraw the live data as if "now" were hh:mm (exercises
          // time-dependent UI — night band, sunset countdown — without waiting)
          wx.now_hour = mockH; wx.now_min = mockM;
          render_and_show(true);
          Serial.printf("mock: redrawn as %02d:%02d\n", mockH, mockM);
        }
        blen = 0;
      }
    } else if (blen < sizeof(buf) - 1) {
      buf[blen++] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("== desk-buddy 5.79 e-paper: weather + travel (persistent) ==");

  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, HIGH);
  delay(100);
  Serial.println("panel power: on");

  pinMode(BTN_PRESS, INPUT_PULLUP);
  pinMode(BTN_UP,    INPUT_PULLUP);
  pinMode(BTN_DOWN,  INPUT_PULLUP);

  Preferences prefs;
  prefs.begin("wx", false);
  int savedPage = prefs.getInt("page", PAGE_WEATHER);
  if (savedPage != PAGE_WEATHER && savedPage != PAGE_TRAVEL) savedPage = PAGE_WEATHER;
  cur_page = savedPage;
  cur_city = 0;   // single city (Bratislava)
  Serial.printf("page: %s (city %s)\n", cur_page == PAGE_TRAVEL ? "travel" : "weather", CITIES[cur_city].name);

  memset(&wx, 0, sizeof(wx));
  for (int i = 0; i < 13; i++) wx.temps[i] = NAN;
  wx.sunrise_hour = -1;
  wx.sunset_hour = -1;
  wx.now_hour = -1;
  wx.now_min = 0;

  // first fetch
  bool ok = refresh_data();
  if (!ok) Serial.println("wx: FETCH FAILED -> status page");

  EPD_GPIOInit();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  Serial.print("init: ");
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_Update();
  Serial.println("blank full refresh done");

  render_and_show(ok);
}

void loop() {
  poll_serial();

  // rotary input: an UP/DOWN detent edge TOGGLES the PAGE — turn the knob to
  // switch between weather/travel, no press needed (the old press-to-toggle is
  // gone). Pressing the knob force-refreshes the current page (the detent's old
  // job). Simple rising-edge detect on the (active-low) rotary pins.
  static bool lastPress = false, lastUp = false, lastDown = false;
  bool p = !digitalRead(BTN_PRESS);
  bool u = !digitalRead(BTN_UP);
  bool d = !digitalRead(BTN_DOWN);
  if ((u && !lastUp) || (d && !lastDown)) toggle_page("btn");   // rotate up/down: toggle page
  if (p && !lastPress) {                                        // press: force-refresh now
    Serial.println("btn: press -> refresh");
    bool ok = refresh_data();
    render_and_show(ok);
  }
  lastPress = p; lastUp = u; lastDown = d;

  if (!wifi_ok) ensure_wifi();             // opportunistic reconnect
  // Travel refreshes more often (buses move); weather uses the day/quiet cadence.
  uint32_t interval = (cur_page == PAGE_TRAVEL) ? TRAVEL_REFRESH_MS : current_refresh_ms();
  if (millis() - lastTick >= interval) {  // periodic refresh of the current page
    Serial.printf("tick: %s refresh\n",
                  cur_page == PAGE_TRAVEL ? "travel 5-min" : (interval == REFRESH_MS_NIGHT ? "1h night" : "30-min"));
    bool ok = refresh_data();
    render_and_show(ok);
    lastTick = millis();
  }
  delay(40);
}
