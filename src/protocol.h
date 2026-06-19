#pragma once
#include "conn.h"
#include "token.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const KeyStore *keys;
    bool            check_token;
    bool            v2_only;       /* accept only v2 (#v2#) tokens; reject legacy v1 */
    bool            compat_v03;    /* use RMBTv0.3 greeting + old CHUNKSIZE format */
    int             thread_num;
    const uint8_t  *source_ip;     /* 16-byte client address (IPv4-mapped IPv6), or NULL */
} ProtocolCfg;

/*
 * Handle one complete RMBT client session on `conn`.
 * The caller owns `conn` and closes it after this returns.
 */
void protocol_handle(Conn *conn, const ProtocolCfg *cfg);
