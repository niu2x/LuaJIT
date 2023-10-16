/*
** Math helper functions for assembler VM.
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_vmmath_c
#define LUA_CORE

#include <errno.h>
#include <math.h>

#include "lj_obj.h"
#include "lj_ir.h"
#include "lj_vm.h"

/* -- Wrapper functions --------------------------------------------------- */

#if LJ_TARGET_X86 && __ELF__ && __PIC__
/* Wrapper functions to deal with the ELF/x86 PIC disaster. */
LJ_FUNCA double lj_wrap_log(double x) { return log(x); }
LJ_FUNCA double lj_wrap_log10(double x) { return log10(x); }
LJ_FUNCA double lj_wrap_exp(double x) { return exp(x); }
LJ_FUNCA double lj_wrap_sin(double x) { return sin(x); }
LJ_FUNCA double lj_wrap_cos(double x) { return cos(x); }
LJ_FUNCA double lj_wrap_tan(double x) { return tan(x); }
LJ_FUNCA double lj_wrap_asin(double x) { return asin(x); }
LJ_FUNCA double lj_wrap_acos(double x) { return acos(x); }
LJ_FUNCA double lj_wrap_atan(double x) { return atan(x); }
LJ_FUNCA double lj_wrap_sinh(double x) { return sinh(x); }
LJ_FUNCA double lj_wrap_cosh(double x) { return cosh(x); }
LJ_FUNCA double lj_wrap_tanh(double x) { return tanh(x); }
LJ_FUNCA double lj_wrap_atan2(double x, double y) { return atan2(x, y); }
LJ_FUNCA double lj_wrap_pow(double x, double y) { return pow(x, y); }
LJ_FUNCA double lj_wrap_fmod(double x, double y) { return fmod(x, y); }
#endif

/* -- Helper functions for generated machine code ------------------------- */

double lj_vm_foldarith(double x, double y, int op)
{
    switch (op) {
        case IR_ADD - IR_ADD:
            return x + y;
            break;
        case IR_SUB - IR_ADD:
            return x - y;
            break;
        case IR_MUL - IR_ADD:
            return x * y;
            break;
        case IR_DIV - IR_ADD:
            return x / y;
            break;
        case IR_MOD - IR_ADD:
            return x - lj_vm_floor(x / y) * y;
            break;
        case IR_POW - IR_ADD:
            return pow(x, y);
            break;
        case IR_NEG - IR_ADD:
            return -x;
            break;
        case IR_ABS - IR_ADD:
            return fabs(x);
            break;
        default:
            return x;
    }
}

#if LJ_TARGET_MIPS
int32_t LJ_FASTCALL lj_vm_modi(int32_t a, int32_t b)
{
    uint32_t y, ua, ub;
    lua_assert(b != 0); /* This must be checked before using this function. */
    ua = a < 0 ? (uint32_t)-a : (uint32_t)a;
    ub = b < 0 ? (uint32_t)-b : (uint32_t)b;
    y = ua % ub;
    if (y != 0 && (a ^ b) < 0)
        y = y - ub;
    if (((int32_t)y ^ b) < 0)
        y = (uint32_t) - (int32_t)y;
    return (int32_t)y;
}
#endif
