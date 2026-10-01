// DeskBuddy M1-live — data-driven card on the 480x480 AMOLED.
//
// M1-live: Wi-Fi + /slots poller feeding the SAME card the widget draws
// (wiki/widget/modules/render.md): a dark rounded card with ONE DATA ROW
// PER SERVER — two independent servers (srv1 → m.tower1:11444, srv2 →
// m.mini1:11444, via LAN_HOST / LAN2_HOST from net_config.h). Each row:
// status dot + big number left, model + capacity right, leftover line, dim
// "label host:port" footer.
//
// Live data (spec §3.2/§3.3), refreshed adaptively (spec §3.3: 500 ms while
// working, 2 s idle, 5 s on error):
//   * GET /slots:
//       working  = any slot.is_processing
//       decoded  = sum(slot.next_token[].n_decoded)
//       n_remain = busy slot next_token[].n_remain
//       ctx used = sum(slot.n_prompt_tokens) across ALL slots — the last
//                  request's prompt stays in the slot while idle, so this
//                  is the true occupancy the widget shows (parity P1/P5)
//       tok/s    = Δ(decoded) / REAL elapsed between consecutive polls (B5)
//   * GET /props  once (and on recovery): model name + ftype +
//       default_generation_settings.n_ctx for the model/capacity lines.
//     Parse failures keep the last good values — never a crash (B9).
//
// States (spec §5): wifi down -> "--" + "wifi down"; /slots unreachable or
// unparseable -> "--" + "server down"; idle is a healthy state
// ("- " + "ready"), not an error. Prompting (request in flight, prompt
// still processing, no decoded tokens yet) gets the amber phase the widget
// shows (parity P3).
//
// Debug capture: at boot it re-renders the screen and streams the
// framebuffer over USB-CDC (per-tile base64) so the host composites a PNG
// of the real panel. The SH8601 panel is write-only (no hardware
// readback), so the LVGL draw buffers are the ground truth. Host:
// python esp32c6_480x480/firmware/tools/capture_panel.py COM3 out.png
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "lvgl.h"
#include "bsp_lvgl_port.h"
#include "./src/port_bsp/i2c_bsp.h"
#include "./src/port_bsp/axp2101_bsp.h"
#include "plumber_scene.h" // pre-rendered scene frames (working/prompting/idle/down)

I2cMasterBus I2cMasterBus_(GPIO_NUM_7, GPIO_NUM_8, I2C_NUM_0);

// ---- Config (shared: shared/net.env -> shared/net_config.h) ---------------
#include <net_config.h>
// Router DNS can't resolve the hostname from the device, so LAN IP.
// Adaptive refresh cadence (spec §3.3): 500 ms while working, 2 s idle,
// 5 s on error. (Was a fixed 5 s; parity P4 — a 5 s poll makes tok/s and
// "~Ns left" jump 4-6x between frames and makes "prompting" invisible.)
static const uint32_t POLL_MS_WORKING = 500;
static const uint32_t POLL_MS_IDLE = 2000;
static const uint32_t POLL_MS_ERR = 5000;
#define HTTP_TIMEOUT_MS 1500 // spec §3.3

// ---- Widget palette (fixed dark card, from render.md) -------------------
static const lv_color_t C_BG     = lv_color_make(0x1E, 0x1E, 0x22); // card bg
static const lv_color_t C_BORDER = lv_color_make(0x2A, 0x2A, 0x30); // subtle
static const lv_color_t C_INK    = lv_color_make(0xF2, 0xF3, 0xF5); // text
static const lv_color_t C_MUTED  = lv_color_make(0x9A, 0xA0, 0xAA);
static const lv_color_t C_DIM    = lv_color_make(0x5F, 0x65, 0x6F);
static const lv_color_t C_ACCENT = lv_color_make(0x4E, 0xC9, 0xB0); // number
static const lv_color_t C_GREEN  = lv_color_make(0x4E, 0xC9, 0x78); // dot
static const lv_color_t C_GRAY   = lv_color_make(0x7A, 0x80, 0x8A); // idle dot
static const lv_color_t C_RED    = lv_color_make(0xC9, 0x55, 0x55); // down dot
static const lv_color_t C_AMBER  = lv_color_make(0xD9, 0xA0, 0x4E); // prompting dot (parity P3)
static const lv_color_t C_PANE   = lv_color_make(0x00, 0x00, 0x00); // AMOLED off

enum Conn { OK_NET, OFFLINE_WIFI, OFFLINE_SERVER };

// ---- Per-server live state + labels (one card row each) -----------------
struct Lsrv {
  const char *label;      // "srv1" / "srv2"
  const char *host;       // LAN address (srv1 = tower1, srv2 = mini1)
  int port;
  char model[48];         // overwritten by /props once fetched
  char ftype[32];
  uint32_t n_ctx;
  Conn conn;
  bool working, prompting; // is_processing / prompt still processing
  float toks;              // smoothed tok/s for the current generation
  int32_t remain;          // busy slot next_token.n_remain
  uint32_t ctx_left, ctx_used; // n_ctx - used / sum(p+n) over all slots
  uint32_t prompt_tokens;  // prompt length of the in-flight request
  uint32_t prompt_processed; // n_prompt_tokens_processed (prefill progress, -> NN%)
  bool gen_open;           // true while the server is mid-generation
  uint32_t gen_open_us;    // millis() when decoding first seen (B5)
  bool have_decoded;       // previous-poll baseline for the rate (B5)
  uint32_t last_decoded;
  bool props_fetched;
  uint32_t tokens_total;  // prompt_tokens_total + tokens_predicted_total (/metrics); 0 = not fetched yet
  uint32_t cached_ratio;  // prompt_tokens_cached_total / prompt_tokens_total * 100 (0-100); 0 = unknown
  uint32_t spec_ratio;    // spec_decode_num_accepted / num_draft * 100 (0-100); 0 = speculative off/unknown
  float avg_tps;          // predicted_tokens_seconds gauge (session avg tok/s); 0 = unknown
  uint32_t n_slots;       // /slots array length -> the "×N slots" chip; 0 = unknown
  lv_obj_t *l_model, *l_num, *l_sub, *l_left, *l_cap, *l_foot, *l_note, *l_total;
  lv_obj_t *dot;
};

static Lsrv SRV[2] = {
  { "tower1", LAN_HOST, LAN_PORT, "", "",
    160000, OFFLINE_WIFI, false, false, 0.0f, -1, 0, 0, 0, 0, false, 0, false, 0, false, 0,
    0, 0, 0.0f, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  { "mini1", LAN2_HOST, LAN2_PORT, "", "",
    160000, OFFLINE_WIFI, false, false, 0.0f, -1, 0, 0, 0, 0, false, 0, false, 0, false, 0,
    0, 0, 0.0f, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
};

// ---- Card layout: one row per server ------------------------------------
// Model on its own line (28px name is ~367px wide — too wide to share the
// row with the big number). Left: dot + big number + unit. Capacity line
// under the number; leftover line beside the unit; footer.
static void make_row(Lsrv &s, lv_obj_t *card, int x, int y) {
  s.l_model = lv_label_create(card);
  lv_obj_set_style_text_font(s.l_model, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(s.l_model, C_INK, 0);
  lv_obj_set_pos(s.l_model, x, y);
  // Right-align so a long model name truncates its START, not its end
  // (the .gguf suffix always survives) — matches the widget. D1.
  lv_obj_set_width(s.l_model, 448 - 2 * x);
  lv_obj_set_style_text_align(s.l_model, LV_TEXT_ALIGN_RIGHT, 0);
  s.dot = lv_obj_create(card);
  lv_obj_remove_style_all(s.dot);
  lv_obj_set_size(s.dot, 16, 16);
  lv_obj_set_pos(s.dot, x, y + 52);
  lv_obj_set_style_radius(s.dot, 8, 0);
  lv_obj_set_style_bg_opa(s.dot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s.dot, 0, 0);
  s.l_num = lv_label_create(card);
  lv_obj_set_style_text_font(s.l_num, &lv_font_montserrat_48, 0);
  lv_obj_set_pos(s.l_num, x + 28, y + 32);
  s.l_cap = lv_label_create(card);
  lv_obj_set_style_text_font(s.l_cap, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s.l_cap, C_MUTED, 0);
  lv_obj_set_pos(s.l_cap, x, y + 92);
  s.l_sub = lv_label_create(card);
  lv_obj_set_style_text_font(s.l_sub, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s.l_sub, C_MUTED, 0);
  lv_obj_set_pos(s.l_sub, x + 28, y + 120);
  s.l_left = lv_label_create(card);
  lv_obj_set_style_text_font(s.l_left, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s.l_left, C_MUTED, 0);
  lv_obj_set_pos(s.l_left, x + 100, y + 120);
  s.l_foot = lv_label_create(card);
  lv_obj_set_style_text_font(s.l_foot, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s.l_foot, C_DIM, 0);
  lv_obj_set_pos(s.l_foot, x, y + 148);
  // "NN total" as its own right-anchored label on the footer row — the widget
  // right-aligns the session total on the same line (D4).
  s.l_total = lv_label_create(card);
  lv_obj_set_style_text_font(s.l_total, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s.l_total, C_DIM, 0);
  lv_obj_set_pos(s.l_total, x, y + 148);
  lv_obj_set_width(s.l_total, 448 - 2 * x);
  lv_obj_set_style_text_align(s.l_total, LV_TEXT_ALIGN_RIGHT, 0);
  // Session footnote: "avg NN | NN% cached | spec NN%".
  s.l_note = lv_label_create(card);
  lv_obj_set_style_text_font(s.l_note, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(s.l_note, C_DIM, 0);
  lv_obj_set_pos(s.l_note, x, y + 176);
}

void build_card(lv_obj_t *scr) {
  lv_obj_set_style_bg_color(scr, C_PANE, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_t *card = lv_obj_create(scr);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, 448, 428);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, C_BG, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 24, 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_border_color(card, C_BORDER, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
  make_row(SRV[0], card, 26, 16);
  make_row(SRV[1], card, 26, 220);
  // Dim divider between the two server rows (row 1's footnote ends ~y+192;
  // row 2 starts at y=220, so y=210 sits in the clear gap).
  lv_obj_t *div = lv_obj_create(card);
  lv_obj_remove_style_all(div);
  lv_obj_set_size(div, 416, 2);
  lv_obj_set_pos(div, 16, 210);
  lv_obj_set_style_bg_color(div, C_BORDER, 0);
  lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
}

// ---- Plumber scene (picture follows the live server state) ----------------
// Four pre-rendered 480x384 pictures (shared/scene-assets/render_scene.py ->
// plumber_scene.h; the bitmaps are the shared source of truth, see
// shared/scene-assets/README.md). Tapping the card opens the scene; the
// picture mirrors what the card would show (same priority as the widget's
// Phase): any server generating -> WORKING, else any prompt running ->
// PROMPTING, else all servers reachable -> IDLE, else DOWN. It re-syncs on
// every poll while the scene is on screen, so a run that finishes (or a
// server that drops) swaps the picture on its own. Tapping the scene goes
// back to the card.
//
// The pictures are lv_image objects streaming from flash (the 360 KB arrays
// live in rodata, not RAM — the C6 has no PSRAM), so this adds no heap. They
// are 480x384 (the level at 6 px/cell): the LVGL canvas is portrait 384x480,
// so the 480-wide picture is centered and the black top/bottom bands come
// from the AMOLED screen itself. They render
// through the normal LVGL path (same 90deg-CW flush as the card), which also
// keeps them verifiable with the serial screenshot capture (cap_flush reads
// the active screen's draw buffers; a raw panel blit would be invisible to
// it — which is why the scene is an image, not a blit).
static lv_obj_t *card_scr  = nullptr;  // the data-card screen (screen 0)
static lv_obj_t *scene_scr = nullptr;  // scene screen
static lv_obj_t *scene_img = nullptr;  // the picture on scene_scr
static int s_scene_override = -1;      // held manual picture via serial (-1 = live)

static const lv_image_dsc_t IMG_WORKING = {
  .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
              .w = SCENE_W, .h = SCENE_H, .stride = SCENE_W * 2 },
  .data_size = SCENE_BYTES,
  .data = scene_working_data,
};
static const lv_image_dsc_t IMG_PROMPTING = {
  .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
              .w = SCENE_W, .h = SCENE_H, .stride = SCENE_W * 2 },
  .data_size = SCENE_BYTES,
  .data = scene_prompting_data,
};
static const lv_image_dsc_t IMG_IDLE = {
  .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
              .w = SCENE_W, .h = SCENE_H, .stride = SCENE_W * 2 },
  .data_size = SCENE_BYTES,
  .data = scene_idle_data,
};
static const lv_image_dsc_t IMG_DOWN = {
  .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
              .w = SCENE_W, .h = SCENE_H, .stride = SCENE_W * 2 },
  .data_size = SCENE_BYTES,
  .data = scene_down_data,
};
static const lv_image_dsc_t *SCENES[] = {
  &IMG_WORKING, &IMG_PROMPTING, &IMG_IDLE, &IMG_DOWN };
static const char *SCENE_NAMES[] = { "WORKING", "PROMPTING", "IDLE", "DOWN" };

// Full-screen, invisible, clickable catcher: the WHOLE screen is the control.
// Created last so it sits on top; it has no bg/border (remove_style_all), so
// the content beneath shows through while every click lands on it.
static lv_obj_t *add_tap_catcher(lv_obj_t *scr, lv_event_cb_t cb) {
  lv_obj_t *b = lv_obj_create(scr);
  lv_obj_remove_style_all(b);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, LV_COORD_MAX, LV_COORD_MAX);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

// Derived scene phase from the live per-server state — the device twin of
// the widget's BuddyState::phase() (buddy_state.rs), aggregated over both
// rows: worst-network first, then the busiest work phase wins.
static int scene_phase() {
  bool any_net = false, any_prompt = false, any_gen = false;
  for (int i = 0; i < 2; i++) {
    if (SRV[i].conn == OK_NET) any_net = true;
    if (SRV[i].working && SRV[i].prompting) any_prompt = true;
    else if (SRV[i].working) any_gen = true;
  }
  if (!any_net) return 3;                  // DOWN
  if (any_gen) return 0;                   // WORKING
  if (any_prompt) return 1;                // PROMPTING
  return 2;                                // IDLE
}

// Switch the picture on the scene screen. Recreating the image (not
// set_src-ed) + invalidating the screen covers both call paths: card->scene
// gets the full redraw from lv_scr_load; an in-place swap marks the screen
// dirty and paints on the lvgl task's next frame. Caveat: while CAP holds the
// lvgl lock (take_screenshot) that refresh is impossible, so a swap issued in
// that window is queued in s_scene_override and applied by the poll loop on
// the next free lock — otherwise the panel silently keeps the old picture.
static void scene_show(const lv_image_dsc_t *img) {
  lv_scr_load(scene_scr);
  if (scene_img) lv_obj_delete(scene_img);
  scene_img = lv_image_create(scene_scr);
  lv_obj_align(scene_img, LV_ALIGN_CENTER, 0, 0);
  lv_image_set_src(scene_img, img);
  lv_obj_invalidate(scene_scr);
}

// Open the scene, showing the picture the live state maps to (the loop keeps
// it in sync afterwards).
static void card_tap_cb(lv_event_t *e) {
  if (!scene_scr) return;
  scene_show(SCENES[scene_phase()]);
  Serial.printf("scene: %s\n", SCENE_NAMES[scene_phase()]);
}
static void scene_tap_cb(lv_event_t *e) {
  s_scene_override = -1;  // closing the scene resets back to live-follow
  lv_scr_load(card_scr);
  Serial.println("scene: back to card");
}

static void build_scene() {
  scene_scr = lv_obj_create(nullptr);          // new screen (parent NULL)
  lv_obj_remove_flag(scene_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scene_scr, C_PANE, 0);
  lv_obj_set_style_bg_opa(scene_scr, LV_OPA_COVER, 0);
  scene_img = lv_image_create(scene_scr);
  lv_obj_align(scene_img, LV_ALIGN_CENTER, 0, 0);
  lv_image_set_src(scene_img, &IMG_WORKING);
  add_tap_catcher(scene_scr, scene_tap_cb);
}

// ---- HTTP helpers (tiny, defensive: any failure -> error state, B7/B9) --
static bool http_get(const char *host, int port, const char *path, String &body, size_t cap) {
  HTTPClient http;
  String url = String("http://") + host + ":" + port + path;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(url)) { http.end(); return false; }
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  body = http.getString();          // full body; the poller caps below
  http.end();
  return body.length() > 0 && body.length() <= cap;
}

static bool fetch_props(Lsrv &s) {
  String body;
  if (!http_get(s.host, s.port, "/props", body, 20480)) return false;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  if (e) return false;
  const char *alias = doc["model_alias"];
  if (alias && strlen(alias) > 0) {
    // show just the filename, not the full Windows path
    const char *slash = strrchr(alias, '/');
    const char *bslash = strrchr(alias, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    strncpy(s.model, slash ? slash + 1 : alias, sizeof(s.model) - 1);
    s.model[sizeof(s.model) - 1] = 0;
  }
  const char *ft = doc["model_ftype"];
  if (ft && strlen(ft) > 0) { strncpy(s.ftype, ft, sizeof(s.ftype) - 1); s.ftype[sizeof(s.ftype) - 1] = 0; }
  uint32_t nctx = doc["default_generation_settings"]["n_ctx"].as<uint32_t>();
  if (nctx >= 1024) s.n_ctx = nctx;
  return true;
}

// /metrics is a big Prometheus body. We pull six cumulative counters to
// mirror the widget's session chips (parity): prompt + predicted (-> "NNk
// total"), prompt_cached/prompt_total (-> "NN% cached"), spec accepted/draft
// (-> "spec NN%"). Counter values can be scientific notation (1.79e+07), so
// parse as double. Tokenize on whitespace/newline, match the metric-name
// token EXACTLY (the longer *_cached_total / *_seconds names can't collide,
// and per_pos labels make those tokens != the base name). HELP/TYPE lines
// name the counter too but their "value" is text, so the number parse fails
// there. Failures keep the previous value (or nothing).
static void fetch_metrics(Lsrv &s) {
  String body;
  if (!http_get(s.host, s.port, "/metrics", body, 24576)) return;
  struct M { const char *name; double v; } m[] = {
    { "llamacpp:prompt_tokens_total", 0 },
    { "llamacpp:tokens_predicted_total", 0 },
    { "llamacpp:prompt_tokens_cached_total", 0 },
    { "llamacpp:spec_decode_num_accepted_tokens_total", 0 },
    { "llamacpp:spec_decode_num_draft_tokens_total", 0 },
    { "llamacpp:predicted_tokens_seconds", 0 },
  };
  const char *p = body.c_str();
  size_t n = body.length(), i = 0;
  auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == 13; };
  while (i < n) {
    while (i < n && (p[i] == '#' || blank(p[i]))) i++;
    if (i >= n) break;
    size_t j = i;
    while (j < n && !blank(p[j])) j++;
    size_t len = j - i;
    for (int k = 0; k < 6; k++) {
      if (len == strlen(m[k].name) && strncmp(p + i, m[k].name, len) == 0) {
        size_t k2 = j;
        while (k2 < n && (p[k2] == ' ' || p[k2] == '\t')) k2++;
        double v; if (sscanf(p + k2, "%lf", &v) == 1) m[k].v = v;
        break;
      }
    }
    i = j;
  }
  if (m[0].v || m[1].v) s.tokens_total = (uint32_t)(m[0].v + m[1].v);
  if (m[0].v > 0)
    s.cached_ratio = (uint32_t)min(100.0, max(0.0, m[2].v / m[0].v * 100.0));
  if (m[4].v > 0)
    s.spec_ratio = (uint32_t)min(100.0, max(0.0, m[3].v / m[4].v * 100.0));
  if (m[5].v > 0.0)
    s.avg_tps = (float)m[5].v;
}

static void fetch_slots(Lsrv &s) {
  String body;
  if (!http_get(s.host, s.port, "/slots", body, 10240)) { s.conn = OFFLINE_SERVER; return; }
  JsonDocument doc;
  if (deserializeJson(doc, body)) { s.conn = OFFLINE_SERVER; return; } // B9
  JsonArray slots = doc.as<JsonArray>();
  if (!slots || slots.isNull()) { s.conn = OFFLINE_SERVER; return; }

  bool working = false, prompting = false;
  uint32_t decoded = 0;
  int32_t remain = -1;
  uint32_t used = 0; // occupancy over ALL slots (parity P1/P5): the last
                     // request's tokens stay in the slot while idle
  uint32_t prompt_tok = 0;
  uint32_t prompt_proc = 0;
  uint32_t slot_count = 0;
  for (JsonObject slot : slots) {
    slot_count++;
    uint32_t p = slot["n_prompt_tokens"].as<uint32_t>();
    uint32_t n = slot["n_tokens"].as<uint32_t>(); // tokens generated so far
    used += p + n;
    if (slot["is_processing"].as<bool>()) {
      working = true;
      prompt_tok += p;
      prompt_proc += slot["n_prompt_tokens_processed"].as<uint32_t>();
      for (JsonObject nt : slot["next_token"].as<JsonArray>()) {
        uint32_t d = nt["n_decoded"].as<uint32_t>();
        if (d > 0) decoded = d;
        int32_t r = nt["n_remain"].as<int32_t>();
        if (r > 0) remain = r; // bounded generation only
      }
      if (decoded == 0) prompting = true; // prompt still processing (parity P3)
    }
  }
  s.working = working;
  s.prompting = prompting;
  s.n_slots = slot_count;
  s.prompt_tokens = prompt_tok;
  s.prompt_processed = prompt_proc;
  s.remain = remain;
  s.ctx_used = used;
  s.ctx_left = s.n_ctx > used ? s.n_ctx - used : 0;

  // B5 (fixed 2026-09-26, parity P2): rate = Δ(decoded)/Δ(real time) between
  // consecutive poll snapshots. n_decoded is a per-request counter (resets
  // on the next request), so the baseline is cleared the moment the slot
  // stops working and re-armed at the next generation's first snapshot —
  // prompt-processing time is excluded. EMA (0.6/0.4) keeps the big number
  // calm on a 500 ms cadence while tracking rate changes within ~2 s.
  uint32_t now = millis();
  if (working && decoded > 0) {
    if (!s.have_decoded) {
      s.have_decoded = true;
      s.gen_open = true;
      s.gen_open_us = now;
      s.toks = 0.0f; // no baseline until the next snapshot
    } else {
      float secs = (now - s.gen_open_us) / 1000.0f;
      if (secs > 0.05f && decoded > s.last_decoded) {
        float sample = (float)(decoded - s.last_decoded) / secs;
        s.toks = (s.toks < 0.05f) ? sample : 0.6f * sample + 0.4f * s.toks;
      }
    }
    s.gen_open_us = now;
    s.last_decoded = decoded;
  }
  if (!working) {
    s.gen_open = false;     // generation finished -> baseline resets (parity P2)
    s.have_decoded = false;
    s.toks = 0.0f;          // never freeze a stale rate (spec §4)
  }
  s.conn = OK_NET;
  if (!s.props_fetched) s.props_fetched = fetch_props(s);
}

// ---- Render (only touches labels; caller holds the LVGL lock) -----------
static void fmt_rate(char *buf, size_t n, const Lsrv &s) {
  if (!s.working || s.toks < 0.05f) { strncpy(buf, "-", n); return; }
  if (s.toks >= 10.0f) snprintf(buf, n, "%.0f", (double)s.toks);
  else snprintf(buf, n, "%.1f", (double)s.toks);
}

// Session "NNk total" / "NNM total" chip — same fmt as the widget's fmt_ctx.
// Empty string when not yet fetched (0) so the footer can omit it.
static void fmt_total(char *buf, size_t n, const Lsrv &s) {
  if (s.tokens_total < 1000) {
    if (s.tokens_total == 0) { buf[0] = 0; return; }
    snprintf(buf, n, "%lu", (unsigned long)s.tokens_total);
  } else if (s.tokens_total < 1000000) {
    snprintf(buf, n, "%.0fk", (double)s.tokens_total / 1000.0);
  } else {
    snprintf(buf, n, "%.0fM", (double)s.tokens_total / 1000000.0);
  }
}

static void render_srv(Lsrv &s) {
  char buf[48], cap[120], left[64], foot[64], note[64];
  lv_color_t dot, numc;

  if (s.conn == OFFLINE_WIFI) {
    dot = C_RED; numc = C_DIM;
    snprintf(buf, sizeof(buf), "--");
    snprintf(cap, sizeof(cap), "%s  %s", s.model, s.ftype);
    snprintf(left, sizeof(left), "offline");
    snprintf(foot, sizeof(foot), "%s  %s:%d  wifi down", s.label, s.host, s.port);
  } else if (s.conn == OFFLINE_SERVER) {
    dot = C_RED; numc = C_DIM;
    snprintf(buf, sizeof(buf), "--");
    snprintf(cap, sizeof(cap), "%s  %s", s.model, s.ftype);
    snprintf(left, sizeof(left), "offline");
    snprintf(foot, sizeof(foot), "%s  %s:%d  server down", s.label, s.host, s.port);
  } else if (s.prompting) {
    // Request in flight, prompt still processing, no decoded tokens yet —
    // the amber phase the widget shows (parity P3). The widget's big number
    // is the prefill % (processed/total prompt tokens), so mirror that.
    dot = C_AMBER; numc = C_DIM;
    if (s.prompt_tokens > 0)
      snprintf(buf, sizeof(buf), "%u%%",
               (unsigned)min(100.0, (double)s.prompt_processed * 100.0 / s.prompt_tokens));
    else
      snprintf(buf, sizeof(buf), "···");
    snprintf(cap, sizeof(cap), "%.0fk/%uk  %u%%  %s",
             (double)s.ctx_used / 1000.0, (unsigned)(s.n_ctx / 1000),
             s.n_ctx ? (unsigned)(s.ctx_used * 100 / s.n_ctx) : 0, s.ftype);
    if (s.prompt_tokens > 0)
      snprintf(left, sizeof(left), "processing  %uk/%uk tok",
               (unsigned)(s.prompt_processed / 1000), (unsigned)(s.prompt_tokens / 1000));
    else
      snprintf(left, sizeof(left), "processing prompt");
    snprintf(foot, sizeof(foot), "%s  %s:%d", s.label, s.host, s.port);
  } else if (!s.working) {
    dot = C_GRAY; numc = C_DIM;
    snprintf(buf, sizeof(buf), "-");
    // True occupancy (parity P1), same used/total convention as the widget
    // (parity P5): the last request's prompt+generated tokens stay in the
    // slot while idle — "95k/160k  59%  Q4_K - Medium".
    snprintf(cap, sizeof(cap), "%.0fk/%uk  %u%%  %s",
             (double)s.ctx_used / 1000.0, (unsigned)(s.n_ctx / 1000),
             s.n_ctx ? (unsigned)(s.ctx_used * 100 / s.n_ctx) : 0, s.ftype);
    snprintf(left, sizeof(left), "ready");
    snprintf(foot, sizeof(foot), "%s  %s:%d", s.label, s.host, s.port);
  } else {
    dot = C_GREEN; numc = C_ACCENT;
    fmt_rate(buf, sizeof(buf), s);
    // "7k/160k  4%  Q4_K - Medium" — used/total like the widget (parity P5)
    snprintf(cap, sizeof(cap), "%.0fk/%uk  %u%%  %s",
             (double)s.ctx_used / 1000.0, (unsigned)(s.n_ctx / 1000),
             s.n_ctx ? (unsigned)(s.ctx_used * 100 / s.n_ctx) : 0, s.ftype);
    if (s.toks > 0.05f && s.remain > 0) {
      snprintf(left, sizeof(left), "~%us left  %d tok",
               (unsigned)((double)s.remain / s.toks), (int)s.remain);
    } else {
      snprintf(left, sizeof(left), "generating");
    }
    snprintf(foot, sizeof(foot), "%s  %s:%d", s.label, s.host, s.port);
  }

  // Session chips (parity with the widget). The "×N slots" chip prepends the
  // caption line (widget puts it right after the quant, before the ctx). The
  // footer row carries the session "NNk total" (widget footer-right). The
  // footnote row carries the session stats: "avg NN · NN% cached · spec NN%"
  // (widget footnote()). All omitted offline / before the first fetch.
  if (s.conn == OK_NET) {
    // "N slots" chip leads the capacity line (widget order: slots · ctx ·
    // mem% · ftype). ASCII "x" (not "×") — the built-in Montserrat fonts are
    // ASCII-only and "×" would draw as a tofu box. D3, D5.
    if (s.n_slots > 0) {
      char chip[16];
      int cw = snprintf(chip, sizeof(chip), "x%u slots", (unsigned)s.n_slots);
      size_t cl = strlen(cap);
      memmove(cap + cw + 2, cap, cl + 1);   // make room at the front
      memcpy(cap, chip, (size_t)cw);
      cap[cw] = cap[cw + 1] = ' ';
    }
    // "NN total" as its own right-anchored label (widget footer-right). D4.
    char tt[16];
    fmt_total(tt, sizeof(tt), s);
    if (tt[0])
      lv_label_set_text_fmt(s.l_total, "%s total", tt);
    else
      lv_label_set_text(s.l_total, "");
    // footnote: "avg NN | NN% cached | spec NN%" — ASCII "|" (not "·"), the
    // built-in font has no middle-dot glyph. D2.
    note[0] = 0;
    char part[24];
    size_t nl = 0;
    if (s.avg_tps > 0.5f) {
      snprintf(part, sizeof(part), "avg %d", (int)(s.avg_tps + 0.5f));
      snprintf(note, sizeof(note), "%s", part);
      nl = strlen(note);
    }
    if (s.cached_ratio > 0) {
      if (nl) snprintf(note + nl, sizeof(note) - nl, " | ");
      snprintf(note + strlen(note), sizeof(note) - strlen(note), "%u%% cached",
               (unsigned)s.cached_ratio);
    }
    if (s.spec_ratio > 0) {
      nl = strlen(note);
      if (nl) snprintf(note + nl, sizeof(note) - nl, " | ");
      snprintf(note + strlen(note), sizeof(note) - strlen(note), "spec %u%%",
               (unsigned)s.spec_ratio);
    }
  } else {
    note[0] = 0;
    lv_label_set_text(s.l_total, "");
  }

  lv_label_set_text(s.l_model, s.model);
  lv_obj_set_style_bg_color(s.dot, dot, 0);
  lv_label_set_text(s.l_num, buf);
  lv_obj_set_style_text_color(s.l_num, numc, 0);
  lv_label_set_text(s.l_cap, cap);
  lv_label_set_text(s.l_sub, "tok/s");
  lv_label_set_text(s.l_left, left);
  lv_label_set_text(s.l_foot, foot);
  lv_label_set_text(s.l_note, note);
}

// ---- Framebuffer capture (panel is write-only; capture from LVGL) -------
// Same mechanism as M1-static: swap the port flush callback, force a full
// redraw, stream every tile as "TILE x y w h" + base64 RGB565 until "END".
// Host composites the PNG (tools/capture_panel.py).
#define CAP_LINE_CHUNK 1600 // raw bytes per base64 line
static lv_display_t *g_disp = NULL;

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p); // bsp_lvgl_port.cpp
void rot90_cw(uint16_t *src, uint16_t *dst, int w, int h); // bsp_lvgl_port.cpp

static const char B64TAB[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64_line(const uint8_t *in, size_t n, char *out) {
  size_t i = 0, o = 0;
  while (i + 3 <= n) {
    uint32_t v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
    out[o++] = B64TAB[v >> 18]; out[o++] = B64TAB[(v >> 12) & 63];
    out[o++] = B64TAB[(v >> 6) & 63];  out[o++] = B64TAB[v & 63]; i += 3;
  }
  if (i + 1 == n) {
    uint32_t v = in[i] << 16;
    out[o++] = B64TAB[v >> 18]; out[o++] = B64TAB[(v >> 12) & 63]; out[o++] = '='; out[o++] = '=';
  } else if (i + 2 == n) {
    uint32_t v = (in[i] << 16) | (in[i + 1] << 8);
    out[o++] = B64TAB[v >> 18]; out[o++] = B64TAB[(v >> 12) & 63]; out[o++] = B64TAB[(v >> 6) & 63]; out[o++] = '=';
  }
  out[o] = 0;
}

static void cap_flush(lv_display_t *d, const lv_area_t *a, uint8_t *p) {
  uint32_t w = lv_area_get_width(a), h = lv_area_get_height(a);
  // Mirror my_disp_flush: the panel shows the UI rotated 90 deg CW, so the
  // captured tile is rotated too and its PHYSICAL position is streamed.
  rot90_cw((uint16_t *)p, (uint16_t *)p, (int)w, (int)h);
  const int H = 480; // panel is square (BSP_LCD_H_RES, defined in bsp_lvgl_port.cpp)
  uint32_t px = H - 1 - a->y2; // full 480x480 canvas, no offset (matches my_disp_flush)
  uint32_t py = a->x1;
  uint32_t npix = (uint32_t)w * h * 2;
  char line[4300];
  Serial.printf("TILE %lu %lu %lu %lu\n", (unsigned long)px, (unsigned long)py,
                (unsigned long)h, (unsigned long)w);
  for (uint32_t off = 0; off < npix; off += CAP_LINE_CHUNK) {
    uint32_t n = (npix - off > CAP_LINE_CHUNK) ? CAP_LINE_CHUNK : (npix - off);
    b64_line(p + off, n, line);
    Serial.println(line);
  }
  lv_disp_flush_ready(d);
}

static void take_screenshot() {
  if (!g_disp) { Serial.println("capture: not ready"); return; }
  lv_display_set_flush_cb(g_disp, cap_flush);
  lv_obj_invalidate(lv_screen_active()); // dirty the whole screen
  lv_refr_now(g_disp);                   // redraw; tiles stream as they flush
  lv_display_set_flush_cb(g_disp, my_disp_flush);
  Serial.println("END");
}

// ---- Wi-Fi ---------------------------------------------------------------
static void net_diagnostics() {
  // TEMP (M1-live bring-up): figure out where the device sits on the LAN.
  // DNS + TCP + HTTP for BOTH servers — srv2 (mini1) was showing
  // "server down" while srv1 was healthy, so probe each one to separate
  // DNS / TCP / HTTP causes.
  IPAddress ip1, ip2;
  bool h1 = WiFi.hostByName("m.tower1", ip1);
  bool h2 = WiFi.hostByName("m.mini1", ip2);
  Serial.printf("diag: dns m.tower1 -> %s, m.mini1 -> %s\n",
                h1 ? ip1.toString().c_str() : "FAIL",
                h2 ? ip2.toString().c_str() : "FAIL");
  struct { const char *host; int port; const char *name; } targets[2] = {
    { LAN_HOST,  LAN_PORT,  "tower1" },
    { LAN2_HOST, LAN2_PORT, "mini1"  },
  };
  for (int i = 0; i < 2; i++) {
    uint8_t ok = 0;
    for (uint8_t t = 0; t < 4; t++) {
      WiFiClient tc;
      tc.setTimeout(1500);
      if (tc.connect(targets[i].host, targets[i].port)) { tc.stop(); ok++; }
      delay(200);
    }
    HTTPClient http;
    http.setTimeout(1500);
    http.begin(String("http://") + targets[i].host + ":" + targets[i].port + "/slots");
    int code = http.GET();
    String body = (code == HTTP_CODE_OK) ? http.getString() : String("");
    http.end();
    Serial.printf("diag: %s tcp %s:%d -> %u/4 ok, http /slots code=%d len=%u\n",
                  targets[i].name, targets[i].host, targets[i].port,
                  (unsigned)ok, code, (unsigned)body.length());
  }
}

static bool wifi_up() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("deskbuddy");
  WiFi.begin(WIFI_SSID, WIFI_PSWD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(100);
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.printf("wifi: connected %s ip=%s mac=%s\n", WIFI_SSID,
                WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str());
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("DeskBuddy boot: initializing PMIC + display");
  Custom_PmicPortInit(&I2cMasterBus_, 0x34);

  bsp_lvgl_init(I2cMasterBus_);
  g_disp = lv_display_get_default();
  if (!bsp_lvgl_lock(0)) { Serial.println("ERROR: could not lock LVGL"); return; }
  Lcd_SetBacklight(100);
  card_scr = lv_screen_active();
  // Screens are scrollable by default in LVGL 9; a swipe bubbles up to the
  // screen and shifts the whole card. The tap-catcher on top is not
  // scrollable, so the only thing that can scroll is the screen itself —
  // disable it (same as build_scene) so swipes don't move the UI.
  lv_obj_remove_flag(card_scr, LV_OBJ_FLAG_SCROLLABLE);
  build_card(card_scr);
  add_tap_catcher(card_scr, card_tap_cb);   // tap the card -> plumber scene
  build_scene();
  // Default screen is the GRAPHIC (live-follow; the poll loop keeps the
  // picture honest). Tap anywhere on it to get the data card; tap the card
  // to come back. The scene's live-follow also applies while open, so it
  // lands on the current phase within one poll tick.
  lv_scr_load(scene_scr);
  render_srv(SRV[0]); render_srv(SRV[1]); // placeholder (wifi-down), never blank
  bsp_lvgl_unlock();
  Serial.println("Display up: DeskBuddy scene (default; follows live state) - TAP the screen for the data card");

  if (!wifi_up()) {
    SRV[0].conn = OFFLINE_WIFI; SRV[1].conn = OFFLINE_WIFI;
    Serial.println("wifi: NOT connected (15s) - card shows 'wifi down'");
  } else {
    delay(500);
    net_diagnostics();
  }

  delay(500);
  Serial.println("screenshot: capturing");
  if (bsp_lvgl_lock(0)) { take_screenshot(); bsp_lvgl_unlock(); }
  Serial.println("polling /slots per server (2 servers)");
}

void loop() {
  // On-demand screenshot: type "CAP" on the serial console to re-stream the
  // current framebuffer over USB-CDC without a board reset (so a live
  // generation keeps running). Tools: capture_live.py COM3 out.png
  static char capbuf[8]; static size_t caplen = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      bool matched = false;
      if (caplen == 3 && capbuf[0]=='C' && capbuf[1]=='A' && capbuf[2]=='P') {
        if (bsp_lvgl_lock(0)) { take_screenshot(); bsp_lvgl_unlock(); }
        matched = true;
      }
      // Manual picture override (SCN/SCP/SCI/SDN) or jump to the card
      // (CAR). Match the whole 3-char token (SDN starts with 'S'+'D',
      // not 'S'+'C' — the old prefix test swallowed it). The picture is
      // queued, not shown here: the poll loop applies it under the LVGL lock
      // (showing it in the serial handler would race the lvgl task's frame,
      // and a swap issued while CAP holds the lock paints nothing).
      if (caplen == 3 && capbuf[0]=='S') {
        int idx = -1;
        if (capbuf[1]=='C' && capbuf[2]=='N') idx = 0;   // SCN working
        else if (capbuf[1]=='C' && capbuf[2]=='P') idx = 1;  // SCP prompting
        else if (capbuf[1]=='C' && capbuf[2]=='I') idx = 2;  // SCI idle
        else if (capbuf[1]=='D' && capbuf[2]=='N') idx = 3;  // SDN down
        if (idx >= 0 && scene_scr) {
          if (lv_screen_active() != scene_scr) card_tap_cb(nullptr); // open it
          s_scene_override = idx; matched = true;
        }
      }
      if (caplen == 3 && capbuf[0]=='C' && capbuf[1]=='A' && capbuf[2]=='R') {
        if (card_scr && bsp_lvgl_lock(0)) {
          s_scene_override = -1;  // like a scene tap: back to live-follow
          lv_scr_load(card_scr);
          bsp_lvgl_unlock();
        }
        matched = true;
      }
      // SOP: open the scene exactly like a card tap (same code path) and
      // reset any held manual override back to live-follow.
      if (caplen == 3 && capbuf[0]=='S' && capbuf[1]=='O' && capbuf[2]=='P') {
        s_scene_override = -1;
        card_tap_cb(nullptr);
        matched = true;
      }
      if (matched) { char lbl[8]; memcpy(lbl, capbuf, caplen); lbl[caplen]=0; Serial.println(String("cmd: ") + lbl); }
      caplen = 0;
    } else if (caplen < 7) {
      capbuf[caplen++] = c;
    }
  }

  // Live poll: keeps the card's data fresh regardless of which screen is
  // active (render_srv writes to card_scr's labels, which LVGL only paints
  // while card_scr is the active screen). Spec §3.3 cadence (parity P4):
  // 500 ms while ANY server is working, 2 s idle, 5 s while all disconnected.
  {
    bool any_work = false, any_net = false;
    for (int i = 0; i < 2; i++) {
      if (SRV[i].working) any_work = true;
      if (SRV[i].conn == OK_NET) any_net = true;
    }
    uint32_t cadence = any_net ? (any_work ? POLL_MS_WORKING : POLL_MS_IDLE) : POLL_MS_ERR;
    static uint32_t last_poll_us = 0;
    if (millis() - last_poll_us >= cadence) {
      last_poll_us = millis();
      if (WiFi.status() == WL_CONNECTED) {
        for (int i = 0; i < 2; i++) fetch_slots(SRV[i]);
        // Session total from /metrics (prompt+predicted cumulative). The body
        // is ~24 KB and the counter barely moves, so it runs on its own 2 s
        // throttle — not the 500 ms working cadence the slots poll uses.
        static uint32_t last_metrics_us = 0;
        if (millis() - last_metrics_us >= 2000) {
          last_metrics_us = millis();
          for (int i = 0; i < 2; i++) if (SRV[i].conn == OK_NET) fetch_metrics(SRV[i]);
        }
      } else {
        SRV[0].conn = OFFLINE_WIFI; SRV[1].conn = OFFLINE_WIFI;
      }

      if (bsp_lvgl_lock(0)) {
        render_srv(SRV[0]);
        render_srv(SRV[1]);
        // Keep the scene picture honest: while the scene is on screen, apply
        // a queued serial override, else re-derive the phase from the fresh
        // poll and swap when it changed. (All picture swaps happen here,
        // under the LVGL lock, so the lvgl task paints them next frame.)
        if (scene_img && lv_screen_active() == scene_scr) {
          const void *cur = lv_image_get_src(scene_img);
          int want = -1;
          for (int i = 0; i < 4; i++) if (SCENES[i] == cur) want = i;
          if (s_scene_override >= 0) {
            // A manual override HOLDS (shows it, then stays put) — the live
            // re-derivation must not fight it back to IDLE next tick. It is
            // cleared when the scene closes (below) or by SOP.
            if (s_scene_override != want) {
              scene_show(SCENES[s_scene_override]);
              Serial.printf("scene: %s\n", SCENE_NAMES[s_scene_override]);
            }
          } else {
            int ph = scene_phase();
            if (want != ph) {
              scene_show(SCENES[ph]);
              Serial.printf("scene: %s\n", SCENE_NAMES[ph]);
            }
          }
        } else if (s_scene_override >= 0 && lv_screen_active() != scene_scr) {
          s_scene_override = -1;  // on the card: drop stale overrides
        }
        bsp_lvgl_unlock();
      }

      char buf[200];
      if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf),
                 "poll: srv1 %s %.0f tok/s ctx=%lu/%lu rem=%d tot=%lu | srv2 %s %.0f tok/s ctx=%lu/%lu rem=%d tot=%lu",
                 SRV[0].working ? (SRV[0].prompting ? "PROMPTING" : "WORKING") : "idle",
                 (double)SRV[0].toks, (unsigned long)SRV[0].ctx_used, (unsigned long)SRV[0].n_ctx,
                 SRV[0].remain, (unsigned long)SRV[0].tokens_total,
                 SRV[1].working ? (SRV[1].prompting ? "PROMPTING" : "WORKING") : "idle",
                 (double)SRV[1].toks, (unsigned long)SRV[1].ctx_used, (unsigned long)SRV[1].n_ctx,
                 SRV[1].remain, (unsigned long)SRV[1].tokens_total);
      } else {
        snprintf(buf, sizeof(buf), "poll: wifi down");
      }
      Serial.println(buf);
    }
  }
  delay(10);
}
