/*
** JIT library.
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#define lib_jit_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_debug.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_state.h"
#include "lj_bc.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#endif
#include "lj_trace.h"
#include "lj_dispatch.h"
#include "lj_vm.h"
#include "lj_vmevent.h"
#include "lj_lib.h"

#include "luajit.h"

/* -- jit.* functions ----------------------------------------------------- */

#define LJLIB_MODULE_jit

static int setjitmode(lua_State *L, int mode)
{
  int idx = 0;
  if (L->base == L->top || tvisnil(L->base)) {  /* jit.on/off/flush([nil]) */
    mode |= LUAJIT_MODE_ENGINE;
  } else {
    /* jit.on/off/flush(func|proto, nil|true|false) */
    if (tvisfunc(L->base) || tvisproto(L->base))
      idx = 1;
    else if (!tvistrue(L->base))  /* jit.on/off/flush(true, nil|true|false) */
      goto err;
    if (L->base+1 < L->top && tvisbool(L->base+1))
      mode |= boolV(L->base+1) ? LUAJIT_MODE_ALLFUNC : LUAJIT_MODE_ALLSUBFUNC;
    else
      mode |= LUAJIT_MODE_FUNC;
  }
  if (luaJIT_setmode(L, idx, mode) != 1) {
    if ((mode & LUAJIT_MODE_MASK) == LUAJIT_MODE_ENGINE)
      lj_err_caller(L, LJ_ERR_NOJIT);
  err:
    lj_err_argt(L, 1, LUA_TFUNCTION);
  }
  return 0;
}

LJLIB_CF(jit_on)
{
  return setjitmode(L, LUAJIT_MODE_ON);
}

LJLIB_CF(jit_off)
{
  return setjitmode(L, LUAJIT_MODE_OFF);
}

LJLIB_CF(jit_flush)
{
  return setjitmode(L, LUAJIT_MODE_FLUSH);
}


LJLIB_CF(jit_status)
{
  setboolV(L->top++, 0);
  return 1;
}

LJLIB_CF(jit_attach)
{
#ifdef LUAJIT_DISABLE_VMEVENT
  luaL_error(L, "vmevent API disabled");
#else
  GCfunc *fn = lj_lib_checkfunc(L, 1);
  GCstr *s = lj_lib_optstr(L, 2);
  luaL_findtable(L, LUA_REGISTRYINDEX, LJ_VMEVENTS_REGKEY, LJ_VMEVENTS_HSIZE);
  if (s) {  /* Attach to given event. */
    const uint8_t *p = (const uint8_t *)strdata(s);
    uint32_t h = s->len;
    while (*p) h = h ^ (lj_rol(h, 6) + *p++);
    lua_pushvalue(L, 1);
    lua_rawseti(L, -2, VMEVENT_HASHIDX(h));
    G(L)->vmevmask = VMEVENT_NOCACHE;  /* Invalidate cache. */
  } else {  /* Detach if no event given. */
    setnilV(L->top++);
    while (lua_next(L, -2)) {
      L->top--;
      if (tvisfunc(L->top) && funcV(L->top) == fn) {
	setnilV(lj_tab_set(L, tabV(L->top-2), L->top-1));
      }
    }
  }
#endif
  return 0;
}

LJLIB_PUSH(top-5) LJLIB_SET(os)
LJLIB_PUSH(top-4) LJLIB_SET(arch)
LJLIB_PUSH(top-3) LJLIB_SET(version_num)
LJLIB_PUSH(top-2) LJLIB_SET(version)

#include "lj_libdef.h"

/* -- jit.util.* functions ------------------------------------------------ */

#define LJLIB_MODULE_jit_util

/* -- Reflection API for Lua functions ------------------------------------ */

/* Return prototype of first argument (Lua function or prototype object) */
static GCproto *check_Lproto(lua_State *L, int nolua)
{
  TValue *o = L->base;
  if (L->top > o) {
    if (tvisproto(o)) {
      return protoV(o);
    } else if (tvisfunc(o)) {
      if (isluafunc(funcV(o)))
	return funcproto(funcV(o));
      else if (nolua)
	return NULL;
    }
  }
  lj_err_argt(L, 1, LUA_TFUNCTION);
  return NULL;  /* unreachable */
}

static void setintfield(lua_State *L, GCtab *t, const char *name, int32_t val)
{
  setintV(lj_tab_setstr(L, t, lj_str_newz(L, name)), val);
}

/* local info = jit.util.funcinfo(func [,pc]) */
LJLIB_CF(jit_util_funcinfo)
{
  GCproto *pt = check_Lproto(L, 1);
  if (pt) {
    BCPos pc = (BCPos)lj_lib_optint(L, 2, 0);
    GCtab *t;
    lua_createtable(L, 0, 16);  /* Increment hash size if fields are added. */
    t = tabV(L->top-1);
    setintfield(L, t, "linedefined", pt->firstline);
    setintfield(L, t, "lastlinedefined", pt->firstline + pt->numline);
    setintfield(L, t, "stackslots", pt->framesize);
    setintfield(L, t, "params", pt->numparams);
    setintfield(L, t, "bytecodes", (int32_t)pt->sizebc);
    setintfield(L, t, "gcconsts", (int32_t)pt->sizekgc);
    setintfield(L, t, "nconsts", (int32_t)pt->sizekn);
    setintfield(L, t, "upvalues", (int32_t)pt->sizeuv);
    if (pc < pt->sizebc)
      setintfield(L, t, "currentline", lj_debug_line(pt, pc));
    lua_pushboolean(L, (pt->flags & PROTO_VARARG));
    lua_setfield(L, -2, "isvararg");
    lua_pushboolean(L, (pt->flags & PROTO_CHILD));
    lua_setfield(L, -2, "children");
    setstrV(L, L->top++, proto_chunkname(pt));
    lua_setfield(L, -2, "source");
    lj_debug_pushloc(L, pt, pc);
    lua_setfield(L, -2, "loc");
    setprotoV(L, lj_tab_setstr(L, t, lj_str_newlit(L, "proto")), pt);
  } else {
    GCfunc *fn = funcV(L->base);
    GCtab *t;
    lua_createtable(L, 0, 4);  /* Increment hash size if fields are added. */
    t = tabV(L->top-1);
    if (!iscfunc(fn))
      setintfield(L, t, "ffid", fn->c.ffid);
    setintptrV(lj_tab_setstr(L, t, lj_str_newlit(L, "addr")),
	       (intptr_t)(void *)fn->c.f);
    setintfield(L, t, "upvalues", fn->c.nupvalues);
  }
  return 1;
}

/* local ins, m = jit.util.funcbc(func, pc) */
LJLIB_CF(jit_util_funcbc)
{
  GCproto *pt = check_Lproto(L, 0);
  BCPos pc = (BCPos)lj_lib_checkint(L, 2);
  if (pc < pt->sizebc) {
    BCIns ins = proto_bc(pt)[pc];
    BCOp op = bc_op(ins);
    lua_assert(op < BC__MAX);
    setintV(L->top, ins);
    setintV(L->top+1, lj_bc_mode[op]);
    L->top += 2;
    return 2;
  }
  return 0;
}

/* local k = jit.util.funck(func, idx) */
LJLIB_CF(jit_util_funck)
{
  GCproto *pt = check_Lproto(L, 0);
  ptrdiff_t idx = (ptrdiff_t)lj_lib_checkint(L, 2);
  if (idx >= 0) {
    if (idx < (ptrdiff_t)pt->sizekn) {
      copyTV(L, L->top-1, proto_knumtv(pt, idx));
      return 1;
    }
  } else {
    if (~idx < (ptrdiff_t)pt->sizekgc) {
      GCobj *gc = proto_kgc(pt, idx);
      setgcV(L, L->top-1, gc, ~gc->gch.gct);
      return 1;
    }
  }
  return 0;
}

/* local name = jit.util.funcuvname(func, idx) */
LJLIB_CF(jit_util_funcuvname)
{
  GCproto *pt = check_Lproto(L, 0);
  uint32_t idx = (uint32_t)lj_lib_checkint(L, 2);
  if (idx < pt->sizeuv) {
    setstrV(L, L->top-1, lj_str_newz(L, lj_debug_uvname(pt, idx)));
    return 1;
  }
  return 0;
}

/* -- Reflection API for traces ------------------------------------------- */


#include "lj_libdef.h"

static int luaopen_jit_util(lua_State *L)
{
  LJ_LIB_REG(L, NULL, jit_util);
  return 1;
}


#if LJ_HASPROFILE

#define LJLIB_MODULE_jit_profile

/* Not loaded by default, use: local profile = require("jit.profile") */

static const char KEY_PROFILE_THREAD = 't';
static const char KEY_PROFILE_FUNC = 'f';

static void jit_profile_callback(lua_State *L2, lua_State *L, int samples,
				 int vmstate)
{
  TValue key;
  cTValue *tv;
  setlightudV(&key, (void *)&KEY_PROFILE_FUNC);
  tv = lj_tab_get(L, tabV(registry(L)), &key);
  if (tvisfunc(tv)) {
    char vmst = (char)vmstate;
    int status;
    setfuncV(L2, L2->top++, funcV(tv));
    setthreadV(L2, L2->top++, L);
    setintV(L2->top++, samples);
    setstrV(L2, L2->top++, lj_str_new(L2, &vmst, 1));
    status = lua_pcall(L2, 3, 0, 0);  /* callback(thread, samples, vmstate) */
    if (status) {
      if (G(L2)->panic) G(L2)->panic(L2);
      exit(EXIT_FAILURE);
    }
    lj_trace_abort(G(L2));
  }
}

/* profile.start(mode, cb) */
LJLIB_CF(jit_profile_start)
{
  GCtab *registry = tabV(registry(L));
  GCstr *mode = lj_lib_optstr(L, 1);
  GCfunc *func = lj_lib_checkfunc(L, 2);
  lua_State *L2 = lua_newthread(L);  /* Thread that runs profiler callback. */
  TValue key;
  /* Anchor thread and function in registry. */
  setlightudV(&key, (void *)&KEY_PROFILE_THREAD);
  setthreadV(L, lj_tab_set(L, registry, &key), L2);
  setlightudV(&key, (void *)&KEY_PROFILE_FUNC);
  setfuncV(L, lj_tab_set(L, registry, &key), func);
  lj_gc_anybarriert(L, registry);
  luaJIT_profile_start(L, mode ? strdata(mode) : "",
		       (luaJIT_profile_callback)jit_profile_callback, L2);
  return 0;
}

/* profile.stop() */
LJLIB_CF(jit_profile_stop)
{
  GCtab *registry;
  TValue key;
  luaJIT_profile_stop(L);
  registry = tabV(registry(L));
  setlightudV(&key, (void *)&KEY_PROFILE_THREAD);
  setnilV(lj_tab_set(L, registry, &key));
  setlightudV(&key, (void *)&KEY_PROFILE_FUNC);
  setnilV(lj_tab_set(L, registry, &key));
  lj_gc_anybarriert(L, registry);
  return 0;
}

/* dump = profile.dumpstack([thread,] fmt, depth) */
LJLIB_CF(jit_profile_dumpstack)
{
  lua_State *L2 = L;
  int arg = 0;
  size_t len;
  int depth;
  GCstr *fmt;
  const char *p;
  if (L->top > L->base && tvisthread(L->base)) {
    L2 = threadV(L->base);
    arg = 1;
  }
  fmt = lj_lib_checkstr(L, arg+1);
  depth = lj_lib_checkint(L, arg+2);
  p = luaJIT_profile_dumpstack(L2, strdata(fmt), depth, &len);
  lua_pushlstring(L, p, len);
  return 1;
}

#include "lj_libdef.h"

static int luaopen_jit_profile(lua_State *L)
{
  LJ_LIB_REG(L, NULL, jit_profile);
  return 1;
}

#endif

/* -- JIT compiler initialization ----------------------------------------- */


#if LJ_TARGET_ARM && LJ_TARGET_LINUX
#include <sys/utsname.h>
#endif

/* Arch-dependent CPU detection. */
static uint32_t jit_cpudetect(lua_State *L)
{
  uint32_t flags = 0;
#if LJ_TARGET_X86ORX64
  uint32_t vendor[4];
  uint32_t features[4];
  if (lj_vm_cpuid(0, vendor) && lj_vm_cpuid(1, features)) {
#define JIT_F_SSE2	2
    flags |= ((features[3] >> 26)&1) * JIT_F_SSE2;
  }
  /* Check for required instruction set support on x86 (unnecessary on x64). */
#if LJ_TARGET_X86
  if (!(flags & JIT_F_SSE2))
    luaL_error(L, "CPU with SSE2 required");
#endif
#elif LJ_TARGET_ARM

#elif LJ_TARGET_ARM64
  /* No optional CPU features to detect (for now). */
#elif LJ_TARGET_PPC

#elif LJ_TARGET_MIPS

#else
#error "Missing CPU detection for this architecture"
#endif
  UNUSED(L);
  return flags;
}

/* Initialize JIT compiler. */
static void jit_init(lua_State *L)
{
  uint32_t flags = jit_cpudetect(L);
  UNUSED(flags);
}

LUALIB_API int luaopen_jit(lua_State *L)
{
  jit_init(L);
  lua_pushliteral(L, LJ_OS_NAME);
  lua_pushliteral(L, LJ_ARCH_NAME);
  lua_pushinteger(L, LUAJIT_VERSION_NUM);
  lua_pushliteral(L, LUAJIT_VERSION);
  LJ_LIB_REG(L, LUA_JITLIBNAME, jit);
#if LJ_HASPROFILE
  lj_lib_prereg(L, LUA_JITLIBNAME ".profile", luaopen_jit_profile,
		tabref(L->env));
#endif
#ifndef LUAJIT_DISABLE_JITUTIL
  lj_lib_prereg(L, LUA_JITLIBNAME ".util", luaopen_jit_util, tabref(L->env));
#endif
  L->top -= 2;
  return 1;
}

