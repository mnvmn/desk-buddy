// ---- network: raw mbedTLS -----------------------------------------------------
#include "panel_net.h"
#include <net_config.h>
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "esp_random.h"
#include "le_dercainfo.h"  // LE_ANCHORS: Let's Encrypt trust roots as DER

// ---- network: raw mbedTLS -----------------------------------------------------
static int le_rng(void *p, unsigned char *out, size_t len) {
  (void)p; esp_fill_random(out, len); return 0;
}

// GET https://WEATHER_HOST/path -> body (de-chunked). Returns bytes or -1.
int http_get(const char *path, char *body_out, size_t cap) {
  char req[512];
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

