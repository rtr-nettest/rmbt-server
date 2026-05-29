#include "protocol.h"
#include "config.h"
#include "random.h"

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <time.h>

/* ── monotonic timing ────────────────────────────────────────────────────── */

static void ts_now(struct timespec *ts)
{
    if (clock_gettime(CLOCK_MONOTONIC, ts) != 0) {
        syslog(LOG_ERR, "clock_gettime: %m");
        ts->tv_sec = 0; ts->tv_nsec = 0;
    }
}

static int64_t ts_elapsed_ns(const struct timespec *start)
{
    struct timespec now;
    ts_now(&now);
    return (int64_t)(now.tv_sec  - start->tv_sec)  * INT64_C(1000000000)
         + (int64_t)(now.tv_nsec - start->tv_nsec);
}

/* ── GETTIME ─────────────────────────────────────────────────────────────── */

static void cmd_gettime(Conn *conn, int secs, uint8_t *chunk, uint32_t chunk_size)
{
    int64_t max_ns = (int64_t)secs * INT64_C(1000000000);
    size_t  roff   = 0;
    size_t  rsize  = random_size();

    struct timespec t0;
    ts_now(&t0);

    for (;;) {
        if (roff + chunk_size > rsize) roff = 0;
        memcpy(chunk, random_ptr(roff, chunk_size), chunk_size);
        roff += chunk_size;

        chunk[chunk_size - 1] = (ts_elapsed_ns(&t0) >= max_ns) ? 0xFF : 0x00;

        if (conn_write_binary(conn, chunk, chunk_size) != (ssize_t)chunk_size)
            return;

        if (chunk[chunk_size - 1] == 0xFF) break;
    }

    char line[CFG_LINE_MAX];
    if (conn_readline(conn, line, sizeof(line)) <= 0) return;
    if (strncmp(line, "OK", 2) != 0) { conn_writef(conn, "ERR\n"); return; }

    conn_writef(conn, "TIME %" PRId64 "\n", ts_elapsed_ns(&t0));
}

/* ── GETCHUNKS ───────────────────────────────────────────────────────────── */

static void cmd_getchunks(Conn *conn, uint32_t count,
                           uint8_t *chunk, uint32_t chunk_size)
{
    if (count == 0 || count > CFG_CHUNK_MAX_COUNT) {
        conn_writef(conn, "ERR\n");
        return;
    }

    size_t roff  = 0;
    size_t rsize = random_size();

    struct timespec t0;
    ts_now(&t0);

    for (uint32_t i = 0; i < count; i++) {
        if (roff + chunk_size > rsize) roff = 0;
        memcpy(chunk, random_ptr(roff, chunk_size), chunk_size);
        roff += chunk_size;

        chunk[chunk_size - 1] = (i + 1 == count) ? 0xFF : 0x00;

        if (conn_write_binary(conn, chunk, chunk_size) != (ssize_t)chunk_size)
            return;
    }

    char line[CFG_LINE_MAX];
    if (conn_readline(conn, line, sizeof(line)) <= 0) return;
    if (strncmp(line, "OK", 2) != 0) { conn_writef(conn, "ERR\n"); return; }

    conn_writef(conn, "TIME %" PRId64 "\n", ts_elapsed_ns(&t0));
}

/* ── PUT / PUTNORESULT ───────────────────────────────────────────────────── */

static void cmd_put(Conn *conn, uint8_t *chunk, uint32_t chunk_size,
                    bool send_progress)
{
    if (conn_writef(conn, "OK\n") < 0) return;

    struct timespec t0;
    ts_now(&t0);

    uint8_t term_byte   = 0;
    long    total_read  = 0;
    int64_t last_report = -1;

    for (;;) {
        ssize_t r = conn_read(conn, chunk, chunk_size);
        if (r <= 0) break;

        /* The terminal byte sits at offset (chunk_size-1) within each chunk.
         * Calculate its position within the current read buffer. */
        long pos = (long)(chunk_size - 1) - (long)(total_read % chunk_size);
        if (r > pos)
            term_byte = chunk[pos];
        total_read += (long)r;

        if (send_progress) {
            int64_t now_ns = ts_elapsed_ns(&t0);
            if (last_report < 0 || now_ns - last_report > INT64_C(1000000)) {
                last_report = now_ns;
                if (conn_writef(conn, "TIME %" PRId64 " BYTES %ld\n",
                                now_ns, total_read) < 0)
                    return;
            }
        }

        if (term_byte == 0xFF) break;
    }

    if (term_byte != 0xFF) return;

    conn_writef(conn, "TIME %" PRId64 "\n", ts_elapsed_ns(&t0));
}

/* ── PING ────────────────────────────────────────────────────────────────── */

static void cmd_ping(Conn *conn)
{
    struct timespec t0;
    ts_now(&t0);

    if (conn_writef(conn, "PONG\n") < 0) return;

    char line[CFG_LINE_MAX];
    if (conn_readline(conn, line, sizeof(line)) <= 0) return;
    if (strncmp(line, "OK", 2) != 0) { conn_writef(conn, "ERR\n"); return; }

    conn_writef(conn, "TIME %" PRId64 "\n", ts_elapsed_ns(&t0));
}

/* ── main entry ──────────────────────────────────────────────────────────── */

void protocol_handle(Conn *conn, const ProtocolCfg *cfg)
{
    int thr = cfg->thread_num;

    const char *greeting = cfg->compat_v03 ? CFG_GREETING_V03 : CFG_GREETING;
    if (conn_writef(conn, "%s\n", greeting) < 0) return;
    if (conn_writef(conn, "ACCEPT TOKEN QUIT\n") < 0) return;

    char line[CFG_LINE_MAX];
    if (conn_readline(conn, line, sizeof(line)) <= 0) {
        syslog(LOG_INFO, "[THR %d] no TOKEN received", thr);
        return;
    }

    char uuid[40], ts_str[16], hmac[56];
    if (sscanf(line, "TOKEN %36[0-9a-f-]_%12[0-9]_%50[a-zA-Z0-9+/=]",
               uuid, ts_str, hmac) != 3) {
        syslog(LOG_ERR, "[THR %d] bad TOKEN syntax: %.80s", thr, line);
        return;
    }

    if (cfg->check_token) {
        if (!token_validate(cfg->keys, thr, uuid, ts_str, hmac)) {
            syslog(LOG_ERR, "[THR %d] token rejected", thr);
            return;
        }
        syslog(LOG_INFO, "[THR %d] valid token; uuid=%s", thr, uuid);
    } else {
        syslog(LOG_INFO, "[THR %d] token NOT CHECKED; uuid=%s", thr, uuid);
    }

    if (conn_writef(conn, "OK\n") < 0) return;

    if (cfg->compat_v03)
        conn_writef(conn, "CHUNKSIZE %u\n", CFG_CHUNK_DEFAULT);
    else
        conn_writef(conn, "CHUNKSIZE %u %u %u\n",
                    CFG_CHUNK_DEFAULT, CFG_CHUNK_MIN, CFG_CHUNK_MAX);

    uint32_t chunk_size = CFG_CHUNK_DEFAULT;
    uint8_t *chunk = malloc(CFG_CHUNK_MAX);
    if (!chunk) {
        syslog(LOG_ERR, "[THR %d] malloc chunk buffer: %m", thr);
        return;
    }

    for (;;) {
        if (conn_writef(conn, "ACCEPT GETCHUNKS GETTIME PUT PUTNORESULT PING QUIT\n") < 0)
            break;

        if (conn_readline(conn, line, sizeof(line)) <= 0) break;

        char cmd[64], arg1[16], arg2[16];
        int  parts = sscanf(line, "%50s %12s %12s", cmd, arg1, arg2);
        if (parts < 1) break;

        if (strcmp(cmd, "GETTIME") == 0 && parts >= 2) {
            if (parts == 3) {
                uint32_t cs = (uint32_t)strtoul(arg2, NULL, 10);
                if (cs < CFG_CHUNK_MIN || cs > CFG_CHUNK_MAX)
                    { conn_writef(conn, "ERR\n"); continue; }
                chunk_size = cs;
            }
            int secs = atoi(arg1);
            if (secs <= 0 || secs > CFG_TEST_SECS_MAX)
                { conn_writef(conn, "ERR\n"); continue; }
            cmd_gettime(conn, secs, chunk, chunk_size);

        } else if (strcmp(cmd, "GETCHUNKS") == 0 && parts >= 2) {
            if (parts == 3) {
                uint32_t cs = (uint32_t)strtoul(arg2, NULL, 10);
                if (cs < CFG_CHUNK_MIN || cs > CFG_CHUNK_MAX)
                    { conn_writef(conn, "ERR\n"); continue; }
                chunk_size = cs;
            }
            uint32_t count = (uint32_t)strtoul(arg1, NULL, 10);
            cmd_getchunks(conn, count, chunk, chunk_size);

        } else if (strcmp(cmd, "PUT") == 0 || strcmp(cmd, "PUTNORESULT") == 0) {
            if (parts >= 2) {
                uint32_t cs = (uint32_t)strtoul(arg1, NULL, 10);
                if (cs < CFG_CHUNK_MIN || cs > CFG_CHUNK_MAX)
                    { conn_writef(conn, "ERR\n"); continue; }
                chunk_size = cs;
            }
            cmd_put(conn, chunk, chunk_size, strcmp(cmd, "PUT") == 0);

        } else if (strcmp(cmd, "PING") == 0 && parts == 1) {
            cmd_ping(conn);

        } else if (strcmp(cmd, "QUIT") == 0) {
            conn_writef(conn, "BYE\n");
            break;

        } else {
            conn_writef(conn, "ERR\n");
        }
    }

    free(chunk);
}
