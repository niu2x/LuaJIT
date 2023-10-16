/*
** Memory access optimizations.
** AA: Alias Analysis using high-level semantic disambiguation.
** FWD: Load Forwarding (L2L) + Store Forwarding (S2L).
** DSE: Dead-Store Elimination.
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_opt_mem_c
#define LUA_CORE

#include "lj_obj.h"

