/*
** String formatting.
** Copyright (C) 2005-2013 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_STRFMT_H
#define _LJ_STRFMT_H

#include "lj_obj.h"

typedef uint32_t SFormat;  /* Format indicator. */

/* Format parser state. */
typedef struct FormatState {
  const uint8_t *p;	/* Current format string pointer. */
  const uint8_t *e;	/* End of format string. */
  const char *str;	/* Returned literal string. */
  MSize len;		/* Size of literal string. */
} FormatState;

/* Format types (max. 16). */
typedef enum FormatType {
  STRFMT_EOF, STRFMT_ERR, STRFMT_LIT,
  STRFMT_INT, STRFMT_UINT, STRFMT_NUM, STRFMT_STR, STRFMT_CHAR, STRFMT_PTR
} FormatType;

/* Format subtypes (bits are reused). */
#define STRFMT_T_HEX	0x0010	/* STRFMT_UINT */
#define STRFMT_T_OCT	0x0020	/* STRFMT_UINT */
#define STRFMT_T_FP_A	0x0000	/* STRFMT_NUM */
#define STRFMT_T_FP_E	0x0010	/* STRFMT_NUM */
#define STRFMT_T_FP_F	0x0020	/* STRFMT_NUM */
#define STRFMT_T_FP_G	0x0030	/* STRFMT_NUM */
#define STRFMT_T_QUOTED	0x0010	/* STRFMT_STR */

/* Format flags. */
#define STRFMT_F_LEFT	0x0100
#define STRFMT_F_PLUS	0x0200
#define STRFMT_F_ZERO	0x0400
#define STRFMT_F_SPACE	0x0800
#define STRFMT_F_ALT	0x1000
#define STRFMT_F_UPPER	0x2000

/* Format indicator fields. */
#define STRFMT_SH_WIDTH	16
#define STRFMT_SH_PREC	24

#define STRFMT_TYPE(sf)		((FormatType)((sf) & 15))
#define STRFMT_WIDTH(sf)	(((sf) >> STRFMT_SH_WIDTH) & 255u)
#define STRFMT_PREC(sf)		((((sf) >> STRFMT_SH_PREC) & 255u) - 1u)
#define STRFMT_FP(sf)		(((sf) >> 4) & 3)

/* Formats for conversion characters. */
#define STRFMT_A	(STRFMT_NUM|STRFMT_T_FP_A)
#define STRFMT_C	(STRFMT_CHAR)
#define STRFMT_D	(STRFMT_INT)
#define STRFMT_E	(STRFMT_NUM|STRFMT_T_FP_E)
#define STRFMT_F	(STRFMT_NUM|STRFMT_T_FP_F)
#define STRFMT_G	(STRFMT_NUM|STRFMT_T_FP_G)
#define STRFMT_I	STRFMT_D
#define STRFMT_O	(STRFMT_UINT|STRFMT_T_OCT)
#define STRFMT_P	(STRFMT_PTR)
#define STRFMT_Q	(STRFMT_STR|STRFMT_T_QUOTED)
#define STRFMT_S	(STRFMT_STR)
#define STRFMT_U	(STRFMT_UINT)
#define STRFMT_X	(STRFMT_UINT|STRFMT_T_HEX)

static LJ_AINLINE void lj_strfmt_init(FormatState *fs, const char *p, MSize len)
{
  fs->p = (const uint8_t *)p;
  fs->e = (const uint8_t *)p + len;
  lua_assert(*fs->e == 0);  /* Must be NUL-terminated (may have NULs inside). */
}

LJ_FUNC SFormat LJ_FASTCALL lj_strfmt_parse(FormatState *fs);
LJ_FUNC SBuf *lj_strfmt_putchar(SBuf *sb, SFormat, int32_t c);
LJ_FUNC SBuf *lj_strfmt_putstr(SBuf *sb, SFormat, GCstr *str);
LJ_FUNC SBuf *lj_strfmt_putquoted(SBuf *sb, GCstr *str);
LJ_FUNC SBuf *lj_strfmt_putxint(SBuf *sb, SFormat sf, uint64_t k);
LJ_FUNC SBuf *lj_strfmt_putnum_int(SBuf *sb, SFormat sf, lua_Number n);
LJ_FUNC SBuf *lj_strfmt_putnum_uint(SBuf *sb, SFormat sf, lua_Number n);
LJ_FUNC SBuf *lj_strfmt_putnum(SBuf *sb, SFormat, lua_Number n);

#endif
