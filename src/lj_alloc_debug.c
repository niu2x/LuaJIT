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

const char *lj_alloc_debug_strcat(const char *a, const GCstr *b) {
	static char buffer[4096];
	strcpy(buffer,a);
	printf("b->len %u\n", b->len);
	strncat(buffer, strdata(b), b->len);
	return buffer;
}
