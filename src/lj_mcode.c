/*
** Machine code management.
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_mcode_c
#define LUA_CORE

#include "lj_obj.h"
#if LJ_HASFFI
    #include "lj_vm.h"
#endif

/* -- OS-specific functions ----------------------------------------------- */

#if LJ_HASFFI

    /* Define this if you want to run LuaJIT with Valgrind. */
    #ifdef LUAJIT_USE_VALGRIND
        #include <valgrind/valgrind.h>
    #endif

    #if LJ_TARGET_IOS
void sys_icache_invalidate(void* start, size_t len);
    #endif

/* Synchronize data/instruction cache. */
void lj_mcode_sync(void* start, void* end)
{
    #ifdef LUAJIT_USE_VALGRIND
    VALGRIND_DISCARD_TRANSLATIONS(start, (char*)end - (char*)start);
    #endif
    #if LJ_TARGET_X86ORX64
    UNUSED(start);
    UNUSED(end);
    #elif LJ_TARGET_IOS
    sys_icache_invalidate(start, (char*)end - (char*)start);
    #elif LJ_TARGET_PPC
    lj_vm_cachesync(start, end);
    #elif defined(__GNUC__)
    __clear_cache(start, end);
    #else
        #error "Missing builtin to flush instruction cache"
    #endif
}

#endif

