/* The engine binder (src/wire_engine.c) over the real bridge (src/engine.c, src/wire_win_engine.c) and a
 * small stand-in for lua51.dll: a value stack, a registry, tables, protected calls and the engine's
 * script functions as C functions that write down how they were called. engine.c is compiled into
 * this file over tests/wire_engine_host/windows.h and gets its table of Lua C API functions from
 * here, where the DLL would fill it by name. No game, no Wine. */
#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
uint64_t tfp_test_ticks = 123456;
#include "../src/engine.c"
#include "../src/wire_win_engine.c"
#include "../src/wire_off.h"
#include "../src/head_visibility.h"

static int checks;
static int failed(const char *condition, int line);
#define CHECK(condition) do { checks++; if (!(condition)) return failed(#condition, __LINE__); } while (0)

/* ---- the session log ---- */
static char lines[400][900];
static int line_count;
static void capture(void *user, const char *text) {
  (void)user;
  if (line_count < 400) snprintf(lines[line_count++], sizeof lines[0], "%s", text);
}
static bool logged(const char *part) {
  for (int i = 0; i < line_count; i++) if (strstr(lines[i], part)) return true;
  return false;
}
static char calls[8192];
static int failed(const char *condition, int line) { /* with what was logged and called since the last fresh() */
  printf("wire engine: FAILED %s (line %d)\n", condition, line);
  for (int i = 0; i < line_count; i++) printf("  log: %s\n", lines[i]);
  printf("  calls: %s\n", calls);
  return 1;
}
static int logged_count(const char *part) {
  int n = 0;
  for (int i = 0; i < line_count; i++) n += strstr(lines[i], part) != NULL;
  return n;
}

/* ---- game memory ---- */
#define EXE UINT64_C(0x140000000)
static struct { uint64_t at; size_t size; uint8_t *bytes; } patches[96];
static int patch_count;
static uint64_t unreadable; /* an address that fails */
static uint8_t *mem_map(uint64_t at, size_t size) {
  patches[patch_count].at = at, patches[patch_count].size = size, patches[patch_count].bytes = calloc(1, size);
  return patches[patch_count++].bytes;
}
static bool mem_read(void *user, uint64_t at, void *out, size_t size) {
  (void)user;
  if (unreadable && at <= unreadable && unreadable < at + size) return false;
  for (int i = 0; i < patch_count; i++)
    if (at >= patches[i].at && at + size <= patches[i].at + patches[i].size) { memcpy(out, patches[i].bytes + (at - patches[i].at), size); return true; }
  return false;
}
static void put32(uint64_t at, uint32_t value) { for (int i = 0; i < patch_count; i++) if (at >= patches[i].at && at + 4 <= patches[i].at + patches[i].size) memcpy(patches[i].bytes + (at - patches[i].at), &value, 4); }
static void put64(uint64_t at, uint64_t value) { for (int i = 0; i < patch_count; i++) if (at >= patches[i].at && at + 8 <= patches[i].at + patches[i].size) memcpy(patches[i].bytes + (at - patches[i].at), &value, 8); }
static uint32_t get32(uint64_t at) { uint32_t v = 0; mem_read(NULL, at, &v, 4); return v; }

/* ---- the engine's world ---- */
enum { BODY = 11, RIG = 77, PIECE = 90, WEAPON = 91, STRANGER = 500, UNITS = 5 };
typedef struct Unit { uint32_t ref; uint64_t object; uint32_t nodes, meshes; bool alive; } Unit;
static Unit units[UNITS] = {{BODY, 0x20000000, 90, 3, true}, {RIG, 0x20010000, 63, 2, true}, {PIECE, 0x20020000, 20, 1, true}, {WEAPON, 0x20030000, 12, 1, true},
                            {STRANGER, 0x20040000, 5, 0, true}};
static uint64_t world_block[2];
#define NATIVE_WORLD UINT64_C(0x30000000)
static unsigned handle_skew; /* a test: the handles stop being the unit references */
static Unit *unit_by_value(uintptr_t value) { for (int i = 0; i < UNITS; i++) if (((uintptr_t)(units[i].ref + handle_skew) << 2 | 1) == value) return &units[i]; return NULL; }
static bool mem_object(void *user, uint32_t unit, bool has_owner, uint64_t owner, bool has_index, uint32_t index, uint64_t *object, char *why, size_t size) {
  (void)user; (void)has_owner; (void)owner; (void)has_index; (void)index;
  for (int i = 0; i < UNITS; i++) if (units[i].ref == unit) { *object = units[i].object; return true; }
  snprintf(why, size, "Unit %u retired", unit);
  return false;
}
static uint64_t names_at(const Unit *u) { return u->object + 0x1000; }
static uint64_t links_at(const Unit *u) { return u->object + 0x2000; }
static uint64_t meshes_at(const Unit *u) { return u->object + 0x3000; }
static uint64_t mesh_object(const Unit *u, uint32_t index) { return u->object + 0x4000 + (index - 1) * 0x100; }
static float rig_scale = 1, right_arm = .30f;
static void build_memory(void) {
  for (int i = 0; i < UNITS; i++) {
    const Unit *u = &units[i];
    mem_map(u->object, 0x5000);
    put32(u->object + 8, u->ref), put64(u->object + 0x18, NATIVE_WORLD), put32(u->object + 0x70, u->nodes), put64(u->object + 0x98, links_at(u)), put64(u->object + 0xA0, names_at(u));
    put32(u->object + 0xC8, u->meshes), put64(u->object + 0xD0, meshes_at(u));
    for (uint32_t m = 1; m <= u->meshes; m++) put64(meshes_at(u) + (m - 1) * 16, mesh_object(u, m));
    for (uint32_t n = 0; n < u->nodes; n++) put32(names_at(u) + n * 4, 0xA0000000u + u->ref * 256 + n), put32(links_at(u) + n * 4, n ? 1u | ((n - 1) << 16) : 0);
  }
  const Unit *body = &units[0], *rig = &units[1], *piece = &units[2], *weapon = &units[3];
  /* the rig: weapon bone 8; the left arm 9 < 10 < 11 < 12 < 13, the right 29 < 30 < 31 < 32 < 33 < 34, both under bone 8's parent chain */
  put32(names_at(rig) + 8 * 4, 0x4FDD3337), put32(names_at(rig) + 10 * 4, 0x03752C98), put32(names_at(rig) + 30 * 4, 0x1620B2CE);
  put32(links_at(rig) + 29 * 4, 1u | (5u << 16)), put32(links_at(rig) + 14 * 4, 1u | (5u << 16));
  put32(names_at(body) + 40 * 4, 0x03752C98), put32(names_at(body) + 41 * 4, 0x1620B2CE), put32(names_at(body) + 42 * 4, 0x37EF0820);
  put32(names_at(body) + 43 * 4, 0xC4787B4E), put32(names_at(body) + 44 * 4, 0xB2BCD839), put32(names_at(body) + 45 * 4, 0x5C0C68D3);
  put32(names_at(body) + 46 * 4, 0x248004F5);
  put64(piece->object + 0x1D0, rig->object), put64(weapon->object + 0x1D0, rig->object | UINT64_C(0xABCD000000000000));
  put32(links_at(piece), 2u | (3u << 16)), put32(links_at(weapon), 2u | (8u << 16));
  mem_map((uint64_t)(uintptr_t)world_block, sizeof world_block);
  put64((uint64_t)(uintptr_t)world_block, NATIVE_WORLD);
  /* the three fields of the head's build table */
  const TfpHeadBuild *b = &tfp_head_build_25480438;
  memcpy(mem_map(EXE + b->registry_code, 22), b->registry_code_bytes, 22);
  mem_map(EXE + b->context_default, 32);
  put32(EXE + b->context_default, b->context_default_value), put32(EXE + b->context_shadow, b->context_shadow_value);
}

/* ---- the stand-in for the Lua C API ---- */
enum { K_NIL, K_BOOL, K_LIGHT, K_NUM, K_STR, K_TAB, K_FUN, K_USER, K_THREAD };
typedef struct KTab KTab;
typedef struct KVal { int t; double n; const char *s; void *p; KTab *tab; lua_CFunction f; uintptr_t id; } KVal;
struct KTab { KVal arr[400]; int n; struct { const char *k; KVal v; } fields[64]; int nf; lua_CFunction call; };
static KVal kstack[512], kregs[2048], knil;
static int ktop, kbase, live_refs;
static KTab kglobals, kregistry;
static lua_State *const VM = (lua_State *)&kglobals;
static KVal *kat(int i) {
  static KVal globals = {K_TAB, 0, 0, 0, &kglobals, 0, 0}, registry = {K_TAB, 0, 0, 0, &kregistry, 0, 0};
  if (i == LUA_GLOBALSINDEX) return &globals;
  if (i == LUA_REGISTRYINDEX) return &registry;
  if (i > 0) return kbase + i - 1 < ktop ? &kstack[kbase + i - 1] : &knil;
  return &kstack[ktop + i];
}
static void kpush(KVal v) { assert(ktop < 512); kstack[ktop++] = v; }
static KVal knum(double n) { KVal v = {K_NUM, n, 0, 0, 0, 0, 0}; return v; }
static KVal kbool(bool b) { KVal v = {K_BOOL, b, 0, 0, 0, 0, 0}; return v; }
static KVal kstr(const char *s) { KVal v = {K_STR, 0, s, 0, 0, 0, 0}; return v; }
static KVal klight(uintptr_t p) { KVal v = {K_LIGHT, 0, 0, (void *)p, 0, 0, 0}; return v; }
static KVal ktable(KTab *t) { KVal v = {K_TAB, 0, 0, 0, t, 0, 0}; return v; }
static KVal *kfield(KTab *t, const char *key) { for (int i = 0; i < t->nf; i++) if (!strcmp(t->fields[i].k, key)) return &t->fields[i].v; return NULL; }
static void kset(KTab *t, const char *key, KVal v) {
  KVal *at = kfield(t, key);
  if (at) { *at = v; return; }
  assert(t->nf < 64);
  t->fields[t->nf].k = key, t->fields[t->nf++].v = v;
}
static int k_gettop(lua_State *L) { (void)L; return ktop - kbase; }
static void k_settop(lua_State *L, int n) {
  (void)L;
  if (n < 0) { ktop += n + 1; return; }
  while (ktop < kbase + n) kpush(knil);
  ktop = kbase + n;
}
static int k_checkstack(lua_State *L, int n) { (void)L; return ktop + n < 512; }
static void k_pushvalue(lua_State *L, int i) { (void)L; kpush(*kat(i)); }
static void k_pushnil(lua_State *L) { (void)L; kpush(knil); }
static void k_pushnumber(lua_State *L, lua_Number n) { (void)L; kpush(knum(n)); }
static void k_pushinteger(lua_State *L, lua_Integer n) { (void)L; kpush(knum((double)n)); }
static void k_pushboolean(lua_State *L, int b) { (void)L; kpush(kbool(b != 0)); }
static void k_pushstring(lua_State *L, const char *s) { (void)L; kpush(kstr(strdup(s))); }
static void k_pushcclosure(lua_State *L, lua_CFunction f, int n) { KVal v = {K_FUN, 0, 0, 0, 0, f, (uintptr_t)f}; (void)L; ktop -= n; kpush(v); }
static int k_pushthread(lua_State *L) { KVal v = {K_THREAD, 0, 0, 0, 0, 0, 0}; (void)L; kpush(v); return 1; }
static int k_type(lua_State *L, int i) { (void)L; int t = kat(i)->t; return t == K_THREAD ? LUA_TTHREAD : t; }
static lua_Number k_tonumber(lua_State *L, int i) { (void)L; return kat(i)->t == K_NUM ? kat(i)->n : 0; }
static int k_toboolean(lua_State *L, int i) { (void)L; const KVal *v = kat(i); return v->t == K_NIL ? 0 : v->t == K_BOOL ? v->n != 0 : 1; }
static const char *k_tolstring(lua_State *L, int i, size_t *len) {
  const KVal *v = kat(i);
  (void)L;
  if (v->t != K_STR) return NULL;
  if (len) *len = strlen(v->s);
  return v->s;
}
static KVal kget(const KVal *table, const KVal *key) {
  if (table->t != K_TAB) return knil;
  if (key->t == K_STR) { const KVal *v = kfield(table->tab, key->s); return v ? *v : knil; }
  if (key->t == K_NUM && key->n >= 1 && key->n <= table->tab->n) return table->tab->arr[(int)key->n - 1];
  return knil;
}
static void k_rawget(lua_State *L, int i) { (void)L; KVal t = *kat(i); kstack[ktop - 1] = kget(&t, &kstack[ktop - 1]); }
static void k_getfield(lua_State *L, int i, const char *key) { KVal t = *kat(i), k = kstr(key); (void)L; kpush(kget(&t, &k)); }
static void k_rawgeti(lua_State *L, int i, int n) {
  (void)L;
  if (i == LUA_REGISTRYINDEX) { kpush(n > 0 && n < 2048 ? kregs[n] : knil); return; }
  KVal t = *kat(i), k = knum(n);
  kpush(kget(&t, &k));
}
static void k_rawseti(lua_State *L, int i, int n) {
  KTab *t = kat(i)->tab;
  (void)L;
  assert(n >= 1 && n <= 400);
  t->arr[n - 1] = kstack[--ktop];
  if (n > t->n) t->n = n;
}
static void k_rawset(lua_State *L, int i) { KTab *t = kat(i)->tab; (void)L; kset(t, kstack[ktop - 2].s, kstack[ktop - 1]); ktop -= 2; }
static int k_rawequal(lua_State *L, int a, int b) { const KVal *x = kat(a), *y = kat(b); (void)L; return x->t == y->t && x->n == y->n && x->p == y->p && x->tab == y->tab && x->f == y->f; }
static size_t k_objlen(lua_State *L, int i) { (void)L; return kat(i)->t == K_TAB ? (size_t)kat(i)->tab->n : 0; }
static int k_next(lua_State *L, int i) { (void)L; (void)i; ktop--; return 0; }
static void k_insert(lua_State *L, int i) { KVal *at = kat(i), top = kstack[ktop - 1]; (void)L; memmove(at + 1, at, (size_t)(&kstack[ktop - 1] - at) * sizeof *at); *at = top; }
static void k_remove(lua_State *L, int i) { KVal *at = kat(i); (void)L; memmove(at, at + 1, (size_t)(&kstack[ktop - 1] - at) * sizeof *at); ktop--; }
static int k_ref(lua_State *L, int registry) {
  (void)L; (void)registry;
  KVal v = kstack[--ktop];
  if (v.t == K_NIL) return -1;
  for (int i = 1; i < 2048; i++) if (kregs[i].t == K_NIL) { kregs[i] = v; live_refs++; return i; }
  assert(!"registry full");
  return -1;
}
static void k_unref(lua_State *L, int registry, int i) { (void)L; (void)registry; if (i > 0 && kregs[i].t != K_NIL) { kregs[i] = knil; live_refs--; } }
static const void *k_topointer(lua_State *L, int i) { const KVal *v = kat(i); (void)L; return v->t == K_TAB ? (const void *)v->tab : v->p; }
static void *k_touserdata(lua_State *L, int i) { const KVal *v = kat(i); (void)L; return v->t == K_LIGHT || v->t == K_USER ? v->p : NULL; }
static lua_CFunction k_tocfunction(lua_State *L, int i) { const KVal *v = kat(i); (void)L; return v->t == K_FUN ? (lua_CFunction)v->id : NULL; }
static void k_createtable(lua_State *L, int a, int b) { (void)L; (void)a; (void)b; kpush(ktable(calloc(1, sizeof(KTab)))); }
static int k_error(lua_State *L) { (void)L; return -1; }
/* A protected call: a C function gets its arguments as 1..n and answers how many results it left on top,
 * or -1 with the error's text on top. A table that can be called gets itself first, as __call does. */
static int k_pcall(lua_State *L, int nargs, int nresults, int errfunc) {
  int at = ktop - nargs - 1, saved = kbase;
  KVal fn = kstack[at];
  lua_CFunction f = fn.t == K_FUN ? fn.f : fn.t == K_TAB ? fn.tab->call : NULL;
  (void)errfunc;
  if (!f) { kstack[at] = kstr("attempt to call a value that is not a function"); ktop = at + 1; return 2; }
  kbase = fn.t == K_TAB ? at : at + 1;
  int n = f(L);
  kbase = saved;
  if (n < 0) { kstack[at] = kstack[ktop - 1]; ktop = at + 1; return 2; }
  memmove(&kstack[at], &kstack[ktop - n], (size_t)n * sizeof *kstack);
  ktop = at + n;
  if (nresults != LUA_MULTRET) { while (n < nresults) { kpush(knil); n++; } ktop = at + nresults; }
  return 0;
}

/* ---- the engine's script functions ---- */
static void note(const char *format, ...) {
  size_t used = strlen(calls);
  va_list args;
  va_start(args, format);
  vsnprintf(calls + used, sizeof calls - used, format, args);
  va_end(args);
}
static const char *raise_in;   /* the function of this path raises */
static bool swap_multiply, odd_boolean, parent_off;
static int link_mode;          /* 0: links; 1: does nothing; 2: links somewhere else */
static TfpArmEngine em;        /* the engine's value arithmetic */
static float vpool[256][3], qpool[256][4], mpool[256][10];
static unsigned vnext, qnext, mnext;
static KVal kvec(TfpArmVector3 v) { float *at = vpool[vnext++ % 256]; at[0] = v.x, at[1] = v.y, at[2] = v.z; return klight((uintptr_t)at); }
static KVal kquat(TfpArmQuaternion q) { float *at = qpool[qnext++ % 256]; at[0] = q.x, at[1] = q.y, at[2] = q.z, at[3] = q.w; return klight((uintptr_t)at); }
static KVal kmat(TfpArmQuaternion q, TfpArmVector3 p, float scale) {
  float *at = mpool[mnext++ % 256];
  at[0] = q.x, at[1] = q.y, at[2] = q.z, at[3] = q.w, at[4] = p.x, at[5] = p.y, at[6] = p.z, at[7] = at[8] = at[9] = scale;
  return klight((uintptr_t)at);
}
static TfpArmVector3 avec(int i) { const float *at = kat(i)->p; return (TfpArmVector3){at[0], at[1], at[2]}; }
static TfpArmQuaternion aquat(int i) { const float *at = kat(i)->p; return (TfpArmQuaternion){at[0], at[1], at[2], at[3]}; }
static TfpArmMatrix4x4 amat(int i) { TfpArmMatrix4x4 m; memset(&m, 0, sizeof m); memcpy(m.m, kat(i)->p, 7 * sizeof(float)); return m; }
static bool is_vec(int i) { const void *p = kat(i)->p; return kat(i)->t == K_LIGHT && p >= (void *)vpool && p < (void *)(vpool + 256); }
static bool is_quat(int i) { const void *p = kat(i)->p; return kat(i)->t == K_LIGHT && p >= (void *)qpool && p < (void *)(qpool + 256); }
static bool is_mat(int i) { const void *p = kat(i)->p; return kat(i)->t == K_LIGHT && p >= (void *)mpool && p < (void *)(mpool + 256); }
static Unit *aunit(int i) { return kat(i)->t == K_LIGHT ? unit_by_value((uintptr_t)kat(i)->p) : NULL; }
static int oops(const char *text) { kpush(kstr(text)); return -1; }
#define ENTER(path) do { if (raise_in && !strcmp(raise_in, path)) return oops(path " raised on purpose"); } while (0)
#define UNIT(i) Unit *u = aunit(i); if (!u) return oops("not a Unit")
static TfpArmError ignored;

static int f_main_world(lua_State *L) { KVal v = {K_USER, 0, 0, world_block, 0, 0, 0}; (void)L; ENTER("Application.main_world"); note("main_world();"); kpush(v); return 1; }
static int listed = UNITS;
static int f_units(lua_State *L) {
  (void)L; ENTER("World.units");
  if (kat(1)->t != K_USER || kat(1)->p != world_block) return oops("not a World");
  KTab *t = calloc(1, sizeof *t);
  for (int i = 0; i < listed; i++) t->arr[t->n++] = klight((uintptr_t)(units[i % UNITS].ref + handle_skew) << 2 | 1);
  note("units();");
  kpush(ktable(t));
  return 1;
}
static int f_link_unit(lua_State *L) {
  (void)L; ENTER("World.link_unit");
  Unit *child = aunit(2), *parent = aunit(4);
  if (kat(1)->t != K_USER || !child || !parent || kat(3)->t != K_NUM || kat(5)->t != K_NUM || k_gettop(L) != 5) return oops("World.link_unit: bad arguments");
  note("link_unit(%u,%.0f,%u,%.0f);", child->ref, kat(3)->n, parent->ref, kat(5)->n);
  uint32_t bone = (uint32_t)kat(5)->n - 1 + (link_mode == 2 ? 4 : 0);
  if (link_mode != 1) put32(links_at(child) + ((uint64_t)kat(3)->n - 1) * 4, 2u | (bone << 16));
  return 0;
}
static int f_set_mesh_visibility(lua_State *L) {
  (void)L; ENTER("Unit.set_mesh_visibility"); UNIT(1);
  if (kat(2)->t != K_NUM || kat(3)->t != K_BOOL) return oops("Unit.set_mesh_visibility: bad arguments");
  note("set_mesh_visibility(%u,%.0f,%s,%s)/%d;", u->ref, kat(2)->n, kat(3)->n ? "true" : "false", kat(4)->t == K_STR ? kat(4)->s : "~", k_gettop(L));
  return 0;
}
static int f_has_mesh(lua_State *L) {
  (void)L; ENTER("Unit.has_mesh"); UNIT(1);
  note("has_mesh(%u,%s);", u->ref, kat(2)->s);
  kpush(odd_boolean ? knum(1) : kbool(!strcmp(kat(2)->s, "g_l_shoulderpad")));
  return 1;
}
static int f_mesh(lua_State *L) {
  (void)L; ENTER("Unit.mesh"); UNIT(1);
  if (kat(2)->t == K_STR) { note("mesh(%u,%s);", u->ref, kat(2)->s); kpush(!strcmp(kat(2)->s, "g_l_shoulderpad") ? klight(mesh_object(u, 2)) : knil); return 1; }
  note("mesh(%u,%.0f);", u->ref, kat(2)->n);
  kpush(kat(2)->n >= 1 && kat(2)->n <= u->meshes ? klight(mesh_object(u, (uint32_t)kat(2)->n)) : knil);
  return 1;
}
static int mesh_answer(const char *name, KVal value) { if (kat(1)->t != K_LIGHT) return oops("not a Mesh"); note("%s(%#llx);", name, (unsigned long long)(uintptr_t)kat(1)->p); kpush(value); return 1; }
static int f_mesh_name(lua_State *L) { (void)L; ENTER("Mesh.name"); return mesh_answer("name", knum(0x44E19D12)); }
static int f_mesh_node(lua_State *L) { (void)L; ENTER("Mesh.node"); return mesh_answer("node", knum(57)); }
static int f_mesh_materials(lua_State *L) { (void)L; ENTER("Mesh.num_materials"); return mesh_answer("num_materials", knum(1)); }
static int f_mesh_visibility(lua_State *L) { (void)L; ENTER("Mesh.visibility"); return mesh_answer("visibility", odd_boolean ? knum(1) : kbool(true)); }
static int f_mesh_has_material(lua_State *L) {
  (void)L; ENTER("Mesh.has_material");
  note("has_material(%#llx,%s);", (unsigned long long)(uintptr_t)kat(1)->p, kat(2)->s);
  kpush(kbool(true));
  return 1;
}
static int f_items(lua_State *L) { (void)L; ENTER("Unit.num_scene_graph_items"); UNIT(1); kpush(knum(u->nodes)); return 1; }
static int f_num_meshes(lua_State *L) { (void)L; ENTER("Unit.num_meshes"); UNIT(1); kpush(knum(u->meshes)); return 1; }
static int f_alive(lua_State *L) { (void)L; ENTER("Unit.alive"); UNIT(1); note("alive(%u);", u->ref); kpush(odd_boolean ? knum(1) : kbool(u->alive)); return 1; }
/* A node's place: the rig's arms are 0.30 m and 0.25 m long. */
static TfpArmVector3 node_position(const Unit *u, double node) {
  double bone = node - 1;
  if (u->ref == RIG && (bone == 12 || bone == 32)) return (TfpArmVector3){bone == 12 ? .30f : right_arm, bone == 12 ? 1 : 2, 0};
  if (u->ref == RIG && (bone == 13 || bone == 34)) return (TfpArmVector3){bone == 13 ? .30f : right_arm, bone == 13 ? 1 : 2, .25f};
  if (u->ref == RIG && (bone == 10 || bone == 30)) return (TfpArmVector3){0, bone == 10 ? 1 : 2, 0};
  return (TfpArmVector3){(float)u->ref, (float)node, .5f};
}
static TfpArmQuaternion node_rotation(const Unit *u, double node) {
  TfpArmQuaternion q;
  em.Quaternion_axis_angle(NULL, (TfpArmVector3){1, (float)u->ref, 2}, .1 * node, &q, &ignored);
  return q;
}
static int f_world_position(lua_State *L) { (void)L; ENTER("Unit.world_position"); UNIT(1); note("world_position(%u,%.0f);", u->ref, kat(2)->n); kpush(kvec(node_position(u, kat(2)->n))); return 1; }
static int f_world_rotation(lua_State *L) { (void)L; ENTER("Unit.world_rotation"); UNIT(1); note("world_rotation(%u,%.0f);", u->ref, kat(2)->n); kpush(kquat(node_rotation(u, kat(2)->n))); return 1; }
static int f_world_pose(lua_State *L) {
  (void)L; ENTER("Unit.world_pose"); UNIT(1);
  kpush(kmat(node_rotation(u, kat(2)->n), node_position(u, kat(2)->n), u->ref == RIG ? rig_scale : 1));
  return 1;
}
static int f_set_local_pose(lua_State *L) {
  (void)L; ENTER("Unit.set_local_pose"); UNIT(1);
  if (!is_mat(3) || kat(2)->t != K_NUM) return oops("Unit.set_local_pose: bad arguments");
  const float *m = kat(3)->p;
  note("set_local_pose(%u,%.0f,[%g %g %g %g|%g %g %g]);", u->ref, kat(2)->n, m[0], m[1], m[2], m[3], m[4], m[5], m[6]);
  return 0;
}
static int f_parent(lua_State *L) {
  (void)L; ENTER("Unit.scene_graph_parent"); UNIT(1);
  uint32_t word = get32(links_at(u) + ((uint64_t)kat(2)->n - 1) * 4);
  if ((word & 0xffff) != 1) { kpush(knil); return 1; }
  kpush(knum((word >> 16) + 1 + (parent_off && kat(2)->n == 21 ? 3 : 0)));
  return 1;
}
static int f_vector3(lua_State *L) { (void)L; if (kat(2)->t != K_NUM) return oops("Vector3: numbers wanted"); kpush(kvec((TfpArmVector3){(float)kat(2)->n, (float)kat(3)->n, (float)kat(4)->n})); return 1; }
static int f_v_x(lua_State *L) { (void)L; if (!is_vec(1)) return oops("not a Vector3"); kpush(knum(avec(1).x)); return 1; }
static int f_v_y(lua_State *L) { (void)L; if (!is_vec(1)) return oops("not a Vector3"); kpush(knum(avec(1).y)); return 1; }
static int f_v_z(lua_State *L) { (void)L; if (!is_vec(1)) return oops("not a Vector3"); kpush(knum(avec(1).z)); return 1; }
static int f_v_cross(lua_State *L) { TfpArmVector3 r; (void)L; em.Vector3_cross(NULL, avec(1), avec(2), &r, &ignored); kpush(kvec(r)); return 1; }
static int f_v_lerp(lua_State *L) { TfpArmVector3 r; (void)L; em.Vector3_lerp(NULL, avec(1), avec(2), kat(3)->n, &r, &ignored); kpush(kvec(r)); return 1; }
static int f_v_normalize(lua_State *L) { TfpArmVector3 r; (void)L; em.Vector3_normalize(NULL, avec(1), &r, &ignored); kpush(kvec(r)); return 1; }
static int f_v_dot(lua_State *L) { double r; (void)L; em.Vector3_dot(NULL, avec(1), avec(2), &r, &ignored); kpush(knum(r)); return 1; }
static int f_v_length(lua_State *L) { double r; (void)L; em.Vector3_length(NULL, avec(1), &r, &ignored); kpush(knum(r)); return 1; }
static int f_q_from(lua_State *L) { (void)L; kpush(kquat((TfpArmQuaternion){(float)kat(1)->n, (float)kat(2)->n, (float)kat(3)->n, (float)kat(4)->n})); return 1; }
static int f_q_to(lua_State *L) { (void)L; if (!is_quat(1)) return oops("not a Quaternion"); TfpArmQuaternion q = aquat(1); kpush(knum(q.x)); kpush(knum(q.y)); kpush(knum(q.z)); kpush(knum(q.w)); return 4; }
static int f_q_multiply(lua_State *L) {
  TfpArmQuaternion r;
  (void)L;
  if (swap_multiply) em.Quaternion_multiply(NULL, aquat(2), aquat(1), &r, &ignored); else em.Quaternion_multiply(NULL, aquat(1), aquat(2), &r, &ignored);
  kpush(kquat(r));
  return 1;
}
static int f_q_rotate(lua_State *L) { TfpArmVector3 r; (void)L; em.Quaternion_rotate(NULL, aquat(1), avec(2), &r, &ignored); kpush(kvec(r)); return 1; }
static int f_q_axis(lua_State *L) { TfpArmQuaternion r; (void)L; em.Quaternion_axis_angle(NULL, avec(1), kat(2)->n, &r, &ignored); kpush(kquat(r)); return 1; }
static int f_q_inverse(lua_State *L) { TfpArmQuaternion r; (void)L; em.Quaternion_inverse(NULL, aquat(1), &r, &ignored); kpush(kquat(r)); return 1; }
static int f_q_lerp(lua_State *L) { TfpArmQuaternion r; (void)L; em.Quaternion_lerp(NULL, aquat(1), aquat(2), kat(3)->n, &r, &ignored); r.x = -r.x, r.y = -r.y, r.z = -r.z, r.w = -r.w; kpush(kquat(r)); return 1; }
static int f_m_from(lua_State *L) { (void)L; ENTER("Matrix4x4.from_quaternion_position"); if (!is_quat(1) || !is_vec(2)) return oops("Matrix4x4.from_quaternion_position: bad arguments"); kpush(kmat(aquat(1), avec(2), 1)); return 1; }
static int f_m_rotation(lua_State *L) { TfpArmMatrix4x4 m = amat(1); TfpArmQuaternion q; (void)L; em.Matrix4x4_rotation(NULL, &m, &q, &ignored); kpush(kquat(q)); return 1; }
static int f_m_translation(lua_State *L) { TfpArmMatrix4x4 m = amat(1); TfpArmVector3 p; (void)L; em.Matrix4x4_translation(NULL, &m, &p, &ignored); kpush(kvec(p)); return 1; }
static int f_m_multiply(lua_State *L) {
  TfpArmMatrix4x4 a = amat(1), b = amat(2), r;
  (void)L;
  em.Matrix4x4_multiply(NULL, &a, &b, &r, &ignored);
  kpush(kmat((TfpArmQuaternion){r.m[0], r.m[1], r.m[2], r.m[3]}, (TfpArmVector3){r.m[4], r.m[5], r.m[6]}, 1));
  return 1;
}
static int f_m_inverse(lua_State *L) {
  TfpArmMatrix4x4 a = amat(1), r;
  (void)L;
  em.Matrix4x4_inverse(NULL, &a, &r, &ignored);
  kpush(kmat((TfpArmQuaternion){r.m[0], r.m[1], r.m[2], r.m[3]}, (TfpArmVector3){r.m[4], r.m[5], r.m[6]}, 1));
  return 1;
}
static int f_m_scale(lua_State *L) { const float *m = kat(1)->p; (void)L; kpush(kvec((TfpArmVector3){m[7], m[8], m[9]})); return 1; }

static KTab kstingray;
/* stingray.<path> = a C function whose address, as lua_tocfunction gives it, is this build's exe + RVA. */
static KVal *reg(const char *path, lua_CFunction f) {
  size_t count;
  const TfpWireEngineFunction *table = tfp_wire_engine_functions(&count);
  const char *dot = strchr(path, '.');
  KVal fn = {K_FUN, 0, 0, 0, 0, f, (uintptr_t)f};
  for (size_t i = 0; i < count; i++) if (!strcmp(table[i].path, path) && table[i].rva) fn.id = (uintptr_t)(EXE + table[i].rva);
  if (!dot) { KTab *t = calloc(1, sizeof *t); t->call = f; kset(&kstingray, path, ktable(t)); return kfield(&kstingray, path); }
  char *name = strndup(path, (size_t)(dot - path));
  if (!kfield(&kstingray, name)) kset(&kstingray, name, ktable(calloc(1, sizeof(KTab))));
  kset(kfield(&kstingray, name)->tab, dot + 1, fn);
  return kfield(kfield(&kstingray, name)->tab, dot + 1);
}
static void build_engine(void) {
  kset(&kglobals, "stingray", ktable(&kstingray));
  reg("Vector3", f_vector3); reg("Quaternion", NULL); reg("Matrix4x4", NULL);
  reg("Application.main_world", f_main_world); reg("World.units", f_units); reg("World.link_unit", f_link_unit);
  reg("Unit.set_mesh_visibility", f_set_mesh_visibility); reg("Unit.has_mesh", f_has_mesh); reg("Unit.mesh", f_mesh);
  reg("Mesh.name", f_mesh_name); reg("Mesh.node", f_mesh_node); reg("Mesh.num_materials", f_mesh_materials);
  reg("Mesh.has_material", f_mesh_has_material); reg("Mesh.visibility", f_mesh_visibility);
  reg("Unit.num_scene_graph_items", f_items); reg("Unit.num_meshes", f_num_meshes); reg("Unit.alive", f_alive);
  reg("Unit.world_position", f_world_position); reg("Unit.world_rotation", f_world_rotation); reg("Unit.world_pose", f_world_pose);
  reg("Unit.set_local_pose", f_set_local_pose); reg("Unit.scene_graph_parent", f_parent);
  reg("Vector3.x", f_v_x); reg("Vector3.y", f_v_y); reg("Vector3.z", f_v_z); reg("Vector3.cross", f_v_cross); reg("Vector3.lerp", f_v_lerp);
  reg("Vector3.normalize", f_v_normalize); reg("Vector3.dot", f_v_dot); reg("Vector3.length", f_v_length);
  reg("Quaternion.from_elements", f_q_from); reg("Quaternion.to_elements", f_q_to); reg("Quaternion.multiply", f_q_multiply);
  reg("Quaternion.rotate", f_q_rotate); reg("Quaternion.axis_angle", f_q_axis); reg("Quaternion.inverse", f_q_inverse); reg("Quaternion.lerp", f_q_lerp);
  reg("Matrix4x4.from_quaternion_position", f_m_from); reg("Matrix4x4.rotation", f_m_rotation); reg("Matrix4x4.translation", f_m_translation);
  reg("Matrix4x4.multiply", f_m_multiply); reg("Matrix4x4.inverse", f_m_inverse); reg("Matrix4x4.scale", f_m_scale);
}

static void k_close(lua_State *L) { (void)L; ktop = kbase = 0; }
/* ---- HellForge, as far as engine.c asks ---- */
static void hf_log(HfMod *mod, const char *format, ...) { (void)mod; (void)format; }
static uintptr_t hf_module_base(const char *module, size_t *size) { *size = 0x4000000; return strcmp(module, "helldivers2.exe") ? 0 : (uintptr_t)EXE; }
static HfApi fake_hf;
static void bridge(void) {
  L_.pcall = k_pcall, L_.error = k_error, L_.settop = k_settop, L_.gettop = k_gettop, L_.pushvalue = k_pushvalue, L_.pushnil = k_pushnil;
  L_.pushnumber = k_pushnumber, L_.pushinteger = k_pushinteger, L_.pushboolean = k_pushboolean, L_.pushstring = k_pushstring;
  L_.pushcclosure = k_pushcclosure, L_.pushthread = k_pushthread, L_.rawget = k_rawget, L_.rawgeti = k_rawgeti, L_.rawset = k_rawset;
  L_.rawequal = k_rawequal, L_.type = k_type, L_.tonumber = k_tonumber, L_.toboolean = k_toboolean, L_.tolstring = k_tolstring;
  L_.getfield = k_getfield, L_.objlen = k_objlen, L_.next = k_next, L_.insert = k_insert, L_.remove = k_remove, L_.checkstack = k_checkstack;
  L_.ref = k_ref, L_.unref = k_unref, L_.topointer = k_topointer, L_.touserdata = k_touserdata, L_.tocfunction = k_tocfunction;
  L_.createtable = k_createtable, L_.rawseti = k_rawseti;
  fake_hf.log = hf_log, fake_hf.module_base = hf_module_base;
  g_hf = &fake_hf, hf = &fake_hf, hooked = true;
  s_pcall = k_pcall, s_close = k_close;
  s_captureEnabled = 1;
}
static int f_nothing(lua_State *L) { (void)L; return 0; }
static void script_call(void) { /* the engine runs a script: its lua_pcall goes through the bridge's hook, which catches the state */
  k_pushcclosure(VM, f_nothing, 0);
  PcallHook(VM, 0, 0, 0);
}

/* ---- a wire with no game behind the backend ---- */
static uint64_t no_module(void *u, TfpOpsModule which) { (void)u; (void)which; return 0; }
static bool no_read(void *u, uint64_t at, void *out, size_t size) { (void)u; (void)at; (void)out; (void)size; return false; }
static bool no_write(void *u, uint64_t at, const void *bytes, size_t size) { (void)u; (void)at; (void)bytes; (void)size; return false; }
static void no_query(void *u, uint64_t at, TfpOpsRegion *out) { (void)u; (void)at; memset(out, 0, sizeof *out); }
static bool no_call(void *u, TfpOpsNative which, uint64_t address, const uint64_t args[4], uint64_t *result, TfpOpsText *raised) {
  (void)u; (void)which; (void)address; (void)args; (void)result;
  raised->present = true;
  snprintf(raised->text, sizeof raised->text, "no game");
  return false;
}
static double test_clock(void *u) { (void)u; return 100; }
static int test_mouse(void *u, int button) { (void)u; (void)button; return 0; }
static TfpWire wire;
static bool text_is(TfpRtValue v, const char *text) { return v.kind == TFP_RT_TEXT && v.length == strlen(text) && !memcmp(v.text, text, v.length); }
static const TfpWireEngineBody body = {BODY, RIG, 2, 0x6000, true};
static void fresh(void) { calls[0] = 0; line_count = 0; }
static TfpOpsSnapshot sample(void) {
  TfpOpsSnapshot s;
  memset(&s, 0, sizeof s);
  s.base = 0x7ff600000000, s.world = NATIVE_WORLD, s.avatar_owner = 0x6000, s.camera_kind = 6, s.avatar_index = 2, s.body = BODY, s.viewmodel = RIG;
  s.weapon = 912, s.weapon_unit = WEAPON, s.safe = true, s.full = true;
  return s;
}

int main(void) {
  TfpOpsHost memory;
  TfpWirePlatform platform = {NULL, test_clock, test_mouse, capture};
  TfpWireHelpers helpers = {NULL, NULL, NULL, NULL};
  TfpWireEngineMemory game = {NULL, mem_read, mem_object};
  TfpRtValue v, v2;
  TfpRtTable t;
  memset(&memory, 0, sizeof memory);
  memory.module = no_module, memory.read = no_read, memory.write = no_write, memory.query = no_query, memory.call = no_call;
  tfp_wire_engine_maths(&em);
  build_memory();
  build_engine();
  bridge();
  tfp_wire_init(&wire, &memory, &platform, &helpers);
  TfpRtHost off = wire.host;
  TfpRtHost *h = &wire.host;

  /* ---- bound before the engine's Lua state is caught: every pointer answers as wire_off.c does ---- */
  CHECK(tfp_wire_engine_bind(&wire, tfp_win_engine_api(), &game));
  CHECK(logged("Ready: model shadows: no engine function; reads the appearance rows at avatar owner +0x546a70"));
  CHECK(h->arm_pieces_new && h->grip_hand_new && h->head_sdk_handles && h->head_update != off.head_update);
  CHECK(h->profile_body_sleeves(h).kind == TFP_RT_TRUE && h->profile_grip_hand(h).kind == TFP_RT_TRUE && h->profile_seated_head(h).kind == TFP_RT_TRUE);
  tfp_wire_engine_begin(&wire);
  tfp_wire_engine_begin(&wire);
  CHECK(logged_count("Waiting: the engine's Lua state is not caught yet") == 1);
  for (int m = 0; m < TFP_WE_MODULES; m++) CHECK(tfp_wire_engine_state(&wire, m) == (m == TFP_WE_SHADOWS ? TFP_WE_READY : TFP_WE_WAITING));
  CHECK(h->model_shadows_new(h) && h->head_new(h));
  CHECK(h->arm_pieces_new(h, &v) && v.kind == TFP_RT_TRUE && h->grip_hand_new(h, &v) && v.kind == TFP_RT_TRUE); /* they exist and come up later */
  CHECK(h->head_update(h, &v, &v2) && v.kind == TFP_RT_TRUE && v2.kind == TFP_RT_NIL);
  CHECK(h->head_result(h, &t) && t.present && text_is(t.f[TFP_RT_TARGET_PHASE], "idle"));
  CHECK(h->head_is_clean(h, &v) && v.kind == TFP_RT_TRUE && h->head_stop(h, &v, &v2) && v.kind == TFP_RT_TRUE);
  CHECK(h->head_sdk_handles(h, &v) && v.kind == TFP_RT_NIL);
  CHECK(h->arm_pieces_update(h, NULL, true, tfp_wire_nil()) && h->grip_hand_update(h, NULL, true, tfp_wire_nil()));
  CHECK(h->arm_pieces_is_clean(h, &v) && v.kind == TFP_RT_TRUE && h->grip_hand_stop(h, &v, &v2) && v.kind == TFP_RT_TRUE && v2.kind == TFP_RT_NIL);
  CHECK(h->arm_pieces_result(h, &t) && t.present && h->grip_hand_result(h, &t) && t.present);
  CHECK(!calls[0] && live_refs == 0); /* nothing reached the engine */
  char second[200];
  CHECK(tfp_wire_engine_second(&wire, second, sizeof second) && !strcmp(second, "head=waiting sleeves=waiting grip=waiting corr=0 max=0.000m/0.0deg"));
  const TfpEngineIface *head = tfp_wire_engine_head_iface(&wire);
  const TfpArmEngine *sleeves = tfp_wire_engine_arm_iface(&wire, TFP_WE_SLEEVES), *grip = tfp_wire_engine_arm_iface(&wire, TFP_WE_GRIP);
  TfpEngineIface head_before = *head;
  TfpEngineError error;
  TfpArmError arm_error;
  TfpEngineWorld world = NULL;
  CHECK(!head->Application_main_world(head->user, &world, &error) && strstr(error.text, "not available"));

  /* ---- the state is caught: every function is compared with this build's, module by module ---- */
  fresh();
  script_call();
  CHECK(eng_ready() && eng_state_generation() == 1 && ktop == 0);
  tfp_wire_engine_begin(&wire);
  CHECK(logged("Ready: head visibility: 11 engine functions are build 25480438's own (exe at 0x140000000)"));
  CHECK(logged("Ready: arm pieces: 31 engine functions") && logged("Ready: grip hand: 35 engine functions"));
  CHECK(logged("Check: arm pieces: value maths ok observed=largest difference") && logged("Check: grip hand: value maths ok") && logged("over 16 comparisons"));
  CHECK(!logged("Refused"));
  for (int m = 0; m < TFP_WE_MODULES; m++) CHECK(tfp_wire_engine_state(&wire, m) == (m == TFP_WE_SHADOWS ? TFP_WE_READY : TFP_WE_RESOLVED));
  CHECK(live_refs == 1 && ktop == 0); /* the stingray table; the checks kept nothing */
  CHECK(!memcmp(&head_before, head, sizeof *head)); /* the nine pointers never change (hv:569-574) */

  /* ---- marshalling: what each wrapper makes the engine's function see ---- */
  fresh();
  const TfpEngineUnit *list = NULL;
  size_t count = 0;
  CHECK(head->Application_main_world(head->user, &world, &error) && world);
  CHECK(tfp_wire_engine_address(&wire, world) == (double)(uintptr_t)world_block);
  TfpEngineWorld again = NULL;
  CHECK(head->Application_main_world(head->user, &again, &error) && again == world && live_refs == 2); /* one value, one handle, one reference */
  CHECK(head->World_units(head->user, world, &list, &count, &error) && count == UNITS);
  CHECK(tfp_wire_engine_address(&wire, list[0]) == BODY * 4 + 1 && tfp_wire_engine_address(&wire, list[1]) == RIG * 4 + 1);
  CHECK(live_refs == 3); /* the list; no reference per unit until one is used */
  TfpEngineUnit body_unit = list[0], rig_unit = list[1], piece_unit = list[2], weapon_unit = list[3], stranger = list[4];
  bool present = false;
  TfpEngineMesh mesh = NULL;
  double number = 0;
  calls[0] = 0;
  CHECK(head->Unit_set_mesh_visibility(head->user, rig_unit, 3, true, "shadow_caster", &error));
  CHECK(head->Unit_set_mesh_visibility(head->user, body_unit, 1, false, "default", &error));
  CHECK(head->Unit_has_mesh(head->user, rig_unit, "g_l_shoulderpad", &present, &error) && present);
  CHECK(head->Unit_has_mesh(head->user, rig_unit, "g_r_shoulderpad", &present, &error) && !present);
  CHECK(head->Unit_mesh(head->user, rig_unit, "g_r_shoulderpad", &mesh, &error) && !mesh);
  CHECK(head->Unit_mesh(head->user, rig_unit, "g_l_shoulderpad", &mesh, &error) && mesh);
  CHECK(tfp_wire_engine_address(&wire, mesh) == (double)mesh_object(&units[1], 2)); /* a Mesh handle's address is the mesh object */
  CHECK(head->Mesh_name(head->user, mesh, &number, &error) && number == 0x44E19D12 && head->Mesh_node(head->user, mesh, &number, &error) && number == 57);
  CHECK(head->Mesh_num_materials(head->user, mesh, &number, &error) && number == 1);
  CHECK(head->Mesh_has_material(head->user, mesh, "m_armor_standard", &present, &error) && present);
  CHECK(!strcmp(calls, "set_mesh_visibility(77,3,true,shadow_caster)/4;set_mesh_visibility(11,1,false,default)/4;has_mesh(77,g_l_shoulderpad);"
                       "has_mesh(77,g_r_shoulderpad);mesh(77,g_r_shoulderpad);mesh(77,g_l_shoulderpad);name(0x20014100);node(0x20014100);"
                       "num_materials(0x20014100);has_material(0x20014100,m_armor_standard);"));
  CHECK(ktop == 0 && live_refs == 6); /* + the rig, the body, the mesh */
  /* a raised Lua error and an answer of the wrong kind are failed calls with the text */
  raise_in = "Unit.set_mesh_visibility";
  CHECK(!head->Unit_set_mesh_visibility(head->user, rig_unit, 3, true, "default", &error) && strstr(error.text, "Unit.set_mesh_visibility raised on purpose"));
  raise_in = NULL, odd_boolean = true;
  CHECK(!head->Unit_has_mesh(head->user, rig_unit, "g_l_shoulderpad", &present, &error) && !strcmp(error.text, "Unit.has_mesh did not answer a Boolean"));
  bool alive = true, visible = true;
  CHECK(sleeves->Unit_alive(sleeves->user, (TfpArmUnit)rig_unit, &alive, &arm_error) && !alive); /* `== true`: 1 is not true */
  odd_boolean = false;
  CHECK(sleeves->Unit_alive(sleeves->user, (TfpArmUnit)rig_unit, &alive, &arm_error) && alive && ktop == 0 && live_refs == 6);
  /* the arms */
  TfpArmWorld arm_world = NULL;
  TfpArmMatrix4x4 pose;
  TfpArmVector3 position;
  TfpArmQuaternion rotation;
  TfpArmMesh arm_mesh = NULL;
  CHECK(sleeves->Application_main_world(sleeves->user, &arm_world, &arm_error) && (const void *)arm_world == (const void *)world);
  calls[0] = 0;
  CHECK(sleeves->Unit_num_scene_graph_items(sleeves->user, (TfpArmUnit)rig_unit, &number, &arm_error) && number == 63);
  CHECK(sleeves->Unit_world_pose(sleeves->user, (TfpArmUnit)rig_unit, 11, &pose, &arm_error));
  CHECK(sleeves->Unit_world_position(sleeves->user, (TfpArmUnit)rig_unit, 11, &position, &arm_error) && position.x == 0 && position.y == 1);
  CHECK(sleeves->Unit_world_rotation(sleeves->user, (TfpArmUnit)rig_unit, 11, &rotation, &arm_error));
  CHECK(pose.m[0] == rotation.x && pose.m[3] == rotation.w && pose.m[4] == position.x && pose.m[5] == position.y && pose.m[6] == position.z);
  TfpArmMatrix4x4 offset;
  memset(&offset, 0, sizeof offset);
  offset.m[0] = (float)sin(.25 * 3.14159265358979), offset.m[3] = (float)cos(.25 * 3.14159265358979), offset.m[4] = .003f, offset.m[5] = .004f; /* a quarter turn, 5 mm */
  CHECK(sleeves->Unit_set_local_pose(sleeves->user, (TfpArmUnit)piece_unit, 4, &offset, &arm_error));
  CHECK(!strcmp(calls, "world_rotation(77,11);world_position(77,11);world_position(77,11);world_rotation(77,11);set_local_pose(90,4,[0.707107 0 0 0.707107|0.003 0.004 0]);"));
  calls[0] = 0;
  CHECK(grip->Unit_num_meshes(grip->user, (TfpArmUnit)rig_unit, &number, &arm_error) && number == 2);
  CHECK(grip->Unit_mesh(grip->user, (TfpArmUnit)rig_unit, 2, &arm_mesh, &arm_error) && (const void *)arm_mesh == (const void *)mesh); /* by index: the same mesh, the same handle */
  CHECK(grip->Unit_mesh(grip->user, (TfpArmUnit)rig_unit, 9, &arm_mesh, &arm_error) && !arm_mesh);
  CHECK(grip->Mesh_visibility(grip->user, (TfpArmMesh)mesh, &visible, &arm_error) && visible && grip->Mesh_num_materials(grip->user, (TfpArmMesh)mesh, &number, &arm_error));
  CHECK(grip->Unit_set_mesh_visibility(grip->user, (TfpArmUnit)rig_unit, 2, false, &arm_error)); /* three arguments: no pass name (gh:480) */
  CHECK(grip->Unit_set_local_pose(grip->user, (TfpArmUnit)piece_unit, 4, &offset, &arm_error));
  CHECK(!strcmp(calls, "mesh(77,2);mesh(77,9);visibility(0x20014100);num_materials(0x20014100);set_mesh_visibility(77,2,false,~)/3;"
                       "set_local_pose(90,4,[0.707107 0 0 0.707107|0.003 0.004 0]);"));
  CHECK(!sleeves->Unit_num_meshes && !sleeves->Mesh_visibility && !grip->World_link_unit); /* each module's table has its own functions only */
  CHECK(sleeves->Vector3_call && sleeves->Quaternion_multiply && grip->Matrix4x4_inverse);
  CHECK(tfp_wire_engine_second(&wire, second, sizeof second));
  CHECK(!strcmp(second, "head=waiting sleeves=waiting grip=waiting corr=0 max=0.005m/90.0deg")); /* the largest offset a grip wrote, then from zero */
  CHECK(tfp_wire_engine_second(&wire, second, sizeof second) && strstr(second, "max=0.000m/0.0deg"));
  CHECK(ktop == 0 && live_refs == 7); /* + the piece; no matrix was kept */

  /* ---- the handle table ---- */
  int before = live_refs;
  for (int i = 0; i < 50; i++) CHECK(head->World_units(head->user, world, &list, &count, &error) && count == UNITS);
  CHECK(live_refs == before && list[1] == rig_unit && list[0] == body_unit && list[2] == piece_unit); /* a used handle stays the same handle */
  CHECK(list[4] != NULL && tfp_wire_engine_address(&wire, list[4]) == STRANGER * 4 + 1);
  (void)stranger; (void)weapon_unit;
  listed = 3; /* the stranger left the world and was never used: its handle is gone with the old list */
  CHECK(head->World_units(head->user, world, &list, &count, &error) && count == 3);
  listed = UNITS;
  CHECK(head->World_units(head->user, world, &list, &count, &error) && count == UNITS && live_refs == before);
  weapon_unit = list[3];
  CHECK(sleeves->Unit_alive(sleeves->user, (TfpArmUnit)weapon_unit, &alive, &arm_error) && alive && live_refs == before + 1); /* referenced when first used */
  listed = 400; /* one unit many times over: one handle (a lightuserdata has no identity but its value) */
  CHECK(head->World_units(head->user, world, &list, &count, &error) && count == 400 && list[5] == list[0] && list[399] == list[4]);
  listed = UNITS;
  CHECK(!head->Unit_set_mesh_visibility(head->user, (TfpEngineUnit)&wire, 1, true, "default", &error) && strstr(error.text, "handle"));

  /* ---- the in-game checks of head visibility ---- */
  fresh();
  CHECK(tfp_wire_engine_check_head(&wire, &body));
  CHECK(logged("Check: head visibility: unit registry code ok observed=48895c24084889742410574883ec20488b351a289701"));
  CHECK(logged("Check: head visibility: default pass id ok observed=f2760503 at exe+0x27ce5b0") && logged("shadow pass id ok observed=3100c3d2"));
  CHECK(logged("Check: head visibility: world handle ok observed=0x30000000 behind the userdata"));
  CHECK(logged("Check: head visibility: unit handle ok observed=0x2d (tag 1) once among 5 units"));
  CHECK(logged("Check: head visibility: unit object ok observed=object 0x20000000: reference 0xb, 90 nodes, 3 meshes; engine: 90 nodes, 3 meshes"));
  CHECK(logged("Check: head visibility: mesh handle ok observed=0x20004000 (Lua type 2)") && !logged("Refused") && !strstr(calls, "set_"));
  /* each one failing: one Refused line with what was seen, nothing written */
  const TfpHeadBuild *hb = &tfp_head_build_25480438;
#define REFUSES(text) (fresh(), !tfp_wire_engine_check_head(&wire, &body) && logged(text) && logged_count("Refused") == 1 && !strstr(calls, "set_"))
  put32(EXE + hb->registry_code + 18, 0x01972819);
  CHECK(REFUSES("Refused: head visibility: unit registry code observed=48895c24084889742410574883ec20488b3519289701 expected=48895c24084889742410574883ec20488b351a289701"));
  put32(EXE + hb->registry_code + 18, 0x0197281a);
  put32(EXE + hb->context_shadow, 7);
  CHECK(REFUSES("Refused: head visibility: shadow pass id observed=00000007 at exe+0x27ce5c8 expected=3100c3d2"));
  put32(EXE + hb->context_shadow, hb->context_shadow_value);
  unreadable = EXE + hb->context_default;
  CHECK(REFUSES("Refused: head visibility: default pass id observed=unreadable at exe+0x27ce5b0 expected=f2760503"));
  unreadable = 0;
  put64((uint64_t)(uintptr_t)world_block, 0x31000000);
  CHECK(REFUSES("Refused: head visibility: world handle observed=0x31000000 behind the userdata") && logged("expected=0x30000000 (the body unit's world, object +0x18)"));
  unreadable = units[0].object + 0x18;
  CHECK(REFUSES("Refused: head visibility: body unit object observed=unreadable"));
  unreadable = 0;
  put64((uint64_t)(uintptr_t)world_block, NATIVE_WORLD);
  handle_skew = 0x40; /* the handles are not the references: nothing in the inventory is the body */
  CHECK(REFUSES("Refused: head visibility: unit handle observed=none of 5 units (the first is 0x12d) expected=one handle whose address / 4 is the body's reference 0xb"));
  handle_skew = 0;
  put32(units[0].object + 0x70, 91);
  CHECK(REFUSES("Refused: head visibility: unit object observed=object 0x20000000: reference 0xb, 91 nodes, 3 meshes; engine: 90 nodes, 3 meshes"));
  put32(units[0].object + 0x70, 90);
  put64(meshes_at(&units[0]), 0x20004100);
  CHECK(REFUSES("Refused: head visibility: mesh handle observed=0x20004000 (Lua type 2) expected=0x20004100 (the first mesh object of the unit)"));
  put64(meshes_at(&units[0]), mesh_object(&units[0], 1));
  raise_in = "World.units";
  CHECK(REFUSES("Refused: head visibility: unit inventory observed=World.units: World.units raised on purpose"));
  raise_in = NULL;
  CHECK(tfp_wire_engine_check_head(&wire, &body));

  /* ---- a failed check is tried again, not kept for the session (session 3850350d: the pass id was still 0
   * when the view was first switched on, before a mission world, and the helmet showed for the whole session) ---- */
  tfp_wire_engine_force(&wire, TFP_WE_HEAD, TFP_WE_RESOLVED);
  CHECK(tfp_wire_engine_head_due(&wire));
  put32(EXE + hb->context_default, 0);
  fresh();
  CHECK(!tfp_wire_engine_check_head(&wire, &body) && logged("Refused: head visibility: default pass id observed=00000000"));
  tfp_wire_engine_head_checked(&wire, false);
  CHECK(tfp_wire_engine_state(&wire, TFP_WE_HEAD) == TFP_WE_RESOLVED && logged("Waiting: head visibility: its checks are tried again"));
  CHECK(!tfp_wire_engine_head_due(&wire)); /* not every frame */
  fresh();
  for (int i = 0; i < 119; i++) tfp_wire_engine_begin(&wire);
  CHECK(!tfp_wire_engine_head_due(&wire));
  tfp_wire_engine_begin(&wire);
  CHECK(tfp_wire_engine_head_due(&wire));
  CHECK(!tfp_wire_engine_check_head(&wire, &body) && !logged("Refused") && !logged("Check:")); /* the same failure is not said again */
  tfp_wire_engine_head_checked(&wire, false);
  CHECK(!logged("Waiting: head visibility"));
  put32(EXE + hb->context_default, hb->context_default_value);
  for (int i = 0; i < 120; i++) tfp_wire_engine_begin(&wire);
  CHECK(tfp_wire_engine_head_due(&wire) && tfp_wire_engine_check_head(&wire, &body));
  tfp_wire_engine_head_checked(&wire, true);
  CHECK(tfp_wire_engine_state(&wire, TFP_WE_HEAD) == TFP_WE_READY && logged("Ready: head visibility: the in-game checks passed on try 3"));
  CHECK(logged("Check: head visibility: default pass id ok")); /* the passing run is said in full */

  /* ---- the in-game checks of the arms ---- */
  CHECK(head->World_units(head->user, world, &list, &count, &error) && count == UNITS);
  TfpArmHandle rows[4] = {{BODY, (TfpArmUnit)list[0]}, {RIG, (TfpArmUnit)list[1]}, {PIECE, (TfpArmUnit)list[2]}, {WEAPON, (TfpArmUnit)list[3]}};
  TfpArmHandles handles = {1, 4, rows}, bare = {2, 2, rows}, no_rig = {3, 1, rows};
  bool sleeves_ok = false, grip_ok = false;
  fresh();
  CHECK(tfp_wire_engine_check_arms(&wire, &body, &no_rig, &sleeves_ok, &grip_ok) == -1 && line_count == 0);
  CHECK(tfp_wire_engine_check_arms(&wire, &body, &bare, &sleeves_ok, &grip_ok) == -1 && tfp_wire_engine_check_arms(&wire, &body, &bare, &sleeves_ok, &grip_ok) == -1);
  CHECK(line_count == 1 && logged("Waiting: arm pieces, grip hand: none of 2 units names the rig as its parent at +0x1D0 yet")); /* judged later, said once */
  fresh();
  CHECK(tfp_wire_engine_check_arms(&wire, &body, &handles, &sleeves_ok, &grip_ok) == 1 && sleeves_ok && grip_ok);
  CHECK(logged("Check: arm pieces: +0x1D0 parent unit ok observed=2 of 4 units have the rig object 0x20010000 there; 1 of them linked to rig bone 8"));
  CHECK(logged("Check: grip hand: +0x70 node count ok observed=rig 63 (engine 63), body 90 (engine 90)"));
  CHECK(logged("Bones: rig names 0-55 of 63: a0004d00 a0004d01") && logged("Bones: rig names 56-62 of 63:") && logged("Bones: rig links 0-55 of 63: 00000000 00000001 00010001"));
  CHECK(logged("Bones: body names 56-89 of 90:") && logged(" 4fdd3337 "));
  CHECK(logged("Check: grip hand: rig bone count ok observed=63"));
  CHECK(logged("Check: arm pieces: sleeve bone names ok observed=left shoulder 03752c98 rig 10 body 40, collar c4787b4e body 43; right shoulder 1620b2ce rig 30 body 41, collar b2bcd839 body 44; chest 37ef0820 body 42"));
  CHECK(logged("Check: grip hand: grip bone names ok observed=weapon bone 4fdd3337 on rig bone 8, left hand 5c0c68d3 on body bone 45, right hand 248004f5 on body bone 46"));
  CHECK(logged("Check: arm pieces: +0x98 link words ok observed=Unit.scene_graph_parent(rig, bone + 1) is the word's high half + 1 on all 62 own-parent bones of 63"));
  CHECK(logged("Check: grip hand: arm bone chain ok observed=13<12<11<10<9<8<7<6<5<4<3<2<1<0; 34<33<32<31<30<29<5<4<3<2<1<0"));
  CHECK(logged("Check: grip hand: arm bone lengths ok observed=left 0.300 + 0.250 m, right 0.300 + 0.250 m"));
  CHECK(logged("Check: arm pieces: pose scale ok observed=rig node 11 (1.00000, 1.00000, 1.00000), body node 43 (1.00000, 1.00000, 1.00000)"));
  CHECK(!logged("Refused") && !strstr(calls, "set_") && !strstr(calls, "link_unit"));
#define ARMS(result, a, b) (fresh(), tfp_wire_engine_check_arms(&wire, &body, &handles, &sleeves_ok, &grip_ok) == (result) && sleeves_ok == (a) && grip_ok == (b) && \
                            !strstr(calls, "set_") && !strstr(calls, "link_unit"))
  put32(units[1].object + 0x70, 62); /* +0x70 is not the node count */
  CHECK(ARMS(0, false, false) && logged("Refused: arm pieces: +0x70 node count observed=rig 62 (engine 63), body 90 (engine 90) expected=Unit.num_scene_graph_items of both, 1 to 255"));
  CHECK(logged("Refused: grip hand: +0x70 node count") && logged_count("Refused") == 2);
  put32(units[1].object + 0x70, 63);
  units[1].nodes = 64, put32(units[1].object + 0x70, 64); /* another rig: the sleeves do not mind, the grips' bone numbers are for 63 */
  CHECK(ARMS(1, true, false) && logged("Refused: grip hand: rig bone count observed=64 expected=63") && logged_count("Refused") == 1);
  units[1].nodes = 63, put32(units[1].object + 0x70, 63);
  put32(names_at(&units[0]) + 43 * 4, 1); /* the body has no left collar */
  CHECK(ARMS(1, false, true) && logged("Refused: arm pieces: sleeve bone names observed=left shoulder 03752c98 rig 10 body 40, collar c4787b4e body -1;") && logged_count("Refused") == 1);
  put32(names_at(&units[0]) + 43 * 4, 0xC4787B4E);
  put32(names_at(&units[1]) + 8 * 4, 2), put32(names_at(&units[1]) + 9 * 4, 0x4FDD3337); /* the weapon bone is rig bone 9 */
  CHECK(ARMS(1, true, false) && logged("Refused: grip hand: grip bone names observed=weapon bone 4fdd3337 on rig bone 9,") && logged("expected=the weapon bone on rig bone 8 and the left hand on the body"));
  put32(names_at(&units[1]) + 8 * 4, 0x4FDD3337), put32(names_at(&units[1]) + 9 * 4, 3);
  parent_off = true; /* +0x98 is not the parent table: the engine names another parent for bone 20 */
  CHECK(ARMS(0, false, false) && logged("Refused: arm pieces: +0x98 link words observed=1 of 62 bones differ; first: bone 20 has link word 00130001, the engine says parent 23"));
  parent_off = false;
  put32(links_at(&units[1]) + 12 * 4, 1u | (9u << 16)); /* the elbow is not between hand and shoulder */
  CHECK(ARMS(1, true, false) && logged("Refused: grip hand: arm bone chain observed=13<12<9<8<") && logged("expected=13 under 12 under 10, 34 under 32 under 30"));
  put32(links_at(&units[1]) + 12 * 4, 1u | (11u << 16));
  right_arm = .45f; /* the two arms are not alike: these are not the arm bones */
  CHECK(ARMS(1, true, false) && logged("Refused: grip hand: arm bone lengths observed=left 0.300 + 0.250 m, right 0.450 + 0.250 m"));
  right_arm = .30f;
  rig_scale = 1.1f; /* a scaled bone: rotation and position alone would lose it */
  CHECK(ARMS(0, false, false) && logged("Refused: grip hand: pose scale observed=rig node 11 (1.10000, 1.10000, 1.10000), body node 43 (1.00000, 1.00000, 1.00000) expected=1 within 0.001 on each axis"));
  rig_scale = 1;
  unreadable = units[1].object + 0xA0;
  CHECK(ARMS(0, false, false) && logged("Refused: arm pieces: +0xA0 names and +0x98 links observed=unreadable"));
  unreadable = 0;
  CHECK(ARMS(1, true, true) && !logged("Refused"));

  /* ---- World.link_unit: the first link is read back before a second is made ---- */
  h->arm_pieces_update(h, NULL, false, tfp_wire_nil()); /* no snapshot yet: nothing to read a link back for */
  TfpOpsSnapshot snapshot = sample();
  tfp_wire_snapshot_table(&wire, &snapshot, &t);
  CHECK(h->arm_pieces_update(h, &t, false, tfp_wire_nil())); /* the binder now knows whose units these are */
  fresh();
  link_mode = 1; /* the engine takes the call and links nothing */
  CHECK(!sleeves->World_link_unit(sleeves->user, arm_world, (TfpArmUnit)piece_unit, 3, (TfpArmUnit)rig_unit, 12, &arm_error));
  CHECK(logged("Refused: arm pieces: link readback observed=00010001 (before 00010001) expected=000b0002 (mode 2, bone 11)") && !logged("Undo"));
  CHECK(!strcmp(calls, "link_unit(90,3,77,12);") && strstr(arm_error.text, "did not read back"));
  calls[0] = 0;
  CHECK(!sleeves->World_link_unit(sleeves->user, arm_world, (TfpArmUnit)piece_unit, 3, (TfpArmUnit)rig_unit, 12, &arm_error) && !calls[0]); /* no second link */
  CHECK(!sleeves->Unit_set_local_pose(sleeves->user, (TfpArmUnit)piece_unit, 4, &offset, &arm_error) && !calls[0]);                          /* and no sleeve write */
  CHECK(grip->Unit_set_local_pose(grip->user, (TfpArmUnit)piece_unit, 4, &offset, &arm_error));                                             /* the grips are another module */
  tfp_wire_engine_force(&wire, TFP_WE_SLEEVES, TFP_WE_READY);
  CHECK(h->arm_pieces_update(h, &t, false, tfp_wire_nil()) && tfp_wire_engine_state(&wire, TFP_WE_SLEEVES) == TFP_WE_REFUSED); /* off for the session */

  /* ---- the Lua state is replaced: every handle of the old one is dead, the modules start over ---- */
  fresh();
  CloseHook(VM);
  CHECK(!eng_ready());
  tfp_wire_engine_begin(&wire);
  CHECK(logged("The engine's Lua state went away") && tfp_wire_engine_state(&wire, TFP_WE_HEAD) == TFP_WE_WAITING);
  CHECK(!head->Unit_set_mesh_visibility(head->user, rig_unit, 3, true, "default", &error) && !calls[0]);
  CHECK(tfp_wire_engine_address(&wire, rig_unit) == 0);
  memset(kregs, 0, sizeof kregs), live_refs = 0; /* the new state's own registry */
  script_call();
  CHECK(eng_ready() && eng_state_generation() == 3);
  tfp_wire_engine_begin(&wire);
  CHECK(tfp_wire_engine_state(&wire, TFP_WE_HEAD) == TFP_WE_RESOLVED && tfp_wire_engine_state(&wire, TFP_WE_SLEEVES) == TFP_WE_RESOLVED);
  CHECK(!head->Unit_set_mesh_visibility(head->user, rig_unit, 3, true, "default", &error) && strstr(error.text, "earlier Lua state") && !strstr(calls, "set_"));
  CHECK(head->Application_main_world(head->user, &world, &error) && head->World_units(head->user, world, &list, &count, &error) && count == UNITS);
  rig_unit = list[1], piece_unit = list[2];
  CHECK(sleeves->Application_main_world(sleeves->user, &arm_world, &arm_error));
  /* the link read back, and with a link that lands on another bone, retail's undo: back to where the game had it */
  h->arm_pieces_update(h, &t, false, tfp_wire_nil());
  fresh();
  link_mode = 0;
  CHECK(sleeves->World_link_unit(sleeves->user, arm_world, (TfpArmUnit)piece_unit, 1, (TfpArmUnit)rig_unit, 12, &arm_error));
  CHECK(logged("Check: arm pieces: link readback ok observed=000b0002 (before 00030002)") && get32(links_at(&units[2])) == (2u | (11u << 16)));
  link_mode = 2;
  CHECK(sleeves->World_link_unit(sleeves->user, arm_world, (TfpArmUnit)piece_unit, 1, (TfpArmUnit)rig_unit, 4, &arm_error) && line_count == 1); /* checked once */
  script_call(); /* (no new state: the hook only catches when none is held) */
  CHECK(eng_state_generation() == 3);
  CloseHook(VM), memset(kregs, 0, sizeof kregs), live_refs = 0, script_call();
  tfp_wire_engine_begin(&wire);
  CHECK(head->Application_main_world(head->user, &world, &error) && head->World_units(head->user, world, &list, &count, &error));
  rig_unit = list[1], piece_unit = list[2];
  CHECK(sleeves->Application_main_world(sleeves->user, &arm_world, &arm_error));
  h->arm_pieces_update(h, &t, false, tfp_wire_nil());
  put32(links_at(&units[2]), 2u | (3u << 16));
  fresh();
  CHECK(!sleeves->World_link_unit(sleeves->user, arm_world, (TfpArmUnit)piece_unit, 1, (TfpArmUnit)rig_unit, 12, &arm_error));
  CHECK(logged("Refused: arm pieces: link readback observed=000f0002 (before 00030002) expected=000b0002 (mode 2, bone 11)"));
  CHECK(!strcmp(calls, "link_unit(90,1,77,12);link_unit(90,1,77,4);") && logged("Undo: arm pieces: the link was put back to bone 3: called, link word now 00070002 (not as before)"));
  link_mode = 0;

  /* ---- a function that is not this build's: the module that needs it refuses as a whole, by name ---- */
#define RECATCH() (CloseHook(VM), memset(kregs, 0, sizeof kregs), live_refs = 0, script_call(), fresh(), tfp_wire_engine_begin(&wire))
  KVal *link = reg("World.link_unit", f_link_unit), saved = *link;
  link->id += 0x40;
  RECATCH();
  CHECK(logged("Refused: arm pieces: stingray.World.link_unit is at exe+0x3eb8a0, expected exe+0x3eb860") && logged_count("Refused") == 1);
  CHECK(tfp_wire_engine_state(&wire, TFP_WE_SLEEVES) == TFP_WE_REFUSED && tfp_wire_engine_state(&wire, TFP_WE_GRIP) == TFP_WE_RESOLVED);
  CHECK(tfp_wire_engine_state(&wire, TFP_WE_HEAD) == TFP_WE_RESOLVED && logged("Ready: grip hand") && logged("Check: grip hand: value maths ok"));
  CHECK(h->arm_pieces_update(h, &t, true, tfp_wire_nil()) && !strstr(calls, "link_unit")); /* off: the off answer, no engine call */
  link->id = 0; /* a script function in its place (another mod wrapped it) */
  RECATCH();
  CHECK(logged("Refused: arm pieces: stingray.World.link_unit is not the engine's C function (a script replaced it), expected exe+0x3eb860"));
  link->id = 0x7000;
  RECATCH();
  CHECK(logged("Refused: arm pieces: stingray.World.link_unit is at 0x7000 outside the executable, expected exe+0x3eb860"));
  *link = saved;
  KVal *has = reg("Unit.has_mesh", f_has_mesh), kept = *has;
  has->t = K_NIL;
  RECATCH();
  CHECK(logged("Refused: head visibility: stingray.Unit.has_mesh is missing") && logged_count("Refused") == 1);
  CHECK(tfp_wire_engine_state(&wire, TFP_WE_HEAD) == TFP_WE_REFUSED && tfp_wire_engine_state(&wire, TFP_WE_GRIP) == TFP_WE_RESOLVED);
  CHECK(h->head_update(h, &v, &v2) && v.kind == TFP_RT_TRUE && h->head_sdk_handles(h, &v) && v.kind == TFP_RT_NIL && !calls[0]);
  CHECK(h->grip_hand_update(h, &t, true, tfp_wire_nil()) && logged("Refused: grip hand: head visibility is off") && tfp_wire_engine_state(&wire, TFP_WE_GRIP) == TFP_WE_REFUSED);
  *has = kept;
  KVal *vector = kfield(&kstingray, "Vector3"), vector_was = *vector;
  vector->t = K_NIL;
  RECATCH();
  CHECK(logged("Refused: arm pieces: stingray.Vector3 is missing") && logged("Refused: grip hand: stingray.Vector3 is missing") && logged("Ready: head visibility"));
  *vector = vector_was;
  swap_multiply = true; /* every function is there, but the engine multiplies the other way round */
  RECATCH();
  CHECK(logged("Refused: arm pieces: value maths observed=largest difference") && logged("(Quaternion.multiply) over 16 comparisons expected=the engine's own answers within 0.0001"));
  CHECK(logged("Refused: grip hand: value maths") && tfp_wire_engine_state(&wire, TFP_WE_GRIP) == TFP_WE_REFUSED && tfp_wire_engine_state(&wire, TFP_WE_HEAD) == TFP_WE_RESOLVED);
  swap_multiply = false;
  RECATCH();
  CHECK(!logged("Refused") && tfp_wire_engine_state(&wire, TFP_WE_GRIP) == TFP_WE_RESOLVED);

  /* ---- bound: the real modules behind the host pointers (no game behind the backend, so each says why it does nothing) ---- */
  for (int m = 0; m < TFP_WE_MODULES; m++) tfp_wire_engine_force(&wire, m, TFP_WE_READY);
  fresh();
  CHECK(h->head_update(h, &v, &v2) && (v.kind == TFP_RT_TRUE || v.kind == TFP_RT_FALSE));
  CHECK(h->head_result(h, &t) && t.present && t.f[TFP_RT_TARGET_PHASE].kind == TFP_RT_TEXT && h->head_is_clean(h, &v) && v.kind == TFP_RT_TRUE);
  CHECK(h->head_sdk_handles(h, &v) && v.kind == TFP_RT_NIL); /* head visibility holds no handle table yet */
  CHECK(h->head_stop(h, &v, &v2) && h->head_shutdown(h, &v, &v2));
  snapshot = sample();
  tfp_wire_snapshot_table(&wire, &snapshot, &t);
  CHECK(h->arm_pieces_update(h, &t, true, tfp_wire_nil()) && h->grip_hand_update(h, &t, true, tfp_wire_nil())); /* handles nil: "not a table" */
  CHECK(h->arm_pieces_is_clean(h, &v) && v.kind == TFP_RT_TRUE && h->grip_hand_is_clean(h, &v) && v.kind == TFP_RT_TRUE);
  CHECK(h->arm_pieces_stop(h, &v, &v2) && v.kind == TFP_RT_TRUE && v2.kind == TFP_RT_NIL && h->grip_hand_stop(h, &v, &v2) && v.kind == TFP_RT_TRUE);
  tfp_wire_engine_frame(&wire);
  CHECK(tfp_wire_engine_second(&wire, second, sizeof second) && !strcmp(second, "head=idle:0 sleeves=idle:0 grip=idle corr=0 max=0.005m/90.0deg")); /* the grip write above */
  CHECK(tfp_wire_engine_second(&wire, second, sizeof second) && !strcmp(second, "head=idle:0 sleeves=idle:0 grip=idle corr=0 max=0.000m/0.0deg"));
  CHECK(!strstr(second, "waiting") && !strstr(calls, "set_") && !strstr(calls, "link_unit") && ktop == 0);

  printf("Engine binder: %d checks: the bridge's marshalling over a stand-in Lua state (%zu functions compared with build 25480438's), the handle table across "
         "inventories and Lua states, every in-game check passing and failing, the link readback and its undo, the runtime adapters off and bound\n",
         checks, (tfp_wire_engine_functions(&count), count));
  return 0;
}
