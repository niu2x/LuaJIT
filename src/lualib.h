/*
** Standard library header.
** Copyright (C) 2026 niu2x
*/

#ifndef _LUALIB_H
#define _LUALIB_H

#include "lua.h"

#define LUA_FILE_HANDLE "FILE*"

#define LUA_CO_LIB_NAME   "coroutine"
#define LUA_MATH_LIB_NAME "math"
#define LUA_STR_LIB_NAME  "string"
#define LUA_TAB_LIB_NAME  "table"
#define LUA_IO_LIB_NAME   "io"
#define LUA_OS_LIB_NAME   "os"
#define LUA_LOAD_LIB_NAME "package"
#define LUA_DB_LIB_NAME   "debug"
#define LUA_BIT_LIB_NAME  "bit"
#define LUA_JIT_LIB_NAME  "jit"

int luaopen_base(lua_State* L);
int luaopen_math(lua_State* L);
int luaopen_string(lua_State* L);
int luaopen_table(lua_State* L);
int luaopen_io(lua_State* L);
int luaopen_os(lua_State* L);
int luaopen_package(lua_State* L);
int luaopen_debug(lua_State* L);
int luaopen_bit(lua_State* L);
int luaopen_jit(lua_State* L);
int luaopen_ffi(lua_State* L);
int luaopen_string_buffer(lua_State* L);

void luaL_openlibs(lua_State* L);

#ifndef lua_assert
    #define lua_assert(x) ((void)0)
#endif

#endif
