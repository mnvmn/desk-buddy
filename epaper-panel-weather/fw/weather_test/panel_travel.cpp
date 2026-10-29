// ---- TRAVEL: imhd.sk feed + rendering (the travel half of the unified panel) --
// draw_travel() renders a full-width HEADER (stop + day + time) over THREE
// equal LINE columns — a verbatim port of web/panel_v2/travel_ui.js.
//
// Data: imhd.sk itself (no LAN daemon). fetch_travel() does the Socket.IO v4
// long-poll handshake over HTTPS (open -> connect -> tabStart -> read the
// 'tabs' snapshot), then travel_parse_tabs() filters the 3 target lines
// (32/49/44) by line + destination keyword + window and converts the ms epochs
// to local HH:MM. imhd gives LIVE departures only (the GTFS schedule is 48 MB,
// too big on-device), so the panel shows the live window.
//
// The draw path is pure canvas (no network): it compiles on the host
// pixel-harness (tools/host_render_travel.cpp) via #ifndef HOST_BUILD around
// the fetch + parse-network parts only.
#include "panel_travel.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifndef HOST_BUILD
#include "panel_net.h"
#endif

// localtime is platform-split: POSIX (the ESP32) has localtime_r, Win32/MinGW
// (the host parse harness) has localtime_s. travel_parse_tabs is host-testable,
// so route through a portable wrapper.
static void local_time(const time_t *t, struct tm *out) {
#ifdef _WIN32
  localtime_s(out, t);
#else
  localtime_r(t, out);
#endif
}

// ---- icons (web travel_icons.js port — procedural, centred) ------------------
// The web draws these with procedural pixel math (fillRect/setPx/fillCircle/
// thickLine == fill_rect/cv_set/fill_circle/thick_line). busIcon is centred at
// (cx,cy); body + win are two colours (default ink-on-paper).
static void tvBusIcon(int cx, int cy, int body, int win) {
  int x0 = cx - 12, x1 = cx + 12, y0 = cy - 7, y1 = cy + 6;
  fill_rect(x0, y0, x1, y1, body);
  cv_set(x0, y0, win); cv_set(x1, y0, win); cv_set(x0, y1, win); cv_set(x1, y1, win);
  cv_set(x0 + 1, y0, win); cv_set(x1 - 1, y0, win);
  fill_rect(x0 + 2, y0 + 2, x1 - 2, y0 + 5, win);
  for (int yy = y0 + 2; yy <= y0 + 5; yy++) { cv_set(cx - 4, yy, body); cv_set(cx + 3, yy, body); }
  fill_rect(x0, y1, x1, y1 + 1, win);
  fill_circle(cx - 7, y1 + 3, 3, body); fill_circle(cx + 7, y1 + 3, 3, body);
  fill_circle(cx - 7, y1 + 3, 1, win);  fill_circle(cx + 7, y1 + 3, 1, win);
}
static void tvLiveTick(int x, int y) { fill_rect(x, y, x + 2, y + 2, INK); }
static void tvSchedTick(int x, int y) {
  for (int yy = y; yy <= y + 2; yy++)
    for (int xx = x; xx <= x + 2; xx++)
      if (xx == x || xx == x + 2 || yy == y || yy == y + 2) cv_set(xx, yy, INK);
}
static void tvLateTick(int x, int y) {
  for (int yy = 0; yy < 4; yy++)
    for (int xx = 0; xx <= yy; xx++) cv_set(x + yy, y + 3 - yy, INK);
}
static void tvClockIcon(int cx, int cy, int r) {
  for (int y = cy - r; y <= cy + r; y++)
    for (int x = cx - r; x <= cx + r; x++) {
      int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
      if (d2 >= (r - 1) * (r - 1) && d2 <= (r + 1) * (r + 1)) cv_set(x, y, INK);
    }
  thick_line(cx, cy, cx, cy - r + 2, INK, 1);
  thick_line(cx, cy, cx + r - 2, cy, INK, 1);
  cv_set(cx, cy, INK);
}

// ---- header + columns (web tvDrawHeader / tvDrawLineCol / tvDrawRow) --------
static const char *tvDOW3[7] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
static int tvDow(const char *dstr) {   // "YYYY-MM-DD" -> 0=Sun..6=Sat (Sakamoto)
  int y = (dstr[0]-'0')*1000 + (dstr[1]-'0')*100 + (dstr[2]-'0')*10 + (dstr[3]-'0');
  int m = (dstr[5]-'0')*10 + (dstr[6]-'0');
  int d = (dstr[8]-'0')*10 + (dstr[9]-'0');
  static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  if (m < 3) y--;
  return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}
static int tvSecAhead(const TravelDep *dep, int nowSec) {
  int h = (dep->t[0]-'0')*10 + (dep->t[1]-'0');
  int m = (dep->t[3]-'0')*10 + (dep->t[4]-'0');
  return h*3600 + m*60 + dep->d - nowSec;
}
static void tvDrawRow(int x0, int x1, int y, const TravelDep *dep, int nowSec) {
  put_text(x0+2, y+2, dep->t, INK, 3);
  int secs = tvSecAhead(dep, nowSec);
  int mins = (secs + 30) / 60;   // Math.round(secs/60); clamped to >=0 below
  if (mins < 0) mins = 0;
  char inT[16]; snprintf(inT, sizeof inT, "IN %dM", mins);
  int inX = x1 - 2 - (int)strlen(inT) * 6;   // right-align "IN XM" in the column
  put_text(inX, y+5, inT, INK, 1);
  int tx = inX - 9;
  if (dep->d >= 45) tvLateTick(tx, y+4);
  else if (dep->live) tvLiveTick(tx, y+3);
  else tvSchedTick(tx, y+3);
}
static void tvDrawLineCol(int ci, const TravelLine *L, int nowSec) {
  const int x0 = (ci==0) ? 8 : (ci==1) ? 274 : 540;
  const int x1 = (ci==0) ? 258 : (ci==1) ? 524 : 790;
  put_text(x0, 50, L->line, INK, 4);
  put_text(x0+56, 58, L->dest, INK, 2);
  char pl[10]; snprintf(pl, sizeof pl, "N %s", L->plat);
  put_text(x1 - (int)strlen(pl) * 6, 58, pl, INK, 1);
  thick_line(x0, 88, x1, 88, INK, 1);
  for (int i = 0; i < L->ndep; i++) tvDrawRow(x0, x1, 102 + i*38, &L->dep[i], nowSec);
}
static void tvDrawHeader(const travel_t *tv) {
  tvBusIcon(24, 20, INK, PAPER);
  put_text(46, 13, tv->stop, INK, 2);
  char dl[12];
  int dom = (tv->day[8]-'0')*10 + (tv->day[9]-'0');
  snprintf(dl, sizeof dl, "%s %02d", tvDOW3[tvDow(tv->day)], dom);
  char rt[24]; snprintf(rt, sizeof rt, "%s %s", dl, tv->now);
  put_text(774 - (int)strlen(rt) * 12, 13, rt, INK, 2);
  tvClockIcon(784, 20, 6);
  thick_line(8, 38, 792, 38, INK, 1);
}
void draw_travel(const travel_t *tv) {
  // fill_rect is INCLUSIVE on both edges (x0..x1, y0..y1) and the framebuffer
  // is 800x272 (valid x 0..799, y 0..271), so the full clear is (0,0,799,271).
  // (The web's fillRect(0,0,800,272) is x,y,w,h; porting it verbatim wrote the
  // OOB column x=800 / row y=272 -> memory corruption on the real, unclamped
  // Paint_SetPixel. The host stub clamps OOB, which is why the harness missed it.)
  fill_rect(0, 0, EPD_W - 1, EPD_H - 1, PAPER);
  int nowSec = ((tv->now[0]-'0')*10 + (tv->now[1]-'0'))*3600 + ((tv->now[3]-'0')*10 + (tv->now[4]-'0'))*60;
  tvDrawHeader(tv);
  for (int i = 0; i < tv->nlines && i < TRAVEL_MAX_LINES; i++) tvDrawLineCol(i, &tv->lines[i], nowSec);
  thick_line(266, 46, 266, 258, INK, 1);
  thick_line(532, 46, 532, 258, INK, 1);
}

// ---- mock fallback (baked 2026-10-18 15:35 — web travel_data.js MOCK) -------
void travel_now_mock(travel_t *tv) {
  memset(tv, 0, sizeof *tv);
  strcpy(tv->stop, "KARPATSKA");
  strcpy(tv->platform, "313");
  strcpy(tv->now, "15:35");
  strcpy(tv->day, "2026-10-18");
  tv->nlines = 3;
  tv->ok = true;

  // 32 -> Dlhé diely (nástupiste 313)
  tv->lines[0].line[0] = '3'; tv->lines[0].line[1] = '2'; tv->lines[0].line[2] = 0;
  strcpy(tv->lines[0].dest, "DLHE DIELY");
  strcpy(tv->lines[0].plat, "313");
  tv->lines[0].ndep = 4;
  const char *t32[4] = {"15:40", "15:55", "16:10", "16:25"};
  int d32[4]  = {0, 0, 0, 0};
  bool lv32[4] = {true, true, false, false};
  for (int k = 0; k < 4; k++) {
    strcpy(tv->lines[0].dep[k].t, t32[k]);
    tv->lines[0].dep[k].d = d32[k]; tv->lines[0].dep[k].live = lv32[k];
  }
  // 49 -> Kramáre, NÚSCH (nástupiste 313)
  tv->lines[1].line[0] = '4'; tv->lines[1].line[1] = '9'; tv->lines[1].line[2] = 0;
  strcpy(tv->lines[1].dest, "KRAMARE NUSCH");
  strcpy(tv->lines[1].plat, "313");
  tv->lines[1].ndep = 4;
  const char *t49[4] = {"15:48", "16:03", "16:18", "16:33"};
  int d49[4]  = {0, 0, 0, 0};
  bool lv49[4] = {true, true, true, false};
  for (int k = 0; k < 4; k++) {
    strcpy(tv->lines[1].dep[k].t, t49[k]);
    tv->lines[1].dep[k].d = d49[k]; tv->lines[1].dep[k].live = lv49[k];
  }
  // 44 -> Koliba (nástupiste 315)
  tv->lines[2].line[0] = '4'; tv->lines[2].line[1] = '4'; tv->lines[2].line[2] = 0;
  strcpy(tv->lines[2].dest, "KOLIBA");
  strcpy(tv->lines[2].plat, "315");
  tv->lines[2].ndep = 4;
  const char *t44[4] = {"15:39", "15:49", "16:04", "16:19"};
  int d44[4]  = {6, 1, 0, 0};
  bool lv44[4] = {true, true, false, false};
  for (int k = 0; k < 4; k++) {
    strcpy(tv->lines[2].dep[k].t, t44[k]);
    tv->lines[2].dep[k].d = d44[k]; tv->lines[2].dep[k].live = lv44[k];
  }
}

// ---- pure imhd 'tabs' parse (no network, host-testable) ---------------------
// Target: line number, diacritic-free destination keyword (strstr-safe), and the
// display destination + nástupiste. The keyword is the ASCII core of the UTF-8
// cielStr (e.g. "diely" for "Dlhé diely").
struct tv_target { const char *line; const char *kw; const char *dest; const char *plat; };
static const struct tv_target TVT[TRAVEL_MAX_LINES] = {
  { "32", "diely",  "DLHE DIELY",    "313" },
  { "49", "Kram",   "KRAMARE NUSCH", "313" },
  { "44", "Koliba", "KOLIBA",        "315" },
};

// Match the balanced {..} or [..] starting at s (string-aware). Returns the
// closing char ptr, or NULL.
static const char *match_pair(const char *s, char open, char close) {
  if (*s != open) return NULL;
  int depth = 0; bool in = false;
  for (const char *p = s; *p; p++) {
    if (in) { if (*p == '\\') p++; else if (*p == '"') in = false; continue; }
    if (*p == '"') in = true;
    else if (*p == open) depth++;
    else if (*p == close) { depth--; if (depth == 0) return p; }
  }
  return NULL;
}
static const char *match_brace(const char *s)   { return match_pair(s, '{', '}'); }
static const char *match_bracket(const char *s) { return match_pair(s, '[', ']'); }

// A long long (64-bit) value for "key" inside [from,to). 0 if absent.
// (ms epochs exceed int32, hence long long.)
static long long rng_ll(const char *from, const char *to, const char *key) {
  char pat[24]; int plen = snprintf(pat, sizeof pat, "\"%s\"", key);
  for (const char *p = from; p && (const char *)(p + plen) < to; p++) {
    if (strncmp(p, pat, (size_t)plen) != 0) continue;
    const char *q = strchr(p + plen, ':');
    if (!q || q >= to) return 0;
    return atoll(q + 1);
  }
  return 0;
}
// A string value for "key" inside [from,to) into out (null-terminated at len-1).
static int rng_str(const char *from, const char *to, const char *key, char *out, int len) {
  char pat[24]; int plen = snprintf(pat, sizeof pat, "\"%s\"", key);
  for (const char *p = from; p && (const char *)(p + plen) < to; p++) {
    if (strncmp(p, pat, (size_t)plen) != 0) continue;
    const char *q = strchr(p + plen, ':');
    if (!q || q >= to) return 0;
    const char *s = strchr(q, '"');
    if (!s || s >= to) return 0;
    s++;
    int i = 0;
    while (s < to && *s != '"' && i < len - 1) out[i++] = *s++;
    out[i] = 0;
    return i;
  }
  return 0;
}

bool travel_parse_tabs(const char *tabs, long now_epoch, travel_t *tv) {
  memset(tv, 0, sizeof *tv);
  strcpy(tv->stop, "KARPATSKA");
  time_t ntt = (time_t)now_epoch;
  struct tm lt; local_time(&ntt, &lt);
  snprintf(tv->now, sizeof tv->now, "%02d:%02d", lt.tm_hour, lt.tm_min);
  snprintf(tv->day, sizeof tv->day, "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
  for (int i = 0; i < TRAVEL_MAX_LINES; i++) {
    strcpy(tv->lines[i].line, TVT[i].line);
    strcpy(tv->lines[i].dest, TVT[i].dest);
    strcpy(tv->lines[i].plat, TVT[i].plat);
  }
  tv->nlines = TRAVEL_MAX_LINES;

  struct Cand { int li; long long ms; int d; bool live; };
  static Cand cands[TRAVEL_MAX_LINES * 24];
  int nc = 0;
  const long now_s = now_epoch;
  const long horizon = 60 * 60;   // 60-min live window

  // Scan every "tab" array (each platform table has one) in the snapshot.
  const char *p = tabs;
  while (p) {
    const char *tabkey = strstr(p, "\"tab\"");
    if (!tabkey) break;
    const char *db = strchr(tabkey, '[');
    if (!db) break;
    const char *dbEnd = match_bracket(db);
    if (!dbEnd) break;
    const char *q = db + 1;
    while (q < dbEnd) {
      while (q < dbEnd && (*q == ' ' || *q == '\n' || *q == '\r' || *q == ',' || *q == ':')) q++;
      if (q >= dbEnd || *q != '{') break;
      const char *objEnd = match_brace(q);
      if (!objEnd || objEnd > dbEnd) break;
      char linka[8], typ[8], ciel[48], kone[48], dest[100];
      rng_str(q, objEnd, "linka", linka, sizeof linka);
      rng_str(q, objEnd, "typ", typ, sizeof typ);
      rng_str(q, objEnd, "cielStr", ciel, sizeof ciel);
      rng_str(q, objEnd, "konecnaZstr", kone, sizeof kone);
      long long cas = rng_ll(q, objEnd, "cas");
      long long casCP = rng_ll(q, objEnd, "casCP");
      snprintf(dest, sizeof dest, "%s %s", ciel, kone);
      for (int li = 0; li < TRAVEL_MAX_LINES; li++) {
        if (strcmp(linka, TVT[li].line) != 0) continue;
        if (!strstr(dest, TVT[li].kw)) continue;
        long long pred = cas ? cas : casCP;              // live time for the window
        long delta_s = (long)(pred / 1000) - now_s;
        if (pred <= 0 || delta_s < 0 || delta_s > horizon) continue;
        if (nc < (int)(sizeof cands / sizeof cands[0])) {
          cands[nc].li = li;
          cands[nc].ms = casCP ? casCP : pred;           // t (display) = scheduled
          cands[nc].d = (cas && casCP) ? (int)((cas - casCP) / 1000) : 0;
          cands[nc].live = (strcmp(typ, "online") == 0);
          nc++;
        }
      }
      q = objEnd + 1;
    }
    p = dbEnd + 1;
  }

  // Per line: dedupe by minute (keep live), sort by time, take top 4.
  for (int li = 0; li < TRAVEL_MAX_LINES; li++) {
    struct { long long ms; int d; bool live; } uniq[24];
    int nu = 0;
    for (int i = 0; i < nc; i++) {
      if (cands[i].li != li) continue;
      bool dup = false;
      for (int j = 0; j < nu; j++) {
        if (uniq[j].ms / 60000 == cands[i].ms / 60000) {
          if (cands[i].live && !uniq[j].live) { uniq[j].live = true; uniq[j].d = cands[i].d; }
          dup = true; break;
        }
      }
      if (!dup && nu < (int)(sizeof uniq / sizeof uniq[0])) {
        uniq[nu].ms = cands[i].ms; uniq[nu].d = cands[i].d; uniq[nu].live = cands[i].live;
        nu++;
      }
    }
    for (int a = 1; a < nu; a++) {
      auto cur = uniq[a]; int b = a - 1;
      while (b >= 0 && uniq[b].ms > cur.ms) { uniq[b+1] = uniq[b]; b--; }
      uniq[b+1] = cur;
    }
    TravelLine *L = &tv->lines[li];
    L->ndep = 0;
    for (int a = 0; a < nu && a < 4; a++) {
      time_t dtt = (time_t)(uniq[a].ms / 1000);
      struct tm d; local_time(&dtt, &d);
      snprintf(L->dep[a].t, sizeof L->dep[a].t, "%02d:%02d", d.tm_hour, d.tm_min);
      L->dep[a].d = uniq[a].d;
      L->dep[a].live = uniq[a].live;
      L->ndep++;
    }
  }
  tv->ok = tv->now[0] != 0;
  return tv->ok;
}

// ---- live fetch: imhd.sk Socket.IO v4 long-poll over HTTPS ------------------
#ifndef HOST_BUILD
bool fetch_travel(travel_t *tv) {
  // 1) wait for NTP (a plausible epoch) so the HH:MM conversion is correct
  uint32_t wt0 = millis();
  while (time(NULL) < 1600000000L && millis() - wt0 < 8000) delay(50);
  long now_epoch = (long)time(NULL);
  if (now_epoch < 1600000000L) { Serial.println("travel: NTP not synced"); return false; }

  static char buf[24576];
  static char tabs[12288];
  char path[160];
  char pong[64];   // throwaway for PONG acks (never clobbers buf mid-iteration)

  // 2) open: GET -> 0{"sid":...}
  snprintf(path, sizeof path, "/rt/sio2/?EIO=4&transport=polling");
  int ret = imhd_request(path, nullptr, buf, sizeof buf);
  if (ret < 0) { Serial.println("travel: imhd open FAIL"); return false; }
  char sid[64];
  const char *s = strstr(buf, "\"sid\":\"");
  if (!s) { Serial.println("travel: no sid"); return false; }
  s += 7; int sl = 0;
  while (s[sl] && s[sl] != '"' && sl < (int)sizeof sid - 1) { sid[sl] = s[sl]; sl++; }
  sid[sl] = 0;
  snprintf(path, sizeof path, "/rt/sio2/?EIO=4&transport=polling&sid=%s", sid);
  Serial.printf("travel: sid=%s\n", sid);

  // 3) connect the namespace. The default-namespace connect ("40") has no ack
  //    callback, so the server sends NO "40" back on a bare poll — it only flushes
  //    packets (the connect-confirm among them) once tabStart is queued. So just
  //    POST it; do NOT poll for an ack (that would long-poll-hang with nothing queued).
  imhd_request(path, "40", buf, sizeof buf);

  // 4) subscribe: POST tabStart for the stop. This is what makes the next poll
  //    return the connect-confirm + the 'tabs' snapshot in a single flush.
  char sub[64]; snprintf(sub, sizeof sub, "42[\"tabStart\",[%d,[\"*\"]]]", IMHD_STOP_ID);
  imhd_request(path, sub, buf, sizeof buf);

  // 5) read the 'tabs' snapshot (one GET has all the platforms); answer PINGs
  int tl = 0; uint32_t t1 = millis(); bool got = false;
  while (!got && millis() - t1 < 15000) {
    if (imhd_request(path, nullptr, buf, sizeof buf) >= 0) {
      const char *m = buf;
      while (m && *m) {
        const char *sep = strchr(m, 0x1e);
        int mlen = sep ? (int)(sep - m) : (int)strlen(m);
        if (m[0] == '2') {
          imhd_request(path, "3", pong, sizeof pong);
        } else if (!strncmp(m, "42[\"tabs\"", 9) && mlen > 0) {
          if (tl + mlen < (int)sizeof tabs - 1) { memcpy(tabs + tl, m, mlen); tl += mlen; }
          got = true;
        }
        if (!sep || sep[1] == 0) break;
        m = sep + 1;
      }
    }
    if (got) break;
    delay(50);
  }
  if (!got) { Serial.println("travel: no tabs"); return false; }
  tabs[tl] = 0;
  Serial.printf("travel: tabs %d bytes\n", tl);

  bool ok = travel_parse_tabs(tabs, now_epoch, tv);
  int nd = 0; for (int i = 0; i < tv->nlines; i++) nd += tv->lines[i].ndep;
  Serial.printf("travel: fetch %s (%d deps)\n", ok ? "ok" : "FAIL", nd);
  return ok;
}
#endif
