/* Shared by the tests of the effects modules (dof_fx, vignette, aim_markers, first_person_movement,
 * camera_compat, native_ads_option, diagnostics): the C side of tools/retail_ref/effects_common.lua.
 * A vector header (tests/vectors/<m>_vectors.h, made by tools/retail_ref/gen_<m>.py from the retail
 * Lua's own answers) lists runs of operations; each operation carries the world changes the scripted
 * fakes see during it and `expect`: what retail returned, its state afterwards and the ordered trace
 * of its host calls. The test's fakes answer from the same world, in the same way as the Lua fakes,
 * and the two texts must be equal byte for byte. Numbers that are not small integers are printed
 * as the bits of the double. */
#ifndef TFP_EFFECTS_FX_H
#define TFP_EFFECTS_FX_H
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* t: 'n' nil, 'f' false, 't' true, 'd' number (n), 's' text (s, len). */
typedef struct FxValue { char t; double n; const char *s; int len; } FxValue;
/* A world change: before the operation (at 0) or just before host call number `at` is answered. */
typedef struct FxEvent { int at, slot, keyed; double key; FxValue v; } FxEvent;
typedef struct FxOp { const char *call; FxValue a[4]; int first, nev; const char *expect; } FxOp;
typedef struct FxRun { const char *name; int first, nops; } FxRun;

#define FX_SLOTS 256
#define FX_KEYED (1u << 18)
static FxValue fx_world[FX_SLOTS];
static struct FxCell { bool used; int slot; double key; FxValue v; } fx_cells[FX_KEYED];
static int fx_mem = -1;                     /* the slot that is byte memory, when the module has one */
static int fx_any = -1;                     /* the slot x_any, when the module has one */
static const FxEvent *fx_ev;
static int fx_nev, fx_calls;
static char fx_out[1 << 20], fx_trace[1 << 20];
static size_t fx_out_used, fx_trace_used;

static struct FxCell *fx_cell(int slot, double key, bool create) {
  uint64_t h;
  memcpy(&h, &key, sizeof h);
  h = (h ^ (h >> 29) ^ ((uint64_t)slot << 40)) * 0x9E3779B97F4A7C15ull;
  for (uint32_t i = (uint32_t)(h >> 40) % FX_KEYED, n = 0; n < FX_KEYED; n++, i = (i + 1) % FX_KEYED) {
    struct FxCell *c = &fx_cells[i];
    if (!c->used) {
      if (!create) return NULL;
      c->used = true; c->slot = slot; c->key = key; c->v = (FxValue){'n', 0, NULL, 0};
      return c;
    }
    if (c->slot == slot && c->key == key) return c;
  }
  fprintf(stderr, "effects_fx: keyed world full\n");
  exit(2);
}
static FxValue fx_getk(int slot, double key) {
  struct FxCell *c = fx_cell(slot, key, false);
  return c ? c->v : (FxValue){'n', 0, NULL, 0};
}
static void fx_poke(int slot, double key, FxValue v) {
  if (slot == fx_mem && v.t == 's') {
    for (int i = 0; i < v.len; i++) fx_cell(slot, key + i, true)->v = (FxValue){'d', (unsigned char)v.s[i], NULL, 0};
  } else if (slot == fx_mem && v.t == 'd') {
    for (int i = 0; i < (int)v.n; i++) { struct FxCell *c = fx_cell(slot, key + i, false); if (c) c->v.t = 'n'; }
  } else fx_cell(slot, key, true)->v = v;
}
static void fx_apply(const FxEvent *e) {
  if (e->keyed) fx_poke(e->slot, e->key, e->v);
  else fx_world[e->slot] = e->v;
}
/* Memory: false when any byte is unmapped. */
static bool fx_read(double address, int size, unsigned char *out) {
  for (int i = 0; i < size; i++) {
    FxValue v = fx_getk(fx_mem, address + i);
    if (v.t != 'd') return false;
    out[i] = (unsigned char)v.n;
  }
  return true;
}
static void fx_write(double address, const unsigned char *bytes, int size) {
  fx_poke(fx_mem, address, (FxValue){'s', 0, (const char *)bytes, size});
}

/* Text helpers; each returns one of a few rotating buffers. */
static char *fx_scratch(size_t *size) {
  static char buffers[8][16384];
  static unsigned next;
  *size = sizeof buffers[0];
  return buffers[next++ % 8];
}
static const char *fx_num(double x) {
  size_t size;
  char *out = fx_scratch(&size);
  uint64_t bits;
  memcpy(&bits, &x, sizeof bits);
  if (x != x) snprintf(out, size, "nan");
  else if (x == floor(x) && fabs(x) < 9007199254740992.0 && !(x == 0 && signbit(x))) snprintf(out, size, "%.0f", x);
  else snprintf(out, size, "x%016llx", (unsigned long long)bits);
  return out;
}
static const char *fx_strn(const char *s, size_t len) {
  size_t size, used = 0;
  char *out = fx_scratch(&size);
  out[used++] = '"';
  for (size_t i = 0; i < len && used + 6 < size; i++) {
    unsigned char b = (unsigned char)s[i];
    if (b < 32 || b > 126 || b == '"' || b == '\\') used += (size_t)snprintf(out + used, size - used, "\\x%02x", b);
    else out[used++] = (char)b;
  }
  out[used++] = '"';
  out[used] = 0;
  return out;
}
/* NULL prints as nil. */
static const char *fx_str(const char *s) { return s ? fx_strn(s, strlen(s)) : "~"; }
static const char *fx_bool(bool b) { return b ? "T" : "F"; }
static const char *fx_show(FxValue v) {
  switch (v.t) {
    case 'n': return "~";
    case 't': return "T";
    case 'f': return "F";
    case 'd': return fx_num(v.n);
    default: return fx_strn(v.s, (size_t)v.len);
  }
}

static void fx_put(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void fx_put(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fx_out_used += (size_t)vsnprintf(fx_out + fx_out_used, sizeof fx_out - fx_out_used, format, args);
  va_end(args);
  if (fx_out_used >= sizeof fx_out) { fprintf(stderr, "effects_fx: result text too long\n"); exit(2); }
}
/* One trace entry. */
static void fx_note(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void fx_note(const char *format, ...) {
  va_list args;
  if (fx_trace_used) fx_trace[fx_trace_used++] = ' ';
  va_start(args, format);
  fx_trace_used += (size_t)vsnprintf(fx_trace + fx_trace_used, sizeof fx_trace - fx_trace_used, format, args);
  va_end(args);
  if (fx_trace_used >= sizeof fx_trace) { fprintf(stderr, "effects_fx: trace too long\n"); exit(2); }
}
/* A host call begins: count it and apply the world changes due at that count. */
static void fx_tick(void) {
  fx_calls++;
  for (int i = 0; i < fx_nev; i++) if (fx_ev[i].at == fx_calls) fx_apply(&fx_ev[i]);
}
/* The text a host function raises now, or NULL. */
static const char *fx_raise(int slot) {
  if (fx_world[slot].t == 's') return fx_world[slot].s;
  return fx_any >= 0 && fx_world[fx_any].t == 's' ? fx_world[fx_any].s : NULL;
}
static void fx_log(const char *text) { fx_note("log(%s)", fx_str(text)); }

static bool fx_truthy(FxValue v) { return v.t != 'n' && v.t != 'f'; }
static bool fx_is(int slot, char t) { return fx_world[slot].t == t; }
static double fx_n(int slot) { return fx_world[slot].n; }
static const char *fx_s(int slot) { return fx_world[slot].t == 's' ? fx_world[slot].s : NULL; }

/* Replays every run. `fresh` builds a new module instance; `dispatch` makes the call and prints its
 * results and the state with fx_put. Returns the process exit code. */
static int fx_main(const char *label, const char *commit, const FxRun *runs, int nruns, const FxOp *ops, const FxEvent *events,
                   const char *const *slot_names, int nslots, void (*fresh)(void), void (*dispatch)(const FxOp *)) {
  int total = 0, failed = 0;
  static char line[(2 << 20) + 8];
  if (nslots > FX_SLOTS) { fprintf(stderr, "effects_fx: too many slots\n"); return 2; }
  for (int r = 0; r < nruns; r++) {
    memset(fx_world, 0, sizeof fx_world);
    for (int i = 0; i < FX_SLOTS; i++) fx_world[i].t = 'n';
    memset(fx_cells, 0, sizeof fx_cells);
    fresh();
    for (int o = 0; o < runs[r].nops; o++, total++) {
      const FxOp *op = &ops[runs[r].first + o];
      fx_ev = events + op->first; fx_nev = op->nev; fx_calls = 0; fx_out_used = fx_trace_used = 0;
      fx_out[0] = fx_trace[0] = 0;
      for (int i = 0; i < fx_nev; i++) if (fx_ev[i].at == 0) fx_apply(&fx_ev[i]);
      dispatch(op);
      for (int i = 0; i < nslots; i++) if (slot_names[i][0] == 'x' && slot_names[i][1] == '_') fx_world[i] = (FxValue){'n', 0, NULL, 0};
      memcpy(line, fx_out, fx_out_used); memcpy(line + fx_out_used, " | ", 3);
      memcpy(line + fx_out_used + 3, fx_trace, fx_trace_used + 1);
      if (strcmp(line, op->expect) != 0) {
        size_t at = 0;
        while (line[at] && line[at] == op->expect[at]) at++;
        fprintf(stderr, "%s: run %s, operation %d (%s) differs at byte %zu\n  retail: %s\n  C:      %s\n", label, runs[r].name, o, op->call,
                at, op->expect, line);
        if (++failed >= 5) { fprintf(stderr, "%s: stopping after 5 differences\n", label); return 1; }
      }
    }
  }
  if (failed) return 1;
  printf("%s: %d operations in %d runs match retail %s\n", label, total, nruns, commit);
  return 0;
}
/* Not every test uses every helper. */
static inline void fx_unused(void) {
  (void)fx_getk; (void)fx_read; (void)fx_write; (void)fx_bool; (void)fx_show; (void)fx_put; (void)fx_note; (void)fx_tick;
  (void)fx_raise; (void)fx_log; (void)fx_truthy; (void)fx_is; (void)fx_n; (void)fx_s; (void)fx_main; (void)fx_str; (void)fx_num;
}
#endif
