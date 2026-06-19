#pragma once
#include <stdbool.h>
#include "config.h"

typedef struct {
    char keys  [CFG_MAX_KEYS][CFG_KEY_LEN];
    char labels[CFG_MAX_KEYS][CFG_KEY_LEN];
    int  count;
} KeyStore;

int  keystore_load(KeyStore *ks, const char *path);

/*
 * Validate token fields parsed from "TOKEN UUID_TIMESTAMP_HMAC".
 * Checks HMAC-SHA1 against all loaded keys and the acceptance time window.
 * Sleeps briefly when the client connects slightly before the token's start time.
 */
bool token_validate(const KeyStore *ks, int thread_num,
                    const char *uuid, const char *ts_str, const char *hmac);

/*
 * Validate a v2 token (open-rmbt-udp-ping schema): base64 of 16 bytes
 *   time(4 BE) || HMAC-SHA256(key, time)[0..8] || HMAC-SHA256(key, time||ip16)[0..4].
 * The time HMAC identifies the key, the IP HMAC binds the 16-byte client source
 * address (IPv4-mapped IPv6, network byte order), and the 32-bit time must fall in
 * the acceptance window. Returns false (and never sleeps) if source_ip is NULL.
 */
bool token_validate_v2(const KeyStore *ks, int thread_num,
                       const char *b64, const uint8_t *source_ip);
