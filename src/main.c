#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "config.h"
#include "server.h"
#include "random.h"
#include "token.h"

static void print_help(const char *prog)
{
    printf(
        "Usage: %s [options]\n\n"
        "Options:\n"
        "  -l ADDR   Plain TCP listen address (port, IP:port, or [IPv6]:port)\n"
        "  -L ADDR   TLS listen address (same format as -l)\n"
        "  -c PATH   TLS certificate file (PEM chain; required for -L)\n"
        "  -k PATH   TLS private key file (PEM; required for -L)\n"
        "  -t N      Worker threads (default: %d)\n"
        "  -u USER   Drop privileges to USER after binding\n"
        "  -d        Daemonise (fork to background)\n"
        "  -D        Enable debug logging\n"
        "  -w        Expect HTTP/WebSocket upgrade before RMBT\n"
        "  -s PATH   Secret key file (default: secret.key)\n"
        "  -r PATH   Random data file (default: random)\n"
        "  -n        Disable token validation\n"
        "  -2        Accept only v2 tokens (SHA256, IP+time bound); reject legacy v1\n"
        "            (long form: --v2-only)\n"
        "  -v VER    Compatibility mode ('0.3')\n"
        "  -h        Print this help\n\n"
        "At least one -l or -L is required.\n",
        prog, CFG_MAX_THREADS);
}

/* Parse "port", "IP:port", "[IPv6]:port", "*:port" into addr+port. */
static bool parse_listen_addr(const char *s,
                               uint8_t addr_out[16], uint16_t *port_out)
{
    char ip6[64], ip4[20];
    int  port;

    if (sscanf(s, "[%63[0-9a-fA-F:]]:%d", ip6, &port) == 2) {
        struct in6_addr a;
        if (inet_pton(AF_INET6, ip6, &a) != 1) return false;
        memcpy(addr_out, &a, 16);
        *port_out = (uint16_t)port;
        return true;
    }
    if (sscanf(s, "%19[0-9.]:%d", ip4, &port) == 2) {
        char mapped[32];
        snprintf(mapped, sizeof(mapped), "::ffff:%s", ip4);
        struct in6_addr a;
        if (inet_pton(AF_INET6, mapped, &a) != 1) return false;
        memcpy(addr_out, &a, 16);
        *port_out = (uint16_t)port;
        return true;
    }
    if (sscanf(s, "*:%d",   &port) == 1 ||
        sscanf(s, "[*]:%d", &port) == 1 ||
        sscanf(s, "%d",     &port) == 1) {
        memset(addr_out, 0, 16);   /* in6addr_any */
        *port_out = (uint16_t)port;
        return true;
    }
    return false;
}

static void sig_handler(int sig)
{
    (void)sig;
    server_shutdown();
}

int main(int argc, char **argv)
{
    openlog("rmbtd", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_DAEMON);

    ServerCfg cfg = {
        .num_threads = CFG_MAX_THREADS,
        .check_token = true,
        .timeout_s   = CFG_TIMEOUT_S,
    };
    KeyStore keys = { .count = 0 };

    const char *cert_path   = NULL;
    const char *key_path    = NULL;
    const char *secret_path = "secret.key";
    const char *random_path = "random";
    const char *drop_user   = NULL;
    bool        do_fork     = false;
    bool        debug       = false;

    static const struct option long_opts[] = {
        { "v2-only", no_argument, NULL, '2' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "l:L:c:k:t:u:s:r:v:dDwnh2",
                              long_opts, NULL)) != -1) {
        switch (opt) {
        case 'l':
        case 'L': {
            if (cfg.num_listens >= 32) {
                fprintf(stderr, "too many -l/-L options\n");
                return EXIT_FAILURE;
            }
            int i = cfg.num_listens;
            if (!parse_listen_addr(optarg,
                                   cfg.listens[i].addr,
                                   &cfg.listens[i].port)) {
                fprintf(stderr, "cannot parse address: %s\n", optarg);
                return EXIT_FAILURE;
            }
            cfg.listens[i].use_ssl = (opt == 'L');
            cfg.num_listens++;
            break;
        }
        case 'c': cert_path   = optarg; break;
        case 'k': key_path    = optarg; break;
        case 's': secret_path = optarg; break;
        case 'r': random_path = optarg; break;
        case 'u': drop_user   = optarg; break;
        case 'd': do_fork     = true;   break;
        case 'D': debug       = true;   break;
        case 'w': cfg.use_http = true;  break;
        case 'n': cfg.check_token = false; break;
        case '2': cfg.v2_only     = true;  break;
        case 't':
            cfg.num_threads = atoi(optarg);
            if (cfg.num_threads <= 0) {
                fprintf(stderr, "-t must be positive\n");
                return EXIT_FAILURE;
            }
            break;
        case 'v':
            if (strcmp(optarg, "0.3") == 0) cfg.compat_v03 = true;
            else {
                fprintf(stderr, "unsupported -v version: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        case 'h': print_help(argv[0]); return EXIT_SUCCESS;
        default:  print_help(argv[0]); return EXIT_FAILURE;
        }
    }

    if (cfg.num_listens == 0) {
        fprintf(stderr, "at least one -l or -L is required\n");
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    bool need_ssl = false;
    for (int i = 0; i < cfg.num_listens; i++)
        if (cfg.listens[i].use_ssl) { need_ssl = true; break; }

    if (need_ssl && (!cert_path || !key_path)) {
        fprintf(stderr, "TLS listeners require -c and -k\n");
        return EXIT_FAILURE;
    }

    if (do_fork) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return EXIT_FAILURE; }
        if (pid > 0) return EXIT_SUCCESS;
        setsid();
    }

    setlogmask(LOG_UPTO(debug ? LOG_DEBUG : LOG_INFO));
    syslog(LOG_INFO, "starting rmbtd (%s)", CFG_GREETING);

    struct sigaction sa = { .sa_handler = sig_handler };
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);

    if (random_load(random_path) < 0)
        return EXIT_FAILURE;

    if (cfg.check_token && keystore_load(&keys, secret_path) < 0)
        return EXIT_FAILURE;
    cfg.keys = &keys;

    if (need_ssl) {
        SSL_CTX *ctx = SSL_CTX_new(TLS_method());
        if (!ctx) { ERR_print_errors_fp(stderr); return EXIT_FAILURE; }

        if (!SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)
            || SSL_CTX_use_certificate_chain_file(ctx, cert_path) <= 0
            || SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
            ERR_print_errors_fp(stderr);
            SSL_CTX_free(ctx);
            return EXIT_FAILURE;
        }
        cfg.ssl_ctx = ctx;
    }

    if (drop_user) {
        if (getuid() != 0) { fprintf(stderr, "-u requires root\n"); return EXIT_FAILURE; }
        struct passwd *pw = getpwnam(drop_user);
        if (!pw) { fprintf(stderr, "unknown user: %s\n", drop_user); return EXIT_FAILURE; }
        if (setgroups(1, &pw->pw_gid) < 0
            || setgid(pw->pw_gid) < 0
            || setuid(pw->pw_uid) < 0) {
            perror("drop privileges");
            return EXIT_FAILURE;
        }
        syslog(LOG_INFO, "dropped privileges to %s", drop_user);
    }

    int rc = server_run(&cfg);

    if (cfg.ssl_ctx) {
        SSL_CTX_free(cfg.ssl_ctx);
        ERR_free_strings();
    }
    random_unload();
    syslog(LOG_INFO, "exiting");
    closelog();
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
