// App state + orchestration: globals, wifi, refresh cycle, render, capture.
#pragma once
#include "panel_data.h"

// Pages (views) shown on the panel — turning the side rotary UP/DOWN toggles
// between them (choice persisted in flash). Munich was a second *city*; it's
// gone, so the rotary now switches the *view* instead (weather <-> travel).
#define PAGE_WEATHER 0
#define PAGE_TRAVEL  1

struct travel_t;   // defined in panel_travel.h

extern int cur_city;      // always 0 now (single city) — kept for the weather fetch
extern int cur_page;      // PAGE_WEATHER or PAGE_TRAVEL
extern travel_t travel;   // travel view data (Karpatská 32/49/44)
extern bool wifi_ok;
extern uint32_t lastTick;

void ensure_wifi();
bool refresh_data();
void render_and_show(bool ok);
void toggle_page(const char *src);   // weather <-> travel (rotary UP/DOWN / serial PAGE)
void do_capture();
