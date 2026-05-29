#pragma once
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

typedef struct Conn Conn;

Conn *conn_new_plain(int fd, int timeout_secs);
Conn *conn_new_ssl(SSL_CTX *ctx, int fd, int timeout_secs);
void  conn_free(Conn *c);

/* Enable or disable WebSocket framing on this connection. */
void conn_set_ws(Conn *c, bool ws);

/* Direct BIO access needed for HTTP/WS upgrade in server.c. */
BIO *conn_bio(Conn *c);

/*
 * Read up to `n` bytes (plain) or one complete WS application message (WS).
 * Returns >0 bytes read, 0 on EOF/close, -1 on error.
 */
ssize_t conn_read(Conn *c, void *buf, size_t n);

/*
 * Write `n` bytes as a text frame (WS) or raw bytes (plain).
 * Returns n on success, -1 on error.
 */
ssize_t conn_write_text(Conn *c, const void *buf, size_t n);

/*
 * Write `n` bytes as a binary frame (WS) or raw bytes (plain).
 * Returns n on success, -1 on error.
 */
ssize_t conn_write_binary(Conn *c, const void *buf, size_t n);

/*
 * Read a newline-terminated line into `buf` (size includes NUL terminator).
 * Strips the trailing newline; NUL-terminates the result.
 * Returns chars placed in buf (excluding NUL), -1 on error/EOF/overflow.
 */
int conn_readline(Conn *c, char *buf, size_t size);

/* printf-style write as text. Returns 0 on success, -1 on error. */
int conn_writef(Conn *c, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
