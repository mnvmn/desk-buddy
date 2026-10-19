// Raw mbedTLS HTTPS GET against the weather host (WEATHER_HOST).
#pragma once
#include "panel_canvas.h"

// GET https://WEATHER_HOST/path -> body (de-chunked). Returns bytes or -1.
int http_get(const char *path, char *body_out, size_t cap);

// imhd.sk Socket.IO poll (the travel feed): one HTTPS GET (body==NULL) or POST
// (body!=NULL) to https://imhd.sk<path>. Returns the de-chunked engine.io body
// in body_out, or -1 on failure. The travel handshake drives this.
int imhd_request(const char *path, const char *body, char *body_out, size_t cap);
