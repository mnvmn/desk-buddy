// ---- network: raw mbedTLS (GET + POST, any desk-buddy host) ------------------
// The panel's whole internet path (the weather feed + the imhd.sk transit feed)
// is hand-rolled mbedTLS: no HTTP client library, no certificate store. Trust is
// pinned to the DER roots in le_dercainfo.h (TRUST_ANCHORS): Let's Encrypt for
// open-meteo.com, Sectigo for imhd.sk. The handshake VERIFIES against those
// roots — MITM fails. Connection is "close" per call (a fresh
// handshake each poll; fine at a 5-min cadence).
#include "panel_net.h"
#include <net_config.h>
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "esp_random.h"
#include "le_dercainfo.h"  // TRUST_ANCHORS: TLS trust roots (LE + Sectigo) as DER

#ifndef IMHD_HOST
#define IMHD_HOST "imhd.sk"
#endif

// mbedTLS needs an RNG: esp_fill_random gives hardware-grade bytes.
static int le_rng(void *p, unsigned char *out, size_t len) {
  (void)p; esp_fill_random(out, len); return 0;
}

// One HTTPS exchange to https://host<path>: GET (body==NULL) or POST (body!=NULL).
// Sends "Connection: close", reads the response to end, de-chunks it, and copies
// the de-chunked body into out. Returns the body byte count, or -1 on any failure.
static int _tls_request(const char *host, const char *path, const char *body,
                        char *out, size_t cap) {
  char req[1024];
  int ql;
  if (body)
    ql = snprintf(req, sizeof req,
      "POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: desk-buddy-s3\r\n"
      "Content-Type: text/plain;charset=UTF-8\r\nContent-Length: %d\r\n"
      "Connection: close\r\n\r\n%s", path, host, (int)strlen(body), body);
  else
    ql = snprintf(req, sizeof req,
      "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: desk-buddy-s3\r\n"
      "Connection: close\r\n\r\n", path, host);
  if (ql < 0 || (size_t)ql >= sizeof(req)) return -1;
  Serial.printf("net: %s https://%s%s\n", body ? "POST" : "GET", host, path);

  mbedtls_net_context net; mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
  mbedtls_x509_crt cacrt;
  mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
  mbedtls_x509_crt_init(&cacrt);
  auto freeall = [&]() { mbedtls_net_free(&net); mbedtls_ssl_free(&ssl);
                         mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&cacrt); };

  int r = mbedtls_net_connect(&net, host, "443", MBEDTLS_NET_PROTO_TCP);
  if (r) { char b[80]; mbedtls_strerror(r, b, sizeof(b));
           Serial.printf("net: tcp FAIL (%s)\n", b); freeall(); return -1; }

  mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                              MBEDTLS_SSL_TRANSPORT_STREAM, NULL);
  mbedtls_ssl_conf_rng(&conf, le_rng, NULL);
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  for (size_t i = 0; i < sizeof(TRUST_ANCHORS) / sizeof(TRUST_ANCHORS[0]); i++)
    mbedtls_x509_crt_parse_der(&cacrt, TRUST_ANCHORS[i].der, TRUST_ANCHORS[i].len);
  mbedtls_ssl_conf_ca_chain(&conf, &cacrt, NULL);
  mbedtls_ssl_setup(&ssl, &conf);
  mbedtls_ssl_set_hostname(&ssl, host);
  mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv,
                      mbedtls_net_recv_timeout);

  int h = mbedtls_ssl_handshake(&ssl);
  if (h) { char b[160]; mbedtls_strerror(h, b, sizeof(b));
           Serial.printf("net: TLS FAIL r=%d (%s)\n", h, b); freeall(); return -1; }
  Serial.printf("net: TLS verified (flags 0x%08lx)\n",
                (unsigned long)mbedtls_ssl_get_verify_result(&ssl));

  if (mbedtls_ssl_write(&ssl, (const unsigned char *)req, (size_t)ql) < 0) {
    Serial.println("net: request write FAIL"); freeall(); return -1;
  }

  // static response buffer (shared across calls; not re-entrant — single-threaded)
  static char raw[24576];
  int n = 0; uint32_t t0 = millis(); bool closed = false;
  while (!closed && millis() - t0 < 15000) {
    int rc = mbedtls_ssl_read(&ssl, (unsigned char *)raw + n, sizeof(raw) - 1 - n);
    if (rc > 0) { n += rc; continue; }
    if (rc == MBEDTLS_ERR_SSL_WANT_READ) { delay(10); continue; }
    if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == MBEDTLS_ERR_NET_CONN_RESET) { closed = true; break; }
    if (n > 0) break;   // partial body before a hard error: use it
    Serial.printf("net: read FAIL r=0x%04x\n", (unsigned)rc); break;
  }
  raw[n] = 0;
  char hdr[32]; sscanf(raw, "%31s", hdr);
  int sc = 0; sscanf(raw, "%*s %d", &sc);
  Serial.printf("net: %d bytes in %lums (%s %d)\n", n, millis() - t0, hdr, sc);
  mbedtls_ssl_close_notify(&ssl);
  freeall();

  char *p = strstr(raw, "\r\n\r\n");
  char *resp_body = p ? p + 4 : raw;
  // De-chunk ONLY if the server actually chunked it (Transfer-Encoding: chunked).
  // The sid handshake + open-meteo use Content-Length, and imhd's engine.io body
  // starts with the namespace digit "0" (e.g. `0{"sid":...}`) — an unconditional
  // de-chunk mis-reads that "0" as a zero-length chunk and drops the whole body.
  char *ce = strstr(raw, "chunked");
  bool chunked = p && ce && ce < p;
  char *o = out;
  if (chunked) {
    char *src = resp_body;
    while (*src) {
      unsigned long len = 0;
      while (isxdigit((unsigned char)*src)) {
        len = len * 16 + (isdigit((unsigned char)*src) ? *src - '0' : *src - 'a' + 10);
        src++;
      }
      if (*src == '\r') { src++; if (*src == '\n') src++; }
      if (len == 0) break;
      if ((size_t)(o - out) + len >= cap) break;
      memcpy(o, src, len); o += len; src += len;
      if (*src == '\r') src++;
      if (*src == '\n') src++;
    }
  } else {
    size_t off = (size_t)(resp_body - raw);
    size_t blen = (size_t)n > off ? (size_t)n - off : 0;
    if (blen >= cap) blen = cap - 1;
    if (blen > 0) { memcpy(o, resp_body, blen); o += blen; }
  }
  *o = 0;
  return (int)(o - out);
}

// Weather feed (open-meteo.com): one GET. Existing call sites stay unchanged.
int http_get(const char *path, char *body_out, size_t cap) {
  return _tls_request(WEATHER_HOST, path, nullptr, body_out, cap);
}

// imhd.sk transit feed: one GET (body==NULL) or POST (body!=NULL) to the
// Socket.IO polling endpoint. The path carries the sid query; the body is the
// engine.io payload ("40" / "3" / "42[\"tabStart\",[129,[\"*\"]]]").
int imhd_request(const char *path, const char *body, char *body_out, size_t cap) {
  return _tls_request(IMHD_HOST, path, body, body_out, cap);
}
