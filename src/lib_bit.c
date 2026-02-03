/*
** Bit manipulation library.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
*/

#define lib_bit_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"
#include "lj_err.h"
#include "lj_buf.h"
#include "lj_strscan.h"
#include "lj_strfmt.h"
#include "lj_ff.h"
#include "lj_lib.h"

/* ------------------------------------------------------------------------ */

#define LJLIB_MODULE_bit

static int32_t bit_checkbit(lua_State* L, int narg)
{
    TValue* o = L->base + narg - 1;
    if (!(o < L->top && lj_strscan_numberobj(o)))
        lj_err_argt(L, narg, LUA_TNUMBER);
    if (LJ_LIKELY(tvisint(o))) {
        return intV(o);
    } else {
        int32_t i = lj_num2bit(numV(o));
        if (LJ_DUALNUM)
            setintV(o, i);
        return i;
    }
}

LJLIB_ASM(bit_tobit) 
LJLIB_REC(bit_tobit)
{
    lj_lib_checknumber(L, 1);
    return FFH_RETRY;
}

LJLIB_ASM(bit_bnot) 
LJLIB_REC(bit_unary IR_BNOT)
{
    lj_lib_checknumber(L, 1);
    return FFH_RETRY;
}

LJLIB_ASM(bit_bswap) 
LJLIB_REC(bit_unary IR_BSWAP)
{
    lj_lib_checknumber(L, 1);
    return FFH_RETRY;
}

LJLIB_ASM(bit_lshift) 
LJLIB_REC(bit_shift IR_BSHL)
{
    lj_lib_checknumber(L, 1);
    bit_checkbit(L, 2);
    return FFH_RETRY;
}
LJLIB_ASM_(bit_rshift)
LJLIB_REC(bit_shift IR_BSHR) 
LJLIB_ASM_(bit_arshift) 
LJLIB_REC(bit_shift IR_BSAR)
LJLIB_ASM_(bit_rol) 
LJLIB_REC(bit_shift IR_BROL) 
LJLIB_ASM_(bit_ror)
LJLIB_REC(bit_shift IR_BROR)

LJLIB_ASM(bit_band) 
LJLIB_REC(bit_nary IR_BAND)
{
    int i = 0;
    do {
        lj_lib_checknumber(L, ++i);
    } while (L->base + i < L->top);
    return FFH_RETRY;
}
LJLIB_ASM_(bit_bor)
LJLIB_REC(bit_nary IR_BOR) 
LJLIB_ASM_(bit_bxor) 
LJLIB_REC(bit_nary IR_BXOR)

/* ------------------------------------------------------------------------ */

LJLIB_CF(bit_tohex) 
LJLIB_REC(.)
{
    uint32_t b = (uint32_t)bit_checkbit(L, 1);
    int32_t  n = L->base + 1 >= L->top ? 8 : bit_checkbit(L, 2);
    SBuf*   sb = lj_buf_tmp_(L);
    SFormat sf = (STRFMT_UINT | STRFMT_T_HEX);
    if (n < 0) {
        n = (int32_t)(~(uint32_t)n + 1u);
        sf |= STRFMT_F_UPPER;
    }
    if ((uint32_t)n > 254)
        n = 254;
    sf |= ((SFormat)((n + 1) & 255) << STRFMT_SH_PREC);
    if (n < 8)
        b &= (1u << 4 * n) - 1;
    sb = lj_strfmt_putfxint(sb, sf, b);
    setstrV(L, L->top - 1, lj_buf_str(L, sb));
    lj_gc_check(L);
    return 1;
}

/* ------------------------------------------------------------------------ */

#include "lj_libdef.h"

LUALIB_API int luaopen_bit(lua_State* L)
{
    LJ_LIB_REG(L, LUA_BITLIBNAME, bit);
    return 1;
}
