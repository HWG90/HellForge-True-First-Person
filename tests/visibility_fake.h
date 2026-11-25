/* The scripted world of the head-visibility and model-shadow-source vector tests, built as
 * tools/retail_ref/run_visibility.lua builds its own: a sparse byte memory, named numbers and
 * texts, a list of SDK Unit handles, named rig meshes, and a backend, engine and options that
 * answer from them. A script line changes the world, may change it again when its N-th call
 * arrives (`@N`), and names the call. Every call made on the world is an event: all of them go
 * into two running checksums in order, and those that are not plain memory reads are also
 * kept as text. Include after model_shadow_source.h (and head_visibility.h for the head parts). */
#ifndef TFP_VISIBILITY_FAKE_H
#define TFP_VISIBILITY_FAKE_H
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wunused-function"   /* each test uses its own part of this file */

/* ---- text building ---- */
typedef struct VfBuf { char *data; size_t len, room; } VfBuf;
static void vf_put(VfBuf *b, const char *bytes, size_t len) {
  if (b->len + len + 1 > b->room) {
    b->room = (b->len + len + 1) * 2;
    b->data = realloc(b->data, b->room);
  }
  memcpy(b->data + b->len, bytes, len);
  b->len += len;
  b->data[b->len] = 0;
}
static void vf_printf(VfBuf *b, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void vf_printf(VfBuf *b, const char *format, ...) {
  char small[512];
  va_list args;
  va_start(args, format);
  int need = vsnprintf(small, sizeof small, format, args);
  va_end(args);
  if (need < (int)sizeof small) { vf_put(b, small, (size_t)need); return; }
  char *big = malloc((size_t)need + 1);
  va_start(args, format);
  vsnprintf(big, (size_t)need + 1, format, args);
  va_end(args);
  vf_put(b, big, (size_t)need);
  free(big);
}
static void vf_clear(VfBuf *b) { b->len = 0; if (b->data) b->data[0] = 0; }
/* A number as both sides print it. */
static const char *vf_num(double x) {
  static char ring[16][40];
  static int at;
  char *out = ring[at++ & 15];
  if (x != x) strcpy(out, "nan");
  else if (x == INFINITY) strcpy(out, "inf");
  else if (x == -INFINITY) strcpy(out, "-inf");
  else if (x == floor(x) && x > -9007199254740992.0 && x < 9007199254740992.0) snprintf(out, 40, "%.0f", x);
  else snprintf(out, 40, "%.17g", x);
  return out;
}
static void vf_hex(VfBuf *b, const uint8_t *bytes, size_t len) {
  for (size_t i = 0; i < len; i++) vf_printf(b, "%02x", bytes[i]);
}
static bool vf_full;   /* run option full=1: long parts are printed whole */
/* A long part of a result line is printed as its length and checksums. */
static void vf_part(VfBuf *out, const char *s, size_t len) {
  if (len <= 64 || vf_full) { vf_put(out, s, len); return; }
  uint64_t h1 = 1, h2 = 2;
  for (size_t i = 0; i < len; i++) {
    uint64_t v = (uint8_t)s[i];
    h1 = (h1 * 65599 + v) % 4294967291u;
    h2 = (h2 * 40503 + v + 1) % 4294967279u;
  }
  vf_printf(out, "#%zu:%llu.%llu", len, (unsigned long long)h1, (unsigned long long)h2);
}
static void vf_identity(VfBuf *b, const TfpVisIdentity *id) {
  if (!id->present) { vf_put(b, "~", 1); return; }
  vf_put(b, "[", 1); vf_put(b, (const char *)id->bytes, id->len); vf_put(b, "]", 1);
}

/* ---- memory ---- */
#define VF_PAGES 16384
typedef struct VfPage { uint64_t key; uint8_t *bytes; } VfPage;
static VfPage vf_pages[VF_PAGES];
static uint8_t *vf_page(uint64_t at, bool create) {
  uint64_t key = at >> 12;
  size_t slot = (size_t)((key * 0x9E3779B97F4A7C15ull) >> 50) % VF_PAGES;
  for (size_t tries = 0; tries < VF_PAGES; tries++, slot = (slot + 1) % VF_PAGES) {
    if (vf_pages[slot].bytes && vf_pages[slot].key == key) return vf_pages[slot].bytes;
    if (!vf_pages[slot].bytes) {
      if (!create) return NULL;
      vf_pages[slot].key = key;
      return vf_pages[slot].bytes = calloc(1, 4096);
    }
  }
  fprintf(stderr, "visibility fake: memory full\n");
  exit(2);
}
static bool vf_whole(double at) { return at == floor(at) && at >= 0 && at < 18446744073709549568.0; }
static uint8_t vf_byte(double at) {
  if (!vf_whole(at)) return 0;
  uint8_t *page = vf_page((uint64_t)at, false);
  return page ? page[(uint64_t)at & 4095] : 0;
}
static void vf_poke_byte(double at, uint8_t value) {
  if (vf_whole(at)) vf_page((uint64_t)at, true)[(uint64_t)at & 4095] = value;
}
static void vf_raw(double at, size_t n, uint8_t *out) { for (size_t i = 0; i < n; i++) out[i] = vf_byte(at + (double)i); }
static double vf_rawu32(double at) {
  return vf_byte(at) + vf_byte(at + 1) * 256.0 + vf_byte(at + 2) * 65536.0 + vf_byte(at + 3) * 16777216.0;
}
static double vf_rawptr(double at) { return vf_rawu32(at) + vf_rawu32(at + 4) * 4294967296.0; }
static double vf_uint(const uint8_t *b, size_t o) { return b[o] + b[o + 1] * 256.0 + b[o + 2] * 65536.0 + b[o + 3] * 16777216.0; }

/* ---- named numbers and texts ---- */
#define VF_VARS(X) X(exe) X(exe_fail) X(tick) X(mode) X(enabled) X(foreign) X(fallback) X(pinned) X(full_body) X(hide_torso) \
  X(show_torso) X(seated_head) X(pack_mode) X(pack_ref) X(act_mode) X(on_foot) X(full_fail) X(player_fail) X(pin_fail) \
  X(fail_fn) X(fail_nth) X(fail_mode) X(desc_at) X(insp_mode) X(world_handle) X(s_base) X(s_record) X(s_head) X(s_tail) \
  X(s_kind) X(s_request_weapon) X(s_world) X(s_avatar_owner) X(s_avatar_index) X(s_body) X(s_viewmodel) X(s_weapon) \
  X(s_weapon_unit) X(s_wcd) X(s_safe) X(s_seated) X(s_wt) X(s_hct) X(s_active) X(s_aiming) X(addr_bias)
#define VF_STRS(X) X(s_pid) X(s_identity) X(s_cbytes) X(s_flags) X(s_wdesc) X(pack_id)
#define X(name) VF_##name,
enum { VF_VARS(X) VF_VAR_COUNT };
#undef X
#define X(name) VFS_##name,
enum { VF_STRS(X) VF_STR_COUNT };
#undef X
#define X(name) #name,
static const char *const vf_var_names[] = {VF_VARS(X)};
static const char *const vf_str_names[] = {VF_STRS(X)};
#undef X
typedef struct VfStr { bool present; size_t len; uint8_t bytes[256]; } VfStr;
typedef struct VfRig { char name[32]; double addr, hash, node, mats, unit; bool material; } VfRig;
typedef struct VfTrigger { long event; char **tokens; size_t count; } VfTrigger;
enum { VF_MAIN_WORLD = 1, VF_UNITS, VF_SET, VF_HAS_MESH, VF_MESH, VF_MESH_NAME, VF_MESH_NODE, VF_MESH_MATERIALS, VF_MESH_HAS_MATERIAL,
       VF_PLAYER_SNAPSHOT, VF_PIN_SNAPSHOT, VF_BACKPACK, VF_FOREIGN, VF_FALLBACK, VF_PINNED, VF_FULL_BODY, VF_HIDE_TORSO,
       VF_SHOW_TORSO, VF_SEATED_HEAD, VF_DESCRIPTOR, VF_BIND_COUNT };
static struct {
  double vars[VF_VAR_COUNT];
  VfStr strs[VF_STR_COUNT];
  double *units; size_t unit_count, unit_room;
  struct { double lo, hi; } bad[64]; size_t bad_count;
  VfRig rig[32]; size_t rig_count;
  struct { double at; uint8_t value; } *keep; size_t keep_count, keep_room;
  int binds[VF_BIND_COUNT];
  VfTrigger triggers[16]; size_t trigger_count;
  unsigned calls[10];
} vf;
#define V(name) vf.vars[VF_##name]
static void vf_rebind(int code, int mode);   /* the test file points its io and engine structs */

static void vf_apply(char **tokens, size_t count) {
  for (size_t i = 0; i < count; i++) {
    char *token = tokens[i], *rest = token + 1;
    switch (token[0]) {
    case 'P': {
      char *end;
      double at = strtod(rest, &end);
      for (const char *h = end + 1; h[0] && h[1]; h += 2, at += 1) {
        unsigned value;
        sscanf(h, "%2x", &value);
        vf_poke_byte(at, (uint8_t)value);
      }
      break;
    }
    case 'V': case 'T': {
      char *equals = strchr(rest, '=');
      size_t name = (size_t)(equals - rest), found = 0;
      bool ok = false;
      if (token[0] == 'V') {
        for (; found < VF_VAR_COUNT; found++) if (strlen(vf_var_names[found]) == name && !strncmp(vf_var_names[found], rest, name)) { ok = true; break; }
        if (ok) vf.vars[found] = strtod(equals + 1, NULL);
      } else {
        for (; found < VF_STR_COUNT; found++) if (strlen(vf_str_names[found]) == name && !strncmp(vf_str_names[found], rest, name)) { ok = true; break; }
        if (ok) {
          VfStr *s = &vf.strs[found];
          s->present = equals[1] != '-';
          s->len = 0;
          for (const char *h = equals + 1; s->present && h[0] && h[1]; h += 2) {
            unsigned value;
            sscanf(h, "%2x", &value);
            s->bytes[s->len++] = (uint8_t)value;
          }
        }
      }
      if (!ok) { fprintf(stderr, "visibility fake: unknown name in %s\n", token); exit(2); }
      break;
    }
    case 'L':
      vf.unit_count = 0;
      for (char *p = rest; *p;) {
        char *end;
        double value = strtod(p, &end);
        if (vf.unit_count == vf.unit_room) vf.units = realloc(vf.units, (vf.unit_room = vf.unit_room * 2 + 64) * sizeof *vf.units);
        vf.units[vf.unit_count++] = value;
        p = *end == ',' ? end + 1 : end;
      }
      break;
    case 'X': {
      char *end;
      vf.bad[vf.bad_count].lo = strtod(rest, &end);
      vf.bad[vf.bad_count].hi = strtod(end + 1, NULL);
      if (++vf.bad_count == 64) { fprintf(stderr, "visibility fake: too many unreadable ranges\n"); exit(2); }
      break;
    }
    case 'x': vf.bad_count = 0; break;
    case 'M': {
      VfRig row = {0};
      char *comma = strchr(rest, ','), *end;
      snprintf(row.name, sizeof row.name, "%.*s", (int)(comma - rest), rest);
      row.addr = strtod(comma + 1, &end); row.hash = strtod(end + 1, &end); row.node = strtod(end + 1, &end);
      row.mats = strtod(end + 1, &end); row.material = strtod(end + 1, &end) != 0; row.unit = strtod(end + 1, &end);
      size_t at = vf.rig_count;
      for (size_t r = 0; r < vf.rig_count; r++) if (!strcmp(vf.rig[r].name, row.name)) at = r;
      if (at == vf.rig_count && ++vf.rig_count > 32) { fprintf(stderr, "visibility fake: too many rig meshes\n"); exit(2); }
      vf.rig[at] = row;
      break;
    }
    case 'm':
      for (size_t r = 0; r < vf.rig_count; r++) if (!strcmp(vf.rig[r].name, rest)) {
        memmove(&vf.rig[r], &vf.rig[r + 1], (vf.rig_count - r - 1) * sizeof vf.rig[0]);
        vf.rig_count--;
        break;
      }
      break;
    case 'K': {
      char *end;
      double at = strtod(rest, &end);
      size_t found = 0;
      while (found < vf.keep_count && vf.keep[found].at != at) found++;
      if (found == vf.keep_count) {
        if (vf.keep_count == vf.keep_room) vf.keep = realloc(vf.keep, (vf.keep_room = vf.keep_room * 2 + 64) * sizeof *vf.keep);
        vf.keep[vf.keep_count++].at = at;
      }
      vf.keep[found].value = (uint8_t)strtod(end + 1, NULL);
      break;
    }
    case 'k': for (size_t r = 0; r < vf.keep_count; r++) vf_poke_byte(vf.keep[r].at, vf.keep[r].value); break;
    case 'B': {
      int code, mode;
      sscanf(rest, "%d=%d", &code, &mode);
      vf.binds[code] = mode;
      vf_rebind(code, mode);
      break;
    }
    default: fprintf(stderr, "visibility fake: bad token %s\n", token); exit(2);
    }
  }
}

/* ---- events ---- */
static struct { long n; uint64_t h1, h2; VfBuf vis; bool dump; } vf_ev;
static void vf_mixi(uint64_t v) {
  vf_ev.h1 = (vf_ev.h1 * 65599 + v % 4294967291u) % 4294967291u;
  vf_ev.h2 = (vf_ev.h2 * 40503 + v % 4294967279u + 1) % 4294967279u;
}
static void vf_mixs(const char *s, size_t len) { vf_mixi(len); for (size_t i = 0; i < len; i++) vf_mixi((uint8_t)s[i]); }
static void vf_mixv(double v) {
  if (v == floor(v) && v >= 0 && v < 140737488355328.0) vf_mixi((uint64_t)v);
  else { const char *text = vf_num(v); vf_mixs(text, strlen(text)); }
}
static void vf_tick(void) {
  vf_ev.n++;
  for (size_t i = 0; i < vf.trigger_count; i++) if (vf.triggers[i].event == vf_ev.n) vf_apply(vf.triggers[i].tokens, vf.triggers[i].count);
}
static void vf_en(int code, int count, double a, double b, double c) {
  static const char *const names[] = {"", "r", "p", "u", "b", "ctx", "desc", "insp", "A"};
  vf_tick(); vf_mixi((uint64_t)code); vf_mixv(a);
  if (count >= 2) vf_mixv(b);
  if (count >= 3) vf_mixv(c);
  if (vf_ev.dump) {
    fprintf(stderr, "%s(%s", names[code], vf_num(a));
    if (count >= 2) fprintf(stderr, ",%s", vf_num(b));
    if (count >= 3) fprintf(stderr, ",%s", vf_num(c));
    fprintf(stderr, ")\n");
  }
}
static void vf_evis(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void vf_evis(const char *format, ...) {
  char text[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  vf_tick(); vf_mixi(255); vf_mixs(text, strlen(text));
  if (vf_ev.vis.len) vf_put(&vf_ev.vis, ";", 1);
  vf_put(&vf_ev.vis, text, strlen(text));
  if (vf_ev.dump) fprintf(stderr, "%s\n", text);
}

/* ---- the backend's reads ---- */
static bool vf_fail(TfpVisText *why, const char *text) {
  why->present = true;
  snprintf(why->text, sizeof why->text, "%s", text);
  return false;
}
static bool vf_checked(double at, size_t n, uint8_t *out, TfpVisText *why) {
  if (!(at == floor(at) && at >= 65536 && n > 0 && n <= 1024 && at + (double)n < 140737488355328.0)) return vf_fail(why, "Invalid read");
  for (size_t i = 0; i < vf.bad_count; i++) if (at < vf.bad[i].hi && at + (double)n > vf.bad[i].lo) return vf_fail(why, "Memory unavailable");
  vf_raw(at, n, out);
  return true;
}
static bool vf_cptr(double at, double *out, TfpVisText *why) {
  uint8_t b[8];
  if (!vf_checked(at, 8, b, why)) return false;
  *out = vf_uint(b, 0) + vf_uint(b, 4) * 4294967296.0;
  if (!(*out >= 65536 && *out < 140737488355328.0)) return vf_fail(why, "Pointer unavailable");
  return true;
}
static bool vf_valid_id(double n) { return n > 0 && n < 4294967295.0 && n == floor(n); }
static double vf_mod(double a, double b) { return a - floor(a / b) * b; }
static const void *vf_handle(double at) { return (const void *)(uintptr_t)at; }
static double vf_address(const void *handle) { return (double)(uintptr_t)handle; }

/* The same world as another game build lays it out. The scripts and the retail answers are in retail's
 * layout. With `on`, the code under test is given the other build's numbers, and the memory it reads is
 * the retail world with the build-dependent places moved: a read of one of `site` (an offset from the
 * executable base in this build) lands on the retail place and, when the retail bytes are the retail pin,
 * answers this build's pin; a pointer to one of the two renderer functions answers this build's; the
 * appearance rows sit at this build's offset in the avatar owner (the scripts keep the owner at VF_OWNER,
 * give or take a row). Calls are recorded with the retail address, so the checksums of the retail run
 * still apply. */
#define VF_OWNER 2138893713408.0   /* the avatar owner of every scripted world (tools/retail_ref/visibility_world.py World.owner) */
typedef struct VfSite { double here, retail, size; const uint8_t *here_bytes, *retail_bytes; } VfSite;
static struct { bool on; VfSite site[8]; size_t count; double value_here[2], value_retail[2], appearance_here, appearance_retail; } vf_layout;
static double vf_move(double at) {
  if (!vf_layout.on) return at;
  for (size_t i = 0; i < vf_layout.count; i++) {
    const VfSite *site = &vf_layout.site[i];
    if (at >= V(exe) + site->here && at < V(exe) + site->here + site->size) return at - site->here + site->retail;
  }
  double rows = VF_OWNER + vf_layout.appearance_here;
  if (at >= rows && at < rows + 0x2000) return at - vf_layout.appearance_here + vf_layout.appearance_retail;
  return at;
}
static void vf_content(double at, size_t n, uint8_t *out) {
  if (!vf_layout.on) return;
  for (size_t i = 0; i < vf_layout.count; i++) {
    const VfSite *site = &vf_layout.site[i];
    if (site->here_bytes && at == V(exe) + site->retail && (double)n == site->size && !memcmp(out, site->retail_bytes, n)) memcpy(out, site->here_bytes, n);
  }
  for (size_t o = 0; o + 8 <= n; o += 8) for (int k = 0; k < 2; k++)
    if (vf_uint(out, o) + vf_uint(out, o + 4) * 4294967296.0 == V(exe) + vf_layout.value_retail[k]) {
      uint64_t value = (uint64_t)(V(exe) + vf_layout.value_here[k]);
      for (int b = 0; b < 8; b++) out[o + (size_t)b] = (uint8_t)(value >> (8 * b));
    }
}
static bool vf_read(void *user, double at, size_t n, uint8_t *out, TfpVisText *why) {
  (void)user; at = vf_move(at); vf_en(1, 2, at, (double)n, 0);
  if (!vf_checked(at, n, out, why)) return false;
  vf_content(at, n, out);
  return true;
}
static bool vf_ptr(void *user, double at, double *out, TfpVisText *why) { (void)user; at = vf_move(at); vf_en(2, 1, at, 0, 0); return vf_cptr(at, out, why); }
static bool vf_u32(void *user, double at, double *out, TfpVisText *why) {
  uint8_t b[4];
  (void)user; at = vf_move(at); vf_en(3, 1, at, 0, 0);
  if (!vf_checked(at, 4, b, why)) return false;
  *out = vf_uint(b, 0);
  return true;
}
static bool vf_u8(void *user, double at, double *out, TfpVisText *why) {
  uint8_t b[1];
  (void)user; vf_en(4, 1, at, 0, 0);
  if (!vf_checked(at, 1, b, why)) return false;
  *out = b[0];
  return true;
}
static bool vf_alignment_context(void *user, double id, double owner, double avatar_index, TfpModelContext *out, TfpVisText *why) {
  (void)user; vf_en(5, 3, id, owner, avatar_index);
  if (!vf_valid_id(id)) return vf_fail(why, "Native first-person viewmodel unavailable");
  if (V(exe_fail) != 0) return vf_fail(why, "Executable unavailable");
  double registry, object;
  uint8_t metadata[32], generation_now[1];
  if (!vf_cptr(V(exe) + 0x1ab76f8, &registry, why)) return false;
  double index = vf_mod(id, 0x400000), generation = vf_mod(floor(id / 0x400000), 256);
  if (!vf_checked(registry + 0x88, 32, metadata, why)) return false;
  double capacity = vf_uint(metadata, 16);
  if (!(capacity > 0 && capacity <= 0x400000 && index < capacity)) return vf_fail(why, "Viewmodel registry index unavailable");
  double generations = vf_uint(metadata, 24) + vf_uint(metadata, 28) * 4294967296.0;
  double units = vf_uint(metadata, 0) + vf_uint(metadata, 4) * 4294967296.0;
  if (!(generations >= 65536 && generations < 140737488355328.0 && units >= 65536 && units < 140737488355328.0)) return vf_fail(why, "Pointer unavailable");
  if (!vf_checked(generations + index, 1, generation_now, why)) return false;
  if (generation_now[0] != generation) return vf_fail(why, "Viewmodel generation retired");
  if (!vf_cptr(units + index * 8, &object, why)) return false;
  *out = (TfpModelContext){id, index, generation, object, units, generations};
  return true;
}
static bool vf_descriptor(void *user, double world, double id, uint8_t out[24], TfpVisText *why) {
  (void)user; vf_en(6, 2, world, id, 0);
  if (V(desc_at) == 0) return vf_fail(why, "Entity lookup bound reached");
  if (!vf_checked(V(desc_at), 24, out, why)) return false;
  if (!(vf_uint(out, 8) == id && out[20] % 2 == 1)) return vf_fail(why, "Entity generation unavailable");
  return true;
}

/* ---- snapshots ---- */
static void vf_copy_identity(TfpVisIdentity *out, const VfStr *s) {
  out->present = s->present;
  out->len = (uint16_t)s->len;
  memcpy(out->bytes, s->bytes, s->len);
}
static void vf_snapshot_table(TfpVisSnapshot *t, bool full) {
  const VfStr *cb = &vf.strs[VFS_s_cbytes], *fl = &vf.strs[VFS_s_flags], *wd = &vf.strs[VFS_s_wdesc];
  memset(t, 0, sizeof *t);
  t->base = V(s_base); t->record = V(s_record); t->head = V(s_head); t->tail = V(s_tail); t->camera_kind = V(s_kind);
  t->request_weapon = V(s_request_weapon); t->world = V(s_world); t->avatar_owner = V(s_avatar_owner);
  t->avatar_index = V(s_avatar_index); t->body = V(s_body);
  t->has_viewmodel = V(s_viewmodel) >= 0; t->viewmodel = V(s_viewmodel);
  t->safe = V(s_safe) != 0; t->seated = V(s_seated) != 0; t->weapon_transition = V(s_wt) != 0;
  t->head_camera_transition = V(s_hct) != 0; t->controller_active = V(s_active) != 0; t->aiming = V(s_aiming) != 0;
  vf_copy_identity(&t->player_identity, &vf.strs[VFS_s_pid]);
  t->has_controller_bytes = cb->present; t->controller_len = (uint8_t)cb->len; memcpy(t->controller_bytes, cb->bytes, cb->len);
  t->has_flags = fl->present; t->flags_len = (uint8_t)fl->len; memcpy(t->flags, fl->bytes, fl->len);
  if (full) {
    t->has_weapon = V(s_weapon) >= 0; t->weapon = V(s_weapon);
    t->has_weapon_unit = V(s_weapon_unit) >= 0; t->weapon_unit = V(s_weapon_unit);
    t->has_weapon_component_descriptor = V(s_wcd) >= 0; t->weapon_component_descriptor = V(s_wcd);
    t->has_weapon_descriptor = wd->present; t->weapon_descriptor_len = (uint8_t)wd->len; memcpy(t->weapon_descriptor, wd->bytes, wd->len);
    vf_copy_identity(&t->identity, &vf.strs[VFS_s_identity]);
  } else vf_copy_identity(&t->identity, &vf.strs[VFS_s_pid]);
}
static bool vf_snapshot_answer(const char *name, int which, bool full, TfpVisSnapshot *out, TfpVisText *why) {
  static const char *const texts[] = {"", "Equip a firearm before starting the camera test", "Local player unavailable", "Memory unavailable"};
  vf_evis("%s", name);
  double fail = vf.vars[which];   /* read after the call is counted: a change that arrives with this call applies to it */
  why->present = false;
  if (fail == 9) return false;
  if (fail != 0) return vf_fail(why, texts[(int)fail]);
  vf_snapshot_table(out, full);
  return true;
}
static bool vf_snapshot(void *user, TfpVisSnapshot *out, TfpVisText *why) { (void)user; return vf_snapshot_answer("snap", VF_full_fail, true, out, why); }
static bool vf_foreign_model(void *user, int *value, TfpVisText *why) {
  (void)user; vf_evis("fc");
  if (V(foreign) == 2) return vf_fail(why, "Foreign camera query failed");
  *value = V(foreign) == 0 ? 0 : 1;   /* 3 is nil: not false */
  return true;
}
static void vf_custom_appearance(uint8_t out[120]) { vf_raw(V(s_avatar_owner) + 0x546ab0 + V(s_avatar_index) * 0x1b8 + 0xdc, 120, out); }

/* ---- the engine and the head's own backend calls ---- */
#ifdef TFP_HEAD_VISIBILITY_H
static int vf_gate(int code) {
  vf.calls[code]++;
  if (V(fail_fn) == code && (V(fail_nth) == 0 || V(fail_nth) == vf.calls[code])) return (int)V(fail_mode);
  return 0;
}
static bool vf_engine_fail(TfpEngineError *error, int code, const char *suffix) {
  snprintf(error->text, sizeof error->text, "engine %d failed%s", code, suffix);
  return false;
}
static const VfRig *vf_rig_by_name(double unit, const char *name) {
  for (size_t i = 0; i < vf.rig_count; i++) if (!strcmp(vf.rig[i].name, name) && (vf.rig[i].unit == 0 || vf.rig[i].unit == unit)) return &vf.rig[i];
  return NULL;
}
static const VfRig *vf_rig_by_address(double at) {
  for (size_t i = 0; i < vf.rig_count; i++) if (vf.rig[i].addr == at) return &vf.rig[i];
  return NULL;
}
static void vf_set_mask(double unit, double index, bool visible, const char *pass) {
  double ref = floor(unit / 4);
  double registry = vf_rawptr(V(exe) + 0x1ab76f8); if (registry == 0) return;
  double objects = vf_rawptr(registry + 0x88); if (objects == 0) return;
  double object = vf_rawptr(objects + vf_mod(ref, 0x400000) * 8); if (object == 0) return;
  double array = vf_rawptr(object + 0xD0); if (array == 0) return;
  double mesh = vf_rawptr(array + (index - 1) * 16); if (mesh == 0) return;
  int bit = !strcmp(pass, "default") ? 1 : !strcmp(pass, "shadow_caster") ? 2 : 0;
  if (bit == 0) return;
  int mask = vf_byte(mesh + 0xDC);
  bool has = (mask & bit) != 0;
  if (visible && !has) mask += bit; else if (!visible && has) mask -= bit;
  vf_poke_byte(mesh + 0xDC, (uint8_t)mask);
}
static bool vf_main_world(void *user, TfpEngineWorld *world, TfpEngineError *error) {
  (void)user; vf_evis("mw");
  int g = vf_gate(VF_MAIN_WORLD);
  if (g == 1 || g == 2) return vf_engine_fail(error, 1, "");
  *world = g == 3 || V(world_handle) == 0 ? NULL : (TfpEngineWorld)vf_handle(V(world_handle));
  return true;
}
static TfpEngineUnit *vf_unit_list;
static bool vf_units(void *user, TfpEngineWorld world, const TfpEngineUnit **units, size_t *count, TfpEngineError *error) {
  (void)user; vf_evis("un %s", vf_num(vf_address(world)));
  int g = vf_gate(VF_UNITS);
  if (g == 1 || g == 2) return vf_engine_fail(error, 2, "");
  *units = NULL; *count = 0;
  if (g == 3) return true;
  vf_unit_list = realloc(vf_unit_list, (vf.unit_count + 1) * sizeof *vf_unit_list);
  for (size_t i = 0; i < vf.unit_count; i++) vf_unit_list[i] = (TfpEngineUnit)vf_handle(vf.units[i]);
  *units = vf_unit_list; *count = vf.unit_count;
  return true;
}
static void (*vf_after_set)(double unit);   /* NULL: nothing; set by a test only */
static bool vf_set_mesh_visibility(void *user, TfpEngineUnit unit, int index, bool visible, const char *pass, TfpEngineError *error) {
  (void)user; vf_evis("set %s %d %s %s", vf_num(vf_address(unit)), index, visible ? "T" : "F", pass);
  int g = vf_gate(VF_SET);
  if (g == 1) return vf_engine_fail(error, 3, "");
  if (g != 3) vf_set_mask(vf_address(unit), index, visible, pass);
  if (vf_after_set) vf_after_set(vf_address(unit));   /* round 38 test hook: the world changes between two writes */
  if (g == 2) return vf_engine_fail(error, 3, " late");
  return true;
}
static bool vf_has_mesh(void *user, TfpEngineUnit unit, const char *name, bool *present, TfpEngineError *error) {
  (void)user; vf_evis("hm %s %s", vf_num(vf_address(unit)), name);
  int g = vf_gate(VF_HAS_MESH);
  if (g == 1 || g == 2) return vf_engine_fail(error, 4, "");
  *present = g != 3 && vf_rig_by_name(vf_address(unit), name) != NULL;
  return true;
}
static bool vf_mesh(void *user, TfpEngineUnit unit, const char *name, TfpEngineMesh *mesh, TfpEngineError *error) {
  (void)user; vf_evis("me %s %s", vf_num(vf_address(unit)), name);
  int g = vf_gate(VF_MESH);
  if (g == 1 || g == 2) return vf_engine_fail(error, 5, "");
  const VfRig *row = vf_rig_by_name(vf_address(unit), name);
  *mesh = g == 3 || !row ? NULL : (TfpEngineMesh)vf_handle(row->addr);
  return true;
}
static bool vf_mesh_number(int code, TfpEngineMesh mesh, double *out, TfpEngineError *error) {
  vf_evis("m%d %s", code, vf_num(vf_address(mesh)));
  int g = vf_gate(code);
  if (g == 1 || g == 2) return vf_engine_fail(error, code, "");
  const VfRig *row = vf_rig_by_address(vf_address(mesh));
  if (g == 3 || !row) *out = code == VF_MESH_MATERIALS ? 2 : 0;
  else *out = code == VF_MESH_NAME ? row->hash : code == VF_MESH_NODE ? row->node : row->mats;
  return true;
}
static bool vf_mesh_name(void *user, TfpEngineMesh mesh, double *out, TfpEngineError *error) { (void)user; return vf_mesh_number(VF_MESH_NAME, mesh, out, error); }
static bool vf_mesh_node(void *user, TfpEngineMesh mesh, double *out, TfpEngineError *error) { (void)user; return vf_mesh_number(VF_MESH_NODE, mesh, out, error); }
static bool vf_mesh_materials(void *user, TfpEngineMesh mesh, double *out, TfpEngineError *error) { (void)user; return vf_mesh_number(VF_MESH_MATERIALS, mesh, out, error); }
static bool vf_mesh_has_material(void *user, TfpEngineMesh mesh, const char *name, bool *present, TfpEngineError *error) {
  (void)user; vf_evis("m9 %s %s", vf_num(vf_address(mesh)), name);
  int g = vf_gate(VF_MESH_HAS_MATERIAL);
  if (g == 1 || g == 2) return vf_engine_fail(error, 9, "");
  const VfRig *row = vf_rig_by_address(vf_address(mesh));
  *present = g != 3 && row && row->material && !strcmp(name, "m_armor_standard");
  return true;
}
/* The same functions under other addresses: a binding that changed without changing what it does. */
static bool vf_main_world_2(void *u, TfpEngineWorld *w, TfpEngineError *e) { return vf_main_world(u, w, e); }
static bool vf_units_2(void *u, TfpEngineWorld w, const TfpEngineUnit **l, size_t *c, TfpEngineError *e) { return vf_units(u, w, l, c, e); }
static bool vf_set_mesh_visibility_2(void *u, TfpEngineUnit n, int i, bool v, const char *p, TfpEngineError *e) { return vf_set_mesh_visibility(u, n, i, v, p, e); }
static bool vf_has_mesh_2(void *u, TfpEngineUnit n, const char *name, bool *p, TfpEngineError *e) { return vf_has_mesh(u, n, name, p, e); }
static bool vf_mesh_2(void *u, TfpEngineUnit n, const char *name, TfpEngineMesh *m, TfpEngineError *e) { return vf_mesh(u, n, name, m, e); }
static bool vf_mesh_name_2(void *u, TfpEngineMesh m, double *o, TfpEngineError *e) { return vf_mesh_name(u, m, o, e); }
static bool vf_mesh_node_2(void *u, TfpEngineMesh m, double *o, TfpEngineError *e) { return vf_mesh_node(u, m, o, e); }
static bool vf_mesh_materials_2(void *u, TfpEngineMesh m, double *o, TfpEngineError *e) { return vf_mesh_materials(u, m, o, e); }
static bool vf_mesh_has_material_2(void *u, TfpEngineMesh m, const char *n, bool *p, TfpEngineError *e) { return vf_mesh_has_material(u, m, n, p, e); }

static bool vf_alignment_executable(void *user, double *exe, TfpVisText *why) {
  (void)user; vf_evis("exe");
  if (V(exe_fail) != 0) return vf_fail(why, "Executable unavailable");
  *exe = V(exe);
  return true;
}
static bool vf_player_snapshot(void *user, TfpVisSnapshot *out, TfpVisText *why) { (void)user; return vf_snapshot_answer("psnap", VF_player_fail, false, out, why); }
static bool vf_pin_player_snapshot(void *user, TfpVisSnapshot *out, TfpVisText *why) { (void)user; return vf_snapshot_answer("pin", VF_pin_fail, false, out, why); }
static int vf_backpack(void *user, const TfpVisSnapshot *s, TfpHeadBackpack *out, TfpVisText *why) {
  (void)user; (void)s; vf_evis("pack");
  if (V(pack_mode) == 2) { vf_fail(why, "Backpack inventory changed"); return -1; }
  if (V(pack_mode) == 0) return 0;
  out->ref = V(pack_ref);
  vf_copy_identity(&out->identity, &vf.strs[VFS_pack_id]);
  return 1;
}
static bool vf_action_state(void *user, const TfpVisSnapshot *s, TfpHeadActionState *out) {
  (void)user; vf_evis("act");
  int mode = (int)V(act_mode);
  if (mode == 0 || mode == 4) return false;
  out->owner = s->player_identity;
  if (mode == 2) { out->owner.present = true; out->owner.len = 12; memcpy(out->owner.bytes, "someone else", 12); }
  out->body = mode == 3 ? s->body + 1 : s->body;
  out->native = NULL;
  return true;
}
static int vf_on_foot(void *user, const TfpVisSnapshot *s, const TfpHeadActionState *state) {
  (void)user; (void)s; (void)state; vf_evis("foot");
  return V(on_foot) == 2 ? -1 : V(on_foot) == 1;
}
static int vf_foreign_camera(void *user) {
  (void)user; vf_evis("fc");
  return V(foreign) == 2 ? -1 : V(foreign) == 0 ? 0 : 1;
}
static int vf_fallback_ready(void *user, const TfpVisSnapshot *s) { (void)user; (void)s; vf_evis("fb"); return V(fallback) == 2 ? -1 : V(fallback) == 1; }
static bool vf_pinned_ready(void *user, const TfpVisSnapshot *s) { (void)user; (void)s; vf_evis("pr"); return V(pinned) == 1; }
static bool vf_full_body(void *user) { (void)user; vf_evis("fbd"); return V(full_body) == 1; }
static bool vf_hide_torso(void *user) { (void)user; vf_evis("ht"); return V(hide_torso) == 1; }
static bool vf_show_torso(void *user) { (void)user; vf_evis("st"); return V(show_torso) == 1; }
static bool vf_seated_head(void *user) { (void)user; vf_evis("sh"); return V(seated_head) == 1; }
static bool vf_controller_result(void *user) { (void)user; vf_evis("cr"); return V(mode) != 0; }
static bool vf_controller_is_enabled(void *user) { (void)user; vf_evis("ce"); return V(enabled) != 0; }
static double vf_userdata_address(void *user, const void *handle) { (void)user; vf_en(8, 1, vf_address(handle), 0, 0); return vf_address(handle) + V(addr_bias); }
static double vf_clock(void *user) { (void)user; vf_evis("clk"); return V(tick) / 1000; }
static void vf_log(void *user, const char *text) { (void)user; vf_evis("log:%s", text); }
static bool vf_player_snapshot_2(void *u, TfpVisSnapshot *o, TfpVisText *w) { return vf_player_snapshot(u, o, w); }
static bool vf_pin_player_snapshot_2(void *u, TfpVisSnapshot *o, TfpVisText *w) { return vf_pin_player_snapshot(u, o, w); }
static int vf_backpack_2(void *u, const TfpVisSnapshot *s, TfpHeadBackpack *o, TfpVisText *w) { return vf_backpack(u, s, o, w); }
static int vf_foreign_camera_2(void *u) { return vf_foreign_camera(u); }
#endif

/* ---- runs and script lines ---- */
typedef struct VfOptions { char module[16]; bool nolog, models, sr; int bridge; } VfOptions;
static void vf_start_run(const char *header, VfOptions *options) {
  for (size_t i = 0; i < VF_PAGES; i++) { free(vf_pages[i].bytes); vf_pages[i].bytes = NULL; }
  free(vf.units); free(vf.keep);
  memset(&vf, 0, sizeof vf);
  for (int code = 0; code < VF_BIND_COUNT; code++) { vf.binds[code] = 1; if (code) vf_rebind(code, 1); }
  *options = (VfOptions){.models = true, .sr = true};
  vf_full = false;
  char copy[256];
  snprintf(copy, sizeof copy, "%s", header);
  for (char *token = strtok(copy, " "); token; token = strtok(NULL, " ")) {
    char *equals = strchr(token, '=');
    if (!equals) continue;
    *equals++ = 0;
    if (!strcmp(token, "module")) snprintf(options->module, sizeof options->module, "%s", equals);
    else if (!strcmp(token, "full")) vf_full = equals[0] == '1';
    else if (!strcmp(token, "nolog")) options->nolog = equals[0] == '1';
    else if (!strcmp(token, "models")) options->models = equals[0] != '0';
    else if (!strcmp(token, "sr")) options->sr = equals[0] != '0';
    else if (!strcmp(token, "bridge")) options->bridge = atoi(equals);
  }
}
/* Splits a script line (a writable copy), applies what changes at once, files the `@N` changes and
 * returns the call (the text after `!`). */
static char *vf_begin(char *line, char **tokens, size_t room) {
  size_t count = 0, group = 0;
  char *call = NULL;
  vf.trigger_count = 0;
  memset(vf.calls, 0, sizeof vf.calls);
  VfTrigger now = {0, tokens, 0}, *target = &now;
  for (char *token = strtok(line, " "); token; token = strtok(NULL, " ")) {
    if (token[0] == '@') {
      if (vf.trigger_count == 16) { fprintf(stderr, "visibility fake: too many triggers\n"); exit(2); }
      target = &vf.triggers[vf.trigger_count++];
      *target = (VfTrigger){atol(token + 1), tokens + count, 0};
      group = count;
    } else if (token[0] == '!') call = token + 1;
    else {
      if (count == room) { fprintf(stderr, "visibility fake: script line too long\n"); exit(2); }
      tokens[count++] = token;
      target->count = count - (target == &now ? 0 : group);
    }
  }
  vf_apply(now.tokens, now.count);
  vf_ev.n = 0; vf_ev.h1 = 1; vf_ev.h2 = 2;
  vf_clear(&vf_ev.vis);
  return call;
}
static void vf_events(VfBuf *out) {
  vf_printf(out, " ev=%ld,%llu,%llu tr=", vf_ev.n, (unsigned long long)vf_ev.h1, (unsigned long long)vf_ev.h2);
  vf_part(out, vf_ev.vis.data ? vf_ev.vis.data : "", vf_ev.vis.len);
}
/* Compares, prints the first differences, and says how to see every call of the operation on both sides. */
static int vf_compare(const char *test, size_t index, const char *run, const char *script, const char *expect, const VfBuf *got) {
  if (!strcmp(expect, got->data)) return 0;
  size_t at = 0;
  while (expect[at] && expect[at] == got->data[at]) at++;
  size_t from = at > 120 ? at - 120 : 0;
  fprintf(stderr, "%s: vector %zu (run %s) differs at byte %zu\n  script: %.200s%s\n  retail: ...%.400s\n  C:      ...%.400s\n"
          "  every call: VIS_DUMP=%zu on the C side; tools/retail_ref/run_visibility.lua --dump <script line> on the Lua side\n",
          test, index, run, at, script, strlen(script) > 200 ? "..." : "", expect + from, got->data + from, index);
  return 1;
}
#endif
