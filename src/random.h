#pragma once
#include <stddef.h>

int  random_load(const char *path);
void random_unload(void);

/* Returns pointer into the mmap'd buffer at `offset` (wraps around). */
const char *random_ptr(size_t offset, size_t len);

size_t random_size(void);
