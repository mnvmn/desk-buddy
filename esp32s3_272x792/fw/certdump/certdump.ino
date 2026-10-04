// Diagnostic v2: raw mbedTLS (proper RNG via esp_fill_random).
// 1) VERIFY_NONE  -> dump the exact chain the server sends to THIS device
// 2) VERIFY against ISRG Root X1  -> real verify error
// 3) VERIFY against YR1          -> real verify error
// 4) VERIFY against RootYR       -> real verify error
// 5) VERIFY against X1+YR1+RootYR-> real verify error
#include <Arduino.h>
#include <WiFi.h>
#include <net_config.h>
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "esp_random.h"
#include "diagca.h"
#include "diagder.h"
#include <string.h>

static int my_rng(void *p, unsigned char *out, size_t len) {
  (void)p; esp_fill_random(out, len); return 0;
}

static void dump_chain(const mbedtls_ssl_context *ssl) {
  const mbedtls_x509_crt *chain = mbedtls_ssl_get_peer_cert(ssl);
  int n = 0;
  while (chain && n < 6) {
    char ibuf[512]; memset(ibuf, 0, sizeof(ibuf));
    mbedtls_x509_crt_info(ibuf, sizeof(ibuf), "    ", chain);
    Serial.printf("  ---- CERT %d ----\n%s", n, ibuf);
    Serial.printf("  serial: ");
    for (size_t i = 0; i < 8 && i < chain->serial.len; i++) Serial.printf("%02x", chain->serial.p[i]);
    Serial.printf(" (len=%u)\n", (unsigned)chain->serial.len);
    chain = chain->next; n++;
  }
  Serial.printf("  CHAIN COUNT=%d\n", n);
}

static void do_handshake(const char *label, const char *ca_pem, int verify) {
  if (ca_pem) {
    size_t L = strlen(ca_pem);
    Serial.printf("[%s] ca len=%u head=[%.12s] tail=[%.*s]\n", label, (unsigned)L, ca_pem,
                  (int)(L > 12 ? L - 12 : 0), ca_pem + (L > 12 ? L - 12 : 0));
  }
  mbedtls_net_context net; mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
  mbedtls_x509_crt cacrt;
  mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
  mbedtls_x509_crt_init(&cacrt);

  int r = mbedtls_net_connect(&net, WEATHER_HOST, "443", MBEDTLS_NET_PROTO_TCP);
  Serial.printf("[%s] tcp r=%d\n", label, r);
  if (r) { Serial.println("  abort"); return; }

  mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, NULL);
  mbedtls_ssl_conf_rng(&conf, my_rng, NULL);
  mbedtls_ssl_conf_authmode(&conf, verify ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);
  if (verify) {
    r = mbedtls_x509_crt_parse(&cacrt, (const unsigned char *)ca_pem, strlen(ca_pem));
    Serial.printf("[%s] ca parse r=%d (0x%04x)\n", label, r, (unsigned)r);
    mbedtls_ssl_conf_ca_chain(&conf, &cacrt, NULL);
  }
  if (mbedtls_ssl_setup(&ssl, &conf) != 0) { Serial.printf("[%s] setup FAIL\n", label); return; }
  mbedtls_ssl_set_hostname(&ssl, WEATHER_HOST);
  mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);

  int h = mbedtls_ssl_handshake(&ssl);
  if (h) {
    char b[160]; mbedtls_strerror(h, b, sizeof(b));
    Serial.printf("[%s] handshake FAIL r=%d (0x%04x) (%s)\n", label, h, (unsigned)h, b);
  } else {
    Serial.printf("[%s] handshake OK (%s)\n", label, verify ? "CERT VERIFIED" : "verify off");
    if (!verify) dump_chain(&ssl);
    else {
      // show the verify result even on success for cross-checking
      unsigned long v = mbedtls_ssl_get_verify_result(&ssl);
      Serial.printf("  verify flags: 0x%08lx\n", v);
    }
  }
  mbedtls_ssl_close_notify(&ssl);
  mbedtls_net_free(&net); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&cacrt);
}

static void do_combo(const char *label) {
  mbedtls_net_context net; mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
  mbedtls_x509_crt cacrt;
  mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
  mbedtls_x509_crt_init(&cacrt);
  int r = mbedtls_net_connect(&net, WEATHER_HOST, "443", MBEDTLS_NET_PROTO_TCP);
  Serial.printf("[%s] tcp r=%d\n", label, r);
  if (r) return;
  mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, NULL);
  mbedtls_ssl_conf_rng(&conf, my_rng, NULL);
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  int n0 = mbedtls_x509_crt_parse(&cacrt, (const unsigned char *)CA_X1, strlen(CA_X1));
  int n1 = mbedtls_x509_crt_parse(&cacrt, (const unsigned char *)CA_YR1, strlen(CA_YR1));
  int n2 = mbedtls_x509_crt_parse(&cacrt, (const unsigned char *)CA_ROOTYR, strlen(CA_ROOTYR));
  Serial.printf("[%s] parse x1=%d yr1=%d rootyr=%d\n", label, n0, n1, n2);
  mbedtls_ssl_conf_ca_chain(&conf, &cacrt, NULL);
  mbedtls_ssl_setup(&ssl, &conf);
  mbedtls_ssl_set_hostname(&ssl, WEATHER_HOST);
  mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
  int h = mbedtls_ssl_handshake(&ssl);
  if (h) { char b[160]; mbedtls_strerror(h, b, sizeof(b)); Serial.printf("[%s] handshake FAIL r=%d (0x%04x) (%s)\n", label, h, (unsigned)h, b); }
  else { Serial.printf("[%s] handshake OK (CERT VERIFIED)\n", label);
        unsigned long v = mbedtls_ssl_get_verify_result(&ssl); Serial.printf("  verify flags: 0x%08lx\n", v); }
  mbedtls_ssl_close_notify(&ssl);
  mbedtls_net_free(&net); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&cacrt);
}

static void do_der_verified(const char *label) {
  // DER trust anchors (bypasses the broken PEM path): verify leaf<-YR1<-RootYR<-X1
  mbedtls_net_context net; mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
  mbedtls_x509_crt cacrt;
  mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
  mbedtls_x509_crt_init(&cacrt);
  int r = mbedtls_net_connect(&net, WEATHER_HOST, "443", MBEDTLS_NET_PROTO_TCP);
  Serial.printf("[%s] tcp r=%d\n", label, r);
  if (r) return;
  mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, NULL);
  mbedtls_ssl_conf_rng(&conf, my_rng, NULL);
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  int d0 = mbedtls_x509_crt_parse_der(&cacrt, DER_X1, DER_X1_len);
  int d1 = mbedtls_x509_crt_parse_der(&cacrt, DER_YR1, DER_YR1_len);
  Serial.printf("[%s] der parse x1=%d yr1=%d\n", label, d0, d1);
  mbedtls_ssl_conf_ca_chain(&conf, &cacrt, NULL);
  mbedtls_ssl_setup(&ssl, &conf);
  mbedtls_ssl_set_hostname(&ssl, WEATHER_HOST);
  mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
  int h = mbedtls_ssl_handshake(&ssl);
  if (h) { char b[160]; mbedtls_strerror(h, b, sizeof(b)); Serial.printf("[%s] handshake FAIL r=%d (0x%04x) (%s)\n", label, h, (unsigned)h, b); }
  else {
    Serial.printf("[%s] handshake OK (CERT VERIFIED via DER anchors)\n", label);
    unsigned long v = mbedtls_ssl_get_verify_result(&ssl);
    Serial.printf("  verify flags: 0x%08lx (%s)\n", v, v ? "HAS FLAGS" : "clean");
    mbedtls_ssl_close_notify(&ssl);
  }
  mbedtls_net_free(&net); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&cacrt);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("== cert diag v2 ==");
  // quick DER parse sanity (bypasses PEM parsing entirely)
  {
    mbedtls_x509_crt t; mbedtls_x509_crt_init(&t);
    int dr = mbedtls_x509_crt_parse_der(&t, DER_X1, DER_X1_len);
    char ib[256]; memset(ib,0,sizeof(ib));
    Serial.printf("[der-x1] parse r=%d (0x%04x)\n", dr, (unsigned)dr);
    if (dr == 0) mbedtls_x509_crt_info(ib, sizeof(ib), "  ", &t);
    if (dr == 0) Serial.print(ib);
    mbedtls_x509_crt_free(&t);
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PSWD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(100);
  if (WiFi.status() != WL_CONNECTED) { Serial.println("NO WIFI"); return; }
  Serial.printf("ip=%s\n", WiFi.localIP().toString().c_str());

  do_handshake("dump", nullptr, 0); delay(400);
  do_handshake("x1",   CA_X1,    1); delay(400);
  do_handshake("yr1",  CA_YR1,   1); delay(400);
  do_handshake("rootyr", CA_ROOTYR, 1); delay(400);
  do_combo("combo");
  do_der_verified("der-verified");
  Serial.println("== done ==");
}

void loop() { delay(1000); }
