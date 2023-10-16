/*
** Trace management.
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_TRACE_H
#define _LJ_TRACE_H

#include "lj_obj.h"

#define lj_trace_flushall(L)	(UNUSED(L), 0)
#define lj_trace_initstate(g)	UNUSED(g)
#define lj_trace_freestate(g)	UNUSED(g)
#define lj_trace_abort(g)	UNUSED(g)
#define lj_trace_end(J)		UNUSED(J)


#endif
