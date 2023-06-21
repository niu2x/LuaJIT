#include "lj_alloc_debug.h"
#include <string.h>

void (*lj_alloc_debug_handler)(long long size, const char *reason) = NULL;

void lj_alloc_debug(long long size, const char *reason) {
	if(lj_alloc_debug_handler) {
		lj_alloc_debug_handler(size, reason);
	}
	else {
		printf("lj_alloc_debug: %lld %s\n", size, reason);
	}
}

void lj_alloc_debug_set_handler(void (*handler)(long long size, const char *reason)) {
	lj_alloc_debug_handler = handler;
}

const char *lj_alloc_debug_strcat_gcstr(const char *a, const GCstr *b, int line) {
	static char buffer[4096] = {0};
	sprintf(buffer, "%s%s-%d", a, strdata(b), line);
	return buffer;
}

const char *lj_alloc_debug_strcat(const char *a, const char *b, int line) {
	static char buffer[4096] = {0};
	sprintf(buffer, "%s%s-%d", a, b, line);
	return buffer;
}
