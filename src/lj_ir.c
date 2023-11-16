/*
** SSA IR (Intermediate Representation) emitter.
** Copyright (C) 2005-2023 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_ir_c
#define LUA_CORE

/* For pointers to libc/libm functions. */
#include <stdio.h>
#include <math.h>

#include "lj_obj.h"

