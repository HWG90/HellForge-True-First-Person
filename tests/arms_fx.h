/* Shared by arm_pieces_test.c and grip_hand_test.c: the C side of the scripted engine of
 * tools/retail_ref/arms_common.lua.
 *
 * Stateful engine calls (Unit, World, Mesh, Application, the backend, the clock) have no world behind
 * them here: each must be the next call the retail Lua made, with the same argument bits, and gets the
 * answer the Lua got. The value maths (Vector3, Quaternion, Matrix4x4) is the same arithmetic as the
 * runner's, on floats, folded call by call into the same checksum. */
#ifndef ARMS_FX_H
#define ARMS_FX_H
#include "arm_engine_iface.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- one vector line, split ---- */
enum { FX_INSTANCE, FX_CALL, FX_SNAPSHOT, FX_ACTIVE, FX_HANDLES, FX_MFAIL, FX_TRACE, FX_MATHS, FX_ANSWER, FX_FIELDS };
typedef struct FxLine { const char *at[FX_FIELDS]; size_t len[FX_FIELDS]; } FxLine;
static bool fx_split(const char *line, FxLine *out) {
  int field = 0;
  out->at[0] = line;
  for (const char *p = line;; p++) {
    if (*p == '|' && field < FX_FIELDS - 1) { out->len[field] = (size_t)(p - out->at[field]); out->at[++field] = p + 1; }
    else if (!*p) { out->len[field] = (size_t)(p - out->at[field]); break; }
  }
  return field == FX_FIELDS - 1;
}
static bool fx_is(const FxLine *l, int field, const char *text) { return strlen(text) == l->len[field] && !memcmp(l->at[field], text, l->len[field]); }

static struct {
  const char *cursor, *end;      /* the recorded calls not yet made */
  bool broken;
  char why[1400];
  uint32_t sum;
  unsigned count, fail_at;
  char fail_text[TFP_ARM_ERROR_TEXT];
  unsigned calls;
} fx;

static void fx_break(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void fx_break(const char *format, ...) {
  va_list args;
  if (fx.broken) return;
  fx.broken = true;
  va_start(args, format);
  vsnprintf(fx.why, sizeof fx.why, format, args);
  va_end(args);
}
static void fx_num(double x, char *out, size_t size) {
  if (x != x) snprintf(out, size, "nan");
  else if (x == INFINITY) snprintf(out, size, "inf");
  else if (x == -INFINITY) snprintf(out, size, "-inf");
  else snprintf(out, size, "%.17g", x);
}
static void fx_opt(bool has, double x, char *out, size_t size) { if (has) fx_num(x, out, size); else snprintf(out, size, "~"); }
/* Any NaN is one canonical NaN in the trace and the checksum (see arms_common.lua). */
static uint32_t fx_bits(float x) { uint32_t b; if (x != x) return 0x7fc00000u; memcpy(&b, &x, 4); return b; }
static float fx_float(const char *hex) {
  char text[9]; uint32_t b; float x;
  memcpy(text, hex, 8); text[8] = 0;
  b = (uint32_t)strtoul(text, NULL, 16);
  memcpy(&x, &b, 4);
  return x;
}
static double fx_double(const char *hex) {
  char text[17]; uint64_t b; double x;
  memcpy(text, hex, 16); text[16] = 0;
  b = strtoull(text, NULL, 16);
  memcpy(&x, &b, 8);
  return x;
}
__attribute__((unused)) static void fx_double_hex(double x, char *out) { uint64_t b; memcpy(&b, &x, 8); if (x != x) b = 0x7ff8000000000000ull; snprintf(out, 17, "%016llx", (unsigned long long)b); }
static int fx_id(const void *pointer) { return (int)(uintptr_t)pointer; }

/* The next recorded call must be `call`. true: *answer is what the Lua got (up to *len). false: it raised. */
static bool fx_next(const char *call, const char **answer, size_t *len, TfpArmError *error) {
  const char *token = fx.cursor, *stop, *split;
  size_t call_len = strlen(call);
  fx.calls++;
  if (fx.broken) { snprintf(error->text, sizeof error->text, "test: trace already broken"); return false; }
  if (!token || token >= fx.end) {
    fx_break("extra call %s after the last recorded one", call);
    snprintf(error->text, sizeof error->text, "test: unexpected call");
    return false;
  }
  stop = memchr(token, ';', (size_t)(fx.end - token));
  if (!stop) stop = fx.end;
  for (split = token; split < stop && *split != '>' && *split != '!'; split++) {}
  if ((size_t)(split - token) != call_len || memcmp(token, call, call_len) || split == stop) {
    fx_break("call %u differs\n    C      %s\n    retail %.*s", fx.calls, call, (int)(stop - token > 300 ? 300 : stop - token), token);
    snprintf(error->text, sizeof error->text, "test: unexpected call");
    return false;
  }
  fx.cursor = stop < fx.end ? stop + 1 : fx.end;
  if (*split == '!') {
    size_t n = (size_t)(stop - split - 1);
    if (n >= sizeof error->text) n = sizeof error->text - 1;
    memcpy(error->text, split + 1, n); error->text[n] = 0;
    return false;
  }
  *answer = split + 1; *len = (size_t)(stop - split - 1);
  return true;
}
#define FX_CALL(...) \
  char call[400]; const char *answer = NULL; size_t len = 0; \
  (void)user; snprintf(call, sizeof call, __VA_ARGS__); \
  if (!fx_next(call, &answer, &len, error)) return false; \
  (void)answer; (void)len

static void fx_matrix_hex(const TfpArmMatrix4x4 *m, char *out) { for (int i = 0; i < 7; i++) snprintf(out + i * 8, 9, "%08x", fx_bits(m->m[i])); }

static bool fx_main_world(void *user, TfpArmWorld *world, TfpArmError *error) {
  FX_CALL("mw:");
  *world = (TfpArmWorld)(uintptr_t)strtoul(answer, NULL, 10);
  return true;
}
static bool fx_link_unit(void *user, TfpArmWorld world, TfpArmUnit child, double child_node, TfpArmUnit parent, double parent_node, TfpArmError *error) {
  char a[40], b[40];
  fx_num(child_node, a, sizeof a); fx_num(parent_node, b, sizeof b);
  FX_CALL("lk:%d,%d,%s,%d,%s", fx_id(world), fx_id(child), a, fx_id(parent), b);
  return true;
}
static bool fx_alive(void *user, TfpArmUnit unit, bool *alive, TfpArmError *error) {
  FX_CALL("al:%d", fx_id(unit));
  *alive = *answer == 'T';
  return true;
}
static bool fx_items(void *user, TfpArmUnit unit, double *count, TfpArmError *error) {
  FX_CALL("ns:%d", fx_id(unit));
  *count = fx_double(answer);
  return true;
}
static bool fx_world_pose(void *user, TfpArmUnit unit, double node, TfpArmMatrix4x4 *pose, TfpArmError *error) {
  char a[40];
  fx_num(node, a, sizeof a);
  FX_CALL("wp:%d,%s", fx_id(unit), a);
  memset(pose, 0, sizeof *pose);
  for (int i = 0; i < 7; i++) pose->m[i] = fx_float(answer + i * 8);
  return true;
}
static bool fx_world_position(void *user, TfpArmUnit unit, double node, TfpArmVector3 *out, TfpArmError *error) {
  char a[40];
  fx_num(node, a, sizeof a);
  FX_CALL("wo:%d,%s", fx_id(unit), a);
  out->x = fx_float(answer); out->y = fx_float(answer + 8); out->z = fx_float(answer + 16);
  return true;
}
static bool fx_world_rotation(void *user, TfpArmUnit unit, double node, TfpArmQuaternion *out, TfpArmError *error) {
  char a[40];
  fx_num(node, a, sizeof a);
  FX_CALL("wr:%d,%s", fx_id(unit), a);
  out->x = fx_float(answer); out->y = fx_float(answer + 8); out->z = fx_float(answer + 16); out->w = fx_float(answer + 24);
  return true;
}
static bool fx_set_local_pose(void *user, TfpArmUnit unit, double node, const TfpArmMatrix4x4 *pose, TfpArmError *error) {
  char a[40], hex[60];
  fx_num(node, a, sizeof a); fx_matrix_hex(pose, hex);
  FX_CALL("sp:%d,%s,%s", fx_id(unit), a, hex);
  return true;
}
static bool fx_num_meshes(void *user, TfpArmUnit unit, double *count, TfpArmError *error) {
  FX_CALL("nm:%d", fx_id(unit));
  *count = fx_double(answer);
  return true;
}
static bool fx_mesh(void *user, TfpArmUnit unit, double index, TfpArmMesh *mesh, TfpArmError *error) {
  char a[40];
  fx_num(index, a, sizeof a);
  FX_CALL("ms:%d,%s", fx_id(unit), a);
  *mesh = (TfpArmMesh)(uintptr_t)strtoul(answer, NULL, 10);
  return true;
}
static bool fx_set_mesh_visibility(void *user, TfpArmUnit unit, double index, bool visible, TfpArmError *error) {
  char a[40];
  fx_num(index, a, sizeof a);
  FX_CALL("sv:%d,%s,%s", fx_id(unit), a, visible ? "T" : "F");
  return true;
}
static bool fx_mesh_visibility(void *user, TfpArmMesh mesh, bool *visible, TfpArmError *error) {
  FX_CALL("mv:%d", fx_id(mesh));
  *visible = *answer == 'T';
  return true;
}
static bool fx_mesh_materials(void *user, TfpArmMesh mesh, double *count, TfpArmError *error) {
  FX_CALL("mm:%d", fx_id(mesh));
  *count = fx_double(answer);
  return true;
}
/* backend */
static bool fx_object(void *user, bool has_unit, double unit, bool has_owner, double owner, bool has_index, double index, double *object,
                      TfpArmError *error) {
  char a[40], b[40], c[40];
  fx_opt(has_unit, unit, a, sizeof a); fx_opt(has_owner, owner, b, sizeof b); fx_opt(has_index, index, c, sizeof c);
  FX_CALL("obj:%s,%s,%s", a, b, c);
  *object = fx_double(answer);
  return true;
}
static bool fx_read(void *user, double at, double size, uint8_t *out, TfpArmError *error) {
  char a[40], b[40];
  fx_num(at, a, sizeof a); fx_num(size, b, sizeof b);
  FX_CALL("rd:%s,%s", a, b);
  if ((double)len != size * 2) { fx_break("read of %s bytes answered with %zu", b, len / 2); return false; }
  for (size_t i = 0; i < len / 2; i++) {
    char byte[3] = {answer[i * 2], answer[i * 2 + 1], 0};
    out[i] = (uint8_t)strtoul(byte, NULL, 16);
  }
  return true;
}
static bool fx_ptr(void *user, double at, double *out, TfpArmError *error) {
  char a[40];
  fx_num(at, a, sizeof a);
  FX_CALL("ptr:%s", a);
  *out = fx_double(answer);
  return true;
}
static bool fx_u32(void *user, double at, double *out, TfpArmError *error) {
  char a[40];
  fx_num(at, a, sizeof a);
  FX_CALL("u32:%s", a);
  *out = fx_double(answer);
  return true;
}
static bool fx_clock(void *user, double *now, TfpArmError *error) {
  FX_CALL("clk:");
  *now = fx_double(answer);
  return true;
}

/* ---- value maths: arms_common.lua, line for line ---- */
static void fx_mix(uint32_t w) { fx.sum = (((fx.sum << 5) | (fx.sum >> 27)) ^ w) + 0x3243F6A9u; }
static void fx_mixf(float x) { fx_mix(fx_bits(x)); }
static void fx_mixd(double x) { uint64_t b; memcpy(&b, &x, 8); if (x != x) b = 0x7ff8000000000000ull; fx_mix((uint32_t)(b >> 32)); fx_mix((uint32_t)b); }
static void fx_mixv(TfpArmVector3 a) { fx_mixf(a.x); fx_mixf(a.y); fx_mixf(a.z); }
static void fx_mixq(TfpArmQuaternion a) { fx_mixf(a.x); fx_mixf(a.y); fx_mixf(a.z); fx_mixf(a.w); }
static void fx_mixm(const TfpArmMatrix4x4 *a) { for (int i = 0; i < 7; i++) fx_mixf(a->m[i]); }
static bool fx_enter(uint32_t id, TfpArmError *error) {
  fx.count++; fx_mix(id);
  if (fx.count == fx.fail_at) { snprintf(error->text, sizeof error->text, "%s", fx.fail_text); return false; }
  return true;
}
#define FX_ENTER(id) do { (void)user; if (!fx_enter(id, error)) return false; } while (0)
static TfpArmQuaternion fx_q(const TfpArmMatrix4x4 *m) { return (TfpArmQuaternion){m->m[0], m->m[1], m->m[2], m->m[3]}; }
static TfpArmVector3 fx_p(const TfpArmMatrix4x4 *m) { return (TfpArmVector3){m->m[4], m->m[5], m->m[6]}; }
static TfpArmMatrix4x4 fx_m(TfpArmQuaternion q, TfpArmVector3 p) {
  TfpArmMatrix4x4 m;
  memset(&m, 0, sizeof m);
  m.m[0] = q.x; m.m[1] = q.y; m.m[2] = q.z; m.m[3] = q.w; m.m[4] = p.x; m.m[5] = p.y; m.m[6] = p.z;
  return m;
}
static TfpArmQuaternion fx_qmul(TfpArmQuaternion a, TfpArmQuaternion b) {
  double ax = a.x, ay = a.y, az = a.z, aw = a.w, bx = b.x, by = b.y, bz = b.z, bw = b.w;
  TfpArmQuaternion r;
  r.x = (float)(aw * bx + ax * bw + ay * bz - az * by);
  r.y = (float)(aw * by - ax * bz + ay * bw + az * bx);
  r.z = (float)(aw * bz + ax * by - ay * bx + az * bw);
  r.w = (float)(aw * bw - ax * bx - ay * by - az * bz);
  return r;
}
static TfpArmVector3 fx_qrot(TfpArmQuaternion a, TfpArmVector3 p) {
  TfpArmQuaternion r = fx_qmul(fx_qmul(a, (TfpArmQuaternion){p.x, p.y, p.z, 0}), (TfpArmQuaternion){-a.x, -a.y, -a.z, a.w});
  return (TfpArmVector3){r.x, r.y, r.z};
}
static double fx_vlen(TfpArmVector3 a) {
  double x = a.x, y = a.y, z = a.z;
  return (float)sqrt(x * x + y * y + z * z);
}
static TfpArmVector3 fx_vnorm(TfpArmVector3 a) {
  double n = 1 / fx_vlen(a);
  return (TfpArmVector3){(float)(a.x * n), (float)(a.y * n), (float)(a.z * n)};
}
static TfpArmMatrix4x4 fx_mmul(const TfpArmMatrix4x4 *a, const TfpArmMatrix4x4 *b) {
  TfpArmVector3 p = fx_qrot(fx_q(b), fx_p(a)), t = fx_p(b);
  return fx_m(fx_qmul(fx_q(b), fx_q(a)), (TfpArmVector3){(float)((double)p.x + t.x), (float)((double)p.y + t.y), (float)((double)p.z + t.z)});
}
static TfpArmMatrix4x4 fx_minv(const TfpArmMatrix4x4 *a) {
  TfpArmQuaternion q = fx_q(a), i = {-q.x, -q.y, -q.z, q.w};
  TfpArmVector3 p = fx_p(a);
  return fx_m(i, fx_qrot(i, (TfpArmVector3){-p.x, -p.y, -p.z}));
}

static bool fx_v_call(void *user, double x, double y, double z, TfpArmVector3 *out, TfpArmError *error) {
  FX_ENTER(1); fx_mixd(x); fx_mixd(y); fx_mixd(z);
  *out = (TfpArmVector3){(float)x, (float)y, (float)z}; fx_mixv(*out);
  return true;
}
static bool fx_v_add(void *user, TfpArmVector3 a, TfpArmVector3 b, TfpArmVector3 *out, TfpArmError *error) {
  FX_ENTER(2); fx_mixv(a); fx_mixv(b);
  *out = (TfpArmVector3){(float)((double)a.x + b.x), (float)((double)a.y + b.y), (float)((double)a.z + b.z)}; fx_mixv(*out);
  return true;
}
static bool fx_v_sub(void *user, TfpArmVector3 a, TfpArmVector3 b, TfpArmVector3 *out, TfpArmError *error) {
  FX_ENTER(3); fx_mixv(a); fx_mixv(b);
  *out = (TfpArmVector3){(float)((double)a.x - b.x), (float)((double)a.y - b.y), (float)((double)a.z - b.z)}; fx_mixv(*out);
  return true;
}
static bool fx_v_mul(void *user, TfpArmVector3 a, double n, TfpArmVector3 *out, TfpArmError *error) {
  FX_ENTER(4); fx_mixv(a); fx_mixd(n);
  *out = (TfpArmVector3){(float)(a.x * n), (float)(a.y * n), (float)(a.z * n)}; fx_mixv(*out);
  return true;
}
static bool fx_v_unm(void *user, TfpArmVector3 a, TfpArmVector3 *out, TfpArmError *error) {
  FX_ENTER(5); fx_mixv(a);
  *out = (TfpArmVector3){-a.x, -a.y, -a.z}; fx_mixv(*out);
  return true;
}
static bool fx_v_length(void *user, TfpArmVector3 a, double *out, TfpArmError *error) {
  FX_ENTER(6); fx_mixv(a);
  *out = fx_vlen(a); fx_mixd(*out);
  return true;
}
static bool fx_v_dot(void *user, TfpArmVector3 a, TfpArmVector3 b, double *out, TfpArmError *error) {
  double ax = a.x, ay = a.y, az = a.z;
  FX_ENTER(7); fx_mixv(a); fx_mixv(b);
  *out = (float)(ax * b.x + ay * b.y + az * b.z); fx_mixd(*out);
  return true;
}
static bool fx_v_cross(void *user, TfpArmVector3 a, TfpArmVector3 b, TfpArmVector3 *out, TfpArmError *error) {
  double ax = a.x, ay = a.y, az = a.z;
  FX_ENTER(8); fx_mixv(a); fx_mixv(b);
  *out = (TfpArmVector3){(float)(ay * b.z - az * b.y), (float)(az * b.x - ax * b.z), (float)(ax * b.y - ay * b.x)}; fx_mixv(*out);
  return true;
}
static bool fx_v_normalize(void *user, TfpArmVector3 a, TfpArmVector3 *out, TfpArmError *error) {
  FX_ENTER(9); fx_mixv(a);
  *out = fx_vnorm(a); fx_mixv(*out);
  return true;
}
static bool fx_v_lerp(void *user, TfpArmVector3 a, TfpArmVector3 b, double t, TfpArmVector3 *out, TfpArmError *error) {
  double ax = a.x, ay = a.y, az = a.z;
  FX_ENTER(10); fx_mixv(a); fx_mixv(b); fx_mixd(t);
  *out = (TfpArmVector3){(float)(ax + (b.x - ax) * t), (float)(ay + (b.y - ay) * t), (float)(az + (b.z - az) * t)}; fx_mixv(*out);
  return true;
}
static bool fx_v_x(void *user, TfpArmVector3 a, double *out, TfpArmError *error) { FX_ENTER(11); fx_mixv(a); *out = a.x; return true; }
static bool fx_v_y(void *user, TfpArmVector3 a, double *out, TfpArmError *error) { FX_ENTER(12); fx_mixv(a); *out = a.y; return true; }
static bool fx_v_z(void *user, TfpArmVector3 a, double *out, TfpArmError *error) { FX_ENTER(13); fx_mixv(a); *out = a.z; return true; }
static bool fx_q_identity(void *user, TfpArmQuaternion *out, TfpArmError *error) {
  FX_ENTER(14);
  *out = (TfpArmQuaternion){0, 0, 0, 1};
  return true;
}
static bool fx_q_axis_angle(void *user, TfpArmVector3 axis, double angle, TfpArmQuaternion *out, TfpArmError *error) {
  TfpArmVector3 a; double s;
  FX_ENTER(15); fx_mixv(axis); fx_mixd(angle);
  a = fx_vnorm(axis); s = sin(angle / 2);
  *out = (TfpArmQuaternion){(float)(a.x * s), (float)(a.y * s), (float)(a.z * s), (float)cos(angle / 2)}; fx_mixq(*out);
  return true;
}
static bool fx_q_multiply(void *user, TfpArmQuaternion a, TfpArmQuaternion b, TfpArmQuaternion *out, TfpArmError *error) {
  FX_ENTER(16); fx_mixq(a); fx_mixq(b);
  *out = fx_qmul(a, b); fx_mixq(*out);
  return true;
}
static bool fx_q_rotate(void *user, TfpArmQuaternion a, TfpArmVector3 p, TfpArmVector3 *out, TfpArmError *error) {
  FX_ENTER(17); fx_mixq(a); fx_mixv(p);
  *out = fx_qrot(a, p); fx_mixv(*out);
  return true;
}
static bool fx_q_lerp(void *user, TfpArmQuaternion a, TfpArmQuaternion b, double t, TfpArmQuaternion *out, TfpArmError *error) {
  FX_ENTER(18); fx_mixq(a); fx_mixq(b); fx_mixd(t);
  if (t >= 1) *out = b;
  else if (t <= 0) *out = a;
  else {
    double ax = a.x, ay = a.y, az = a.z, aw = a.w;
    double x = ax + (b.x - ax) * t, y = ay + (b.y - ay) * t, z = az + (b.z - az) * t, w = aw + (b.w - aw) * t;
    double n = sqrt(x * x + y * y + z * z + w * w);
    *out = (TfpArmQuaternion){(float)(x / n), (float)(y / n), (float)(z / n), (float)(w / n)};
  }
  fx_mixq(*out);
  return true;
}
static bool fx_q_inverse(void *user, TfpArmQuaternion a, TfpArmQuaternion *out, TfpArmError *error) {
  FX_ENTER(19); fx_mixq(a);
  *out = (TfpArmQuaternion){-a.x, -a.y, -a.z, a.w}; fx_mixq(*out);
  return true;
}
static bool fx_q_to_elements(void *user, TfpArmQuaternion a, double out[4], TfpArmError *error) {
  FX_ENTER(20); fx_mixq(a);
  out[0] = a.x; out[1] = a.y; out[2] = a.z; out[3] = a.w;
  return true;
}
static bool fx_q_from_elements(void *user, double x, double y, double z, double w, TfpArmQuaternion *out, TfpArmError *error) {
  FX_ENTER(21); fx_mixd(x); fx_mixd(y); fx_mixd(z); fx_mixd(w);
  *out = (TfpArmQuaternion){(float)x, (float)y, (float)z, (float)w}; fx_mixq(*out);
  return true;
}
static bool fx_m_identity(void *user, TfpArmMatrix4x4 *out, TfpArmError *error) {
  FX_ENTER(22);
  *out = fx_m((TfpArmQuaternion){0, 0, 0, 1}, (TfpArmVector3){0, 0, 0});
  return true;
}
static bool fx_m_from(void *user, TfpArmQuaternion a, TfpArmVector3 p, TfpArmMatrix4x4 *out, TfpArmError *error) {
  FX_ENTER(23); fx_mixq(a); fx_mixv(p);
  *out = fx_m(a, p);
  return true;
}
static bool fx_m_multiply(void *user, const TfpArmMatrix4x4 *a, const TfpArmMatrix4x4 *b, TfpArmMatrix4x4 *out, TfpArmError *error) {
  FX_ENTER(24); fx_mixm(a); fx_mixm(b);
  *out = fx_mmul(a, b); fx_mixm(out);
  return true;
}
static bool fx_m_inverse(void *user, const TfpArmMatrix4x4 *a, TfpArmMatrix4x4 *out, TfpArmError *error) {
  FX_ENTER(25); fx_mixm(a);
  *out = fx_minv(a); fx_mixm(out);
  return true;
}
static bool fx_m_translation(void *user, const TfpArmMatrix4x4 *a, TfpArmVector3 *out, TfpArmError *error) {
  FX_ENTER(26); fx_mixm(a);
  *out = fx_p(a);
  return true;
}
static bool fx_m_rotation(void *user, const TfpArmMatrix4x4 *a, TfpArmQuaternion *out, TfpArmError *error) {
  FX_ENTER(27); fx_mixm(a);
  *out = fx_q(a);
  return true;
}

static const TfpArmBackend fx_backend = {NULL, fx_object, fx_read, fx_ptr, fx_u32, fx_clock};
static const TfpArmEngine fx_whole_engine = {
  .Application_main_world = fx_main_world, .World_link_unit = fx_link_unit,
  .Unit_alive = fx_alive, .Unit_num_scene_graph_items = fx_items, .Unit_world_pose = fx_world_pose, .Unit_world_position = fx_world_position,
  .Unit_world_rotation = fx_world_rotation, .Unit_set_local_pose = fx_set_local_pose, .Unit_num_meshes = fx_num_meshes, .Unit_mesh = fx_mesh,
  .Unit_set_mesh_visibility = fx_set_mesh_visibility, .Mesh_visibility = fx_mesh_visibility, .Mesh_num_materials = fx_mesh_materials,
  .Vector3_call = fx_v_call, .Vector3_length = fx_v_length, .Vector3_dot = fx_v_dot, .Vector3_cross = fx_v_cross,
  .Vector3_normalize = fx_v_normalize, .Vector3_lerp = fx_v_lerp, .Vector3_x = fx_v_x, .Vector3_y = fx_v_y, .Vector3_z = fx_v_z,
  .Vector3_op_add = fx_v_add, .Vector3_op_sub = fx_v_sub, .Vector3_op_mul = fx_v_mul, .Vector3_op_unm = fx_v_unm,
  .Quaternion_identity = fx_q_identity, .Quaternion_axis_angle = fx_q_axis_angle, .Quaternion_multiply = fx_q_multiply,
  .Quaternion_rotate = fx_q_rotate, .Quaternion_lerp = fx_q_lerp, .Quaternion_inverse = fx_q_inverse,
  .Quaternion_to_elements = fx_q_to_elements, .Quaternion_from_elements = fx_q_from_elements,
  .Matrix4x4_identity = fx_m_identity, .Matrix4x4_from_quaternion_position = fx_m_from, .Matrix4x4_multiply = fx_m_multiply,
  .Matrix4x4_inverse = fx_m_inverse, .Matrix4x4_translation = fx_m_translation, .Matrix4x4_rotation = fx_m_rotation,
};
/* The engine of a run: the whole one without the functions (or whole tables) the run names. */
#define FX_ENTRY(name) {#name, offsetof(TfpArmEngine, name)}
static const struct { const char *name; size_t offset; } fx_entries[] = {
  FX_ENTRY(Application_main_world), FX_ENTRY(World_link_unit), FX_ENTRY(Unit_alive), FX_ENTRY(Unit_num_scene_graph_items),
  FX_ENTRY(Unit_world_pose), FX_ENTRY(Unit_world_position), FX_ENTRY(Unit_world_rotation), FX_ENTRY(Unit_set_local_pose),
  FX_ENTRY(Unit_num_meshes), FX_ENTRY(Unit_mesh), FX_ENTRY(Unit_set_mesh_visibility), FX_ENTRY(Mesh_visibility), FX_ENTRY(Mesh_num_materials),
  FX_ENTRY(Vector3_call), FX_ENTRY(Vector3_length), FX_ENTRY(Vector3_dot), FX_ENTRY(Vector3_cross), FX_ENTRY(Vector3_normalize),
  FX_ENTRY(Vector3_lerp), FX_ENTRY(Vector3_x), FX_ENTRY(Vector3_y), FX_ENTRY(Vector3_z),
  FX_ENTRY(Quaternion_identity), FX_ENTRY(Quaternion_axis_angle), FX_ENTRY(Quaternion_multiply), FX_ENTRY(Quaternion_rotate),
  FX_ENTRY(Quaternion_lerp), FX_ENTRY(Quaternion_inverse), FX_ENTRY(Quaternion_to_elements), FX_ENTRY(Quaternion_from_elements),
  FX_ENTRY(Matrix4x4_identity), FX_ENTRY(Matrix4x4_from_quaternion_position), FX_ENTRY(Matrix4x4_multiply), FX_ENTRY(Matrix4x4_inverse),
  FX_ENTRY(Matrix4x4_translation), FX_ENTRY(Matrix4x4_rotation),
};
static bool fx_engine(const char *missing, size_t len, TfpArmEngine *engine) {
  const char *end = missing + len;
  *engine = fx_whole_engine;
  if (len == 1 && *missing == '-') return true;
  while (missing < end) {
    size_t n = 0, removed = 0;
    while (missing + n < end && missing[n] != ' ') n++;
    for (size_t i = 0; i < sizeof fx_entries / sizeof *fx_entries; i++) {
      const char *name = fx_entries[i].name;
      bool whole = strlen(name) == n && !memcmp(name, missing, n);
      bool table = !memchr(missing, '_', n) && !strncmp(name, missing, n) && name[n] == '_';
      if (whole || table) { memset((char *)engine + fx_entries[i].offset, 0, sizeof(void (*)(void))); removed++; }
    }
    if (!removed) return false;
    missing += n;
    while (missing < end && *missing == ' ') missing++;
  }
  return true;
}

/* ---- the inputs of an operation ---- */
typedef struct FxInputs {
  int instance;
  bool has_snapshot, active, has_handles;
  TfpArmSnapshot snapshot;
  TfpArmHandles handles;
  TfpArmHandle *rows;            /* exactly as many as the table has: a read past the end is caught by the sanitizer */
} FxInputs;
static const char *fx_value(const char *p, const char *end, bool *has, double *value, int *flag) {
  const char *stop = p;
  char text[64];
  while (stop < end && *stop != ',' && *stop != '=') stop++;
  size_t n = (size_t)(stop - p);
  if (n >= sizeof text) n = sizeof text - 1;
  memcpy(text, p, n); text[n] = 0;
  if (flag) *flag = !strcmp(text, "T") ? 1 : !strcmp(text, "F") ? 0 : -1;
  if (has) {
    *has = strcmp(text, "~") != 0;
    *value = !strcmp(text, "nan") ? NAN : !strcmp(text, "inf") ? INFINITY : !strcmp(text, "-inf") ? -INFINITY : *has ? strtod(text, NULL) : 0;
  }
  return stop < end ? stop + 1 : end;
}
static bool fx_inputs(const FxLine *l, FxInputs *in) {
  const char *p, *end;
  free(in->rows);
  memset(in, 0, sizeof *in);
  in->instance = atoi(l->at[FX_INSTANCE]);
  in->active = fx_is(l, FX_ACTIVE, "T");
  in->has_snapshot = !fx_is(l, FX_SNAPSHOT, "~");
  if (in->has_snapshot) {
    TfpArmSnapshot *s = &in->snapshot;
    int flag;
    p = l->at[FX_SNAPSHOT]; end = p + l->len[FX_SNAPSHOT];
    p = fx_value(p, end, &s->has_body, &s->body, NULL);
    p = fx_value(p, end, &s->has_viewmodel, &s->viewmodel, NULL);
    p = fx_value(p, end, &s->has_avatar_owner, &s->avatar_owner, NULL);
    p = fx_value(p, end, &s->has_avatar_index, &s->avatar_index, NULL);
    p = fx_value(p, end, NULL, NULL, &flag); s->aiming = flag == 1;
    p = fx_value(p, end, NULL, NULL, &flag); s->shoulder_aim = flag == 1;
    p = fx_value(p, end, &s->has_weapon, &s->weapon, NULL);
    p = fx_value(p, end, &s->has_request_weapon, &s->request_weapon, NULL);
    p = fx_value(p, end, NULL, NULL, &flag); s->weapon_transition = flag == 1;
  }
  in->has_handles = !fx_is(l, FX_HANDLES, "~");
  if (in->has_handles) {
    p = l->at[FX_HANDLES]; end = p + l->len[FX_HANDLES];
    in->handles.identity = (uintptr_t)strtoul(p, NULL, 10);
    p = memchr(p, ':', (size_t)(end - p));
    if (!p) return false;
    p++;
    size_t rows = 0;
    for (const char *c = p; c < end; c++) rows += *c == '=';
    in->rows = malloc(sizeof *in->rows * (rows ? rows : 1));
    if (!rows) { free(in->rows); in->rows = NULL; }
    in->handles.rows = in->rows;
    while (p < end) {
      bool has; double ref, unit;
      p = fx_value(p, end, &has, &ref, NULL);
      p = fx_value(p, end, &has, &unit, NULL);
      in->rows[in->handles.count++] = (TfpArmHandle){ref, (TfpArmUnit)(uintptr_t)unit};
    }
  }
  /* what this operation's engine will answer */
  memset(&fx, 0, sizeof fx);
  fx.cursor = l->at[FX_TRACE]; fx.end = fx.cursor + l->len[FX_TRACE];
  if (!fx_is(l, FX_MFAIL, "0")) {
    const char *colon = memchr(l->at[FX_MFAIL], ':', l->len[FX_MFAIL]);
    size_t n;
    if (!colon) return false;
    fx.fail_at = (unsigned)strtoul(l->at[FX_MFAIL], NULL, 10);
    n = (size_t)(l->at[FX_MFAIL] + l->len[FX_MFAIL] - colon - 1);
    if (n >= sizeof fx.fail_text) n = sizeof fx.fail_text - 1;
    memcpy(fx.fail_text, colon + 1, n); fx.fail_text[n] = 0;
  }
  return true;
}
/* After the call: every recorded call was made, the maths agrees, and the answer is retail's. */
static bool fx_verdict(const FxLine *l, const char *answer, char *why, size_t size) {
  char maths[40];
  if (fx.broken) { snprintf(why, size, "%s", fx.why); return false; }
  if (fx.cursor < fx.end) {
    snprintf(why, size, "the C stopped after %u calls; retail went on with %.*s", fx.calls, (int)(fx.end - fx.cursor > 200 ? 200 : fx.end - fx.cursor),
             fx.cursor);
    return false;
  }
  snprintf(maths, sizeof maths, "%u,%08x", fx.count, fx.sum);
  if (!fx_is(l, FX_MATHS, maths)) {
    snprintf(why, size, "value maths differs: C %s, retail %.*s", maths, (int)l->len[FX_MATHS], l->at[FX_MATHS]);
    return false;
  }
  if (!fx_is(l, FX_ANSWER, answer)) {
    snprintf(why, size, "answer differs\n    C      %s\n    retail %.*s", answer, (int)l->len[FX_ANSWER], l->at[FX_ANSWER]);
    return false;
  }
  return true;
}
__attribute__((unused)) static const char *fx_flag(bool x) { return x ? "T" : "F"; }
#endif
