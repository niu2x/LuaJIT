/*
** Library initialization.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
**
** Major parts taken verbatim from the Lua interpreter.
** Copyright (C) 1994-2008 Lua.org, PUC-Rio. See Copyright Notice in lua.h
*/

#define lib_init_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_arch.h"

static const luaL_Reg lj_lib_load[] = { { "", luaopen_base },
                                        { LUA_LOAD_LIB_NAME, luaopen_package },
                                        { LUA_TAB_LIB_NAME, luaopen_table },
                                        { LUA_IO_LIB_NAME, luaopen_io },
                                        { LUA_OS_LIB_NAME, luaopen_os },
                                        { LUA_STR_LIB_NAME, luaopen_string },
                                        { LUA_MATH_LIB_NAME, luaopen_math },
                                        { LUA_DB_LIB_NAME, luaopen_debug },
                                        { LUA_BIT_LIB_NAME, luaopen_bit },
                                        { NULL, NULL } };

static const luaL_Reg lj_lib_preload[] = {
    { NULL, NULL }
};

 void luaL_openlibs(lua_State* L)
{
    const luaL_Reg* lib;
    for (lib = lj_lib_load; lib->func; lib++) {
        lua_pushcfunction(L, lib->func);
        lua_pushstring(L, lib->name);
        lua_call(L, 1, 0);
    }
    luaL_findtable(L,
                   LUA_REGISTRYINDEX,
                   "_PRELOAD",
                   sizeof(lj_lib_preload) / sizeof(lj_lib_preload[0]) - 1);
    for (lib = lj_lib_preload; lib->func; lib++) {
        lua_pushcfunction(L, lib->func);
        lua_setfield(L, -2, lib->name);
    }
    lua_pop(L, 1);
}
