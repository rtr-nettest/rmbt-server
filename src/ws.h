#pragma once
#include <openssl/bio.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    WS_UPGRADE_NONE = 0,  /* not an HTTP request or upgrade failed */
    WS_UPGRADE_WS   = 1,  /* RFC 6455 WebSocket; handshake sent */
    WS_UPGRADE_RMBT = 2,  /* plain HTTP 101 Upgrade: RMBT */
} WsUpgradeResult;

/*
 * Parse the HTTP request already read into `buf` (len bytes).
 * Write the appropriate 101 response to `bio` and return the upgrade type.
 */
WsUpgradeResult ws_do_upgrade(BIO *bio, const char *buf, size_t len);

/* Write `data` as a single WebSocket text frame (server→client, unmasked). */
ssize_t ws_write_text(BIO *bio, const void *data, size_t len);

/* Write `data` as a single WebSocket binary frame (server→client, unmasked). */
ssize_t ws_write_binary(BIO *bio, const void *data, size_t len);

/*
 * Read one complete WebSocket application message into `buf` (capacity `cap`).
 * Reassembles continuation frames and unmasks the payload.
 * Returns message length, 0 on close frame, -1 on error.
 */
ssize_t ws_read_message(BIO *bio, void *buf, size_t cap);
