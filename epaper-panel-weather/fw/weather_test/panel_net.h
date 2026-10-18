// Raw mbedTLS HTTPS GET against the weather host (WEATHER_HOST).
#pragma once
#include "panel_canvas.h"

// GET https://WEATHER_HOST/path -> body (de-chunked). Returns bytes or -1.
int http_get(const char *path, char *body_out, size_t cap);
