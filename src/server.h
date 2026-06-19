#pragma once
#include <openssl/ssl.h>
#include <stdint.h>
#include <stdbool.h>
#include "token.h"

typedef struct {
    struct {
        uint8_t  addr[16];  /* IPv6 or IPv4-mapped, network byte order */
        uint16_t port;
        bool     use_ssl;
        int      fd;        /* set by server_run after bind */
    } listens[32];
    int      num_listens;

    SSL_CTX *ssl_ctx;       /* NULL if no TLS listeners */
    int      num_threads;
    bool     use_http;      /* -w: expect HTTP/WebSocket upgrade */
    bool     check_token;
    bool     v2_only;       /* accept only v2 (#v2#) tokens; reject legacy v1 */
    bool     compat_v03;
    int      timeout_s;

    const KeyStore *keys;
} ServerCfg;

/* Bind all listen sockets, start thread pool, run accept loop (blocks). */
int server_run(ServerCfg *cfg);

/* Signal graceful shutdown from a signal handler. */
void server_shutdown(void);
