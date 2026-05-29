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
