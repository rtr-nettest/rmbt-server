#include "conn.h"
#include "ws.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

struct Conn {
    BIO  *bio;
    bool  ws;
    bool  owns_ssl;  /* true → BIO_free_all on close; false → BIO_free */
};

static void apply_socket_opts(int fd, int timeout_secs)
{
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct timeval tv = { .tv_sec = timeout_secs, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

Conn *conn_new_plain(int fd, int timeout_secs)
{
    apply_socket_opts(fd, timeout_secs);

    BIO *bio = BIO_new(BIO_s_fd());
    if (!bio) return NULL;
    BIO_set_fd(bio, fd, BIO_CLOSE);

    Conn *c = calloc(1, sizeof(*c));
    if (!c) { BIO_free(bio); return NULL; }
    c->bio      = bio;
    c->owns_ssl = false;
    return c;
}

Conn *conn_new_ssl(SSL_CTX *ctx, int fd, int timeout_secs)
{
    apply_socket_opts(fd, timeout_secs);

    BIO *bio = BIO_new_ssl(ctx, 0);
    if (!bio) return NULL;

    SSL *ssl;
    BIO_get_ssl(bio, &ssl);
    SSL_set_fd(ssl, fd);

    Conn *c = calloc(1, sizeof(*c));
    if (!c) { BIO_free_all(bio); return NULL; }
    c->bio      = bio;
    c->owns_ssl = true;
    return c;
}

void conn_free(Conn *c)
{
    if (!c) return;
    if (c->owns_ssl)
        BIO_free_all(c->bio);
    else
        BIO_free(c->bio);
    free(c);
}

void conn_set_ws(Conn *c, bool ws) { c->ws = ws; }

BIO *conn_bio(Conn *c) { return c->bio; }

ssize_t conn_read(Conn *c, void *buf, size_t n)
{
    if (c->ws)
        return ws_read_message(c->bio, buf, n);

    int r = BIO_read(c->bio, buf, (int)n);
    if (r == 0) return 0;
    if (r < 0)  return -1;
    return (ssize_t)r;
}

ssize_t conn_write_text(Conn *c, const void *buf, size_t n)
{
    if (c->ws)
        return ws_write_text(c->bio, buf, n);

    int r = BIO_write(c->bio, buf, (int)n);
    return (r == (int)n) ? (ssize_t)n : -1;
}

ssize_t conn_write_binary(Conn *c, const void *buf, size_t n)
{
    if (c->ws)
        return ws_write_binary(c->bio, buf, n);

    int r = BIO_write(c->bio, buf, (int)n);
    return (r == (int)n) ? (ssize_t)n : -1;
}

int conn_readline(Conn *c, char *buf, size_t size)
{
    if (size < 2) return -1;

    size_t total = 0;

    for (;;) {
        ssize_t r = conn_read(c, buf + total, size - 1 - total);
        if (r <= 0) return -1;

        char *nl = memchr(buf + total, '\n', (size_t)r);
        total += (size_t)r;

        if (nl) {
            *nl = '\0';
            if (nl > buf && *(nl - 1) == '\r')
                *(nl - 1) = '\0';
            return (int)(nl - buf);
        }

        if (total >= size - 1) {
            buf[size - 1] = '\0';
            return -1;  /* line too long */
        }
    }
}

int conn_writef(Conn *c, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0 || (size_t)n >= sizeof(buf)) return -1;
    return conn_write_text(c, buf, (size_t)n) == (ssize_t)n ? 0 : -1;
}
