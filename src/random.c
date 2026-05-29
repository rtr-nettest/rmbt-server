#include "random.h"
#include "config.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static const char *g_data;
static size_t      g_size;

int random_load(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        syslog(LOG_ERR, "open random file '%s': %m", path);
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        syslog(LOG_ERR, "fstat random file: %m");
        close(fd);
        return -1;
    }

    size_t sz = (size_t)st.st_size;
    if (sz > CFG_RANDOM_MAX)
        sz = CFG_RANDOM_MAX;

    void *m = mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) {
        syslog(LOG_ERR, "mmap random file: %m");
        return -1;
    }

    /* Warm the pages into the page cache. */
    volatile char sink = 0;
    for (size_t i = 0; i < sz; i += 4096)
        sink |= ((const char *)m)[i];
    (void)sink;

    g_data = m;
    g_size = sz;
    syslog(LOG_INFO, "loaded %zu bytes of random data", sz);
    return 0;
}

void random_unload(void)
{
    if (g_data) {
        munmap((void *)g_data, g_size);
        g_data = NULL;
        g_size = 0;
    }
}

const char *random_ptr(size_t offset, size_t len)
{
    (void)len;
    return g_data + (offset % g_size);
}

size_t random_size(void)
{
    return g_size;
}
