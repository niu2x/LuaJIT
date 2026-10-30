#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "luajit.h"

static size_t used(lua_State *L)
{
  return (size_t)lua_gc(L, LUA_GCCOUNT, 0)*1024 +
         (size_t)lua_gc(L, LUA_GCCOUNTB, 0);
}

int main(void)
{
  lua_State *a, *b;
  size_t before, peak, aftergc;
  assert(luaJIT_getreservedbytes() == 0);
  a = luaL_newstate();
  b = luaL_newstate();
  assert(a && b);
  before = luaJIT_getreservedbytes();
  assert(before >= used(a) + used(b));
  memset(lua_newuserdata(a, 4*1024*1024), 1, 4*1024*1024);
  peak = luaJIT_getreservedbytes();
  assert(peak >= before + 4*1024*1024);
  assert(peak >= used(a) + used(b));
  lua_pop(a, 1);
  lua_gc(a, LUA_GCCOLLECT, 0);
  aftergc = luaJIT_getreservedbytes();
  assert(aftergc < peak);
  assert(aftergc >= used(a) + used(b));
  lua_close(a);
  assert(luaJIT_getreservedbytes() >= used(b));
  lua_close(b);
  assert(luaJIT_getreservedbytes() == 0);
  printf("reserved API passed: two states, direct allocation, GC and close; %zu -> %zu -> %zu -> 0 bytes\n", before, peak, aftergc);
  return 0;
}
