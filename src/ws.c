#include "ws.h"

#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <syslog.h>

static const char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* ── helpers ─────────────────────────────────────────────────────────────── */

static bool bio_read_full(BIO *bio, void *buf, size_t n)
{
    char  *p   = buf;
    size_t got = 0;
    while (got < n) {
        int r = BIO_read(bio, p + got, (int)(n - got));
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

static int b64_encode(const unsigned char *in, size_t in_len,
                      char *out, size_t out_cap)
{
    BIO *b64  = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, in, (int)in_len);
    BIO_flush(b64);

    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);
    int r = -1;
    if (bptr->length + 1 <= out_cap) {
        memcpy(out, bptr->data, bptr->length);
        out[bptr->length] = '\0';
        r = (int)bptr->length;
    }
    BIO_free_all(b64);
    return r;
}

/* ── HTTP header helpers ─────────────────────────────────────────────────── */

static bool header_has(const char *buf, const char *field, const char *value)
{
    const char *p    = buf;
    size_t      flen = strlen(field);
    while ((p = strstr(p, "\r\n"))) {
        p += 2;
        if (strncasecmp(p, field, flen) == 0) {
            const char *v = p + flen;
            while (*v == ' ') v++;
            if (strncasecmp(v, value, strlen(value)) == 0)
                return true;
        }
    }
    return false;
}

static bool extract_ws_key(const char *buf, char *out, size_t cap)
{
    static const char field[] = "Sec-WebSocket-Key: ";
    const char       *p       = buf;
    while ((p = strstr(p, "\r\n"))) {
        p += 2;
        if (strncasecmp(p, field, sizeof(field) - 1) == 0) {
            p += sizeof(field) - 1;
            size_t n = strcspn(p, "\r\n");
            if (n + 1 > cap) return false;
            memcpy(out, p, n);
            out[n] = '\0';
            return true;
        }
    }
    return false;
}

/* ── upgrade ─────────────────────────────────────────────────────────────── */

WsUpgradeResult ws_do_upgrade(BIO *bio, const char *buf, size_t len)
{
    (void)len;
    if (strncmp(buf, "GET ", 4) != 0)
        return WS_UPGRADE_NONE;

    bool is_ws   = header_has(buf, "Upgrade: ", "websocket");
    bool is_rmbt = header_has(buf, "Upgrade: ", "RMBT");

    if (!is_ws && !is_rmbt)
        return WS_UPGRADE_NONE;

    if (is_rmbt) {
        static const char resp[] =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Connection: Upgrade\r\n"
            "Upgrade: RMBT\r\n\r\n";
        BIO_write(bio, resp, (int)(sizeof(resp) - 1));
        BIO_flush(bio);
        return WS_UPGRADE_RMBT;
    }

    char key[128];
    if (!extract_ws_key(buf, key, sizeof(key)))
        return WS_UPGRADE_NONE;

    char combined[256];
    int  clen = snprintf(combined, sizeof(combined), "%s%s", key, WS_GUID);
    if (clen <= 0 || (size_t)clen >= sizeof(combined))
        return WS_UPGRADE_NONE;

    unsigned char sha[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)combined, (size_t)clen, sha);

    char accept[64];
    if (b64_encode(sha, sizeof(sha), accept, sizeof(accept)) < 0)
        return WS_UPGRADE_NONE;

    char resp[512];
    int  rlen = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept);
    if (rlen <= 0) return WS_UPGRADE_NONE;
    BIO_write(bio, resp, rlen);
    BIO_flush(bio);
    return WS_UPGRADE_WS;
}

/* ── frame writing ───────────────────────────────────────────────────────── */

static ssize_t ws_write_frame(BIO *bio, uint8_t opcode,
                               const void *data, size_t len)
{
    uint8_t hdr[10];
    size_t  hdr_len;

    hdr[0] = 0x80u | (opcode & 0x0Fu);  /* FIN + opcode */

    if (len <= 125) {
        hdr[1]  = (uint8_t)len;
        hdr_len = 2;
    } else if (len <= 65535) {
        hdr[1]  = 126;
        hdr[2]  = (uint8_t)(len >> 8);
        hdr[3]  = (uint8_t)(len);
        hdr_len = 4;
    } else {
        hdr[1]  = 127;
        uint64_t l = (uint64_t)len;
        for (int i = 0; i < 8; i++)
            hdr[2 + i] = (uint8_t)(l >> (56 - i * 8));
        hdr_len = 10;
    }

    if (BIO_write(bio, hdr, (int)hdr_len) != (int)hdr_len) return -1;
    if (len > 0 && BIO_write(bio, data, (int)len) != (int)len) return -1;
    BIO_flush(bio);
    return (ssize_t)len;
}

ssize_t ws_write_text(BIO *bio, const void *data, size_t len)
{
    return ws_write_frame(bio, 0x01, data, len);
}

ssize_t ws_write_binary(BIO *bio, const void *data, size_t len)
{
    return ws_write_frame(bio, 0x02, data, len);
}

/* ── frame reading ───────────────────────────────────────────────────────── */

ssize_t ws_read_message(BIO *bio, void *buf, size_t cap)
{
    size_t total = 0;

    for (;;) {
        uint8_t hdr[2];
        if (!bio_read_full(bio, hdr, 2)) return -1;

        bool    fin    = (hdr[0] & 0x80u) != 0;
        uint8_t opcode = hdr[0] & 0x0Fu;
        bool    masked = (hdr[1] & 0x80u) != 0;
        uint64_t plen  = hdr[1] & 0x7Fu;

        if (plen == 126) {
            uint8_t ext[2];
            if (!bio_read_full(bio, ext, 2)) return -1;
            plen = ((uint64_t)ext[0] << 8) | ext[1];
        } else if (plen == 127) {
            uint8_t ext[8];
            if (!bio_read_full(bio, ext, 8)) return -1;
            plen = 0;
            for (int i = 0; i < 8; i++) plen = (plen << 8) | ext[i];
        }

        uint8_t mask[4] = {0};
        if (masked && !bio_read_full(bio, mask, 4)) return -1;

        if (opcode == 0x08) return 0;  /* close frame */

        if (total + (size_t)plen > cap) {
            syslog(LOG_ERR, "ws_read_message: message too large (%llu > %zu)",
                   (unsigned long long)(total + plen), cap);
            return -1;
        }

        if (!bio_read_full(bio, (char *)buf + total, (size_t)plen)) return -1;

        if (masked) {
            for (size_t i = 0; i < (size_t)plen; i++)
                ((uint8_t *)buf)[total + i] ^= mask[i & 3];
        }
        total += (size_t)plen;

        if (fin) break;
    }

    return (ssize_t)total;
}
