#include "token.h"
#include "config.h"

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <syslog.h>

int keystore_load(KeyStore *ks, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        syslog(LOG_ERR, "open key file '%s': %m", path);
        return -1;
    }

    ks->count = 0;
    char line[CFG_KEY_LEN + 128];

    while (ks->count < CFG_MAX_KEYS && fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';
        if (len < 5 || line[0] == '#')
            continue;

        char *sp = strchr(line, ' ');
        if (sp) {
            *sp = '\0';
            snprintf(ks->labels[ks->count], CFG_KEY_LEN, "%s", sp + 1);
        } else {
            ks->labels[ks->count][0] = '\0';
        }
        snprintf(ks->keys[ks->count], CFG_KEY_LEN, "%s", line);
        ks->count++;
    }

    fclose(f);
    syslog(LOG_INFO, "loaded %d secret key(s)", ks->count);
    return ks->count;
}

static bool hmac_sha1_b64(const char *key, const char *msg,
                           char *out, size_t out_size)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int  md_len = sizeof(md);

    if (!HMAC(EVP_sha1(), key, (int)strlen(key),
              (const unsigned char *)msg, strlen(msg), md, &md_len))
        return false;

    BIO *b64  = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, md, (int)md_len);
    BIO_flush(b64);

    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);

    bool ok = bptr->length + 1 <= out_size;
    if (ok) {
        memcpy(out, bptr->data, bptr->length);
        out[bptr->length] = '\0';
    }
    BIO_free_all(b64);
    return ok;
}

bool token_validate(const KeyStore *ks, int thread_num,
                    const char *uuid, const char *ts_str, const char *hmac)
{
    char msg[80];
    if (snprintf(msg, sizeof(msg), "%s_%s", uuid, ts_str) >= (int)sizeof(msg))
        return false;

    char calc[64];
    bool hmac_ok = false;

    for (int i = 0; i < ks->count; i++) {
        if (!hmac_sha1_b64(ks->keys[i], msg, calc, sizeof(calc)))
            continue;
        if (strcmp(calc, hmac) == 0) {
            syslog(LOG_INFO, "[THR %d] token accepted by key '%s'",
                   thread_num, ks->labels[i]);
            hmac_ok = true;
            break;
        }
    }

    if (!hmac_ok) {
        syslog(LOG_ERR, "[THR %d] token HMAC rejected: uuid=%s", thread_num, uuid);
        return false;
    }

    time_t now        = time(NULL);
    long   start_time = atol(ts_str);

    if (start_time - CFG_TOKEN_EARLY_S > (long)now) {
        syslog(LOG_ERR, "[THR %d] client %ld s too early",
               thread_num, start_time - (long)now);
        return false;
    }
    if (start_time + CFG_TOKEN_LATE_S < (long)now) {
        syslog(LOG_ERR, "[THR %d] client %ld s too late",
               thread_num, (long)now - start_time);
        return false;
    }

    if (start_time > (long)now) {
        syslog(LOG_DEBUG, "[THR %d] client %ld s early, sleeping",
               thread_num, start_time - (long)now);
        struct timespec ts = { .tv_sec = start_time - (long)now, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
    }

    return true;
}

/* ── v2 token (open-rmbt-udp-ping schema: SHA256, IP + time bound) ─────────── */

/* Decode standard base64 in `in` into `out`; returns the decoded byte count, or -1. */
static int b64_decode(const char *in, unsigned char *out, int out_size)
{
    BIO *b64 = BIO_new(BIO_f_base64());
    if (!b64) return -1;
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO *mem = BIO_new_mem_buf(in, (int)strlen(in));
    if (!mem) { BIO_free(b64); return -1; }
    mem = BIO_push(b64, mem);
    int n = BIO_read(mem, out, out_size);
    BIO_free_all(mem);
    return n;
}

/* HMAC-SHA256(key, data) into md (>= EVP_MAX_MD_SIZE). True on success. */
static bool hmac_sha256(const char *key, const unsigned char *data, size_t len,
                        unsigned char md[EVP_MAX_MD_SIZE])
{
    unsigned int md_len = 0;
    if (!HMAC(EVP_sha256(), key, (int)strlen(key), data, len, md, &md_len))
        return false;
    return md_len >= 32;
}

bool token_validate_v2(const KeyStore *ks, int thread_num,
                       const char *b64, const uint8_t *source_ip)
{
    /* base64 → 16 bytes: time(4) | packet_hash(8) | ip_hash(4) */
    unsigned char tok[24];   /* >16 so an over-long token is rejected, not truncated */
    int n = b64_decode(b64, tok, (int)sizeof(tok));
    if (n != 16) {
        syslog(LOG_ERR, "[THR %d] v2 token: bad base64 (decoded %d, want 16)",
               thread_num, n);
        return false;
    }
    const unsigned char *time_bytes  = tok;        /* 4 */
    const unsigned char *packet_hash = tok + 4;    /* 8 */
    const unsigned char *ip_hash     = tok + 12;   /* 4 */

    /* v2 binds the source IP, so we must know where the connection came from. */
    if (!source_ip) {
        syslog(LOG_ERR, "[THR %d] v2 token: source IP unavailable", thread_num);
        return false;
    }

    bool matched = false;
    for (int i = 0; i < ks->count; i++) {
        unsigned char md[EVP_MAX_MD_SIZE];

        /* time HMAC identifies the key */
        if (!hmac_sha256(ks->keys[i], time_bytes, 4, md))
            continue;
        if (memcmp(md, packet_hash, 8) != 0)
            continue;

        /* this is the key → the IP HMAC must bind the connecting address */
        unsigned char ipmsg[20];
        memcpy(ipmsg,     time_bytes, 4);
        memcpy(ipmsg + 4, source_ip, 16);
        if (!hmac_sha256(ks->keys[i], ipmsg, sizeof(ipmsg), md))
            break;
        if (memcmp(md, ip_hash, 4) == 0) {
            syslog(LOG_INFO, "[THR %d] v2 token accepted by key '%s'",
                   thread_num, ks->labels[i]);
            matched = true;
        } else {
            syslog(LOG_ERR, "[THR %d] v2 token: source IP does not match token",
                   thread_num);
            return false;
        }
        break;
    }
    if (!matched) {
        syslog(LOG_ERR, "[THR %d] v2 token: HMAC rejected", thread_num);
        return false;
    }

    /* Reconstruct the 32-bit start time to the value nearest "now" (periodicity
     * 2^32 s), then apply the same acceptance window as v1. */
    uint32_t t32 = ((uint32_t)time_bytes[0] << 24) | ((uint32_t)time_bytes[1] << 16)
                 | ((uint32_t)time_bytes[2] <<  8) |  (uint32_t)time_bytes[3];
    int64_t now   = (int64_t)time(NULL);
    int64_t start = (now & ~INT64_C(0xFFFFFFFF)) | (int64_t)t32;
    if      (start - now > INT64_C(0x80000000)) start -= INT64_C(0x100000000);
    else if (now - start > INT64_C(0x80000000)) start += INT64_C(0x100000000);

    if (start - CFG_TOKEN_EARLY_S > now) {
        syslog(LOG_ERR, "[THR %d] v2 client %lld s too early",
               thread_num, (long long)(start - now));
        return false;
    }
    if (start + CFG_TOKEN_LATE_S < now) {
        syslog(LOG_ERR, "[THR %d] v2 client %lld s too late",
               thread_num, (long long)(now - start));
        return false;
    }
    if (start > now) {
        syslog(LOG_DEBUG, "[THR %d] v2 client %lld s early, sleeping",
               thread_num, (long long)(start - now));
        struct timespec tsleep = { .tv_sec = (time_t)(start - now), .tv_nsec = 0 };
        nanosleep(&tsleep, NULL);
    }
    return true;
}
