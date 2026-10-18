// App state + orchestration: globals, wifi, refresh cycle, render, capture.
#include "panel_state.h"
#include <WiFi.h>
#include <Preferences.h>
#include <net_config.h>
#include "statusimg.h"
#include "panel_fetch.h"
#include "panel_ui.h"

uint8_t ImageBW[272 * 800 / 8];  // buffer: 800x272 (two cascaded SSD1683)

// ---- cities -------------------------------------------------------------------
// Second+ cities are defined here (Bratislava comes from shared net_config.h).
const WxCity CITIES[2] = {
  { "BRATISLAVA", WEATHER_LAT, WEATHER_LON },  // index 0 (default, from shared config)
  { "MUNICH",     "48.1351",  "11.5820"      },  // index 1
};


int   cur_city = 0;
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
  return true;
}

void ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) { wifi_ok = true; return; }
  wifi_ok = wifi_up();
}


// Fetch live data for the current cit into the global wx (full refresh on fail).
bool refresh_data() {
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

// One draw+update cycle: clear, render (weather or status), full refresh.
void render_and_show(bool ok) {
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  if (ok) { draw_ui(); Serial.println("ui: live weather drawn"); }
  else { EPD_ShowPicture(0, 0, 792, 272, gImage_status, WHITE); Serial.println("ui: status bitmap"); }
  EPD_Display(ImageBW);
  do_full_refresh();
  lastTick = millis();
  Serial.printf("panel up: %s %s shown (wifi=%d)\n",
                CITIES[cur_city].name, ok ? "live" : "status", (int)wifi_ok);
}


// Switch to the next city, persist, re-fetch, redraw.
// Switch to the next city, persist, re-fetch, redraw. Shared by the side
// rotary switch and the serial 'CITY' command (used for headless verify).
void toggle_city(const char *src) {
  cur_city = (cur_city + 1) % N_CITIES;
  Preferences prefs; prefs.begin("wx", false); prefs.putInt("city", cur_city); prefs.end();
  Serial.printf("%s: toggle -> %s (re-fetching)\n", src, CITIES[cur_city].name);
  bool ok = refresh_data();
  render_and_show(ok);
}

