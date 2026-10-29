// App state + orchestration: globals, wifi, refresh cycle, render, capture.
#include "panel_state.h"
#include <WiFi.h>
#include <Preferences.h>
#include <net_config.h>
#include "panel_error.h"
#include "panel_fetch.h"
#include "panel_ui.h"
#include "panel_travel.h"

uint8_t ImageBW[272 * 800 / 8];  // buffer: 800x272 (two cascaded SSD1683)

// ---- city ----------------------------------------------------------------------
// Single weather city (Munich removed 2026-10-18). The side rotary PRESS no
// longer switches city — it toggles the PAGE (weather <-> travel view).
const WxCity CITIES[1] = {
  { "BRATISLAVA", WEATHER_LAT, WEATHER_LON },  // the only weather city
};


int   cur_city = 0;
int   cur_page = PAGE_WEATHER;    // 0=weather, 1=travel (rotary PRESS toggles)
travel_t travel;                  // travel view data (Karpatská 32/49/44)
bool  wifi_ok = false;
uint32_t lastTick = 0;             // last completed refresh (for the refresh timer)

WX wx;

// ---- capture (CAP command) ----------------------------------------------------
// ---- capture (CAP command) ----------------------------------------------------
static void cap_b64(const uint8_t *data, int len) {
  static const char tb[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char line[2100];
  int p = 0;
  for (int i = 0; i < len; i += 3) {
    int v = (int)data[i] << 16 | (i + 1 < len ? (int)data[i + 1] << 8 : 0) | (i + 2 < len ? (int)data[i + 2] : 0);
    line[p++] = tb[(v >> 18) & 63];
    line[p++] = tb[(v >> 12) & 63];
    line[p++] = (i + 1 < len) ? tb[(v >> 6) & 63] : '=';
    line[p++] = (i + 2 < len) ? tb[v & 63] : '=';
    if (p >= 2000) { Serial.write(line, p); Serial.println(); p = 0; }
  }
  if (p) { Serial.write(line, p); Serial.println(); }
}

void do_capture() {
  Serial.println("CAP_START");
  cap_b64(ImageBW, 272 * 100);
  Serial.println("CAP_END");
}


// ---- boot ----------------------------------------------------------------------
static bool wifi_up() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("deskbuddy-s3");
  WiFi.begin(WIFI_SSID, WIFI_PSWD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(100);
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.printf("net: wifi up ssid=%s ip=%s rssi=%d\n", WIFI_SSID,
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  // Sync the clock (NTP) for the travel page: imhd gives absolute ms epochs, so
  // HH:MM needs a synced local time. tz = Central Europe (CET/CEST, auto-DST).
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.cloudflare.com");
  return true;
}

void ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) { wifi_ok = true; return; }
  wifi_ok = wifi_up();
}


// Fetch live data for the current PAGE into its globals (wx for weather,
// travel for the travel view). On failure the page's .ok is false and the
// caller shows the error page (no more silent mock fallback on the device).
bool refresh_data() {
  if (cur_page == PAGE_TRAVEL) {
    ensure_wifi();
    if (!wifi_ok) { travel.ok = false; return false; }   // wifi down -> error page
    travel.ok = fetch_travel(&travel);                    // imhd live? -> live : error page
    return travel.ok;
  }
  ensure_wifi();
  if (!wifi_ok) return false;
  bool ok = fetch_weather(&CITIES[cur_city]);
  if (ok) { fetch_two_days(&CITIES[cur_city]); fetch_time(); }
  return ok;
}


// Redraw the screen from the framebuffer we already have (drawn by caller).
static void do_full_refresh() {
  EPD_GPIOInit();
  EPD_FastMode1Init();
  EPD_Update();              // full refresh (0xF7) — clears ghosting on every cycle
  Serial.println("panel: full refresh");
}

// One draw+update cycle: clear, render (the current PAGE), full refresh.
// Weather: live UI if ok, else the error page. Travel: draw_travel if travel.ok,
// else the error page (messages reflect whether wifi is up or imhd is unreachable).
void render_and_show(bool ok) {
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  if (cur_page == PAGE_TRAVEL) {
    if (travel.ok) { draw_travel(&travel); Serial.println("ui: travel drawn"); }
    else { draw_error("KARPATSKA", wifi_ok ? "NO TRAVEL DATA" : "NO CONNECTION",
                      wifi_ok ? "RETRYING" : "CHECK WIFI"); Serial.println("ui: travel error page"); }
  } else if (ok) {
    draw_ui(); Serial.println("ui: live weather drawn");
  } else {
    draw_error("WEATHER", wifi_ok ? "NO WEATHER DATA" : "NO CONNECTION",
               wifi_ok ? "RETRYING" : "CHECK WIFI"); Serial.println("ui: weather error page");
  }
  EPD_Display(ImageBW);
  do_full_refresh();
  lastTick = millis();
  Serial.printf("panel up: %s shown (wifi=%d)\n",
                cur_page == PAGE_TRAVEL ? "KARPATSKA" : CITIES[cur_city].name, (int)wifi_ok);
}


// Switch to the other PAGE (weather <-> travel), persist the choice, re-fetch,
// redraw. Shared by the side rotary switch and the serial 'PAGE' command
// (used for headless verify).
void toggle_page(const char *src) {
  cur_page = (cur_page == PAGE_WEATHER) ? PAGE_TRAVEL : PAGE_WEATHER;
  Preferences prefs; prefs.begin("wx", false); prefs.putInt("page", cur_page); prefs.end();
  Serial.printf("%s: page -> %s (re-fetching)\n", src, cur_page == PAGE_TRAVEL ? "TRAVEL" : "WEATHER");
  bool ok = refresh_data();
  render_and_show(ok);
}

