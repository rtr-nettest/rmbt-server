#pragma once
#include "conn.h"
#include "token.h"
#include <stdbool.h>

typedef struct {
    const KeyStore *keys;
    bool            check_token;
    bool            compat_v03;   /* use RMBTv0.3 greeting + old CHUNKSIZE format */
    int             thread_num;
} ProtocolCfg;

/*
 * Handle one complete RMBT client session on `conn`.
 * The caller owns `conn` and closes it after this returns.
 */
void protocol_handle(Conn *conn, const ProtocolCfg *cfg);
