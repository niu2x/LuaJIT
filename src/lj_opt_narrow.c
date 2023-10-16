/*
** NARROW: Narrowing of numbers to integers (double to int32_t).
** STRIPOV: Stripping of overflow checks.
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_opt_narrow_c
#define LUA_CORE

#include "lj_obj.h"
