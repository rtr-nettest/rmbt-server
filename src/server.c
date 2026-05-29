#include "server.h"
#include "config.h"
#include "conn.h"
#include "protocol.h"
#include "ws.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <stdatomic.h>
#include <pthread.h>

/* ── shutdown ────────────────────────────────────────────────────────────── */

static atomic_int g_shutdown = 0;

void server_shutdown(void) { atomic_store(&g_shutdown, 1); }

/* ── accept queue (ring buffer) ──────────────────────────────────────────── */

typedef struct {
    int fd;
    int listen_idx;
} QueueEntry;

typedef struct {
    QueueEntry      entries[CFG_QUEUE_DEPTH];
    int             head;  /* index of oldest entry */
    int             size;  /* number of live entries */
    pthread_mutex_t mu;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} AcceptQueue;

static AcceptQueue g_queue = {
    .mu        = PTHREAD_MUTEX_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
    .not_full  = PTHREAD_COND_INITIALIZER,
};

static void queue_push(int fd, int listen_idx)
{
    pthread_mutex_lock(&g_queue.mu);
    while (!atomic_load(&g_shutdown) && g_queue.size == CFG_QUEUE_DEPTH)
        pthread_cond_wait(&g_queue.not_full, &g_queue.mu);

    if (!atomic_load(&g_shutdown)) {
        int idx = (g_queue.head + g_queue.size++) % CFG_QUEUE_DEPTH;
        g_queue.entries[idx].fd         = fd;
        g_queue.entries[idx].listen_idx = listen_idx;
        pthread_cond_signal(&g_queue.not_empty);
    } else {
        close(fd);
    }
    pthread_mutex_unlock(&g_queue.mu);
}

static bool queue_pop(QueueEntry *out)
{
    pthread_mutex_lock(&g_queue.mu);
    while (!atomic_load(&g_shutdown) && g_queue.size == 0)
        pthread_cond_wait(&g_queue.not_empty, &g_queue.mu);

    if (atomic_load(&g_shutdown)) {
        pthread_mutex_unlock(&g_queue.mu);
        return false;
    }

    bool was_full = (g_queue.size == CFG_QUEUE_DEPTH);
    *out       = g_queue.entries[g_queue.head];
    g_queue.head = (g_queue.head + 1) % CFG_QUEUE_DEPTH;
    g_queue.size--;
    if (was_full) pthread_cond_signal(&g_queue.not_full);

    pthread_mutex_unlock(&g_queue.mu);
    return true;
}

/* ── per-connection handling ─────────────────────────────────────────────── */

static void anonymize_ip(char *ip)
{
    char *last = strrchr(ip, '.');
    if (!last) last = strrchr(ip, ':');
    if (last) *last = '\0';
}

static void handle_accepted(int fd, int listen_idx,
                             int thread_num, const ServerCfg *cfg)
{
    struct sockaddr_in6 peer;
    socklen_t plen = sizeof(peer);
    if (getpeername(fd, (struct sockaddr *)&peer, &plen) == 0) {
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &peer.sin6_addr, ip, sizeof(ip));
        anonymize_ip(ip);
        syslog(LOG_INFO, "[THR %d] connection from [%s]:%d",
               thread_num, ip, ntohs(peer.sin6_port));
    }

    bool  use_ssl = cfg->listens[listen_idx].use_ssl;
    Conn *conn    = use_ssl
        ? conn_new_ssl(cfg->ssl_ctx, fd, cfg->timeout_s)
        : conn_new_plain(fd, cfg->timeout_s);

    if (!conn) {
        syslog(LOG_ERR, "[THR %d] conn_new failed", thread_num);
        close(fd);
        return;
    }

    if (cfg->use_http) {
        /* Read the HTTP upgrade request.  Headers normally arrive in one
         * segment; using a generous buffer covers virtually all clients. */
        char buf[CFG_LINE_MAX * 8];
        int  r = BIO_read(conn_bio(conn), buf, (int)sizeof(buf) - 1);
        if (r <= 0) {
            syslog(LOG_INFO, "[THR %d] no HTTP request received", thread_num);
            conn_free(conn);
            return;
        }
        buf[r] = '\0';

        WsUpgradeResult upg = ws_do_upgrade(conn_bio(conn), buf, (size_t)r);
        if (upg == WS_UPGRADE_NONE) {
            syslog(LOG_INFO, "[THR %d] HTTP upgrade failed", thread_num);
            conn_free(conn);
            return;
        }
        if (upg == WS_UPGRADE_WS)
            conn_set_ws(conn, true);
    }

    ProtocolCfg pcfg = {
        .keys        = cfg->keys,
        .check_token = cfg->check_token,
        .compat_v03  = cfg->compat_v03,
        .thread_num  = thread_num,
    };
    protocol_handle(conn, &pcfg);

    syslog(LOG_INFO, "[THR %d] closing connection", thread_num);
    conn_free(conn);
    close(fd);
}

/* ── worker thread ───────────────────────────────────────────────────────── */

typedef struct {
    int              thread_num;
    const ServerCfg *cfg;
} WorkerArg;

static void *worker_main(void *arg)
{
    WorkerArg *wa = arg;
    QueueEntry e;
    while (queue_pop(&e))
        handle_accepted(e.fd, e.listen_idx, wa->thread_num, wa->cfg);
    return NULL;
}

/* ── listen socket setup ─────────────────────────────────────────────────── */

static int bind_listen(const ServerCfg *cfg, int i)
{
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) { syslog(LOG_ERR, "socket: %m"); return -1; }

    int one = 1;
    setsockopt(fd, SOL_SOCKET,  SO_REUSEADDR, &one, sizeof(one));
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,  &one, sizeof(one));

    struct timeval tv = { .tv_sec = cfg->timeout_s };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in6 sa = {
        .sin6_family = AF_INET6,
        .sin6_port   = htons(cfg->listens[i].port),
    };
    memcpy(&sa.sin6_addr, cfg->listens[i].addr, 16);

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &sa.sin6_addr, ip, sizeof(ip));
        syslog(LOG_ERR, "bind [%s]:%d: %m", ip, cfg->listens[i].port);
        close(fd);
        return -1;
    }
    if (listen(fd, CFG_LISTEN_BACKLOG) < 0) {
        syslog(LOG_ERR, "listen: %m");
        close(fd);
        return -1;
    }

    char ip[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &sa.sin6_addr, ip, sizeof(ip));
    syslog(LOG_INFO, "listening on [%s]:%d (%s)",
           ip, cfg->listens[i].port,
           cfg->listens[i].use_ssl ? "TLS" : "plain");
    return fd;
}

/* ── public entry ────────────────────────────────────────────────────────── */

int server_run(ServerCfg *cfg)
{
    int fds[32];
    for (int i = 0; i < cfg->num_listens; i++) {
        fds[i] = bind_listen(cfg, i);
        if (fds[i] < 0) return -1;
        cfg->listens[i].fd = fds[i];
    }

    pthread_t *threads = calloc((size_t)cfg->num_threads, sizeof(pthread_t));
    WorkerArg *args    = calloc((size_t)cfg->num_threads, sizeof(WorkerArg));
    if (!threads || !args) { syslog(LOG_ERR, "calloc: %m"); return -1; }

    syslog(LOG_INFO, "starting %d worker threads", cfg->num_threads);
    for (int i = 0; i < cfg->num_threads; i++) {
        args[i].thread_num = i;
        args[i].cfg        = cfg;
        if (pthread_create(&threads[i], NULL, worker_main, &args[i]) != 0) {
            syslog(LOG_ERR, "pthread_create: %m");
            return -1;
        }
    }

    struct pollfd pfd[32];
    for (int i = 0; i < cfg->num_listens; i++) {
        pfd[i].fd     = fds[i];
        pfd[i].events = POLLIN;
    }

    syslog(LOG_INFO, "ready for connections");

    while (!atomic_load(&g_shutdown)) {
        int r = poll(pfd, (nfds_t)cfg->num_listens, 500);
        if (r < 0) {
            if (errno != EINTR) syslog(LOG_ERR, "poll: %m");
            continue;
        }
        for (int i = 0; i < cfg->num_listens; i++) {
            if (pfd[i].revents & POLLIN) {
                int cfd = accept(fds[i], NULL, NULL);
                if (cfd >= 0) queue_push(cfd, i);
            }
        }
    }

    syslog(LOG_INFO, "shutting down…");
    for (int i = 0; i < cfg->num_listens; i++)
        close(fds[i]);

    pthread_cond_broadcast(&g_queue.not_empty);
    for (int i = 0; i < cfg->num_threads; i++)
        pthread_join(threads[i], NULL);

    syslog(LOG_INFO, "all worker threads stopped");
    free(threads);
    free(args);
    return 0;
}
