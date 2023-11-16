/*
** Math helper functions for assembler VM.
** Copyright (C) 2005-2023 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_vmmath_c
#define LUA_CORE

#include <errno.h>
#include <math.h>

#include "lj_obj.h"
#include "lj_ir.h"
#include "lj_vm.h"

/* -- Helper functions for generated machine code ------------------------- */

#if LJ_TARGET_X86ORX64
/* Wrapper functions to avoid linker issues on OSX. */
LJ_FUNCA double lj_vm_sinh(double x) { return sinh(x); }
LJ_FUNCA double lj_vm_cosh(double x) { return cosh(x); }
LJ_FUNCA double lj_vm_tanh(double x) { return tanh(x); }
#endif

#if !LJ_TARGET_X86ORX64
double lj_vm_foldarith(double x, double y, int op)
{
  switch (op) {
  case IR_ADD - IR_ADD: return x+y; break;
  case IR_SUB - IR_ADD: return x-y; break;
  case IR_MUL - IR_ADD: return x*y; break;
  case IR_DIV - IR_ADD: return x/y; break;
  case IR_MOD - IR_ADD: return x-lj_vm_floor(x/y)*y; break;
  case IR_POW - IR_ADD: return pow(x, y); break;
  case IR_NEG - IR_ADD: return -x; break;
  case IR_ABS - IR_ADD: return fabs(x); break;
  default: return x;
  }
}
#endif

