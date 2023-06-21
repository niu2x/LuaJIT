#ifndef LJ_ALLOC_DEBUG_H
#define LJ_ALLOC_DEBUG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "lj_obj.h"

extern void (*lj_alloc_debug_handler)(long long size, const char *reason);
extern void lj_alloc_debug(long long size, const char *reason);
extern void lj_alloc_debug_set_handler(void (*handler)(long long size, const char *reason));
extern const char *lj_alloc_debug_strcat(const char *a, const GCstr *b);

#endif