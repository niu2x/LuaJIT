/*
** Machine code management.
** Copyright (C) 2005-2023 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_MCODE_H
#define _LJ_MCODE_H

#include "lj_obj.h"

#if LJ_HASFFI
LJ_FUNC void lj_mcode_sync(void *start, void *end);
#endif


#endif
