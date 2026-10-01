// Desk-buddy CrowPanel 5.79" e-paper: LIVE weather app (persistent).
//
// Keeps the panel live and re-fetches Open-Meteo every 10 minutes (full
// refresh each cycle — doubles as a ghosting-clear). Two cities are
// configured (Bratislava default, Munich); pressing the side rotary switch
// toggles between them (choice persisted in flash). Each cit shows its own
// live 13h forecast, local clock and sunset.
//
// Layout (shared 1:1 with tools/sim_weather_ui.py, same coordinates):
//   left  : 4 condition icons + big current temp + city name
//   right : 13h temp curve with night band + data labels + hour axis
//   bottom: three badge outlines (top border only, black ink on paper)
//
// Input: rotary switch (the side switch) — UP IO6 / DOWN IO4 / PRESS IO5,
//   all active-low with pull-ups (Elecrow wiring). PRESS toggles cit;
//   any detent edge also triggers an immediate refresh.
//
// Serial protocol (for headless verification):
//   CAP -> streams the current framebuffer as base64 lines between
//          CAP_START and CAP_END (the app now stays awake; CAP is always live).

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <Preferences.h>
#include <net_config.h>
#include "EPD.h"
#include "statusimg.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "esp_random.h"
#include "le_dercainfo.h"  // LE_ANCHORS: Let's Encrypt trust roots as DER

#define EPD_PWR_PIN 7

// Input: the side rotary switch (active-low, pull-ups — Elecrow wiring)
#define BTN_PRESS  5   // rotary press / center -> toggle cit
#define BTN_UP     6   // rotary up
#define BTN_DOWN   4   // rotary down
#define REFRESH_MS (10UL * 60UL * 1000UL)  // 10 minutes

uint8_t ImageBW[272 * 800 / 8];  // buffer: 800x272 (two cascaded SSD1683)

// Forward decl: the .ino preprocessor hoists function prototypes to the top,
// so any type used in a signature must be declared before those prototypes.
struct WxCity;

// NOTE: Paint_SetPixel(x,y,WHITE) SETS the buffer bit -> LIGHT on e-ink;
// Paint_SetPixel(x,y,BLK) CLEARS it -> dark ink. (EPD_ShowPicture maps a dark
// source pixel to buffer-clear, so direct drawing must use the same: INK=BLK.)
#define INK   BLACK
#define PAPER WHITE

// ---- layout constants (mirror tools/sim_weather_ui.py) --------------------
#define CX0   240
#define CX1   770
#define CY_T  16
#define CY_B  170
#define IX0   20
#define IGAP  52
#define ICY   56
#define CH_Y  214
#define CH_H  40

// ---- 5x7 pixel font (same glyphs as the render tools) ----------------------
// Order: 0-9, A-H, space, K-Q, R-W, Y, -, ., :, /
static const uint8_t GLYPHS[][7] = {
  /*0 '0'*/{0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110},
  /*1 '1'*/{0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110},
  /*2 '2'*/{0b01110,0b10001,0b00001,0b00110,0b01100,0b10000,0b11111},
  /*3 '3'*/{0b11110,0b00001,0b00001,0b01110,0b00001,0b00001,0b11110},
  /*4 '4'*/{0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010},
  /*5 '5'*/{0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110},
  /*6 '6'*/{0b00110,0b10000,0b10000,0b11110,0b10001,0b10001,0b01110},
  /*7 '7'*/{0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000},
  /*8 '8'*/{0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110},
  /*9 '9'*/{0b01110,0b10001,0b10001,0b01111,0b00001,0b00110,0b01010},
  /*10 'A'*/{0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001},
  /*11 'B'*/{0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110},
  /*12 'C'*/{0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110},
  /*13 'D'*/{0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110},
  /*14 'E'*/{0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111},
  /*15 'F'*/{0b11111,0b10000,0b11110,0b10000,0b10000,0b10000,0b10000},
  /*16 'G'*/{0b01110,0b10001,0b10000,0b10110,0b10001,0b10001,0b01111},
  /*17 'H'*/{0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001},
  /*18 ' '*/{0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b00000},
  /*19 'K'*/{0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001},
  /*20 'L'*/{0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111},
  /*21 'M'*/{0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001},
  /*22 'N'*/{0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001},
  /*23 'O'*/{0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110},
  /*24 'P'*/{0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000},
  /*25 'Q'*/{0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101},
  /*26 'R'*/{0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001},
  /*27 'S'*/{0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110},
  /*28 'T'*/{0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100},
  /*29 'U'*/{0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110},
  /*30 'V'*/{0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100},
  /*31 'W'*/{0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001},
  /*32 'Y'*/{0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100},
  /*33 '-'*/{0b00000,0b00000,0b00000,0b11111,0b00000,0b00000,0b00000},
  /*34 '.'*/{0b00000,0b00000,0b00000,0b00000,0b00000,0b00110,0b00110},
  /*35 ':'*/{0b00000,0b00110,0b00000,0b00000,0b00000,0b00110,0b00000},
  /*36 '/'*/{0b00000,0b00011,0b00110,0b01100,0b11000,0b00000,0b00000},
};
static int glyph_idx(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'H') return 10 + (c - 'A');
  if (c == ' ') return 18;
  if (c >= 'K' && c <= 'Q') return 19 + (c - 'K');
  if (c >= 'R' && c <= 'W') return 26 + (c - 'R');
  if (c == 'Y') return 32;
  if (c == '-') return 33;
  if (c == '.') return 34;
  if (c == ':') return 35;
  if (c == '/') return 36;
  return -1;
}

// ---- drawing primitives -----------------------------------------------------
// The canvas is Paint_NewImage(800, 272, Rotation=180). Paint_SetPixel's
// rotate-180 case handles the physical mapping (including the 8-pixel cascade
// gap between the two 396-wide SSD1683s) internally — that is exactly what the
// proven-good EPD_ShowPicture path relies on (plain landscape x,y). We author
// in landscape mockup coordinates (mx,my) in [0..791]x[0..271] and pass them
// through untouched.
static inline void cv_set(int mx, int my, uint16_t c) {
  Paint_SetPixel(mx, my, c);
}

static int put_text(int x, int y, const char *s, uint16_t color, int sc) {
  int cx = x;
  for (; *s; s++) {
    int gi = glyph_idx(*s);
    if (gi < 0) continue;
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5; c++)
        if (GLYPHS[gi][r] & (1 << (4 - c))) {
          for (int dy = 0; dy < sc; dy++)
            for (int dx = 0; dx < sc; dx++)
              cv_set(cx + c * sc + dx, y + r * sc + dy, color);
        }
    cx += 6 * sc;
  }
  return cx;
}
static int text_w(const char *s, int sc) { return (int)strlen(s) * 6 * sc; }

static void fill_circle(int cx, int cy, int r, uint16_t color) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx * dx + dy * dy <= r * r)
        cv_set(cx + dx, cy + dy, color);
}

// thick line (Bresenham, width ~ 2*h+1)
static void thick_line(int x0, int y0, int x1, int y1, uint16_t color, int w) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int e = dx + dy, h = w / 2;
  for (;;) {
    for (int t = -h; t <= h; t++) { cv_set(x0 + t, y0, color); cv_set(x0, y0 + t, color); }
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += sx; }
    if (e2 <= dx) { e += dx; y0 += sy; }
  }
}

static void fill_rect(int x0, int y0, int x1, int y1, uint16_t color) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      cv_set(x, y, color);
}

// rounded filled rect (radius r)
static void round_rect_fill(int x0, int y0, int x1, int y1, int r, uint16_t color) {
  for (int y = y0; y <= y1; y++) {
    int a = x0, b = x1;
    if (y < y0 + r) {
      int dy = y0 + r - y - 1;
      int dx = (int)sqrtf((float)(r * r - dy * dy));
      if (dx < 0) dx = 0;
      a = x0 + r - dx;
    } else if (y > y1 - r) {
      int dy = y - (y1 - r) + 1;
      int dx = (int)sqrtf((float)(r * r - dy * dy));
      if (dx < 0) dx = 0;
      b = x1 - r + dx;
    }
    for (int x = a; x <= b; x++) cv_set(x, y, color);
  }
}

// ---- weather icons ----------------------------------------------------------
static void icon_sun(int cx, int cy, int r) {
  for (int y = cy - r; y <= cy + r; y++)
    for (int x = cx - r; x <= cx + r; x++) {
      int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
      if (d2 >= (r - 2) * (r - 2)) cv_set(x, y, INK);
    }
  const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
  const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  for (int i = 0; i < 8; i++) {
    int ux = dx8[i], uy = dy8[i];
    int x0 = cx + ux * (r + 3), y0 = cy + uy * (r + 3);
    int x1 = cx + ux * (r + 8), y1 = cy + uy * (r + 8);
    if (ux != 0 && uy != 0) { x1 = cx + (ux < 0 ? -1 : 1) * (r + 6); y1 = cy + (uy < 0 ? -1 : 1) * (r + 6); }
    thick_line(x0, y0, x1, y1, INK, 2);
  }
}

// Crescent moon: a full ink disc knocked out by an offset paper disc (the
// simulator's two-circle trick). The cut is biased to the lower-right so the
// lit crescent opens to the upper-left.
static void icon_moon(int cx, int cy, int r) {
  fill_circle(cx, cy, r, INK);
  fill_circle(cx + r / 2, cy - r / 3, r, PAPER);
}

static void icon_cloud(int cx, int cy) {
  fill_circle(cx - 8, cy, 8, INK);
  fill_circle(cx, cy - 6, 8, INK);
  fill_circle(cx + 9, cy, 8, INK);
  for (int y = cy + 2; y <= cy + 8; y++)
    for (int x = cx - 14; x <= cx + 15; x++) cv_set(x, y, INK);
}

static void icon_part(int cx, int cy) {
  int sx = cx - 4, sy = cy - 4;
  for (int y = sy - 11; y <= sy + 11; y++)
    for (int x = sx - 11; x <= sx + 11; x++) {
      int d2 = (x - sx) * (x - sx) + (y - sy) * (y - sy);
      if (d2 >= 77 && d2 <= 116) cv_set(x, y, INK);
    }
  const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
  const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  for (int i = 0; i < 8; i++) {
    int ux = dx8[i], uy = dy8[i];
    int x0 = sx + ux * 11, y0 = sy + uy * 11;
    int x1 = sx + ux * 16, y1 = sy + uy * 16;
    if (ux != 0 && uy != 0) { x1 = sx + (ux < 0 ? -1 : 1) * 14; y1 = sy + (uy < 0 ? -1 : 1) * 14; }
    thick_line(x0, y0, x1, y1, INK, 2);
  }
  icon_cloud(cx, cy + 8);
}

// ---- cities -------------------------------------------------------------------
// Second+ cities are defined here (Bratislava comes from shared net_config.h).
struct WxCity { const char *name; const char *lat; const char *lon; };
static const WxCity CITIES[2] = {
  { "BRATISLAVA", WEATHER_LAT, WEATHER_LON },  // index 0 (default, from shared config)
  { "MUNICH",     "48.1351",  "11.5820"      },  // index 1
};
static const int N_CITIES = 2;

static int   cur_city = 0;
static bool  wifi_ok = false;
static uint32_t lastTick = 0;             // last completed refresh (for the 10-min timer)

// ---- weather data ------------------------------------------------------------
struct WX {
  float cur, feels, wind;
  int code;
  float temps[13];
  int codes[13];
  float rain_total;
  int sunrise_hour;  // local hour 0..23, -1 if unknown
  int sunrise_min;   // local minute 0..59, 0 if unknown
  int sunset_hour;   // local hour 0..23, -1 if unknown
  int sunset_min;    // local minute 0..59, 0 if unknown
  int now_hour, now_min;
  bool ok;
};
static WX wx;

// Day/night from the local time-of-day window [sunrise, sunset]. The 13h
// forecast starts at "now" and runs forward, so its leading hours can cross
// into the next night — each slot is checked against the same window. With no
// sunrise/sunset data we fall back to a coarse hour heuristic.
static bool is_daytime(int h) {
  if (wx.now_hour < 0) return true;
  int cur = wx.now_hour * 60 + wx.now_min;
  int win = h * 60;  // minutes into "now"
  int t = (cur + win) % 1440;  // absolute local minutes
  if (wx.sunrise_hour >= 0 && wx.sunset_hour >= 0) {
    int sr = wx.sunrise_hour * 60 + wx.sunrise_min;
    int ss = wx.sunset_hour * 60 + wx.sunset_min;
    if (sr < ss) return (t >= sr && t < ss);
    return (t >= sr || t < ss);  // night straddles midnight
  }
  return (t >= 300 && t < 1020);  // ~05:00–17:00 fallback
}

// format a float as "N.N" / "NN.N" (max 5 chars + NUL); returns char count
static int fmt_num(char *buf, float v, int dec) {
  if (isnan(v)) { buf[0] = '?'; buf[1] = 0; return 1; }
  bool neg = v < 0;
  if (neg) v = -v;
  long iv = (long)(v * (dec ? 10 : 1) + 0.5f);
  long ip = iv / (dec ? 10 : 1);
  long fp = iv % (dec ? 10 : 1);
  int p = 0;
  if (neg) buf[p++] = '-';
  if (ip >= 100) buf[p++] = '0' + ip / 100 % 10;
  if (ip >= 10)  buf[p++] = '0' + ip / 10 % 10;
  buf[p++] = '0' + ip % 10;
  if (dec) { buf[p++] = '.'; buf[p++] = '0' + fp; }
  buf[p] = 0;
  return p;
}

// ---- network: raw mbedTLS -----------------------------------------------------
static int le_rng(void *p, unsigned char *out, size_t len) {
  (void)p; esp_fill_random(out, len); return 0;
}

// GET https://WEATHER_HOST/path -> body (de-chunked). Returns bytes or -1.
static int http_get(const char *path, char *body_out, size_t cap) {
  char req[340];
  int ql = snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\n"
                "User-Agent: desk-buddy-s3\r\nConnection: close\r\n\r\n", path, WEATHER_HOST);
  if (ql < 0 || (size_t)ql >= sizeof(req)) return -1;
  Serial.printf("wx: GET https://%s%s\n", WEATHER_HOST, path);

  mbedtls_net_context net; mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
  mbedtls_x509_crt cacrt;
  mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
  mbedtls_x509_crt_init(&cacrt);

  int r = mbedtls_net_connect(&net, WEATHER_HOST, "443", MBEDTLS_NET_PROTO_TCP);
  if (r) { char b[80]; mbedtls_strerror(r, b, sizeof(b)); Serial.printf("wx: tcp FAIL (%s)\n", b); return -1; }

  mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, NULL);
  mbedtls_ssl_conf_rng(&conf, le_rng, NULL);
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  for (size_t i = 0; i < sizeof(LE_ANCHORS) / sizeof(LE_ANCHORS[0]); i++)
    mbedtls_x509_crt_parse_der(&cacrt, LE_ANCHORS[i].der, LE_ANCHORS[i].len);
  mbedtls_ssl_conf_ca_chain(&conf, &cacrt, NULL);
  mbedtls_ssl_setup(&ssl, &conf);
  mbedtls_ssl_set_hostname(&ssl, WEATHER_HOST);
  mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);

  int h = mbedtls_ssl_handshake(&ssl);
  if (h) { char b[160]; mbedtls_strerror(h, b, sizeof(b)); Serial.printf("wx: TLS FAIL r=%d (%s)\n", h, b); return -1; }
  Serial.printf("wx: TLS verified (flags 0x%08lx)\n", (unsigned long)mbedtls_ssl_get_verify_result(&ssl));

  if (mbedtls_ssl_write(&ssl, (const unsigned char *)req, (size_t)ql) < 0) {
    Serial.println("wx: request write FAIL");
    mbedtls_net_free(&net); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&cacrt);
    return -1;
  }

  static char raw[4200];
  int n = 0; uint32_t t0 = millis(); bool closed = false;
  while (!closed && millis() - t0 < 15000) {
    int rc = mbedtls_ssl_read(&ssl, (unsigned char *)raw + n, sizeof(raw) - 1 - n);
    if (rc > 0) { n += rc; continue; }
    if (rc == MBEDTLS_ERR_SSL_WANT_READ) { delay(10); continue; }
    if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == MBEDTLS_ERR_NET_CONN_RESET) { closed = true; break; }
    Serial.printf("wx: read FAIL r=0x%04x\n", (unsigned)rc); break;
  }
  raw[n] = 0;
  char hdr[32]; sscanf(raw, "%31s", hdr);
  int sc = 0; sscanf(raw, "%*s %d", &sc);
  Serial.printf("wx: %d bytes in %lums (%s %d)\n", n, millis() - t0, hdr, sc);

  mbedtls_ssl_close_notify(&ssl);
  mbedtls_net_free(&net); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&cacrt);

  char *p = strstr(raw, "\r\n\r\n");
  if (p) p += 4; else p = raw;
  // de-chunk
  char *o = body_out; char *src = p;
  while (*src) {
    if (isxdigit((unsigned char)src[0])) {
      unsigned long len = 0;
      while (isxdigit((unsigned char)*src)) {
        len = len * 16 + (isdigit((unsigned char)*src) ? *src - '0' : *src - 'a' + 10);
        src++;
      }
      if (*src == '\r') { src++; if (*src == '\n') src++; }
      if (len == 0) break;
      if ((size_t)(o - body_out) + len >= cap) break;
      memcpy(o, src, len); o += len; src += len;
      if (*src == '\r') src++;
      if (*src == '\n') src++;
    } else {
      if ((size_t)(o - body_out) + 1 >= cap) break;
      *o++ = *src++;
    }
  }
  *o = 0;
  return (int)(o - body_out);
}

static float num_after(const char *j, const char *key) {
  const char *p = strstr(j, key);
  if (!p) return NAN;
  while (*p && *p != ':') p++;
  if (!*p) return NAN;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  if (!(*p == '-' || (*p >= '0' && *p <= '9') || *p == '.')) return NAN;
  return (float)atof(p);
}

static int parse_num_array(const char *j, const char *key, float *out, int max) {
  const char *k = strstr(j, key);
  if (!k) return -1;
  const char *a = strchr(k, '[');
  const char *end = a ? strchr(a, ']') : NULL;
  if (!a || !end) return -1;
  const char *c = a + 1;
  int n = 0;
  while (c < end && n < max) {
    if (*c == '-' || (*c >= '0' && *c <= '9') || *c == '.') {
      float v = (float)atof(c);
      if (!isnan(v)) out[n++] = v;
      const char *e = c;
      while (e < end && (*e == '-' || (*e >= '0' && *e <= '9') || *e == '.')) e++;
      c = e + 1;
    } else c++;
  }
  return n;
}

static int fetch_weather(const WxCity *cit) {
  static char body[4200];
  char path[260];
  snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%s&longitude=%s"
           "&current=temperature_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m"
           "&hourly=temperature_2m,weather_code,precipitation&forecast_hours=13&timezone=auto",
           cit->lat, cit->lon);
  int bl = http_get(path, body, sizeof(body));
  if (bl < 0) return false;
  const char *cblk = strstr(body, "\"current\":{");
  wx.cur   = cblk ? num_after(cblk, "\"temperature_2m\"") : NAN;
  wx.feels = cblk ? num_after(cblk, "\"apparent_temperature\"") : NAN;
  wx.wind  = cblk ? num_after(cblk, "\"wind_speed_10m\"") : NAN;
  wx.code  = (int)(cblk ? num_after(cblk, "\"weather_code\"") : NAN);
  parse_num_array(body, "\"temperature_2m\":[", wx.temps, 13);
  parse_num_array(body, "\"weather_code\":[", (float *)wx.codes, 13);
  const char *k = strstr(body, "\"precipitation\":[");
  if (k) {
    const char *a = strchr(k, '[');
    const char *end = a ? strchr(a, ']') : NULL;
    if (a && end) {
      const char *c = a + 1;
      float tot = 0;
      while (c < end) {
        if (*c == '-' || (*c >= '0' && *c <= '9') || *c == '.') {
          tot += (float)atof(c);
          const char *e = c;
          while (e < end && (*e == '-' || (*e >= '0' && *e <= '9') || *e == '.')) e++;
          c = e + 1;
        } else c++;
      }
      wx.rain_total = tot;
    }
  }
  // local clock: hourly.time[0] is the current local hour (timezone=auto)
  {
    const char *k = strstr(body, "\"hourly\":{\"time\":[\"");
    if (k) {
      const char *q = k + strlen("\"hourly\":{\"time\":[\"");
      if (q[10] == 'T') {
        int hh = (q[11] - '0') * 10 + (q[12] - '0');
        int mm = (q[14] - '0') * 10 + (q[15] - '0');
        if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
          wx.now_hour = hh; wx.now_min = mm;
          Serial.printf("wx: now=%d:%d (from forecast)\n", hh, mm);
        }
      }
    }
  }
  // Seed the graph's first point with the LIVE current temp, not the model's
  // first-hour forecast. Open-Meteo's current.temperature_2m (live obs) and
  // hourly[0] can disagree for the same "now" hour, which made the big number
  // and the curve's left edge contradict each other. Using the live value for
  // "now" makes the big temp, the curve start, and the min/max all agree.
  if (!isnan(wx.cur)) wx.temps[0] = wx.cur;
  wx.ok = !isnan(wx.cur) && !isnan(wx.temps[0]);
  float lo = NAN, hi = NAN;
  for (int i = 0; i < 13; i++) {
    if (isnan(wx.temps[i])) continue;
    if (isnan(lo) || wx.temps[i] < lo) lo = wx.temps[i];
    if (isnan(hi) || wx.temps[i] > hi) hi = wx.temps[i];
  }
  Serial.printf("wx: OK now=%.1f feels=%.1f wind=%.1f code=%d rain=%.1fmm range=%.1f..%.1f (13h)\n",
                wx.cur, wx.feels, wx.wind, wx.code, wx.rain_total, lo, hi);
  return wx.ok;
}

static int parse_hhmm(const char *q, int *hh, int *mm) {
  // q points at the first char of an ISO timestamp "YYYY-MM-DDTHH:MM:SS" —
  // the date starts with the year digit, the 'T' separator is at index 10.
  if (!q || q[10] != 'T') return -1;
  int h = (q[11] - '0') * 10 + (q[12] - '0');
  int m = (q[14] - '0') * 10 + (q[15] - '0');
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  *hh = h; *mm = m;
  return 0;
}

static int fetch_sunset(const WxCity *cit) {
  static char body[2048];
  char path[200];
  snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%s&longitude=%s&daily=sunrise,sunset&forecast_days=1&timezone=auto",
           cit->lat, cit->lon);
  int bl = http_get(path, body, sizeof(body));
  if (bl < 0) return false;
  const char *rs = strstr(body, "\"sunrise\":[");
  const char *st = strstr(body, "\"sunset\":[");
  if (!rs || !st) return false;
  int hh, mm;
  if (parse_hhmm(strchr(strchr(rs, '[') + 1, '"') + 1, &hh, &mm) == 0) { wx.sunrise_hour = hh; wx.sunrise_min = mm; }
  if (parse_hhmm(strchr(strchr(st, '[') + 1, '"') + 1, &hh, &mm) == 0) { wx.sunset_hour = hh; wx.sunset_min = mm; }
  Serial.printf("wx: sunrise %d:%02d sunset %d:%02d\n",
                wx.sunrise_hour, wx.sunrise_min, wx.sunset_hour, wx.sunset_min);
  return true;
}

static int fetch_time() {
  // clock is parsed from the forecast body (hourly.time[0]) in fetch_weather;
  // this is a no-op kept for interface clarity.
  return wx.now_hour >= 0;
}

// ---- UI rendering (coords = tools/sim_weather_ui.py) --------------------------
static void draw_temp_curve() {
  float lo = NAN, hi = NAN;
  for (int i = 0; i < 13; i++) {
    if (isnan(wx.temps[i])) continue;
    if (isnan(lo) || wx.temps[i] < lo) lo = wx.temps[i];
    if (isnan(hi) || wx.temps[i] > hi) hi = wx.temps[i];
  }
  if (isnan(lo)) return;
  // keep the points off the plot edges (visual only — labels show the raw values)
  float plo = lo - 1.0f, phi = hi + 1.0f;
  float range = phi - plo;
  if (range < 1.0f) range = 1.0f;

  int px[13], py[13];
  for (int i = 0; i < 13; i++) {
    px[i] = CX0 + i * (CX1 - CX0) / 12;
    py[i] = CY_T + (int)((phi - wx.temps[i]) * (CY_B - CY_T) / range + 0.5f);
  }

  // night band: from sunset hour (offset from now) to end, extending down
  // behind the hour-axis row (the axis labels inside it get white knock-outs)
  if (wx.sunset_hour >= 0 && wx.now_hour >= 0) {
    int i0 = wx.sunset_hour - wx.now_hour;
    if (i0 < 1) i0 = 1;
    if (i0 > 11) i0 = 11;
    int n0 = px[i0] - 3, n1 = px[12] + 3;
    for (int y = CY_T - 14; y <= CY_B + 29; y++)
      for (int x = n0; x <= n1; x++)
        if ((x + y) % 2 == 0) cv_set(x, y, INK);
  }

  // (sunset label removed — the plot now stretches up into the top strip it used)

  // curve
  for (int i = 0; i < 12; i++)
    thick_line(px[i], py[i], px[i + 1], py[i + 1], INK, 3);

  // markers: hollow ring at hour 0, filled dots at hour 9 and 12
  for (int y = py[0] - 6; y <= py[0] + 6; y++)
    for (int x = px[0] - 6; x <= px[0] + 6; x++) {
      int d2 = (x - px[0]) * (x - px[0]) + (y - py[0]) * (y - py[0]);
      if (d2 >= 20 && d2 <= 36) cv_set(x, y, INK);
    }
  fill_circle(px[9], py[9], 4, INK);
  fill_circle(px[12], py[12], 4, INK);

  // ---- data labels --------------------------------------------------------
  // The big number on the left is "now", so the curve needs no reference
  // label. Two plain VALUE plates, no words (the curvature explains the
  // rest):
  //   * the END of the window (index 12, 12h from now),
  //   * the OUTLIER — the interior point (1..11) farthest from the
  //     now->end baseline: the bulge that breaks the trend (midday peak,
  //     pre-dawn dip). Skipped when the bulge is under ~1 degree (a
  //     monotonic curve has nothing to call out) or when it would crowd
  //     the end label (within 2h of it).
  // Plates sit below their point when the point is in the upper half of
  // the plot, above when in the lower half — keeps them out of the curve's
  // way.
  const float nowT = wx.temps[0];
  const float endT = wx.temps[12];
  int outI = -1;
  float outDev = 1.0f;   // <~1 degree bulge isn't worth a label
  for (int k = 1; k <= 11; k++) {
    float base = nowT + (endT - nowT) * (k / 12.0f);
    float ad = fabsf(wx.temps[k] - base);
    if (ad > outDev) { outDev = ad; outI = k; }
  }
  char full[8];
  const int PH = 24;   // plate height
  const int midY = (CY_T + CY_B) / 2;
  {   // end-of-window plate (plain value)
    const int i = 12;
    int bn = fmt_num(full, wx.temps[i], 1);
    int pw = 6 + bn * 12 + 6 + 6;
    int x = px[i] - pw / 2;
    if (x < CX0 - 4) x = CX0 - 4;
    if (x > CX1 - pw + 4) x = CX1 - pw + 4;
    bool above = (py[i] > midY);
    int y = above ? (py[i] - PH - 12) : (py[i] + 12);
    if (y < 2) y = py[i] + 12;   // safety: never clip the top edge
    fill_rect(x, y, x + pw, y + PH, PAPER);
    put_text(x + 6, y + 4, full, INK, 2);
    fill_rect(x + 6 + bn * 12 + 2, y + 4, x + 6 + bn * 12 + 6, y + 10, INK);
    thick_line(x + pw / 2, above ? y + PH : y, x + pw / 2, above ? py[i] - 5 : py[i] + 5, INK, 2);
  }
  if (outI >= 0 && (12 - outI) >= 2) {   // outlier plate, skip if crowding the end label
    int bn = fmt_num(full, wx.temps[outI], 1);
    int pw = 6 + bn * 12 + 6 + 6;
    int x = px[outI] - pw / 2;
    if (x < CX0 - 4) x = CX0 - 4;
    if (x > CX1 - pw + 4) x = CX1 - pw + 4;
    bool above = (py[outI] > midY);
    int y = above ? (py[outI] - PH - 12) : (py[outI] + 12);
    if (y < 2) y = py[outI] + 12;   // safety: never clip the top edge
    fill_rect(x, y, x + pw, y + PH, PAPER);
    put_text(x + 6, y + 4, full, INK, 2);
    fill_rect(x + 6 + bn * 12 + 2, y + 4, x + 6 + bn * 12 + 6, y + 10, INK);
    thick_line(x + pw / 2, above ? y + PH : y, x + pw / 2, above ? py[outI] - 5 : py[outI] + 5, INK, 2);
  }

  // hour axis (12h window: every 3h — 0, 3, 6, 9, 12). Labels that land in
  // the night band get a white knock-out rect cut from the dither first
  // (black text on paper, same treatment as the value plates).
  char lab[4];
  if (wx.now_hour >= 0) {
    int n0 = 0, n1 = 0;
    if (wx.sunset_hour >= 0) {
      int i0 = wx.sunset_hour - wx.now_hour;
      if (i0 < 1) i0 = 1;
      if (i0 > 11) i0 = 11;
      n0 = px[i0] - 3;
      n1 = px[12] + 3;
    }
    const int IDX[5] = {0, 3, 6, 9, 12};
    for (int k = 0; k < 5; k++) {
      int idx = IDX[k];
      int h = (wx.now_hour + idx) % 24;
      lab[0] = '0' + h / 10; lab[1] = '0' + h % 10; lab[2] = 0;
      int lx = px[idx] - 12;
      int lc = lx + 12;   // label center (2 chars * 12px)
      if (lc >= n0 && lc <= n1) fill_rect(lx - 2, 182, lx + 26, 199, PAPER);
      put_text(lx, 184, lab, INK, 2);
    }
  }
}

static void draw_ui() {
  // icons: derived from current + 3h-step codes; clear sky shows sun by day,
  // moon by night (each slot is i*3h into "now")
  for (int i = 0; i < 4; i++) {
    int c = (i == 0) ? wx.code : wx.codes[i * 3];
    int x = IX0 + i * IGAP + 18;
    if (c == 0) { if (is_daytime(i * 3)) icon_sun(x, ICY, 11); else icon_moon(x, ICY, 11); }
    else if (c == 1 || c == 2) icon_part(x, ICY);
    else icon_cloud(x, ICY);
  }

  // top-left small city name removed — the name now lives under the big temp
  // (drawn after the big-temp block below)

  // left: big current temp (4x), centered in the left column
  char b[8];
  int cn = fmt_num(b, wx.cur, 1);
  {
    int tw = cn * 24;
    int nx = 14 + (210 - tw) / 2;
    if (nx < 14) nx = 14;
    put_text(nx, 110, b, INK, 4);
  }

  // left: city name, centered under the big temp (replaces the old "FEELS N"
  // line; one label, bottom position)
  {
    const char *nm = CITIES[cur_city].name;
    int nw = text_w(nm, 2);
    int nx = 14 + (210 - nw) / 2;
    if (nx < 14) nx = 14;
    put_text(nx, 196, nm, INK, 2);
  }

  // right: temp curve + labels + axis
  draw_temp_curve();

  // top-right timestamp removed — the panel has no RTC and it only ever
  // showed the top of the forecast hour ("11:00" at 11:45), so it was just
  // a wrong-looking clock

  // ---- bottom badges: top border only (no fill), all black ink on paper ----
  const int BW = 174, GAP = 12;
  int bx0[3];
  for (int i = 0; i < 3; i++) bx0[i] = 240 + i * (BW + GAP);
  // top border of each badge sits low under the hour axis; the badge content
  // floats well below the line (vertical breathing room under the border)
  for (int i = 0; i < 3; i++)
    for (int x = bx0[i]; x <= bx0[i] + BW; x++) { cv_set(x, CH_Y + 4, INK); cv_set(x, CH_Y + 5, INK); }
  const int ty = CH_Y + 23;   // text top: room under the border

  // badge 1: umbrella + DRY / RAIN
  {
    int ux = 267, uy = CH_Y + 25;   // umbrella apex
    for (int a = 0; a <= 180; a += 3) {
      float rad = a * 3.14159265f / 180.0f;
      int x = ux + (int)(-11 * cosf(rad));
      int y = uy - 1 - (int)(9 * sinf(rad));
      for (int t = 0; t < 2; t++) { cv_set(x + t, y, INK); cv_set(x, y + t, INK); }
    }
    for (int y = uy + 1; y <= uy + 11; y++) for (int t = 0; t < 2; t++) cv_set(ux + t, y, INK);
    if (wx.rain_total < 0.5f) {
      put_text(290, ty, "DRY 12H", INK, 2);
    } else {
      char rb[8];
      fmt_num(rb, wx.rain_total, 1);
      int x = put_text(286, ty, "RAIN ", INK, 2);
      put_text(x, ty, rb, INK, 2);
    }
  }

  // badge 2: wind arrows + wind value (shown in m/s: API km/h / 3.6)
  {
    int wy = CH_Y + 29;
    for (int i = 0; i < 2; i++) {
      int dy = (i == 0) ? -4 : 2;
      int ln = (i == 0) ? 26 : 18;
      for (int t = 0; t < 2; t++)
        for (int x = 448; x <= 448 + ln; x++) cv_set(x, wy + dy + t, INK);
      for (int t = 0; t < 4; t++) {
        cv_set(448 + ln - t, wy + dy + 4 + t, INK);
        cv_set(448 + ln - t + 1, wy + dy + 4 + t, INK);
      }
    }
    char wb[8];
    fmt_num(wb, wx.wind / 3.6f, 1);
    int x = put_text(490, ty, wb, INK, 2);
    put_text(x, ty, " M/S", INK, 2);
  }

  // badge 3: thermometer + lo -> hi range
  {
    int wy = CH_Y + 29;
    for (int y = wy - 11; y <= wy + 4; y++)
      for (int t = 0; t < 5; t++) cv_set(630 + t, y, INK);
    fill_circle(632, wy + 9, 6, INK);
    float lo3 = NAN, hi3 = NAN;
    for (int i = 0; i < 13; i++) {
      if (isnan(wx.temps[i])) continue;
      if (isnan(lo3) || wx.temps[i] < lo3) lo3 = wx.temps[i];
      if (isnan(hi3) || wx.temps[i] > hi3) hi3 = wx.temps[i];
    }
    char lb[8], hb[8];
    fmt_num(lb, lo3, 1);
    fmt_num(hb, hi3, 1);
    put_text(648, ty, lb, INK, 2);
    int ax = 648 + 4 * 12 + 4;
    for (int x = ax; x < ax + 12; x++) for (int t = 0; t < 2; t++) cv_set(x, wy - 1 + t, INK);
    for (int t = 0; t <= 4; t++) {
      cv_set(ax + 12 + 4 - t, wy - 5 + t, INK);
      cv_set(ax + 12 + 4 - t, wy - 5 + t + 1, INK);
    }
    put_text(ax + 22, ty, hb, INK, 2);
  }
}

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

static void do_capture() {
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

static void ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) { wifi_ok = true; return; }
  wifi_ok = wifi_up();
}

// Fetch live data for the current cit into the global wx (full refresh on fail).
static bool refresh_data() {
  ensure_wifi();
  if (!wifi_ok) return false;
  bool ok = fetch_weather(&CITIES[cur_city]);
  if (ok) { fetch_sunset(&CITIES[cur_city]); fetch_time(); }
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
static void render_and_show(bool ok) {
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

// Switch to the next city, persist, re-fetch, redraw. Shared by the side
// rotary switch and the serial 'CITY' command (used for headless verify).
static void toggle_city(const char *src) {
  cur_city = (cur_city + 1) % N_CITIES;
  Preferences prefs; prefs.begin("wx", false); prefs.putInt("city", cur_city); prefs.end();
  Serial.printf("%s: toggle -> %s (re-fetching)\n", src, CITIES[cur_city].name);
  bool ok = refresh_data();
  render_and_show(ok);
}

// Handle a serial line (CAL/CAP/CITY still work while the app stays awake).
// Non-blocking: accumulates bytes until a newline, so a bare command without
// a trailing newline can't wedge the loop.
static void poll_serial() {
  static char buf[96];
  static int blen = 0;
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
        } else if (strstr(buf, "CITY")) {
          toggle_city("ser");   // headless toggle test (mirrors the side switch)
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
  Serial.println("== desk-buddy 5.79 e-paper: weather (persistent) ==");

  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, HIGH);
  delay(100);
  Serial.println("panel power: on");

  pinMode(BTN_PRESS, INPUT_PULLUP);
  pinMode(BTN_UP,    INPUT_PULLUP);
  pinMode(BTN_DOWN,  INPUT_PULLUP);

  Preferences prefs;
  prefs.begin("wx", false);
  int saved = prefs.getInt("city", 0);
  if (saved < 0 || saved >= N_CITIES) saved = 0;
  cur_city = saved;
  Serial.printf("city: %s (index %d)\n", CITIES[cur_city].name, cur_city);

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

  // button debounce: simple rising-edge detect on the (active-low) rotary pins
  static bool lastPress = false, lastUp = false, lastDown = false;
  bool p = !digitalRead(BTN_PRESS);
  bool u = !digitalRead(BTN_UP);
  bool d = !digitalRead(BTN_DOWN);
  if (p && !lastPress) { toggle_city("btn"); }              // rotary pressed: toggle city
  if ((u && !lastUp) || (d && !lastDown)) {                 // detent edge: immediate refresh
    Serial.println("btn: detent -> refresh");
    bool ok = refresh_data();
    render_and_show(ok);
  }
  lastPress = p; lastUp = u; lastDown = d;

  if (!wifi_ok) ensure_wifi();             // opportunistic reconnect
  if (millis() - lastTick >= REFRESH_MS) {  // periodic 10-min refresh
    Serial.println("tick: 10-min refresh");
    bool ok = refresh_data();
    render_and_show(ok);
    lastTick = millis();
  }
  delay(40);
}
