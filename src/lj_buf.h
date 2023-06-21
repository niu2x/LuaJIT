/*
** Buffer handling.
** Copyright (C) 2005-2016 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_BUF_H
#define _LJ_BUF_H

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_str.h"

/* Resizable string buffers. Struct definition in lj_obj.h. */
#define sbufB(sb)	(mref((sb)->b, char))
#define sbufP(sb)	(mref((sb)->p, char))
#define sbufE(sb)	(mref((sb)->e, char))
#define sbufL(sb)	(mref((sb)->L, lua_State))
#define sbufsz(sb)	((MSize)(sbufE((sb)) - sbufB((sb))))
#define sbuflen(sb)	((MSize)(sbufP((sb)) - sbufB((sb))))
#define sbufleft(sb)	((MSize)(sbufE((sb)) - sbufP((sb))))
#define setsbufP(sb, q)	(setmref((sb)->p, (q)))
#define setsbufL(sb, l)	(setmref((sb)->L, (l)))

/* Buffer management */
LJ_FUNC char *LJ_FASTCALL lj_buf_need2(SBuf *sb, MSize sz,const char *reason);
LJ_FUNC char *LJ_FASTCALL lj_buf_more2(SBuf *sb, MSize sz,const char *reason);
LJ_FUNC void LJ_FASTCALL lj_buf_shrink(lua_State *L, SBuf *sb, const char *reason);
LJ_FUNC char * LJ_FASTCALL lj_buf_tmp(lua_State *L, MSize sz);

static LJ_AINLINE void lj_buf_init(lua_State *L, SBuf *sb)
{
  setsbufL(sb, L);
  setmref(sb->p, NULL); setmref(sb->e, NULL); setmref(sb->b, NULL);
}

static LJ_AINLINE void lj_buf_reset(SBuf *sb)
{
  setmrefr(sb->p, sb->b);
}

static LJ_AINLINE SBuf *lj_buf_tmp_(lua_State *L)
{
  SBuf *sb = &G(L)->tmpbuf;
  setsbufL(sb, L);
  lj_buf_reset(sb);
  return sb;
}

static LJ_AINLINE void lj_buf_free(global_State *g, SBuf *sb, const char *reason)
{
  lj_mem_free(g, sbufB(sb), sbufsz(sb), reason);
}

static LJ_AINLINE char *lj_buf_need(SBuf *sb, MSize sz,const char *reason)
{
  if (LJ_UNLIKELY(sz > sbufsz(sb)))
    return lj_buf_need2(sb, sz, reason);
  return sbufB(sb);
}

static LJ_AINLINE char *lj_buf_more(SBuf *sb, MSize sz,const char *reason)
{
  if (LJ_UNLIKELY(sz > sbufleft(sb)))
    return lj_buf_more2(sb, sz,reason);
  return sbufP(sb);
}

/* Low-level buffer put operations */
LJ_FUNC SBuf *lj_buf_putmem(SBuf *sb, const void *q, MSize len, const char *reason);
LJ_FUNC SBuf * LJ_FASTCALL lj_buf_putchar(SBuf *sb, int c, const char *reason);
LJ_FUNC SBuf * LJ_FASTCALL lj_buf_putstr(SBuf *sb, GCstr *s, const char *reason);

static LJ_AINLINE char *lj_buf_wmem(char *p, const void *q, MSize len)
{
  return (char *)memcpy(p, q, len) + len;
}

static LJ_AINLINE void lj_buf_putb(SBuf *sb, int c,const char *reason)
{
  char *p = lj_buf_more(sb, 1,reason);
  *p++ = (char)c;
  setsbufP(sb, p);
}

/* High-level buffer put operations */
LJ_FUNCA SBuf * LJ_FASTCALL lj_buf_putstr_reverse(SBuf *sb, GCstr *s, const char *reason);
LJ_FUNCA SBuf * LJ_FASTCALL lj_buf_putstr_lower(SBuf *sb, GCstr *s, const char *reason);
LJ_FUNCA SBuf * LJ_FASTCALL lj_buf_putstr_upper(SBuf *sb, GCstr *s, const char *reason);
LJ_FUNC SBuf *lj_buf_putstr_rep(SBuf *sb, GCstr *s, int32_t rep, const char *reason);
LJ_FUNC SBuf *lj_buf_puttab(SBuf *sb, GCtab *t, GCstr *sep,
			    int32_t i, int32_t e, const char *reason);

/* Miscellaneous buffer operations */
LJ_FUNCA GCstr * LJ_FASTCALL lj_buf_tostr(SBuf *sb);
LJ_FUNC GCstr *lj_buf_cat2str(lua_State *L, GCstr *s1, GCstr *s2);
LJ_FUNC uint32_t LJ_FASTCALL lj_buf_ruleb128(const char **pp);

static LJ_AINLINE GCstr *lj_buf_str(lua_State *L, SBuf *sb)
{
  return lj_str_new(L, sbufB(sb), sbuflen(sb));
}

#endif
