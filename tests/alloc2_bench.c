/* Compare two static LuaJIT builds. Time GC+BFS+sort+diff and identical output sink. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lj_gc.h"
static double now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static void capture(lua_State *L,const char *phase,int i) {double t=now();lj_gc_diff(L);printf("%s,%d,%.6f\n",phase,i,now()-t);fflush(stdout);}
int main(int argc,char **argv) {
  int n=argc>1?atoi(argv[1]):1000000;
  lua_State *L=luaL_newstate();assert(L);luaL_openlibs(L);
  FILE *f=fopen("/dev/null","w");assert(f);luaL_gcdebugoutput(f);
  lua_createtable(L,n+20000,0);
  for(int i=1;i<=n;i++){lua_newtable(L);lua_rawseti(L,-2,i);}lua_setglobal(L,"bench");
  lua_newtable(L);lua_setglobal(L,"resize_probe");
  capture(L,"baseline",0);
  for(int i=0;i<5;i++)capture(L,"unchanged",i);
  lua_getglobal(L,"resize_probe");
  for(int i=1;i<=100000;i++){lua_pushinteger(L,i);lua_rawseti(L,-2,i);}lua_pop(L,1);
  capture(L,"resize",0);
  lua_getglobal(L,"bench");for(int i=n+1;i<=n+10000;i++){lua_newtable(L);lua_rawseti(L,-2,i);}lua_pop(L,1);
  capture(L,"added_10000",0);
  luaL_gcdebugoutput(NULL);fclose(f);lua_close(L);return 0;
}
