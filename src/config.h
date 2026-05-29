#pragma once
#include <stdint.h>

#define CFG_GREETING        "RMBTv1.3.5"
#define CFG_GREETING_V03    "RMBTv0.3"

#define CFG_MAX_THREADS     200
#define CFG_QUEUE_DEPTH     50
#define CFG_LISTEN_BACKLOG  10
#define CFG_TIMEOUT_S       30

#define CFG_LINE_MAX        1024

#define CFG_CHUNK_DEFAULT   4096u
#define CFG_CHUNK_MIN       4096u
#define CFG_CHUNK_MAX       4194304u
#define CFG_CHUNK_MAX_COUNT 300000u
#define CFG_TEST_SECS_MAX   30

#define CFG_TOKEN_EARLY_S   20
#define CFG_TOKEN_LATE_S    90

#define CFG_MAX_KEYS        128
#define CFG_KEY_LEN         256

#define CFG_RANDOM_MAX      (1200UL * 1024 * 1024)
