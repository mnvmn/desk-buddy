// Panel rendering (curve, 2-day strip, full UI) - coords mirror the web.
#include "panel_ui.h"

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

  // night band: one dithered column per dark slot (is_daytime), extending
  // down behind the hour-axis row (the axis labels inside it get white
  // knock-outs). Autumn days are shorter than the 12h window, so a window
  // can hold the tail of one night AND the head of the next — per-slot
  // columns handle any mix (prefix, suffix, or both).
  for (int i = 0; i < 13; i++) {
    if (is_daytime(i)) continue;
    int n0, n1;
    slot_range(i, px, &n0, &n1);
    for (int y = CY_T - 12; y <= CY_B + 24; y++)
      for (int x = n0; x <= n1; x++)
        if ((x + y) % 2 == 0) cv_set(x, y, INK);
  }

  // (sunset label removed — the plot now stretches up into the top strip it used)

  // curve
  for (int i = 0; i < 12; i++)
    thick_line(px[i], py[i], px[i + 1], py[i + 1], INK, 3);

  // markers: filled dot at "now" (hour 0, nudged to the column edge), plus
  // filled dots at hour 9 and 12
  fill_circle(px[0] + 4, py[0], 4, INK);
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
    const int IDX[5] = {0, 3, 6, 9, 12};
    for (int k = 0; k < 5; k++) {
      int idx = IDX[k];
      int h = (wx.now_hour + idx) % 24;
      lab[0] = '0' + h / 10; lab[1] = '0' + h % 10; lab[2] = 0;
      int lx = px[idx] - 12;
      // clamp into the curve area (web parity: ui.js drawTempCurve)
      if (lx < CX0) lx = CX0;
      if (lx + 24 > CX1) lx = CX1 - 24;
      // the label center px[idx] sits inside slot idx's column, so it lands
      // on the band exactly when that slot is dark
      if (!is_daytime(idx)) fill_rect(lx - 10, 170, lx + 34, 194, PAPER);
      put_text(lx, 176, lab, INK, 2);
    }
  }

  // Day/night border countdowns ("how long till..."): at the day->night
  // border the time till the END of day (sunset), at the night->day border
  // the time till the START of day (sunrise). Shown only when the border
  // falls inside the 12h window and the sun data is known; drawn last so a
  // value plate crowding the top strip never eats them.
  if (wx.now_hour >= 0) {
    int trans[2], dt[2], night[2], nb = 0;
    if (wx.sunset_hour >= 0) {
      for (int i = 0; i < 12; i++)
        if (is_daytime(i) && !is_daytime(i + 1)) {
          trans[nb] = i;
          dt[nb] = (wx.sunset_hour * 60 + wx.sunset_min - wx.now_hour * 60 - wx.now_min + 1440) % 1440;
          night[nb] = true;
          nb++; break;
        }
    }
    if (wx.sunrise_hour >= 0) {
      for (int i = 0; i < 12; i++)
        if (!is_daytime(i) && is_daytime(i + 1)) {
          trans[nb] = i;
          dt[nb] = (wx.sunrise_hour * 60 + wx.sunrise_min - wx.now_hour * 60 - wx.now_min + 1440) % 1440;
          night[nb] = false;
          nb++; break;
        }
    }
    for (int b = 0; b < nb; b++) {
      int i = trans[b], d = dt[b];
      int r = ((d + 15) / 30) * 30;   // nearest half hour
      char s[6];
      if (r % 60 == 0) snprintf(s, sizeof s, "%dh", r / 60);
      else snprintf(s, sizeof s, "%d.5h", r / 60);
      int bx = (px[i] + px[i + 1]) / 2;   // dither border x
      int bw = strlen(s) * 12;            // scale-2 advance
      int lx = bx - bw / 2;
      if (lx < CX0 - 6) lx = CX0 - 6;
      if (lx + bw > CX1 + 6) lx = CX1 + 6 - bw;
      fill_rect(lx - 10, 0, lx + bw + 10, 20, PAPER);
      put_text(lx, 3, s, INK, 2);
      if (night[b]) {   // F16: moon-phase glyph after the "till night" countdown
        int mx = lx + bw + 15;
        if (mx + 7 > CX1 + 2) mx = CX1 - 5;
        fill_rect(mx - 9, 0, mx + 9, 20, PAPER);
        draw_moon_phase(mx, 10, 7, moon_phase());
      }
    }
  }
}

// ---- 2-day rain strip (F15) — bottom, x 240-774 (old badge slot) -----------
static void draw_two_day_strip() {
  const int SX = 240, SW = 534, SY = 212;   // flush with the curve's ink
  const int divX = SX + (SW >> 1);          // = 507, center of the strip
  const int GAP = 16;
  for (int d = 0; d < 2; d++) {
    const WXDay *day = &wx.days[d];
    if (day->date[0] == 0) continue;
    int bx = (d == 0) ? SX : divX + GAP;
    int rightEdge = (d == 0) ? divX - GAP : SX + SW;
    // row 1: DOW name (sc2) + 4 per-6h medium icons (block_code), right-aligned
    put_text(bx, SY + 14, DOW3[date_dow(day->date)], INK, 2);
    const int cY = SY + 20, PITCH = 52, iconStart = rightEdge - 14 - 3 * PITCH;
    for (int k = 0; k < 4; k++) {
      strip_icon(iconStart + k * PITCH, cY, block_code(day->codes, k * 6, 6));
    }
    // row 2: rain amount (left) + daylight lo->hi °C (right-aligned)
    char buf[12];
    const char *rt;
    if (day->rain < 0.5f) rt = "DRY";
    else { int p = fmt_num(buf, day->rain, 1); buf[p] = 'M'; buf[p + 1] = 'M'; buf[p + 2] = 0; rt = buf; }
    put_text(bx, SY + 42, rt, INK, 1);
    char rng[16];
    int rp = fmt_num(rng, day->lo, 1);
    rng[rp++] = '-';
    rp += fmt_num(rng + rp, day->hi, 1);
    rng[rp++] = 0xB0; rng[rp++] = 'C'; rng[rp] = 0;
    put_text(rightEdge - strlen(rng) * 6, SY + 42, rng, INK, 1);
  }
}

void draw_ui() {
  // ---- left-column CURRENT INFO rows (sc2): TOP — temp range / wind / rain state
  // (moved from the bottom; the icon row now lives at the bottom instead)
  // rain state: DRY / RAIN <amt>MM / SNOW <amt>MM / STORM (glyph + label)
  {
    const char *rs = rain_state();
    char amt[12];
    int ap = fmt_num(amt, wx.rain_total, 1);
    amt[ap] = 'M'; amt[ap + 1] = 'M'; amt[ap + 2] = 0;
    if (strcmp(rs, "STORM") == 0) {
      s_bolt(28, 98); put_text(42, 92, "STORM", INK, 2);
    } else if (strcmp(rs, "SNOW") == 0) {
      s_snow(26, 98); int x = put_text(42, 92, "SNOW ", INK, 2); put_text(x, 92, amt, INK, 2);
    } else if (strcmp(rs, "RAIN") == 0) {
      row_rain(25, 95); int x = put_text(42, 92, "RAIN ", INK, 2); put_text(x, 92, amt, INK, 2);
    } else {
      row_rain(25, 95); put_text(42, 92, "DRY", INK, 2);
    }
  }
  // daylight min/max of temp + wind (13h window, day hours; fallback: all)
  float lo = NAN, hi = NAN, wLo = NAN, wHi = NAN;
  for (int i = 0; i < 13; i++) {
    if (!is_daytime(i)) continue;
    if (!isnan(wx.temps[i])) { if (isnan(lo) || wx.temps[i] < lo) lo = wx.temps[i]; if (isnan(hi) || wx.temps[i] > hi) hi = wx.temps[i]; }
    if (!isnan(wx.winds[i])) { if (isnan(wLo) || wx.winds[i] < wLo) wLo = wx.winds[i]; if (isnan(wHi) || wx.winds[i] > wHi) wHi = wx.winds[i]; }
  }
  if (isnan(lo)) { for (int i = 0; i < 13; i++) { if (isnan(wx.temps[i])) continue; if (isnan(lo) || wx.temps[i] < lo) lo = wx.temps[i]; if (isnan(hi) || wx.temps[i] > hi) hi = wx.temps[i]; } }
  if (isnan(wLo)) { for (int i = 0; i < 13; i++) { if (isnan(wx.winds[i])) continue; if (isnan(wLo) || wx.winds[i] < wLo) wLo = wx.winds[i]; if (isnan(wHi) || wx.winds[i] > wHi) wHi = wx.winds[i]; } }
  if (isnan(lo)) { lo = 0; hi = 0; }
  if (isnan(wLo)) { wLo = 0; wHi = 0; }
  {   // wind interval (daylight, m/s)
    char ws[16];
    int p = fmt_num(ws, wLo, 1);
    ws[p++] = '-';
    p += fmt_num(ws + p, wHi, 1);
    ws[p++] = ' '; ws[p++] = 'm'; ws[p++] = '/'; ws[p++] = 's'; ws[p] = 0;
    row_wind(15, 66);
    put_text(42, 62, ws, INK, 2);
  }
  {   // temp range (daylight, °C) — 0xB0 degree byte -> glyph 41
    char tr[16];
    int p = fmt_num(tr, lo, 1);
    tr[p++] = '-';
    p += fmt_num(tr + p, hi, 1);
    tr[p++] = 0xB0; tr[p++] = 'C'; tr[p] = 0;
    row_therm(26, 36);
    put_text(tr[0] == '1' ? 40 : 42, 32, tr, INK, 2);
  }

  // left: big current temp (4x), centered in the left column
  char b[8];
  int cn = fmt_num(b, wx.cur, 1);
  {
    int tw = cn * 24;
    int nx = 14 + (210 - tw) / 2;
    if (nx < 14) nx = 14;
    put_text(nx, 120, b, INK, 4);
  }

  // left: current day label "DOW DD" (sc2), e.g. "SUN 18"
  {
    char dl[8];
    snprintf(dl, sizeof dl, "%s %02d",
             DOW3[dow_sakamoto(wx.now_year, wx.now_month, wx.now_day)], wx.now_day);
    int dlw = strlen(dl) * 12;
    int nx = 14 + (210 - dlw) / 2;
    if (nx < 14) nx = 14;
    put_text(nx, 165, dl, INK, 2);
  }
  // left: city name (sc1), centered under the day label
  {
    const char *nm = CITIES[cur_city].name;
    int nw = text_w(nm, 1);
    int nx = 14 + (210 - nw) / 2;
    if (nx < 14) nx = 14;
    put_text(nx, 195, nm, INK, 1);
  }

  // v2.1 icons: 4 MEDIUM glyphs, one per EQUAL part from "now" to midnight
  // (block_code over the 24h codes24 window) — IDENTICAL logic to the 2-day
  // strip below. SINGLE ROW at the bottom of the left column (moved from the
  // top — the info rows now live there). Replaces the old point-sample
  // (now/+3/+6/+9h) large icons.
  {
    int R = 24 - (wx.now_hour >= 0 ? wx.now_hour : 0);
    if (R < 1) R = 1;
    for (int k = 0; k < 4; k++) {
      int a, b;
      left_part(k, R, &a, &b);
      int x = IX0 + k * IGAP + 18;
      strip_icon(x, ICY, block_code(wx.codes24, a, b - a));
    }
  }

  // right: temp curve + labels + axis
  draw_temp_curve();

  // BOTTOM: 2-day rain strip (old badge slot, x 240-774)
  draw_two_day_strip();
}

