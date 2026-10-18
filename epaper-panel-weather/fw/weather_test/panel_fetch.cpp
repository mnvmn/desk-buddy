// Open-Meteo fetch + JSON mini-parser + the pure date/weather helpers the
// UI and the refresh loop build on.
#include "panel_fetch.h"
#ifndef HOST_BUILD
#include "panel_net.h"
#endif

// Refresh interval from the last fetched local time: every 1h during quiet
// hours (21:00–05:00), otherwise every 30 min. Falls back to the daytime
// interval when the time is unknown (before first fetch).
uint32_t current_refresh_ms() {
  int h = wx.now_hour;
  return (h >= 21 || h < 5) ? REFRESH_MS_NIGHT : REFRESH_MS;
}


// Day/night from the local time-of-day window [sunrise, sunset]. The 13h
// forecast starts at "now" and runs forward, so its leading hours can cross
// into the next night — each slot is checked against the same window. With no
// sunrise/sunset data we fall back to a coarse hour heuristic.
bool is_daytime(int h) {
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


// x-extent of one hour slot for the night band: the column between the
// midpoints of its neighbouring points, extended 3px at the plot edges.
// Adjacent dark slots share a boundary and the global (x+y)%2 dither, so
// they merge into one seamless band.
void slot_range(int i, const int *px, int *x0, int *x1) {
  *x0 = (i == 0) ? px[0] - 3 : (px[i] + px[i - 1]) / 2;
  *x1 = (i == 12) ? px[12] + 3 : (px[i] + px[i + 1]) / 2;
}


// format a float as "N.N" / "NN.N" (max 5 chars + NUL); returns char count
int fmt_num(char *buf, float v, int dec) {
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


// ---- date + daylight helpers (v2) -------------------------------------------
// Day of week: Tomohiko Sakamoto's algorithm. 0=Sunday .. 6=Saturday.
int dow_sakamoto(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// "YYYY-MM-DD" -> 0..6 (Sun..Sat)
int date_dow(const char *dstr) {
  int y = (dstr[0]-'0')*1000 + (dstr[1]-'0')*100 + (dstr[2]-'0')*10 + (dstr[3]-'0');
  int m = (dstr[5]-'0')*10 + (dstr[6]-'0');
  int d = (dstr[8]-'0')*10 + (dstr[9]-'0');
  return dow_sakamoto(y, m, d);
}

const char *DOW3[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };


// min/max of hourly temps over daylight hours (sr/ss in fractional local hours;
// <0 = unknown -> 5-19h). Fallback: all hours, then 0. Used by the 2-day strip.
static void daylight_range(const float *temps, int n, float srH, float ssH, float *lo, float *hi) {
  *lo = NAN; *hi = NAN;
  for (int h = 0; h < n; h++) {
    bool isD = (srH < 0 || ssH < 0) ? (h >= 5 && h < 19)
                                    : (h >= (int)floorf(srH) && h < (int)ceilf(ssH));
    if (!isD) continue;
    float t = temps[h];
    if (isnan(t)) continue;
    if (isnan(*lo) || t < *lo) *lo = t;
    if (isnan(*hi) || t > *hi) *hi = t;
  }
  if (isnan(*lo)) {
    for (int h = 0; h < n; h++) {
      float t = temps[h];
      if (isnan(t)) continue;
      if (isnan(*lo) || t < *lo) *lo = t;
      if (isnan(*hi) || t > *hi) *hi = t;
    }
  }
  if (isnan(*lo)) { *lo = 0; *hi = 0; }
}

// ---- v2.1 block aggregation (1:1 web icons.js blockCode + ui.js leftPart) ------
// Dominant WMO code over an n-hour window. Precipitation ALWAYS wins (it is the
// salient signal): any storm > any snow > any rain > most-frequent dry code, with
// dry ties to the more overcast (3/45/48 > 1/2 > 0). `codes` are integer WMO codes
// (never null on the device; missing hours were zero-filled = clear at parse time).
int block_code(const int *codes, int start, int n) {
  int win[24]; int wn = 0;
  for (int i = 0; i < n && i < 24; i++) {
    int c = codes[start + i];
    if (c >= 0) win[wn++] = c;
  }
  int storm[3] = {95, 96, 99};
  int snow[6]  = {71, 73, 75, 77, 85, 86};
  int rain[13] = {51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82};
  int has = 0;
  for (int i = 0; i < wn; i++)
    for (int k = 0; k < 3; k++) if (win[i] == storm[k]) has = 1;
  if (has) return 96;
  has = 0;
  for (int i = 0; i < wn; i++)
    for (int k = 0; k < 6; k++) if (win[i] == snow[k]) has = 1;
  if (has) return 75;
  has = 0;
  for (int i = 0; i < wn; i++)
    for (int k = 0; k < 13; k++) if (win[i] == rain[k]) has = 1;
  if (has) return 63;
  int cnt3 = 0, cnt45 = 0, cnt48 = 0, cnt1 = 0, cnt2 = 0, cnt0 = 0;
  for (int i = 0; i < wn; i++) {
    int c = win[i];
    if (c == 3) cnt3++; else if (c == 45) cnt45++; else if (c == 48) cnt48++;
    else if (c == 1) cnt1++; else if (c == 2) cnt2++; else if (c == 0) cnt0++;
  }
  int order[6] = {3, 45, 48, 1, 2, 0};
  int cntv[6]  = {cnt3, cnt45, cnt48, cnt1, cnt2, cnt0};
  int best = 0, bc = -1;
  for (int i = 0; i < 6; i++) if (cntv[i] > bc) { bc = cntv[i]; best = order[i]; }
  return bc > 0 ? best : 0;
}

// [a,b) = index range of part k (0..3) of the 4 EQUAL parts from now to midnight.
// R = whole hours left in the day (24-now_hour, clamped >=1). When R<4 the parts
// shrink to a minimum of 1 hour each and may overlap (JS leftPart, verbatim).
void left_part(int k, int R, int *a, int *b) {
  int aa = (k * R) / 4;
  int bb = ((k + 1) * R) / 4;
  if (bb <= aa) { aa = (k < R - 1) ? k : (R - 1); bb = aa + 1; }
  if (bb > R) bb = R;
  *a = aa; *b = bb;
}

// ---- JSON mini-parser (no library; the payload is a fixed shape) ----------
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

// Nth quoted string in the array following `key` (e.g. key=\"time\":[, n=1 -> 2nd date)
static int json_str_array(const char *j, const char *key, int n, char *out, int cap) {
  const char *k = strstr(j, key);
  if (!k) return -1;
  const char *a = strchr(k, '[');
  const char *end = a ? strchr(a, ']') : NULL;
  if (!a || !end) return -1;
  int cnt = -1;
  const char *c = a;
  while (c < end) {
    if (*c == '"') {
      const char *s = c + 1;
      const char *e = strchr(s, '"');
      if (!e || e > end) break;
      cnt++;
      if (cnt == n) {
        int len = (int)(e - s);
        if (len >= cap) len = cap - 1;
        memcpy(out, s, len); out[len] = 0;
        return 0;
      }
      c = e + 1;
    } else c++;
  }
  return -1;
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

#ifndef HOST_BUILD
int fetch_weather(const WxCity *cit) {
  static char body[4200];
  char path[512];
  snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%s&longitude=%s"
           "&current=temperature_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m"
           "&hourly=temperature_2m,weather_code,precipitation,wind_speed_10m&forecast_hours=24&timezone=auto"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max,sunrise,sunset&forecast_days=3",
           cit->lat, cit->lon);
  int bl = http_get(path, body, sizeof(body));
  if (bl < 0) return false;
  const char *cblk = strstr(body, "\"current\":{");
  wx.cur   = cblk ? num_after(cblk, "\"temperature_2m\"") : NAN;
  wx.feels = cblk ? num_after(cblk, "\"apparent_temperature\"") : NAN;
  wx.wind  = cblk ? num_after(cblk, "\"wind_speed_10m\"") : NAN;
  wx.code  = (int)(cblk ? num_after(cblk, "\"weather_code\"") : NAN);
  parse_num_array(body, "\"temperature_2m\":[", wx.temps, 13);
  // v2.1: fetch 24 hourly weather codes. Parse into a FLOAT temp array then cast
  // to int — parse_num_array writes float bit-patterns, so the old
  // (float*)wx.codes store left each code as e.g. 0x40400000 (=3.0f as an int)
  // and every icon fell through to the cloud branch. Integer codes are required
  // for the 4 left-column parts (block_code over codes24).
  {
    float cc[24];
    int cn = parse_num_array(body, "\"weather_code\":[", cc, 24);
    for (int i = 0; i < 24; i++) wx.codes24[i] = (i < cn) ? (int)cc[i] : 0;
    for (int i = 0; i < 13; i++) wx.codes[i] = wx.codes24[i];  // keeps temps/rainState/row in sync
  }
  {   // hourly wind (km/h -> m/s)
    float w[13];
    int wn = parse_num_array(body, "\"wind_speed_10m\":[", w, 13);
    for (int i = 0; i < 13; i++) wx.winds[i] = (i < wn) ? w[i] / 3.6f : NAN;
  }
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
        }
        // current date prefix "YYYY-MM-DD" (feeds day-of-week + moon phase)
        wx.now_year  = (q[0]-'0')*1000 + (q[1]-'0')*100 + (q[2]-'0')*10 + (q[3]-'0');
        wx.now_month = (q[5]-'0')*10 + (q[6]-'0');
        wx.now_day   = (q[8]-'0')*10 + (q[9]-'0');
        Serial.printf("wx: now=%d:%d date=%04d-%02d-%02d (from forecast)\n",
                      hh, mm, wx.now_year, wx.now_month, wx.now_day);
      }
    }
  }
  // 3-day daily: today's sunrise/sunset (replaces the old fetch_sunset request)
  // plus the two future dates + per-day sr/ss that the 2-day strip needs.
  const char *db = strstr(body, "\"daily\":{");
  if (db) {
    char ts[24];
    for (int di = 0; di < 3; di++) json_str_array(db, "\"time\":[", di, wx.day_date[di], 11);
    if (json_str_array(db, "\"sunrise\":[", 0, ts, sizeof ts) == 0) { int hh, mm; if (parse_hhmm(ts, &hh, &mm) == 0) { wx.sunrise_hour = hh; wx.sunrise_min = mm; } }
    if (json_str_array(db, "\"sunset\":[", 0, ts, sizeof ts) == 0)  { int hh, mm; if (parse_hhmm(ts, &hh, &mm) == 0) { wx.sunset_hour = hh; wx.sunset_min = mm; } }
    for (int di = 1; di < 3; di++) {
      memcpy(wx.days[di - 1].date, wx.day_date[di], 11);
      if (json_str_array(db, "\"sunrise\":[", di, ts, sizeof ts) == 0) { int hh, mm; if (parse_hhmm(ts, &hh, &mm) == 0) { wx.days[di - 1].sr_h = hh; wx.days[di - 1].sr_m = mm; } }
      if (json_str_array(db, "\"sunset\":[", di, ts, sizeof ts) == 0)  { int hh, mm; if (parse_hhmm(ts, &hh, &mm) == 0) { wx.days[di - 1].ss_h = hh; wx.days[di - 1].ss_m = mm; } }
    }
    Serial.printf("wx: daily %s / %s / %s\n", wx.day_date[0], wx.day_date[1], wx.day_date[2]);
    Serial.printf("wx: sunrise %d:%02d sunset %d:%02d\n",
                  wx.sunrise_hour, wx.sunrise_min, wx.sunset_hour, wx.sunset_min);
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


// Read the hourly "time" array -> per-entry local hour + "YYYY-MM-DD" date.
static int json_hour_array(const char *j, const char *key, int *hour, char (*date)[11], int max) {
  const char *k = strstr(j, key);
  if (!k) return -1;
  const char *a = strchr(k, '[');
  const char *end = a ? strchr(a, ']') : NULL;
  if (!a || !end) return -1;
  int n = 0;
  const char *c = a;
  while (c < end && n < max) {
    if (*c == '"') {
      const char *s = c + 1;
      const char *e = strchr(s, '"');
      if (!e || e > end) break;
      memcpy(date[n], s, 10); date[n][10] = 0;
      hour[n] = (s[10] == 'T') ? ((s[11] - '0') * 10 + (s[12] - '0')) : -1;
      n++;
      c = e + 1;
    } else c++;
  }
  return n;
}

// F3: 2-day hourly GET (start_date=day1 end_date=day2) -> wx.days[0..1].
// Fills per-hour codes/temps, sums rain, and computes the daylight lo/hi.
bool fetch_two_days(const WxCity *cit) {
  static char body[4200];
  static char dates[48][11];
  static int   hours[48];
  static float tempv[48];
  static float codetmp[48];   // float: parse_num_array writes float bit-patterns
  static float precv[48];
  if (wx.days[0].date[0] == 0 || wx.days[1].date[0] == 0) return false;
  char path[512];
  snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%s&longitude=%s"
           "&hourly=temperature_2m,weather_code,precipitation"
           "&start_date=%s&end_date=%s&timezone=auto",
           cit->lat, cit->lon, wx.days[0].date, wx.days[1].date);
  int bl = http_get(path, body, sizeof(body));
  if (bl < 0) return false;
  int nt = json_hour_array(body, "\"time\":[", hours, dates, 48);
  int nn1 = parse_num_array(body, "\"temperature_2m\":[", tempv, 48);
  int nn2 = parse_num_array(body, "\"weather_code\":[", codetmp, 48);
  int nn3 = parse_num_array(body, "\"precipitation\":[", precv, 48);
  int n = nt;
  if (nn1 < n) n = nn1; if (nn2 < n) n = nn2; if (nn3 < n) n = nn3;
  for (int di = 0; di < 2; di++) {
    for (int hh = 0; hh < 24; hh++) { wx.days[di].codes[hh] = 0; wx.days[di].temps[hh] = NAN; }
    wx.days[di].rain = 0;
    for (int i = 0; i < n; i++) {
      if (hours[i] < 0 || hours[i] > 23) continue;
      if (strcmp(dates[i], wx.days[di].date) != 0) continue;
      int hh = hours[i];
      wx.days[di].codes[hh] = (int)codetmp[i];   // integer WMO code (was float bit-pattern)
      wx.days[di].temps[hh] = tempv[i];
      wx.days[di].rain += precv[i];
    }
    float srH = (wx.days[di].sr_h >= 0) ? (wx.days[di].sr_h + wx.days[di].sr_m / 60.0f) : -1.0f;
    float ssH = (wx.days[di].ss_h >= 0) ? (wx.days[di].ss_h + wx.days[di].ss_m / 60.0f) : -1.0f;
    daylight_range(wx.days[di].temps, 24, srH, ssH, &wx.days[di].lo, &wx.days[di].hi);
    Serial.printf("wx: day%d %s rain=%.1f lo=%.1f hi=%.1f\n", di, wx.days[di].date,
                  wx.days[di].rain, wx.days[di].lo, wx.days[di].hi);
  }
  return true;
}
#endif


// (fetch_sunset removed — today's sunrise/sunset now come from the 3-day daily
//  inside fetch_weather, so it is no longer a separate network request.)

int fetch_time() {
  // clock is parsed from the forecast body (hourly.time[0]) in fetch_weather;
  // this is a no-op kept for interface clarity.
  return wx.now_hour >= 0;
}


// ---- moon phase (F10) --------------------------------------------------------
// Day number from a civil date (Howard Hinnant's days_from_civil).
static long days_from_civil(int y, int m, int d) {
  y -= (m <= 2);
  int era = (int)(((y >= 0) ? y : y - 399) / 400);
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (long)era * 146097L + (long)doe - 719468;
}
// Illuminated phase 0..1 (0=new, .5=full) from the current forecast date/time.
float moon_phase() {
  if (wx.now_year < 1970) return 0.5f;
  const float synodic = 29.53058867f;
  const float ref = (float)days_from_civil(2000, 1, 6) + 18.2333f / 24.0f;  // known new moon
  float cur = (float)days_from_civil(wx.now_year, wx.now_month, wx.now_day)
              + (wx.now_hour + wx.now_min / 60.0f) / 24.0f;
  float p = fmodf(cur - ref, synodic) / synodic;
  if (p < 0) p += 1.0f;
  return p;
}


// ---- rain-state classification (F12) ----------------------------------------
// A serial STATE override (F19) wins; otherwise severity priority:
// storm 95/96/99  >  snow 71/73/75/77/85/86  >  rain_total>=0.5  >  dry.
const char *state_ov = nullptr;

static bool codes_has(const int *a, int n) {
  for (int i = 0; i < n; i++)
    for (int k = 0; k < 13; k++)
      if (wx.codes[k] == a[i]) return true;
  return false;
}
const char *rain_state() {
  if (state_ov) return state_ov;
  static const int storm[3] = {95, 96, 99};
  static const int snow[6] = {71, 73, 75, 77, 85, 86};
  if (codes_has(storm, 3)) return "STORM";
  if (codes_has(snow, 6)) return "SNOW";
  if (wx.rain_total >= 0.5f) return "RAIN";
  return "DRY";
}

