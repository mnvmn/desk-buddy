// App state + orchestration: globals, wifi, refresh cycle, render, capture.
#pragma once
#include "panel_data.h"

extern int cur_city;
extern bool wifi_ok;
extern uint32_t lastTick;

void ensure_wifi();
bool refresh_data();
void render_and_show(bool ok);
void toggle_city(const char *src);
void do_capture();
