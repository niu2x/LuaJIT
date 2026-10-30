/* Build with the same source tree's static library and -Isrc; no engine needed. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lj_gc.h"
#include "lj_obj.h"
#include "lj_tab.h"

typedef struct Stats {
  long long added, removed, resized, reachable_delta, lua_delta, other_delta;
  long long added_objects, removed_objects, resized_objects;
  char *output;
} Stats;
static long long field(const char *s, const char *name)
{
  const char *p = strstr(s, name);
  assert(p);
  return strtoll(p + strlen(name), NULL, 10);
}
static Stats capture(lua_State *L, FILE *f)
{
  long start = ftell(f), end;
  Stats s = {0};
  lj_gc_diff(L);
  fflush(f); end = ftell(f); assert(end >= start);
  s.output = malloc((size_t)(end-start)+1); assert(s.output);
  fseek(f, start, SEEK_SET);
  assert(fread(s.output, 1, (size_t)(end-start), f) == (size_t)(end-start));
  s.output[end-start] = 0; fseek(f, end, SEEK_SET);
  assert(!strstr(s.output, "capture failed"));
  if (strstr(s.output, "added_bytes=")) {
    s.added=field(s.output,"added_bytes="); s.removed=field(s.output,"removed_bytes=");
    s.resized=field(s.output,"resized_delta_bytes=");
    s.reachable_delta=field(s.output,"reachable_delta_bytes=");
    s.lua_delta=field(s.output,"lua_delta_bytes="); s.other_delta=field(s.output,"other_delta_bytes=");
    s.added_objects=field(s.output,"added_objects=");
    s.removed_objects=field(s.output,"removed_objects=");
    s.resized_objects=field(s.output,"resized_objects=");
    assert(s.added-s.removed+s.resized == s.reachable_delta);
    assert(s.reachable_delta+s.other_delta == s.lua_delta);
  }
  return s;
}
static uint64_t table_bytes(GCtab *t)
{
  uint64_t n=sizeof(GCtab);
  if (t->hmask) n+=(uint64_t)(t->hmask+1)*sizeof(Node);
  if (t->colo) n+=((uint32_t)t->colo & 0x7f)*sizeof(TValue);
  if (t->asize && t->colo<=0) n+=(uint64_t)t->asize*sizeof(TValue);
  return n;
}
static void set_false(lua_State *L, const char *name)
{
  lua_pushboolean(L,0); lua_setglobal(L,name);
}
static GCtab *probe(lua_State *L)
{
  GCtab *t; lua_getglobal(L,"alloc2_table"); t=tabV(L->top-1); lua_pop(L,1); return t;
}
int main(void)
{
  lua_State *L=luaL_newstate(); FILE *f=tmpfile(); Stats s;
  assert(L && f); luaL_openlibs(L); luaL_gcdebugoutput(f);
  lua_createtable(L,2,0); lua_setglobal(L,"alloc2_table");
  set_false(L,"alloc2_string"); set_false(L,"alloc2_userdata"); set_false(L,"alloc2_cdata");
  s=capture(L,f); assert(strstr(s.output,"size_metadata_bytes=")); free(s.output);
  s=capture(L,f); assert(s.added==0 && s.removed==0 && s.resized==0); free(s.output);
  GCtab *t=probe(L); uint64_t before=table_bytes(t);
  assert(t->colo>0); /* Exercise separation of the original colocated array. */
  lua_getglobal(L,"alloc2_table");
  for (int i=1;i<=4096;i++) {lua_pushinteger(L,i); lua_rawseti(L,-2,i);} lua_pop(L,1);
  assert(t->colo<0);
  uint64_t after=table_bytes(t);
  s=capture(L,f); assert(s.added_objects==0 && s.removed_objects==0);
  assert(s.resized==(long long)(after-before) && s.resized_objects==1);
  assert(strstr(s.output,"[alloc2-resize]") && strstr(s.output,"STR(alloc2_table)")); free(s.output);
  before=table_bytes(t);
  lua_getglobal(L,"alloc2_table");
  for (int i=1;i<=2048;i++) {lua_pushinteger(L,i); lua_rawseti(L,-2,100000+i);} lua_pop(L,1);
  after=table_bytes(t); assert(after>before);
  s=capture(L,f); assert(s.added_objects==0 && s.resized==(long long)(after-before)); free(s.output);
  lua_getglobal(L,"alloc2_table");
  for (int i=1;i<=4096;i++) {lua_pushnil(L); lua_rawseti(L,-2,i);}
  for (int i=1;i<=2048;i++) {lua_pushnil(L); lua_rawseti(L,-2,100000+i);} lua_pop(L,1);
  before=table_bytes(t); lj_tab_resize(L,t,0,0); after=table_bytes(t); assert(after<before);
  s=capture(L,f); assert(s.added_objects==0 && s.resized==(long long)after-(long long)before); free(s.output);
  const size_t n=1024*1024; char *data=malloc(n); assert(data); memset(data,'Q',n);
  lua_pushlstring(L,data,n); lua_setglobal(L,"alloc2_string"); free(data);
  s=capture(L,f); assert(s.added==(long long)(sizeof(GCstr)+n+1));
  assert(s.added_objects==1); free(s.output);
  set_false(L,"alloc2_string");
  s=capture(L,f); assert(s.removed==(long long)(sizeof(GCstr)+n+1)); free(s.output);
  lua_newuserdata(L,12345); lua_setglobal(L,"alloc2_userdata");
  s=capture(L,f); assert(s.added==(long long)(sizeof(GCudata)+12345)); free(s.output);
  set_false(L,"alloc2_userdata");
  s=capture(L,f); assert(s.removed==(long long)(sizeof(GCudata)+12345)); free(s.output);
#if LJ_HASFFI
  assert(luaL_dostring(L,"alloc2_cdata={require('ffi').new('int64_t',123),require('ffi').new('uint8_t[?]',65536)}")==0);
  s=capture(L,f); assert(strstr(s.output,"CDATA(addr:")); assert(s.added>=65536); free(s.output);
  set_false(L,"alloc2_cdata"); s=capture(L,f); assert(s.removed>=65536); free(s.output);
#endif
  /* Multiple resized descendants must safely reuse/drain the BFS links. */
  lua_createtable(L,64,0);
  for (int i=1;i<=64;i++) {lua_createtable(L,2,0); lua_rawseti(L,-2,i);}
  lua_setglobal(L,"alloc2_resizes"); s=capture(L,f); free(s.output);
  lua_getglobal(L,"alloc2_resizes");
  for (int i=1;i<=64;i++) {
    lua_rawgeti(L,-1,i);
    for (int k=1;k<=512;k++) {lua_pushinteger(L,k); lua_rawseti(L,-2,k);}
    lua_pop(L,1);
  }
  lua_pop(L,1); s=capture(L,f);
  assert(s.resized_objects==64 && s.added_objects==0 && s.removed_objects==0); free(s.output);
  s=capture(L,f); assert(s.resized_objects==0 && s.added_objects==0 && s.removed_objects==0); free(s.output);
  lua_getglobal(L,"alloc2_table"); const void *ptr=lua_topointer(L,-1); lua_pop(L,1);
  char cmd[128]; snprintf(cmd,sizeof(cmd),"collectgarbage('ignore_obj','%p')",ptr);
  assert(luaL_dostring(L,cmd)==0); s=capture(L,f); free(s.output);
  luaL_gcdebugoutput(NULL); fclose(f); lua_close(L);
  puts("PASS: exact string/userdata bytes, colocated/separate table arrays, hash growth, shrink, FFI, rolling/ignored snapshots and both byte identities");
  return 0;
}
