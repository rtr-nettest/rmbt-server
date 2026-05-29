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
