/*
** Math helper functions for assembler VM.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_vmmath_c
#define LUA_CORE

#include <errno.h>
#include <math.h>

#include "lj_obj.h"
#include "lj_ir.h"
#include "lj_vm.h"

/* -- Helper functions ---------------------------------------------------- */

/* Required to prevent the C compiler from applying FMA optimizations.
**
** Yes, there's -ffp-contract and the FP_CONTRACT pragma ... in theory.
** But the current state of C compilers is a mess in this regard.
** Also, this function is not performance sensitive at all.
*/
LJ_NOINLINE static double lj_vm_floormul(double x, double y)
{
    return lj_vm_floor(x / y) * y;
}

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
            return x - lj_vm_floormul(x, y);
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

/* -- Helper functions for generated machine code ------------------------- */

#if (0 && !(LJ_TARGET_ARM || LJ_TARGET_ARM64))
int32_t LJ_FASTCALL lj_vm_modi(int32_t a, int32_t b)
{
    uint32_t y, ua, ub;
    /* This must be checked before using this function. */
    lj_assertX(b != 0, "modulo with zero divisor");
    ua = a < 0 ? ~(uint32_t)a + 1u : (uint32_t)a;
    ub = b < 0 ? ~(uint32_t)b + 1u : (uint32_t)b;
    y  = ua % ub;
    if (y != 0 && (a ^ b) < 0)
        y = y - ub;
    if (((int32_t)y ^ b) < 0)
        y = ~y + 1u;
    return (int32_t)y;
}
#endif
