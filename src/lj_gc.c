/*
** Garbage collector.
** Copyright (C) 2005-2020 Mike Pall. See Copyright Notice in luajit.h
**
** Major portions taken verbatim or adapted from the Lua interpreter.
** Copyright (C) 1994-2008 Lua.org, PUC-Rio. See Copyright Notice in lua.h
*/

#include <stdio.h>

#define lj_gc_c
#define LUA_CORE
#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_func.h"
#include "lj_udata.h"
#include "lj_meta.h"
#include "lj_state.h"
#include "lj_frame.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#include "lj_cdata.h"
#endif
#include "lj_trace.h"
#include "lj_dispatch.h"
#include "lj_vm.h"

#define GCSTEPSIZE	1024u
#define GCSWEEPMAX	40
#define GCSWEEPCOST	10
#define GCFINALIZECOST	100

/* Macros to set GCobj colors and flags. */
#define white2gray(x)		((x)->gch.marked &= (uint8_t)~LJ_GC_WHITES)
#define gray2black(x)		((x)->gch.marked |= LJ_GC_BLACK)
#define isfinalized(u)		((u)->marked & LJ_GC_FINALIZED)

/* -- Mark phase ---------------------------------------------------------- */

/* Mark a TValue (if needed). */
#define gc_marktv(g, tv) \
  { lj_assertG(!tvisgcv(tv) || (~itype(tv) == gcval(tv)->gch.gct), \
	       "TValue and GC type mismatch"); \
    if (tviswhite(tv)) gc_mark(g, gcV(tv)); }

/* Mark a GCobj (if needed). */
#define gc_markobj(g, o) \
  { if (iswhite(obj2gco(o))) gc_mark(g, obj2gco(o)); }

/* Mark a string object. */
#define gc_mark_str(s)		((s)->marked &= (uint8_t)~LJ_GC_WHITES)

/* Mark a white GCobj. */
static void gc_mark(global_State *g, GCobj *o)
{
  int gct = o->gch.gct;
  lj_assertG(iswhite(o), "mark of non-white object");
  lj_assertG(!isdead(g, o), "mark of dead object");
  white2gray(o);
  if (LJ_UNLIKELY(gct == ~LJ_TUDATA)) {
    GCtab *mt = tabref(gco2ud(o)->metatable);
    gray2black(o);  /* Userdata are never gray. */
    if (mt) gc_markobj(g, mt);
    gc_markobj(g, tabref(gco2ud(o)->env));
  } else if (LJ_UNLIKELY(gct == ~LJ_TUPVAL)) {
    GCupval *uv = gco2uv(o);
    gc_marktv(g, uvval(uv));
    if (uv->closed)
      gray2black(o);  /* Closed upvalues are never gray. */
  } else if (gct != ~LJ_TSTR && gct != ~LJ_TCDATA) {
    lj_assertG(gct == ~LJ_TFUNC || gct == ~LJ_TTAB ||
	       gct == ~LJ_TTHREAD || gct == ~LJ_TPROTO || gct == ~LJ_TTRACE,
	       "bad GC type %d", gct);
    setgcrefr(o->gch.gclist, g->gc.gray);
    setgcref(g->gc.gray, o);
  }
}

/* Mark GC roots. */
static void gc_mark_gcroot(global_State *g)
{
  ptrdiff_t i;
  for (i = 0; i < GCROOT_MAX; i++)
    if (gcref(g->gcroot[i]) != NULL)
      gc_markobj(g, gcref(g->gcroot[i]));
}

/* Start a GC cycle and mark the root set. */
static void gc_mark_start(global_State *g)
{
  setgcrefnull(g->gc.gray);
  setgcrefnull(g->gc.grayagain);
  setgcrefnull(g->gc.weak);
  gc_markobj(g, mainthread(g));
  gc_markobj(g, tabref(mainthread(g)->env));
  gc_marktv(g, &g->registrytv);
  gc_mark_gcroot(g);
  g->gc.state = GCSpropagate;
}

/* Mark open upvalues. */
static void gc_mark_uv(global_State *g)
{
  GCupval *uv;
  for (uv = uvnext(&g->uvhead); uv != &g->uvhead; uv = uvnext(uv)) {
    lj_assertG(uvprev(uvnext(uv)) == uv && uvnext(uvprev(uv)) == uv,
	       "broken upvalue chain");
    if (isgray(obj2gco(uv)))
      gc_marktv(g, uvval(uv));
  }
}

/* Mark userdata in mmudata list. */
static void gc_mark_mmudata(global_State *g)
{
  GCobj *root = gcref(g->gc.mmudata);
  GCobj *u = root;
  if (u) {
    do {
      u = gcnext(u);
      makewhite(g, u);  /* Could be from previous GC. */
      gc_mark(g, u);
    } while (u != root);
  }
}

/* Separate userdata objects to be finalized to mmudata list. */
size_t lj_gc_separateudata(global_State *g, int all)
{
  size_t m = 0;
  GCRef *p = &mainthread(g)->nextgc;
  GCobj *o;
  while ((o = gcref(*p)) != NULL) {
    if (!(iswhite(o) || all) || isfinalized(gco2ud(o))) {
      p = &o->gch.nextgc;  /* Nothing to do. */
    } else if (!lj_meta_fastg(g, tabref(gco2ud(o)->metatable), MM_gc)) {
      markfinalized(o);  /* Done, as there's no __gc metamethod. */
      p = &o->gch.nextgc;
    } else {  /* Otherwise move userdata to be finalized to mmudata list. */
      m += sizeudata(gco2ud(o));
      markfinalized(o);
      *p = o->gch.nextgc;
      if (gcref(g->gc.mmudata)) {  /* Link to end of mmudata list. */
	GCobj *root = gcref(g->gc.mmudata);
	setgcrefr(o->gch.nextgc, root->gch.nextgc);
	setgcref(root->gch.nextgc, o);
	setgcref(g->gc.mmudata, o);
      } else {  /* Create circular list. */
	setgcref(o->gch.nextgc, o);
	setgcref(g->gc.mmudata, o);
      }
    }
  }
  return m;
}

static int check_table_weak(global_State *g,GCtab *t) {
  int weak = 0;

  GCtab *mt = tabref(t->metatable);
  cTValue *mode = lj_meta_fastg(g, mt, MM_mode);
  if (mode && tvisstr(mode)) { 
    const char *modestr = strVdata(mode);
    int c;
    while ((c = *modestr++)) {
      if (c == 'k') weak |= LJ_GC_WEAKKEY;
      else if (c == 'v') weak |= LJ_GC_WEAKVAL;
      else if (c == 'K') weak = (int)(~0u & ~LJ_GC_WEAKVAL);
    }
  }
  return weak;
}

/* -- Propagation phase --------------------------------------------------- */

/* Traverse a table. */
static int gc_traverse_tab(global_State *g, GCtab *t)
{
  int weak = 0;
  cTValue *mode;
  GCtab *mt = tabref(t->metatable);
  if (mt)
    gc_markobj(g, mt);
  mode = lj_meta_fastg(g, mt, MM_mode);
  if (mode && tvisstr(mode)) {  /* Valid __mode field? */
    const char *modestr = strVdata(mode);
    int c;
    while ((c = *modestr++)) {
      if (c == 'k') weak |= LJ_GC_WEAKKEY;
      else if (c == 'v') weak |= LJ_GC_WEAKVAL;
    }
    if (weak) {  /* Weak tables are cleared in the atomic phase. */
#if LJ_HASFFI
      CTState *cts = ctype_ctsG(g);
      if (cts && cts->finalizer == t) {
	weak = (int)(~0u & ~LJ_GC_WEAKVAL);
      } else
#endif
      {
	t->marked = (uint8_t)((t->marked & ~LJ_GC_WEAK) | weak);
	setgcrefr(t->gclist, g->gc.weak);
	setgcref(g->gc.weak, obj2gco(t));
      }
    }
  }
  if (weak == LJ_GC_WEAK)  /* Nothing to mark if both keys/values are weak. */
    return 1;
  if (!(weak & LJ_GC_WEAKVAL)) {  /* Mark array part. */
    MSize i, asize = t->asize;
    for (i = 0; i < asize; i++)
      gc_marktv(g, arrayslot(t, i));
  }
  if (t->hmask > 0) {  /* Mark hash part. */
    Node *node = noderef(t->node);
    MSize i, hmask = t->hmask;
    for (i = 0; i <= hmask; i++) {
      Node *n = &node[i];
      if (!tvisnil(&n->val)) {  /* Mark non-empty slot. */
	lj_assertG(!tvisnil(&n->key), "mark of nil key in non-empty slot");
	if (!(weak & LJ_GC_WEAKKEY)) gc_marktv(g, &n->key);
	if (!(weak & LJ_GC_WEAKVAL)) gc_marktv(g, &n->val);
      }
    }
  }
  return weak;
}

/* Traverse a function. */
static void gc_traverse_func(global_State *g, GCfunc *fn)
{
  gc_markobj(g, tabref(fn->c.env));
  if (isluafunc(fn)) {
    uint32_t i;
    lj_assertG(fn->l.nupvalues <= funcproto(fn)->sizeuv,
	       "function upvalues out of range");
    gc_markobj(g, funcproto(fn));
    for (i = 0; i < fn->l.nupvalues; i++)  /* Mark Lua function upvalues. */
      gc_markobj(g, &gcref(fn->l.uvptr[i])->uv);
  } else {
    uint32_t i;
    for (i = 0; i < fn->c.nupvalues; i++)  /* Mark C function upvalues. */
      gc_marktv(g, &fn->c.upvalue[i]);
  }
}

#if LJ_HASJIT
/* Mark a trace. */
static void gc_marktrace(global_State *g, TraceNo traceno)
{
  GCobj *o = obj2gco(traceref(G2J(g), traceno));
  lj_assertG(traceno != G2J(g)->cur.traceno, "active trace escaped");
  if (iswhite(o)) {
    white2gray(o);
    setgcrefr(o->gch.gclist, g->gc.gray);
    setgcref(g->gc.gray, o);
  }
}

/* Traverse a trace. */
static void gc_traverse_trace(global_State *g, GCtrace *T)
{
  IRRef ref;
  if (T->traceno == 0) return;
  for (ref = T->nk; ref < REF_TRUE; ref++) {
    IRIns *ir = &T->ir[ref];
    if (ir->o == IR_KGC)
      gc_markobj(g, ir_kgc(ir));
    if (irt_is64(ir->t) && ir->o != IR_KNULL)
      ref++;
  }
  if (T->link) gc_marktrace(g, T->link);
  if (T->nextroot) gc_marktrace(g, T->nextroot);
  if (T->nextside) gc_marktrace(g, T->nextside);
  gc_markobj(g, gcref(T->startpt));
}

/* The current trace is a GC root while not anchored in the prototype (yet). */
#define gc_traverse_curtrace(g)	gc_traverse_trace(g, &G2J(g)->cur)
#else
#define gc_traverse_curtrace(g)	UNUSED(g)
#endif

/* Traverse a prototype. */
static void gc_traverse_proto(global_State *g, GCproto *pt)
{
  ptrdiff_t i;
  gc_mark_str(proto_chunkname(pt));
  for (i = -(ptrdiff_t)pt->sizekgc; i < 0; i++)  /* Mark collectable consts. */
    gc_markobj(g, proto_kgc(pt, i));
#if LJ_HASJIT
  if (pt->trace) gc_marktrace(g, pt->trace);
#endif
}

/* Traverse the frame structure of a stack. */
static MSize gc_traverse_frames(global_State *g, lua_State *th)
{
  TValue *frame, *top = th->top-1, *bot = tvref(th->stack);
  /* Note: extra vararg frame not skipped, marks function twice (harmless). */
  for (frame = th->base-1; frame > bot+LJ_FR2; frame = frame_prev(frame)) {
    GCfunc *fn = frame_func(frame);
    TValue *ftop = frame;
    if (isluafunc(fn)) ftop += funcproto(fn)->framesize;
    if (ftop > top) top = ftop;
    if (!LJ_FR2) gc_markobj(g, fn);  /* Need to mark hidden function (or L). */
  }
  top++;  /* Correct bias of -1 (frame == base-1). */
  if (top > tvref(th->maxstack)) top = tvref(th->maxstack);
  return (MSize)(top - bot);  /* Return minimum needed stack size. */
}

/* Traverse a thread object. */
static void gc_traverse_thread(global_State *g, lua_State *th)
{
  TValue *o, *top = th->top;
  for (o = tvref(th->stack)+1+LJ_FR2; o < top; o++)
    gc_marktv(g, o);
  if (g->gc.state == GCSatomic) {
    top = tvref(th->stack) + th->stacksize;
    for (; o < top; o++)  /* Clear unmarked slots. */
      setnilV(o);
  }
  gc_markobj(g, tabref(th->env));
  lj_state_shrinkstack(th, gc_traverse_frames(g, th));
}

/* Propagate one gray object. Traverse it and turn it black. */
static size_t propagatemark(global_State *g)
{
  GCobj *o = gcref(g->gc.gray);
  int gct = o->gch.gct;
  lj_assertG(isgray(o), "propagation of non-gray object");
  gray2black(o);
  setgcrefr(g->gc.gray, o->gch.gclist);  /* Remove from gray list. */
  if (LJ_LIKELY(gct == ~LJ_TTAB)) {
    GCtab *t = gco2tab(o);
    if (gc_traverse_tab(g, t) > 0)
      black2gray(o);  /* Keep weak tables gray. */
    return sizeof(GCtab) + sizeof(TValue) * t->asize +
			   (t->hmask ? sizeof(Node) * (t->hmask + 1) : 0);
  } else if (LJ_LIKELY(gct == ~LJ_TFUNC)) {
    GCfunc *fn = gco2func(o);
    gc_traverse_func(g, fn);
    return isluafunc(fn) ? sizeLfunc((MSize)fn->l.nupvalues) :
			   sizeCfunc((MSize)fn->c.nupvalues);
  } else if (LJ_LIKELY(gct == ~LJ_TPROTO)) {
    GCproto *pt = gco2pt(o);
    gc_traverse_proto(g, pt);
    return pt->sizept;
  } else if (LJ_LIKELY(gct == ~LJ_TTHREAD)) {
    lua_State *th = gco2th(o);
    setgcrefr(th->gclist, g->gc.grayagain);
    setgcref(g->gc.grayagain, o);
    black2gray(o);  /* Threads are never black. */
    gc_traverse_thread(g, th);
    return sizeof(lua_State) + sizeof(TValue) * th->stacksize;
  } else {
#if LJ_HASJIT
    GCtrace *T = gco2trace(o);
    gc_traverse_trace(g, T);
    return ((sizeof(GCtrace)+7)&~7) + (T->nins-T->nk)*sizeof(IRIns) +
	   T->nsnap*sizeof(SnapShot) + T->nsnapmap*sizeof(SnapEntry);
#else
    lj_assertG(0, "bad GC type %d", gct);
    return 0;
#endif
  }
}

/* Propagate all gray objects. */
static size_t gc_propagate_gray(global_State *g)
{
  size_t m = 0;
  while (gcref(g->gc.gray) != NULL)
    m += propagatemark(g);
  return m;
}

/* -- Sweep phase --------------------------------------------------------- */

/* Type of GC free functions. */
typedef void (LJ_FASTCALL *GCFreeFunc)(global_State *g, GCobj *o);

/* GC free functions for LJ_TSTR .. LJ_TUDATA. ORDER LJ_T */
static const GCFreeFunc gc_freefunc[] = {
  (GCFreeFunc)lj_str_free,
  (GCFreeFunc)lj_func_freeuv,
  (GCFreeFunc)lj_state_free,
  (GCFreeFunc)lj_func_freeproto,
  (GCFreeFunc)lj_func_free,
#if LJ_HASJIT
  (GCFreeFunc)lj_trace_free,
#else
  (GCFreeFunc)0,
#endif
#if LJ_HASFFI
  (GCFreeFunc)lj_cdata_free,
#else
  (GCFreeFunc)0,
#endif
  (GCFreeFunc)lj_tab_free,
  (GCFreeFunc)lj_udata_free
};

/* Full sweep of a GC list. */
#define gc_fullsweep(g, p)	gc_sweep(g, (p), ~(uint32_t)0)

/* Partial sweep of a GC list. */
static GCRef *gc_sweep(global_State *g, GCRef *p, uint32_t lim)
{
  /* Mask with other white and LJ_GC_FIXED. Or LJ_GC_SFIXED on shutdown. */
  int ow = otherwhite(g);
  GCobj *o;
  while ((o = gcref(*p)) != NULL && lim-- > 0) {
    if (o->gch.gct == ~LJ_TTHREAD)  /* Need to sweep open upvalues, too. */
      gc_fullsweep(g, &gco2th(o)->openupval);
    if (((o->gch.marked ^ LJ_GC_WHITES) & ow)) {  /* Black or current white? */
      lj_assertG(!isdead(g, o) || (o->gch.marked & LJ_GC_FIXED),
		 "sweep of undead object");
      makewhite(g, o);  /* Value is alive, change to the current white. */
      p = &o->gch.nextgc;
    } else {  /* Otherwise value is dead, free it. */
      lj_assertG(isdead(g, o) || ow == LJ_GC_SFIXED,
		 "sweep of unlive object");
      setgcrefr(*p, o->gch.nextgc);
      if (o == gcref(g->gc.root))
	setgcrefr(g->gc.root, o->gch.nextgc);  /* Adjust list anchor. */
      gc_freefunc[o->gch.gct - ~LJ_TSTR](g, o);
    }
  }
  return p;
}

/* Check whether we can clear a key or a value slot from a table. */
static int gc_mayclear(cTValue *o, int val)
{
  if (tvisgcv(o)) {  /* Only collectable objects can be weak references. */
    if (tvisstr(o)) {  /* But strings cannot be used as weak references. */
      gc_mark_str(strV(o));  /* And need to be marked. */
      return 0;
    }
    if (iswhite(gcV(o)))
      return 1;  /* Object is about to be collected. */
    if (tvisudata(o) && val && isfinalized(udataV(o)))
      return 1;  /* Finalized userdata is dropped only from values. */
  }
  return 0;  /* Cannot clear. */
}

/* Clear collected entries from weak tables. */
static void gc_clearweak(global_State *g, GCobj *o)
{
  UNUSED(g);
  while (o) {
    GCtab *t = gco2tab(o);
    lj_assertG((t->marked & LJ_GC_WEAK), "clear of non-weak table");
    if ((t->marked & LJ_GC_WEAKVAL)) {
      MSize i, asize = t->asize;
      for (i = 0; i < asize; i++) {
	/* Clear array slot when value is about to be collected. */
	TValue *tv = arrayslot(t, i);
	if (gc_mayclear(tv, 1))
	  setnilV(tv);
      }
    }
    if (t->hmask > 0) {
      Node *node = noderef(t->node);
      MSize i, hmask = t->hmask;
      for (i = 0; i <= hmask; i++) {
	Node *n = &node[i];
	/* Clear hash slot when key or value is about to be collected. */
	if (!tvisnil(&n->val) && (gc_mayclear(&n->key, 0) ||
				  gc_mayclear(&n->val, 1)))
	  setnilV(&n->val);
      }
    }
    o = gcref(t->gclist);
  }
}

/* Call a userdata or cdata finalizer. */
static void gc_call_finalizer(global_State *g, lua_State *L,
			      cTValue *mo, GCobj *o)
{
  /* Save and restore lots of state around the __gc callback. */
  uint8_t oldh = hook_save(g);
  GCSize oldt = g->gc.threshold;
  int errcode;
  TValue *top;
  lj_trace_abort(g);
  hook_entergc(g);  /* Disable hooks and new traces during __gc. */
  if (LJ_HASPROFILE && (oldh & HOOK_PROFILE)) lj_dispatch_update(g);
  g->gc.threshold = LJ_MAX_MEM;  /* Prevent GC steps. */
  top = L->top;
  copyTV(L, top++, mo);
  if (LJ_FR2) setnilV(top++);
  setgcV(L, top, o, ~o->gch.gct);
  L->top = top+1;
  errcode = lj_vm_pcall(L, top, 1+0, -1);  /* Stack: |mo|o| -> | */
  hook_restore(g, oldh);
  if (LJ_HASPROFILE && (oldh & HOOK_PROFILE)) lj_dispatch_update(g);
  g->gc.threshold = oldt;  /* Restore GC threshold. */
  if (errcode)
    lj_err_throw(L, errcode);  /* Propagate errors. */
}

/* Finalize one userdata or cdata object from the mmudata list. */
static void gc_finalize(lua_State *L)
{
  global_State *g = G(L);
  GCobj *o = gcnext(gcref(g->gc.mmudata));
  cTValue *mo;
  lj_assertG(tvref(g->jit_base) == NULL, "finalizer called on trace");
  /* Unchain from list of userdata to be finalized. */
  if (o == gcref(g->gc.mmudata))
    setgcrefnull(g->gc.mmudata);
  else
    setgcrefr(gcref(g->gc.mmudata)->gch.nextgc, o->gch.nextgc);
#if LJ_HASFFI
  if (o->gch.gct == ~LJ_TCDATA) {
    TValue tmp, *tv;
    /* Add cdata back to the GC list and make it white. */
    setgcrefr(o->gch.nextgc, g->gc.root);
    setgcref(g->gc.root, o);
    makewhite(g, o);
    o->gch.marked &= (uint8_t)~LJ_GC_CDATA_FIN;
    /* Resolve finalizer. */
    setcdataV(L, &tmp, gco2cd(o));
    tv = lj_tab_set(L, ctype_ctsG(g)->finalizer, &tmp);
    if (!tvisnil(tv)) {
      g->gc.nocdatafin = 0;
      copyTV(L, &tmp, tv);
      setnilV(tv);  /* Clear entry in finalizer table. */
      gc_call_finalizer(g, L, &tmp, o);
    }
    return;
  }
#endif
  /* Add userdata back to the main userdata list and make it white. */
  setgcrefr(o->gch.nextgc, mainthread(g)->nextgc);
  setgcref(mainthread(g)->nextgc, o);
  makewhite(g, o);
  /* Resolve the __gc metamethod. */
  mo = lj_meta_fastg(g, tabref(gco2ud(o)->metatable), MM_gc);
  if (mo)
    gc_call_finalizer(g, L, mo, o);
}

/* Finalize all userdata objects from mmudata list. */
void lj_gc_finalize_udata(lua_State *L)
{
  while (gcref(G(L)->gc.mmudata) != NULL)
    gc_finalize(L);
}

#if LJ_HASFFI
/* Finalize all cdata objects from finalizer table. */
void lj_gc_finalize_cdata(lua_State *L)
{
  global_State *g = G(L);
  CTState *cts = ctype_ctsG(g);
  if (cts) {
    GCtab *t = cts->finalizer;
    Node *node = noderef(t->node);
    ptrdiff_t i;
    setgcrefnull(t->metatable);  /* Mark finalizer table as disabled. */
    for (i = (ptrdiff_t)t->hmask; i >= 0; i--)
      if (!tvisnil(&node[i].val) && tviscdata(&node[i].key)) {
	GCobj *o = gcV(&node[i].key);
	TValue tmp;
	makewhite(g, o);
	o->gch.marked &= (uint8_t)~LJ_GC_CDATA_FIN;
	copyTV(L, &tmp, &node[i].val);
	setnilV(&node[i].val);
	gc_call_finalizer(g, L, &tmp, o);
      }
  }
}
#endif

/* Free all remaining GC objects. */
void lj_gc_freeall(global_State *g)
{
  MSize i, strmask;
  /* Free everything, except super-fixed objects (the main thread). */
  g->gc.currentwhite = LJ_GC_WHITES | LJ_GC_SFIXED;
  gc_fullsweep(g, &g->gc.root);
  strmask = g->strmask;
  for (i = 0; i <= strmask; i++)  /* Free all string hash chains. */
    gc_fullsweep(g, &g->strhash[i]);
}

/* -- Collector ----------------------------------------------------------- */

/* Atomic part of the GC cycle, transitioning from mark to sweep phase. */
static void atomic(global_State *g, lua_State *L)
{
  size_t udsize;

  gc_mark_uv(g);  /* Need to remark open upvalues (the thread may be dead). */
  gc_propagate_gray(g);  /* Propagate any left-overs. */

  setgcrefr(g->gc.gray, g->gc.weak);  /* Empty the list of weak tables. */
  setgcrefnull(g->gc.weak);
  lj_assertG(!iswhite(obj2gco(mainthread(g))), "main thread turned white");
  gc_markobj(g, L);  /* Mark running thread. */
  gc_traverse_curtrace(g);  /* Traverse current trace. */
  gc_mark_gcroot(g);  /* Mark GC roots (again). */
  gc_propagate_gray(g);  /* Propagate all of the above. */

  setgcrefr(g->gc.gray, g->gc.grayagain);  /* Empty the 2nd chance list. */
  setgcrefnull(g->gc.grayagain);
  gc_propagate_gray(g);  /* Propagate it. */

  udsize = lj_gc_separateudata(g, 0);  /* Separate userdata to be finalized. */
  gc_mark_mmudata(g);  /* Mark them. */
  udsize += gc_propagate_gray(g);  /* And propagate the marks. */

  /* All marking done, clear weak tables. */
  gc_clearweak(g, gcref(g->gc.weak));

  lj_buf_shrink(L, &g->tmpbuf, "tmpbuf");  /* Shrink temp buffer. */

  /* Prepare for sweep phase. */
  g->gc.currentwhite = (uint8_t)otherwhite(g);  /* Flip current white. */
  g->strempty.marked = g->gc.currentwhite;
  setmref(g->gc.sweep, &g->gc.root);
  g->gc.estimate = g->gc.total - (GCSize)udsize;  /* Initial estimate. */
}

/* GC state machine. Returns a cost estimate for each step performed. */
static size_t gc_onestep(lua_State *L)
{
  global_State *g = G(L);
  switch (g->gc.state) {
  case GCSpause:
    gc_mark_start(g);  /* Start a new GC cycle by marking all GC roots. */
    return 0;
  case GCSpropagate:
    if (gcref(g->gc.gray) != NULL)
      return propagatemark(g);  /* Propagate one gray object. */
    g->gc.state = GCSatomic;  /* End of mark phase. */
    return 0;
  case GCSatomic:
    if (tvref(g->jit_base))  /* Don't run atomic phase on trace. */
      return LJ_MAX_MEM;
    atomic(g, L);
    g->gc.state = GCSsweepstring;  /* Start of sweep phase. */
    g->gc.sweepstr = 0;
    return 0;
  case GCSsweepstring: {
    GCSize old = g->gc.total;
    gc_fullsweep(g, &g->strhash[g->gc.sweepstr++]);  /* Sweep one chain. */
    if (g->gc.sweepstr > g->strmask)
      g->gc.state = GCSsweep;  /* All string hash chains sweeped. */
    lj_assertG(old >= g->gc.total, "sweep increased memory");
    g->gc.estimate -= old - g->gc.total;
    return GCSWEEPCOST;
    }
  case GCSsweep: {
    GCSize old = g->gc.total;
    setmref(g->gc.sweep, gc_sweep(g, mref(g->gc.sweep, GCRef), GCSWEEPMAX));
    lj_assertG(old >= g->gc.total, "sweep increased memory");
    g->gc.estimate -= old - g->gc.total;
    if (gcref(*mref(g->gc.sweep, GCRef)) == NULL) {
      if (g->strnum <= (g->strmask >> 2) && g->strmask > LJ_MIN_STRTAB*2-1)
	lj_str_resize(L, g->strmask >> 1);  /* Shrink string table. */
      if (gcref(g->gc.mmudata)) {  /* Need any finalizations? */
	g->gc.state = GCSfinalize;
#if LJ_HASFFI
	g->gc.nocdatafin = 1;
#endif
      } else {  /* Otherwise skip this phase to help the JIT. */
	g->gc.state = GCSpause;  /* End of GC cycle. */
	g->gc.debt = 0;
      }
    }
    return GCSWEEPMAX*GCSWEEPCOST;
    }
  case GCSfinalize:
    if (gcref(g->gc.mmudata) != NULL) {
      if (tvref(g->jit_base))  /* Don't call finalizers on trace. */
	return LJ_MAX_MEM;
      gc_finalize(L);  /* Finalize one userdata object. */
      if (g->gc.estimate > GCFINALIZECOST)
	g->gc.estimate -= GCFINALIZECOST;
      return GCFINALIZECOST;
    }
#if LJ_HASFFI
    if (!g->gc.nocdatafin) lj_tab_rehash(L, ctype_ctsG(g)->finalizer);
#endif
    g->gc.state = GCSpause;  /* End of GC cycle. */
    g->gc.debt = 0;
    return 0;
  default:
    lj_assertG(0, "bad GC state");
    return 0;
  }
}

/* Perform a limited amount of incremental GC steps. */
int LJ_FASTCALL lj_gc_step(lua_State *L)
{
  global_State *g = G(L);
  GCSize lim;
  int32_t ostate = g->vmstate;
  setvmstate(g, GC);
  lim = (GCSTEPSIZE/100) * g->gc.stepmul;
  if (lim == 0)
    lim = LJ_MAX_MEM;
  if (g->gc.total > g->gc.threshold)
    g->gc.debt += g->gc.total - g->gc.threshold;
  do {
    lim -= (GCSize)gc_onestep(L);
    if (g->gc.state == GCSpause) {
      g->gc.threshold = (g->gc.estimate/100) * g->gc.pause;
      g->vmstate = ostate;
      return 1;  /* Finished a GC cycle. */
    }
  } while (sizeof(lim) == 8 ? ((int64_t)lim > 0) : ((int32_t)lim > 0));
  if (g->gc.debt < GCSTEPSIZE) {
    g->gc.threshold = g->gc.total + GCSTEPSIZE;
    g->vmstate = ostate;
    return -1;
  } else {
    g->gc.debt -= GCSTEPSIZE;
    g->gc.threshold = g->gc.total;
    g->vmstate = ostate;
    return 0;
  }
}

/* Ditto, but fix the stack top first. */
void LJ_FASTCALL lj_gc_step_fixtop(lua_State *L)
{
  if (curr_funcisL(L)) L->top = curr_topL(L);
  lj_gc_step(L);
}

#if LJ_HASJIT
/* Perform multiple GC steps. Called from JIT-compiled code. */
int LJ_FASTCALL lj_gc_step_jit(global_State *g, MSize steps)
{
  lua_State *L = gco2th(gcref(g->cur_L));
  L->base = tvref(G(L)->jit_base);
  L->top = curr_topL(L);
  while (steps-- > 0 && lj_gc_step(L) == 0)
    ;
  /* Return 1 to force a trace exit. */
  return (G(L)->gc.state == GCSatomic || G(L)->gc.state == GCSfinalize);
}
#endif



static void print_space(FILE *fp, int deep, const char *prefix) {
  int i;
  if(prefix[0] != 0){
    for(i = 0; i < deep; i ++) fputc(' ', fp);
    fprintf(fp, "%s: \n", prefix);
  }
  for(i = 0; i < deep; i ++) fputc(' ', fp);
}

#define VISIT_MASK ((uint8_t) (1 << 1))
#define NEWER_MASK ((uint8_t) (1 << 0))

static uint8_t current_visited_flag = 0;

#define CHANGE_VISIT_FLAG() \
  { current_visited_flag ^= VISIT_MASK; }


static void lj_dump_single_gco(global_State *g, FILE *fp, GCobj *o, int deep, const char *prefix) {

  print_space(fp, deep, prefix);
  if(!o) {
    fprintf(fp, "Non-gc obj\n");
    return;
  }
  if((o->gch.debug_flags & VISIT_MASK) != current_visited_flag ) {
    o->gch.debug_flags &= ~VISIT_MASK;
    o->gch.debug_flags |= current_visited_flag;

    int gct = o->gch.gct;
    if (gct == ~LJ_TTAB) {
      GCtab *t = gco2tab(o);
      int weak = check_table_weak(g, t);
      int weak_val = weak & LJ_GC_WEAKVAL;
      int weak_key = weak & LJ_GC_WEAKKEY;

      fprintf(fp, "TAB[%p]\n", t);        
      GCtab *mt = tabref(t->metatable);
      if (mt)
        lj_dump_single_gco(g, fp, obj2gco(mt), deep + 2, "metatable");

      MSize i, asize = t->asize;
      for (i = 0; i < asize; i++){
        if(tvisgcv(arrayslot(t, i)))
          lj_dump_single_gco(g, fp, gcV(arrayslot(t, i)), deep + 2, weak_val? "array_part_weak" : "array_part");
      }

      if (t->hmask > 0) {  /* Mark hash part. */
        Node *node = noderef(t->node);
        MSize i, hmask = t->hmask;
        for (i = 0; i <= hmask; i++) {
          Node *n = &node[i];
          if (!tvisnil(&n->val)) {  /* Mark non-empty slot. */
            if(tvisgcv(&n->key)){
              lj_dump_single_gco(g, fp, gcV(&n->key), deep+2,  weak_key? "hashpart_key_weak" : "hashpart_key");
            }
            else{
              lj_dump_single_gco(g, fp, NULL, deep+2,  weak_key? "hashpart_key_weak" : "hashpart_key");
            }
            if(tvisgcv(&n->val)){
              lj_dump_single_gco(g, fp, gcV(&n->val), deep+2, weak_val? "hashpart_value_weak" : "hashpart_value");
            }
            else{
              lj_dump_single_gco(g, fp, NULL, deep+2, weak_val? "hashpart_value_weak" : "hashpart_value");
            }
          }
        }
      }
    } 
    else if( gct == ~LJ_TSTR) {
      GCstr *str = gco2str(o);
      fprintf(fp, "STR[%p, fixed: %d]: (%d) ", str, (o->gch.marked & LJ_GC_FIXED) != 0, str->len);
      fwrite(strdata(str), 1, str->len, fp);
      fprintf(fp, "\n");
    }
    else if(gct == ~LJ_TUPVAL) {
      GCupval *uv = gco2uv(o);
      fprintf(fp, "UV[%p]\n", uv);
      if(tvisgcv(uvval(uv))){
        lj_dump_single_gco(g, fp, gcV(uvval(uv)), deep+2, "uv_content");
      }
      else {
        lj_dump_single_gco(g, fp, NULL, deep+2, "uv_content");
      }
    }
    else if(gct == ~LJ_TTHREAD) {
      lua_State *th = gco2th(o);
      fprintf(fp, "Thread[%p]\n", th);
      TValue *slot, *top = th->top;
      for (slot = tvref(th->stack)+1+LJ_FR2; slot < top; slot++){
        if(tvisgcv(slot))
          lj_dump_single_gco(g, fp, gcV(slot), deep+2, "stack");
      }
      lj_dump_single_gco(g, fp, obj2gco(tabref(th->env)), deep+2, "env");

    }
    else if(gct == ~LJ_TPROTO) {
      GCproto *pt = gco2pt(o);
      fprintf(fp, "Proto[%p, firstline: %d]\n", pt, pt->firstline);

      lj_dump_single_gco(g, fp, obj2gco(proto_chunkname(pt)), deep+2, "chunkname");

      ptrdiff_t i;
      for (i = -(ptrdiff_t)pt->sizekgc; i < 0; i++) {
        lj_dump_single_gco(g, fp, proto_kgc(pt, i), deep+2, "kgc");
      }

    }
    else if(gct == ~LJ_TFUNC) {
      GCfunc * fn = gco2func(o);
      fprintf(fp, "Func[%p]\n", fn);
      lj_dump_single_gco(g, fp, obj2gco(tabref(fn->c.env)), deep+2, "env");

      if (isluafunc(fn)) {
        lj_dump_single_gco(g, fp, obj2gco(funcproto(fn)), deep+2, "proto");
        uint32_t i;
        for (i = 0; i < fn->l.nupvalues; i++)  /* Mark Lua function upvalues. */
          lj_dump_single_gco(g, fp, obj2gco(&gcref(fn->l.uvptr[i])->uv), deep+2, "uv");
      } else {
        uint32_t i;
        for (i = 0; i < fn->c.nupvalues; i++) {
          if(tvisgcv(&fn->c.upvalue[i])){
            lj_dump_single_gco(g, fp, gcV(&fn->c.upvalue[i]), deep+2, "uv");
          }
          else {
            lj_dump_single_gco(g, fp, NULL, deep+2, "uv");
          }
        }
      }
    }
    else if(gct == ~LJ_TUDATA) {
      GCudata *ud = gco2ud(o);
      fprintf(fp, "udata[%p]\n", ud);
      GCtab *mt = tabref(ud->metatable);
      if(mt) {
        lj_dump_single_gco(g, fp, obj2gco(mt), deep+2, "meta");
      }
    }
    else {
      fprintf(fp, "TODO[%p]\n", o);
    }
  }
  else {
    fprintf(fp, "already dump[%p]\n", o);
  }
}

static void lj_gc_dump_gcroot(global_State *g, FILE *fp)
{
  ptrdiff_t i;
  char prefix[] = "gcroot_00000";
  for (i = 0; i < GCROOT_MAX; i++)
    if (gcref(g->gcroot[i]) != NULL){
      sprintf(prefix, "gc_root_%ld", i);
      lj_dump_single_gco(g, fp, gcref(g->gcroot[i]), 0, prefix);
    }
}

static void _lj_gc_dump(global_State *g, const char *path) {
  CHANGE_VISIT_FLAG();
  FILE * fp = fopen(path, "wb");
  fprintf(fp, "lj_gc_dump: total %llu\n", (unsigned long long)g->gc.total);
  lj_dump_single_gco(g, fp, obj2gco(mainthread(g)), 0, "mainthread");
  lj_dump_single_gco(g, fp, obj2gco(tabref(mainthread(g)->env)), 0, "mainthread_env");
  lj_dump_single_gco(g, fp, gcV(&g->registrytv), 0, "registrytv");
  lj_gc_dump_gcroot(g, fp);
  fclose(fp);
}

void lj_gc_dump(lua_State *L, const char *path) {
  for(int i = 0; i < 16; i ++){
    lj_gc_fullgc(L);
  }
  _lj_gc_dump(G(L), path);
}

typedef int (*PtrCompare)(void *a, void *b);

typedef struct PtrArray {
  void * *base;
  MSize nr;
  MSize alloc;
}PtrArray;

#define SWAP(a, b, tmp) { \
  tmp = a; \
  a = b; \
  b = tmp; \
}

static int PtrArray_compare(void *a, void *b, PtrCompare compare) {
  if (compare) return compare(a, b);
  uintptr_t pa = (uintptr_t)a;
  uintptr_t pb = (uintptr_t)b;
  return pa < pb ? -1 : pa > pb ? 1 : 0;
}

static void PtrArray_sift(void **base, MSize root, MSize count, PtrCompare compare) {
  while (root < count / 2) {
    MSize child = root * 2 + 1;
    if (child + 1 < count && PtrArray_compare(base[child], base[child+1], compare) < 0)
      child++;
    if (PtrArray_compare(base[root], base[child], compare) >= 0)
      return;
    void *tmp = base[root];
    base[root] = base[child];
    base[child] = tmp;
    root = child;
  }
}

static void PtrArray_sort(PtrArray *self, PtrCompare compare) {
  MSize count = self->nr;
  for (MSize start = count / 2; start > 0; start--)
    PtrArray_sift(self->base, start - 1, count, compare);
  while (count > 1) {
    count--;
    void *tmp = self->base[0];
    self->base[0] = self->base[count];
    self->base[count] = tmp;
    PtrArray_sift(self->base, 0, count, compare);
  }
}

static void PtrArray_append(PtrArray *self, void *data) {
  if(self->nr == self->alloc) {
    MSize new_alloc = self->alloc * 3/2 + 32;
    self->base = (void**) realloc(self->base, sizeof(void *) * new_alloc);
    self->alloc = new_alloc;
  }
  self->base[(self->nr) ++] = data;
}

static void PtrArray_clear(PtrArray *self) {
  self->nr = 0;
}

static int PtrArray_bin_search(PtrArray *self, void *data) {
  MSize s = 0;
  MSize e = self->nr;
  MSize next;
  while(s < e) {
    next = (s+e) >> 1;
    if(self->base[next] == data) 
      return 1;
    else if(self->base[next] < data) {
      s = next + 1;
    }
    else {
      e = next;
    }
  }

  return 0;
}

static PtrArray ignore_objs = {NULL, 0, 0};
static PtrArray ignore_keys = {NULL, 0, 0};

static PtrArray obj_ptr_array[2] = {
  {NULL, 0, 0},
  {NULL, 0, 0},
};

static PtrArray diff_objs = {NULL, 0, 0};

/* Parallel to the sorted address snapshots. No extra GC header fields or
** allocation/free hooks: only diff() reads and stores these 32-bit sizes.
** Keep libc-owned bookkeeping outside the Lua heap and its reference graph. */
typedef struct Alloc2Sizes {
  uint32_t *base;
  MSize alloc;
  uint64_t reachable_bytes;
  uint64_t lua_bytes;
  uint64_t string_hash_bytes;
  uint64_t temp_buffer_bytes;
} Alloc2Sizes;

static Alloc2Sizes obj_sizes[2];
static int current_ptr_group = 0;

/* Requested allocation bytes, matching the corresponding free routines.
** Include table array/hash capacities, even when their entries are empty.
** Native allocations referenced by userdata and allocator overhead are excluded. */
static uint64_t alloc2_object_bytes(global_State *g, GCobj *o)
{
  int gct = o->gch.gct;
  if (gct == ~LJ_TSTR) return sizestring(gco2str(o));
  if (gct == ~LJ_TTAB) {
    GCtab *t = gco2tab(o);
    uint64_t n = sizeof(GCtab);
    if (t->hmask > 0) n += (uint64_t)(t->hmask+1)*sizeof(Node);
    if (LJ_MAX_COLOSIZE != 0 && t->colo)
      n += ((uint32_t)t->colo & 0x7f)*sizeof(TValue);
    if (t->asize > 0 && LJ_MAX_COLOSIZE != 0 && t->colo <= 0)
      n += (uint64_t)t->asize*sizeof(TValue);
    return n;
  }
  if (gct == ~LJ_TFUNC) {
    GCfunc *fn = gco2func(o);
    return isluafunc(fn) ? sizeLfunc(fn->l.nupvalues) : sizeCfunc(fn->c.nupvalues);
  }
  if (gct == ~LJ_TPROTO) return gco2pt(o)->sizept;
  if (gct == ~LJ_TUPVAL) return sizeof(GCupval);
  if (gct == ~LJ_TTHREAD)
    return sizeof(lua_State) + (uint64_t)gco2th(o)->stacksize*sizeof(TValue);
  if (gct == ~LJ_TUDATA) return sizeudata(gco2ud(o));
#if LJ_HASFFI
  if (gct == ~LJ_TCDATA) {
    GCcdata *cd = gco2cd(o);
    if (cdataisv(cd)) return sizecdatav(cd);
    CType *ct = ctype_raw(ctype_ctsG(g), cd->ctypeid);
    return sizeof(GCcdata) + (uint64_t)(ctype_hassize(ct->info) ? ct->size : CTSIZE_PTR);
  }
#endif
#if LJ_HASJIT
  if (gct == ~LJ_TTRACE) {
    GCtrace *T = gco2trace(o);
    return ((sizeof(GCtrace)+7)&~7) + (uint64_t)(T->nins-T->nk)*sizeof(IRIns) +
      (uint64_t)T->nsnap*sizeof(SnapShot) + (uint64_t)T->nsnapmap*sizeof(SnapEntry);
  }
#endif
  return 0;
}

static GCobj non_gc;

typedef void (*Visitor)(GCobj *obj);
static void traverse_all(global_State *g, Visitor visitor, PtrArray *ignore_objs, PtrArray *ignore_keys) {
  CHANGE_VISIT_FLAG();
// 使用宽度优先,所以需要一个队列存储后续要遍历的对象
  List to_visit_queue = List_init(&to_visit_queue);
  
  #define __ADD_TO_QUEUE(o, parent, desc, param) { \
    if( (((o)->gch.debug_flags & VISIT_MASK) != current_visited_flag) || ( ((o)->gch.debug_flags & NEWER_MASK) == 0 ) )  { \
      (o)->gch.debug_flags &= ~VISIT_MASK; \
      (o)->gch.debug_flags |= current_visited_flag; \
      (o)->gch.debug_flags |= NEWER_MASK; \
      List_append(&to_visit_queue, &((o)->gch.debug_list)); \
      o->gch.debug_path = (parent); \
      o->gch.debug_path_desc = (desc); \
      o->gch.debug_path_param = (param); \
    } \
  } 

  #define ADD_TO_QUEUE(o, parent, desc, param) if(!PtrArray_bin_search(ignore_objs, o)) {__ADD_TO_QUEUE((o), (parent), (desc), (param));}


  ADD_TO_QUEUE(obj2gco(gcV(&g->registrytv)), NULL, "register", NULL);
  ADD_TO_QUEUE(obj2gco(mainthread(g)), NULL, "mainthread", NULL);
  ADD_TO_QUEUE(obj2gco(tabref(mainthread(g)->env)), NULL, "_G", NULL);

  ptrdiff_t i;
  for (i = 0; i < GCROOT_MAX; i++){
    if (gcref(g->gcroot[i]) != NULL){
      ADD_TO_QUEUE(gcref(g->gcroot[i]), NULL, "gc_root", NULL);
    }
  }


  List *elem;
  GCobj *obj;
  while(!List_empty(&to_visit_queue)) {
    elem = List_first(&to_visit_queue);
    List_remove(elem);
    obj = obj2gco(List_entry(elem, GChead, debug_list));
    visitor(obj);

    int gct = obj->gch.gct;
    if (gct == ~LJ_TTAB) {
      GCtab *t = gco2tab(obj);
      int weak = check_table_weak(g, t);
      int weak_val = weak & LJ_GC_WEAKVAL;
      int weak_key = weak & LJ_GC_WEAKKEY;

      GCtab *mt = tabref(t->metatable);
      if (mt){
        ADD_TO_QUEUE(obj2gco(mt), obj, "metatable", NULL);
      }

      MSize i, asize = t->asize;
      for (i = 0; i < asize; i++){
        if(tvisgcv(arrayslot(t, i))){
          if(!weak_val) {
            ADD_TO_QUEUE(gcV(arrayslot(t, i)), obj, "array_value", NULL);
          }
        }
      }

      if (t->hmask > 0) {  /* Mark hash part. */
        Node *node = noderef(t->node);
        MSize i, hmask = t->hmask;
        for (i = 0; i <= hmask; i++) {
          Node *n = &node[i];
          if (!tvisnil(&n->val)) {  /* Mark non-empty slot. */
            if(tvisgcv(&n->key)){
              if(!weak_key){
                ADD_TO_QUEUE(gcV(&n->key), obj, "key", NULL);
              }
            }
            if(tvisgcv(&n->val)){
              if(!weak_val) {

                if(! PtrArray_bin_search(ignore_keys, gcV(&n->key) )){
                  ADD_TO_QUEUE(gcV(&n->val), obj, "hash_value", tvisgcv(&n->key)? gcV(&n->key): &non_gc );
                }
              }
            }
          }
        }
      }
    } 
    else if( gct == ~LJ_TSTR) {
    }
    else if(gct == ~LJ_TUPVAL) {
      GCupval *uv = gco2uv(obj);
      if(tvisgcv(uvval(uv))){
        ADD_TO_QUEUE(gcV(uvval(uv)), obj, "uv_content", NULL);
      }
    }
    else if(gct == ~LJ_TTHREAD) {
      lua_State *th = gco2th(obj);
      TValue *slot, *top = th->top;
      for (slot = tvref(th->stack)+1+LJ_FR2; slot < top; slot++){
        if(tvisgcv(slot)){
          ADD_TO_QUEUE(gcV(slot), obj, "stack", NULL);
        }
      }
      ADD_TO_QUEUE(obj2gco(tabref(th->env)), obj, "env", NULL);

    }
    else if(gct == ~LJ_TPROTO) {
      GCproto *pt = gco2pt(obj);
      ADD_TO_QUEUE(obj2gco(proto_chunkname(pt)), obj, "chunkname", NULL);

      ptrdiff_t i;
      for (i = -(ptrdiff_t)pt->sizekgc; i < 0; i++) {
        ADD_TO_QUEUE(proto_kgc(pt, i), obj, "kgc", NULL);
      }

    }
    else if(gct == ~LJ_TFUNC) {
      GCfunc * fn = gco2func(obj);
      ADD_TO_QUEUE(obj2gco(tabref(fn->c.env)), obj, "env", NULL);

      if (isluafunc(fn)) {
        ADD_TO_QUEUE(obj2gco(funcproto(fn)), obj, "proto", NULL);
        uint32_t i;
        for (i = 0; i < fn->l.nupvalues; i++)  /* Mark Lua function upvalues. */
          ADD_TO_QUEUE(obj2gco(&gcref(fn->l.uvptr[i])->uv), obj, "l_upval", NULL);
      } else {
        uint32_t i;
        for (i = 0; i < fn->c.nupvalues; i++) {
          if(tvisgcv(&fn->c.upvalue[i])){
            ADD_TO_QUEUE(gcV(&fn->c.upvalue[i]), obj, "c_upval", NULL);
          }
        }
      }
    }
    else if(gct == ~LJ_TUDATA) {
      GCudata *ud = gco2ud(obj);
      GCtab *mt = tabref(ud->metatable);
      if(mt) {
        ADD_TO_QUEUE(obj2gco(mt), obj, "metatable", NULL);
      }
    }
  }
}

static FILE *gc_debug_output = NULL;

LUA_API void luaL_gcdebugoutput(FILE *stream) {
  gc_debug_output = stream;
}

static FILE *lj_gc_debug_stream(void) {
  return gc_debug_output ? gc_debug_output : stdout;
}

static void test_visitor(GCobj *obj) {
  fprintf(lj_gc_debug_stream(), "test_visitor: %p\n", obj);
}

static void *__find_target = NULL;

static void findf_visitor(GCobj *obj) {
  if(obj == __find_target) {
    if (obj->gch.gct != (uint8_t)~LJ_TFUNC || !isluafunc(gco2func(obj))) {
      fprintf(lj_gc_debug_stream(), "findf: address is not a Lua function\n");
      return;
    }
    GCfunc *fn = gco2func(obj);
    GCproto* proto = funcproto(fn);
    fprintf(lj_gc_debug_stream(), "find it %s %d\n", strdata(proto_chunkname(proto)), proto->firstline);
  }
}


static void save_ptr(GCobj *obj) {
  PtrArray_append(&obj_ptr_array[current_ptr_group], obj);
  obj->gch.debug_counter = 0;
}

/* This sequential pass runs once per capture, after the existing address sort.
** Old addresses may be freed: only read their saved sizes, never dereference them. */
static int alloc2_capture_sizes(global_State *g)
{
  PtrArray *objects = &obj_ptr_array[current_ptr_group];
  Alloc2Sizes *sizes = &obj_sizes[current_ptr_group];
  if (sizes->alloc < objects->nr) {
    uint32_t *p = (uint32_t *)realloc(sizes->base, sizeof(uint32_t)*(size_t)objects->nr);
    if (!p) return 0;
    sizes->base = p;
    sizes->alloc = objects->nr;
  }
  sizes->reachable_bytes = 0;
  sizes->lua_bytes = g->gc.total;
  sizes->string_hash_bytes = ((uint64_t)g->strmask+1)*sizeof(GCRef);
  sizes->temp_buffer_bytes = sbufsz(&g->tmpbuf);
  for (MSize i = 0; i < objects->nr; i++) {
    uint64_t n = alloc2_object_bytes(g, (GCobj *)objects->base[i]);
    /* Reject unsupported/oversized objects rather than silently truncate totals. */
    if (!n || n > UINT32_MAX) return 0;
    sizes->base[i] = (uint32_t)n;
    sizes->reachable_bytes += n;
  }
  return 1;
}

typedef struct Alloc2Delta {
  uint64_t added_bytes, removed_bytes;
  int64_t resized_bytes;
  MSize removed_objects, resized_objects;
} Alloc2Delta;

static Alloc2Delta diff_ptr_group(List *resized)
{
  int old_group = 1 - current_ptr_group;
  PtrArray *old = &obj_ptr_array[old_group];
  PtrArray *cur = &obj_ptr_array[current_ptr_group];
  Alloc2Sizes *before = &obj_sizes[old_group];
  Alloc2Sizes *after = &obj_sizes[current_ptr_group];
  Alloc2Delta delta = {0, 0, 0, 0, 0};
  MSize oi = 0, ni = 0;
  PtrArray_clear(&diff_objs);
  while (oi < old->nr || ni < cur->nr) {
    if (oi < old->nr && ni < cur->nr && old->base[oi] == cur->base[ni]) {
      int64_t change = (int64_t)after->base[ni] - before->base[oi];
      if (change) {
        GCobj *obj = (GCobj *)cur->base[ni];
        /* The BFS queue is drained and counters are still zero. Reuse its
        ** existing links and counter until resize paths have been printed. */
        List_append(resized, &obj->gch.debug_list);
        obj->gch.debug_counter = (int32_t)before->base[oi];
        delta.resized_objects++;
        delta.resized_bytes += change;
      }
      oi++; ni++;
    } else if (ni < cur->nr && (oi == old->nr ||
               (uintptr_t)cur->base[ni] < (uintptr_t)old->base[oi])) {
      PtrArray_append(&diff_objs, cur->base[ni]);
      delta.added_bytes += after->base[ni++];
    } else {
      delta.removed_bytes += before->base[oi++];
      delta.removed_objects++;
    }
  }
  return delta;
}

static int gco_debug_counter_compare(void *pa, void *pb) {
  GCobj *a = (GCobj *)pa;
  GCobj *b = (GCobj *)pb;
  int ca = a->gch.debug_counter;
  int cb = b->gch.debug_counter;
  if(ca < cb)
    return -1;
  else if(ca == cb)
    return 0;
  return 1;
}

void dump_gcobj(GCobj *obj) {
  int gct = obj->gch.gct;
  if (gct == ~LJ_TTAB) {
    fprintf(lj_gc_debug_stream(), "TABLE");
  } 
  else if( gct == ~LJ_TSTR) {
    fprintf(lj_gc_debug_stream(), "STR(");
    GCstr *str = gco2str(obj);
    fwrite(strdata(str), 1, str->len, lj_gc_debug_stream());
    fprintf(lj_gc_debug_stream(), ")");
  }
  else if(gct == ~LJ_TUPVAL) {
    fprintf(lj_gc_debug_stream(), "UPVAL");
  }
  else if(gct == ~LJ_TTHREAD) {
    fprintf(lj_gc_debug_stream(), "THREAD");

  }
  else if(gct == ~LJ_TPROTO) {
    fprintf(lj_gc_debug_stream(), "PROTO");
  }
  else if(gct == ~LJ_TFUNC) {
    fprintf(lj_gc_debug_stream(), "FUNC");
  }
  else if(gct == ~LJ_TUDATA) {
    fprintf(lj_gc_debug_stream(), "UDATA");
  }
  else if(gct == ~LJ_TCDATA) {
    fprintf(lj_gc_debug_stream(), "CDATA");
  }
  else if(gct == ~LJ_TTRACE) {
    fprintf(lj_gc_debug_stream(), "TRACE");
  }
}

static PtrArray stack_tmp = {NULL, 0, 0};

/* Reuse the existing intrusive BFS parent links. Byte sizes are self sizes,
** not retained subtree sizes: shared objects must not be charged repeatedly. */
static void alloc2_dump_path(global_State *g, GCobj *leaf, int show_counter)
{
  PtrArray_clear(&stack_tmp);
  for (GCobj *ptr = leaf; ptr; ptr = ptr->gch.debug_path)
    PtrArray_append(&stack_tmp, ptr);
  for (MSize k = stack_tmp.nr; k > 0; k--) {
    GCobj *ptr = (GCobj *)stack_tmp.base[k-1];
    if (ptr->gch.debug_path_desc) {
      fprintf(lj_gc_debug_stream(), "(desc:%s) ", ptr->gch.debug_path_desc);
      if (ptr->gch.debug_path_param) {
        fprintf(lj_gc_debug_stream(), "(param: ");
        if (&non_gc == ptr->gch.debug_path_param)
          fprintf(lj_gc_debug_stream(), "non_gc");
        else
          dump_gcobj(ptr->gch.debug_path_param);
        fprintf(lj_gc_debug_stream(), ")");
      }
    }
    dump_gcobj(ptr);
    fprintf(lj_gc_debug_stream(), "(addr: %p)", (void *)ptr);
    if (show_counter)
      fprintf(lj_gc_debug_stream(), "(counter: %d)", ptr->gch.debug_counter);
    if (ptr == leaf)
      fprintf(lj_gc_debug_stream(), "(self_bytes: %llu)", (unsigned long long)alloc2_object_bytes(g, ptr));
    fprintf(lj_gc_debug_stream(), " ->");
  }
  fprintf(lj_gc_debug_stream(), "\n");
}

static void alloc2_print_totals(int old_group, int has_baseline)
{
  Alloc2Sizes *cur = &obj_sizes[current_ptr_group];
  Alloc2Sizes *old = &obj_sizes[old_group];
  int64_t other = (int64_t)cur->lua_bytes - (int64_t)cur->reachable_bytes;
  fprintf(lj_gc_debug_stream(),
    "[alloc2] lua_bytes=%llu reachable_bytes=%llu other_bytes=%lld "
    "string_hash_bytes=%llu temp_buffer_bytes=%llu size_metadata_bytes=%llu\n",
    (unsigned long long)cur->lua_bytes, (unsigned long long)cur->reachable_bytes,
    (long long)other, (unsigned long long)cur->string_hash_bytes,
    (unsigned long long)cur->temp_buffer_bytes,
    (unsigned long long)(sizeof(uint32_t)*((uint64_t)obj_sizes[0].alloc + obj_sizes[1].alloc)));
  if (has_baseline) {
    int64_t ld = (int64_t)cur->lua_bytes - (int64_t)old->lua_bytes;
    int64_t rd = (int64_t)cur->reachable_bytes - (int64_t)old->reachable_bytes;
    fprintf(lj_gc_debug_stream(),
      "[alloc2] lua_delta_bytes=%lld reachable_delta_bytes=%lld other_delta_bytes=%lld "
      "string_hash_delta_bytes=%lld temp_buffer_delta_bytes=%lld\n",
      (long long)ld, (long long)rd, (long long)(ld-rd),
      (long long)((int64_t)cur->string_hash_bytes - (int64_t)old->string_hash_bytes),
      (long long)((int64_t)cur->temp_buffer_bytes - (int64_t)old->temp_buffer_bytes));
  }
}

static void __lj_gc_diff(lua_State *L) {
  global_State *g = G(L);
  for (int i = 0; i < 16; i++) lj_gc_fullgc(L);
  current_ptr_group = 1 - current_ptr_group;
  PtrArray_clear(&obj_ptr_array[current_ptr_group]);
  traverse_all(g, &save_ptr, &ignore_objs, &ignore_keys);
  PtrArray_sort(&obj_ptr_array[current_ptr_group], NULL);
  if (!alloc2_capture_sizes(g)) {
    fprintf(lj_gc_debug_stream(), "[alloc2] size capture failed (memory/unsupported size); baseline unchanged\n");
    PtrArray_clear(&obj_ptr_array[current_ptr_group]);
    current_ptr_group = 1 - current_ptr_group;
    return;
  }

  int old_group = 1 - current_ptr_group;
  MSize old_nr = obj_ptr_array[old_group].nr;
  if (old_nr == 0) {
    fprintf(lj_gc_debug_stream(), "[alloc2] baseline_objects=%u\n", obj_ptr_array[current_ptr_group].nr);
    alloc2_print_totals(old_group, 0);
    return;
  }

  List resized = List_init(&resized);
  Alloc2Delta delta = diff_ptr_group(&resized);
  fprintf(lj_gc_debug_stream(),
    "[alloc2] baseline_objects=%u current_objects=%u added_objects=%u removed_objects=%u resized_objects=%u\n",
    old_nr, obj_ptr_array[current_ptr_group].nr, diff_objs.nr, delta.removed_objects, delta.resized_objects);
  fprintf(lj_gc_debug_stream(),
    "[alloc2] added_bytes=%llu removed_bytes=%llu resized_delta_bytes=%lld\n",
    (unsigned long long)delta.added_bytes, (unsigned long long)delta.removed_bytes,
    (long long)delta.resized_bytes);
  alloc2_print_totals(old_group, 1);
  fflush(lj_gc_debug_stream());

  /* Drain the temporary intrusive queue before counting added descendants.
  ** No saved address is dereferenced: only live objects enter this list. */
  while (!List_empty(&resized)) {
    List *elem = List_first(&resized);
    List_remove(elem);
    GCobj *obj = obj2gco(List_entry(elem, GChead, debug_list));
    uint32_t before = (uint32_t)obj->gch.debug_counter;
    uint64_t after = alloc2_object_bytes(g, obj);
    obj->gch.debug_counter = 0;
    fprintf(lj_gc_debug_stream(),
      "[alloc2-resize] old_bytes=%u new_bytes=%llu delta_bytes=%lld ",
      before, (unsigned long long)after, (long long)((int64_t)after-before));
    alloc2_dump_path(g, obj, 0);
  }

  for (MSize i = 0; i < diff_objs.nr; i++) {
    GCobj *ptr = (GCobj *)diff_objs.base[i];
    while (ptr) {
      ptr->gch.debug_counter++;
      ptr = ptr->gch.debug_path;
    }
  }
  PtrArray_sort(&diff_objs, gco_debug_counter_compare);
  for (MSize i = 0; i < diff_objs.nr; i++)
    alloc2_dump_path(g, (GCobj *)diff_objs.base[i], 1);


}

void lj_gc_ignore_obj(lua_State *L, void *obj) {
  current_ptr_group = 1 - current_ptr_group;
  PtrArray_append(&ignore_objs, obj);
  PtrArray_sort(&ignore_objs, NULL);
  __lj_gc_diff(L);
}

void lj_gc_findf(lua_State *L, void *obj) {
  __find_target = obj;
  traverse_all(G(L), &findf_visitor, &ignore_objs, &ignore_keys);
}


void lj_gc_ignore_key(lua_State *L, void *key) {
  current_ptr_group = 1 - current_ptr_group;
  PtrArray_append(&ignore_keys, key);
  PtrArray_sort(&ignore_keys, NULL);
  __lj_gc_diff(L);
}


void lj_gc_diff(lua_State *L) {
  PtrArray_clear(&ignore_objs);
  PtrArray_clear(&ignore_keys);
  __lj_gc_diff(L);
}

/* Perform a full GC cycle. */
void lj_gc_fullgc(lua_State *L)
{
  global_State *g = G(L);
  int32_t ostate = g->vmstate;
  setvmstate(g, GC);
  if (g->gc.state <= GCSatomic) {  /* Caught somewhere in the middle. */
    setmref(g->gc.sweep, &g->gc.root);  /* Sweep everything (preserving it). */
    setgcrefnull(g->gc.gray);  /* Reset lists from partial propagation. */
    setgcrefnull(g->gc.grayagain);
    setgcrefnull(g->gc.weak);
    g->gc.state = GCSsweepstring;  /* Fast forward to the sweep phase. */
    g->gc.sweepstr = 0;
  }
  while (g->gc.state == GCSsweepstring || g->gc.state == GCSsweep)
    gc_onestep(L);  /* Finish sweep. */
  lj_assertG(g->gc.state == GCSfinalize || g->gc.state == GCSpause,
	     "bad GC state");
  /* Now perform a full GC. */
  g->gc.state = GCSpause;
  do { gc_onestep(L); } while (g->gc.state != GCSpause);
  g->gc.threshold = (g->gc.estimate/100) * g->gc.pause;
  g->vmstate = ostate;

}

/* -- Write barriers ------------------------------------------------------ */

/* Move the GC propagation frontier forward. */
void lj_gc_barrierf(global_State *g, GCobj *o, GCobj *v)
{
  lj_assertG(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o),
	     "bad object states for forward barrier");
  lj_assertG(g->gc.state != GCSfinalize && g->gc.state != GCSpause,
	     "bad GC state");
  lj_assertG(o->gch.gct != ~LJ_TTAB, "barrier object is not a table");
  /* Preserve invariant during propagation. Otherwise it doesn't matter. */
  if (g->gc.state == GCSpropagate || g->gc.state == GCSatomic)
    gc_mark(g, v);  /* Move frontier forward. */
  else
    makewhite(g, o);  /* Make it white to avoid the following barrier. */
}

/* Specialized barrier for closed upvalue. Pass &uv->tv. */
void LJ_FASTCALL lj_gc_barrieruv(global_State *g, TValue *tv)
{
#define TV2MARKED(x) \
  (*((uint8_t *)(x) - offsetof(GCupval, tv) + offsetof(GCupval, marked)))
  if (g->gc.state == GCSpropagate || g->gc.state == GCSatomic)
    gc_mark(g, gcV(tv));
  else
    TV2MARKED(tv) = (TV2MARKED(tv) & (uint8_t)~LJ_GC_COLORS) | curwhite(g);
#undef TV2MARKED
}

/* Close upvalue. Also needs a write barrier. */
void lj_gc_closeuv(global_State *g, GCupval *uv)
{
  GCobj *o = obj2gco(uv);
  /* Copy stack slot to upvalue itself and point to the copy. */
  copyTV(mainthread(g), &uv->tv, uvval(uv));
  setmref(uv->v, &uv->tv);
  uv->closed = 1;
  setgcrefr(o->gch.nextgc, g->gc.root);
  setgcref(g->gc.root, o);
  if (isgray(o)) {  /* A closed upvalue is never gray, so fix this. */
    if (g->gc.state == GCSpropagate || g->gc.state == GCSatomic) {
      gray2black(o);  /* Make it black and preserve invariant. */
      if (tviswhite(&uv->tv))
	lj_gc_barrierf(g, o, gcV(&uv->tv));
    } else {
      makewhite(g, o);  /* Make it white, i.e. sweep the upvalue. */
      lj_assertG(g->gc.state != GCSfinalize && g->gc.state != GCSpause,
		 "bad GC state");
    }
  }
}

#if LJ_HASJIT
/* Mark a trace if it's saved during the propagation phase. */
void lj_gc_barriertrace(global_State *g, uint32_t traceno)
{
  if (g->gc.state == GCSpropagate || g->gc.state == GCSatomic)
    gc_marktrace(g, traceno);
}
#endif

/* -- Allocator ----------------------------------------------------------- */

/* Call pluggable memory allocator to allocate or resize a fragment. */
void *lj_mem_realloc(lua_State *L, void *p, GCSize osz, GCSize nsz, const char *reason)
{
  global_State *g = G(L);
  lj_assertG((osz == 0) == (p == NULL), "realloc API violation");
  p = g->allocf(g->allocd, p, osz, nsz);
  if (p == NULL && nsz > 0)
    lj_err_mem(L);
  lj_assertG((nsz == 0) == (p == NULL), "allocf API violation");
  lj_assertG(checkptrGC(p),
	     "allocated memory address %p outside required range", p);
  g->gc.total = (g->gc.total - osz) + nsz;
  lj_alloc_debug((long long)(nsz) - (long long)(osz), reason);
  return p;
}

/* Allocate new GC object and link it to the root set. */
void * LJ_FASTCALL lj_mem_newgco(lua_State *L, GCSize size, const char *reason)
{
  global_State *g = G(L);
  GCobj *o = (GCobj *)g->allocf(g->allocd, NULL, 0, size);
  if (o == NULL)
    lj_err_mem(L);
  lj_assertG(checkptrGC(o),
	     "allocated memory address %p outside required range", o);
  g->gc.total += size;
  lj_alloc_debug((long long )size, reason);
  setgcrefr(o->gch.nextgc, g->gc.root);
  setgcref(g->gc.root, o);
  newwhite(g, o);
  return o;
}

/* Resize growable vector. */
void *lj_mem_grow(lua_State *L, void *p, MSize *szp, MSize lim, MSize esz, const char *reason)
{
  MSize sz = (*szp) << 1;
  if (sz < LJ_MIN_VECSZ)
    sz = LJ_MIN_VECSZ;
  if (sz > lim)
    sz = lim;
  p = lj_mem_realloc(L, p, (*szp)*esz, sz*esz, reason);
  *szp = sz;
  return p;
}

