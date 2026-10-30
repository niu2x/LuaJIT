#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include "../src/lj_alloc.c"

static int fail_mmap, fail_mremap, fail_munmap;
void *__real_mmap(void *, size_t, int, int, int, off_t);
void *__real_mremap(void *, size_t, size_t, int, ...);
int __real_munmap(void *, size_t);
void *__wrap_mmap(void *a, size_t s, int p, int f, int d, off_t o)
{
  if (fail_mmap) { errno = ENOMEM; return MAP_FAILED; }
  return __real_mmap(a, s, p, f, d, o);
}
void *__wrap_mremap(void *a, size_t o, size_t n, int f, ...)
{
  if (fail_mremap) { errno = ENOMEM; return MAP_FAILED; }
  return __real_mremap(a, o, n, f);
}
int __wrap_munmap(void *a, size_t s)
{
  if (fail_munmap) { errno = EINVAL; return -1; }
  return __real_munmap(a, s);
}
static void *exercise(void *unused)
{
  int i;
  (void)unused;
  for (i = 0; i < 1000; i++) {
    void *m = lj_alloc_create();
    void *p;
    assert(m);
    p = lj_alloc_f(m, NULL, 0, 256*1024);
    assert(p);
    lj_alloc_f(m, p, 256*1024, 0);
    lj_alloc_destroy(m);
  }
  return NULL;
}
int main(void)
{
  size_t initial, growth;
  void *p;
  pthread_t threads[4];
  int i;
  assert(luaJIT_getreservedbytes() == 0);
  p = CALL_MMAP(128*1024);
  assert(p != MFAIL);
  initial = luaJIT_getreservedbytes();
  assert(initial == 128*1024);
  fail_mmap = 1;
  assert(CALL_MMAP(128*1024) == MFAIL);
  assert(luaJIT_getreservedbytes() == initial);
  fail_mmap = 0;
  fail_mremap = 1;
  assert(CALL_MREMAP(p, 128*1024, 256*1024, CALL_MREMAP_MAYMOVE) == MFAIL);
  assert(luaJIT_getreservedbytes() == initial);
  fail_mremap = 0;
  p = CALL_MREMAP(p, 128*1024, 256*1024, CALL_MREMAP_MAYMOVE);
  assert(p != MFAIL);
  growth = luaJIT_getreservedbytes();
  assert(growth == 256*1024);
  p = CALL_MREMAP(p, 256*1024, 64*1024, CALL_MREMAP_MAYMOVE);
  assert(p != MFAIL);
  assert(luaJIT_getreservedbytes() == 64*1024);
  fail_munmap = 1;
  assert(CALL_MUNMAP(p, 64*1024) == -1);
  assert(luaJIT_getreservedbytes() == 64*1024);
  fail_munmap = 0;
  assert(CALL_MUNMAP(p, 64*1024) == 0);
  assert(luaJIT_getreservedbytes() == 0);
  for (i = 0; i < 4; i++) assert(pthread_create(&threads[i], NULL, exercise, NULL) == 0);
  for (i = 0; i < 4; i++) assert(pthread_join(threads[i], NULL) == 0);
  assert(luaJIT_getreservedbytes() == 0);
  puts("reserved syscall accounting passed: map/remap/unmap success and failure, shrink, four concurrent allocators");
  return 0;
}
