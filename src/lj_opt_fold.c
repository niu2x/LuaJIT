/*
** FOLD: Constant Folding, Algebraic Simplifications and Reassociation.
** ABCelim: Array Bounds Check Elimination.
** CSE: Common-Subexpression Elimination.
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_opt_fold_c
#define LUA_CORE

#include <math.h>

#include "lj_obj.h"


