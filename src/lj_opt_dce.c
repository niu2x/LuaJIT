/*
** DCE: Dead Code Elimination. Pre-LOOP only -- ASM already performs DCE.
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_opt_dce_c
#define LUA_CORE

#include "lj_obj.h"
