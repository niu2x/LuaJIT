#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static void run(lua_State *L, const char *code)
{
  if (luaL_dostring(L, code) != 0) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    exit(1);
  }
  lua_settop(L, 0);
}

static int contains_from(FILE *stream, long start, const char *text)
{
  long length;
  char *buffer;
  int found;
  fflush(stream);
  length = ftell(stream);
  assert(length >= start);
  length -= start;
  buffer = (char *)malloc((size_t)length + 1);
  assert(buffer);
  fseek(stream, start, SEEK_SET);
  assert(fread(buffer, 1, (size_t)length, stream) == (size_t)length);
  buffer[length] = 0;
  found = strstr(buffer, text) != NULL;
  free(buffer);
  fseek(stream, 0, SEEK_END);
  return found;
}

static int contains(FILE *stream, const char *text)
{
  return contains_from(stream, 0, text);
}

static int return_upvalue(lua_State *L)
{
  lua_pushvalue(L, lua_upvalueindex(1));
  return 1;
}

int main(int argc, char **argv)
{
  const luaL_reg legacy_registration[] = {{NULL, NULL}};
  lua_State *L = luaL_newstate();
  FILE *report = tmpfile();
  char code[1024], needle[256];
  const void *function, *object;
  uintptr_t addresses[] = {
    (uintptr_t)0, (uintptr_t)1, (uintptr_t)UINT64_C(0x00007fffffffffff),
    (uintptr_t)UINT64_C(0x0000800000000000),
    (uintptr_t)UINT64_C(0xb400007012345678),
    (uintptr_t)UINT64_C(0xffffffffffffffff)
  };
  size_t i;
  long checkpoint;
  int legacy_reference;
  assert(L && report && argc == 2);
  assert(legacy_registration[0].name == NULL);
  luaL_openlibs(L);
  lua_pushinteger(L, 37);
  legacy_reference = lua_ref(L, 1);
  lua_getref(L, legacy_reference);
  assert(lua_tointeger(L, -1) == 37);
  lua_pop(L, 1);
  lua_unref(L, legacy_reference);
  lua_newtable(L);
  luaL_setn(L, -1, 0);
  assert(luaL_getn(L, -1) == 0);
  for (i = 0; i < sizeof(addresses)/sizeof(addresses[0]); i++) {
    void *pointer = (void *)addresses[i];
    lua_pushlightuserdata(L, pointer);
    assert(lua_touserdata(L, -1) == pointer);
    assert(lua_topointer(L, -1) == pointer);
    lua_pushinteger(L, (lua_Integer)i);
    lua_rawset(L, -3);
    lua_pushlightuserdata(L, pointer);
    lua_rawget(L, -2);
    assert(lua_tointeger(L, -1) == (lua_Integer)i);
    lua_pop(L, 1);
    lua_pushlightuserdata(L, pointer);
    lua_pushcclosure(L, return_upvalue, 1);
    assert(lua_pcall(L, 0, 1, 0) == 0);
    assert(lua_touserdata(L, -1) == pointer);
    lua_pop(L, 1);
  }
  lua_setglobal(L, "full_range_keys");
  luaL_gcdebugoutput(report);
  run(L, "collectgarbage('diff')");
  run(L, "alloc2_merge_probe={callback=function() return 7 end,nested={}};");
  lua_getglobal(L, "alloc2_merge_probe");
  object = lua_topointer(L, -1);
  lua_getfield(L, -1, "callback");
  function = lua_topointer(L, -1);
  lua_settop(L, 0);
  run(L, "collectgarbage('diff')");
  assert(contains(report, "STR(alloc2_merge_probe)"));
  snprintf(needle, sizeof(needle), "FUNC(addr: %p)", function);
  assert(contains(report, needle));
  snprintf(code, sizeof(code), "collectgarbage('findf','%p')", function);
  run(L, code);
  assert(contains(report, "find it"));
  snprintf(code, sizeof(code), "collectgarbage('findf','%p')", object);
  run(L, code);
  assert(contains(report, "address is not a Lua function"));
  snprintf(code, sizeof(code), "collectgarbage('ignore_obj','%p')", object);
  checkpoint = ftell(report);
  run(L, code);
  assert(!contains_from(report, checkpoint, needle));
  run(L, "collectgarbage('diff'); collectgarbage('diff'); alloc2_key_probe={nested={}};");
  run(L, "collectgarbage('diff')");
  assert(contains(report, "STR(alloc2_key_probe)"));
  checkpoint = ftell(report);
  run(L, "collectgarbage('ignore_key','alloc2_key_probe')");
  assert(!contains_from(report, checkpoint, "STR(alloc2_key_probe))TABLE"));
  run(L, "alloc2_key_probe=nil");
  run(L, "local source=string.rep(' ',30000)..'return function(x) return x+7 end';"
         "local f=assert(loadstring(source))();"
         "local g=assert(loadstring(string.dump(f))); assert(g(5)==12);"
         "local co=coroutine.create(function() coroutine.yield({1,2,3}) end);"
         "assert(coroutine.resume(co)); assert(coroutine.resume(co));"
         "local ffi=require('ffi'); local a=ffi.new('double[?]',100); a[99]=42;"
         "assert(a[99]==42);"
         "local sum=0; for i=1,100000 do sum=sum+i end; assert(sum==5000050000);"
         "for i=1,30 do jit.flush(); local x=0; for j=1,10000 do x=x+j end;"
         "assert(x==50005000) end;"
         "alloc2_merge_probe=nil; collectgarbage('collect'); collectgarbage('diff');"
         "sort_stress={}; for i=1,50000 do sort_stress[i]={} end;"
         "collectgarbage('diff'); sort_stress=nil; collectgarbage('diff')");
  snprintf(code, sizeof(code), "collectgarbage('dump',%c%s%c)", '"', argv[1], '"');
  run(L, code);
  luaL_gcdebugoutput(NULL);
  fclose(report);
  lua_close(L);
  puts("PASS: full-range lightuserdata, alloc2 operations, loading, FFI, coroutine, JIT and 50000-object diff");
  return 0;
}
