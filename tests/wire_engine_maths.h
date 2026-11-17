/* The arm modules' vector tests (tests/arm_pieces_test.c, tests/grip_hand_test.c) with the value
 * maths of src/wire_engine.c in the place of the test's own copy: the calls and their arguments and
 * results are folded into the checksum exactly as tests/arms_fx.h folds them, but every result is
 * computed by tfp_wire_engine_maths. The retail reference run's checksums must still come out. */
#ifndef TFP_WIRE_ENGINE_MATHS_H
#define TFP_WIRE_ENGINE_MATHS_H
#include "wire_engine.h"
#include "arms_fx.h"

static TfpArmEngine wx;
#define WX(id) do { (void)user; if (!fx_enter(id, error)) return false; } while (0)
static bool wx_v_call(void *user, double x, double y, double z, TfpArmVector3 *out, TfpArmError *error) {
  WX(1); fx_mixd(x); fx_mixd(y); fx_mixd(z); wx.Vector3_call(0, x, y, z, out, error); fx_mixv(*out); return true; }
static bool wx_v_add(void *user, TfpArmVector3 a, TfpArmVector3 b, TfpArmVector3 *out, TfpArmError *error) {
  WX(2); fx_mixv(a); fx_mixv(b); wx.Vector3_op_add(0, a, b, out, error); fx_mixv(*out); return true; }
static bool wx_v_sub(void *user, TfpArmVector3 a, TfpArmVector3 b, TfpArmVector3 *out, TfpArmError *error) {
  WX(3); fx_mixv(a); fx_mixv(b); wx.Vector3_op_sub(0, a, b, out, error); fx_mixv(*out); return true; }
static bool wx_v_mul(void *user, TfpArmVector3 a, double n, TfpArmVector3 *out, TfpArmError *error) {
  WX(4); fx_mixv(a); fx_mixd(n); wx.Vector3_op_mul(0, a, n, out, error); fx_mixv(*out); return true; }
static bool wx_v_unm(void *user, TfpArmVector3 a, TfpArmVector3 *out, TfpArmError *error) {
  WX(5); fx_mixv(a); wx.Vector3_op_unm(0, a, out, error); fx_mixv(*out); return true; }
static bool wx_v_length(void *user, TfpArmVector3 a, double *out, TfpArmError *error) {
  WX(6); fx_mixv(a); wx.Vector3_length(0, a, out, error); fx_mixd(*out); return true; }
static bool wx_v_dot(void *user, TfpArmVector3 a, TfpArmVector3 b, double *out, TfpArmError *error) {
  WX(7); fx_mixv(a); fx_mixv(b); wx.Vector3_dot(0, a, b, out, error); fx_mixd(*out); return true; }
static bool wx_v_cross(void *user, TfpArmVector3 a, TfpArmVector3 b, TfpArmVector3 *out, TfpArmError *error) {
  WX(8); fx_mixv(a); fx_mixv(b); wx.Vector3_cross(0, a, b, out, error); fx_mixv(*out); return true; }
static bool wx_v_normalize(void *user, TfpArmVector3 a, TfpArmVector3 *out, TfpArmError *error) {
  WX(9); fx_mixv(a); wx.Vector3_normalize(0, a, out, error); fx_mixv(*out); return true; }
static bool wx_v_lerp(void *user, TfpArmVector3 a, TfpArmVector3 b, double t, TfpArmVector3 *out, TfpArmError *error) {
  WX(10); fx_mixv(a); fx_mixv(b); fx_mixd(t); wx.Vector3_lerp(0, a, b, t, out, error); fx_mixv(*out); return true; }
static bool wx_v_x(void *user, TfpArmVector3 a, double *out, TfpArmError *error) { WX(11); fx_mixv(a); return wx.Vector3_x(0, a, out, error); }
static bool wx_v_y(void *user, TfpArmVector3 a, double *out, TfpArmError *error) { WX(12); fx_mixv(a); return wx.Vector3_y(0, a, out, error); }
static bool wx_v_z(void *user, TfpArmVector3 a, double *out, TfpArmError *error) { WX(13); fx_mixv(a); return wx.Vector3_z(0, a, out, error); }
static bool wx_q_identity(void *user, TfpArmQuaternion *out, TfpArmError *error) { WX(14); return wx.Quaternion_identity(0, out, error); }
static bool wx_q_axis_angle(void *user, TfpArmVector3 axis, double angle, TfpArmQuaternion *out, TfpArmError *error) {
  WX(15); fx_mixv(axis); fx_mixd(angle); wx.Quaternion_axis_angle(0, axis, angle, out, error); fx_mixq(*out); return true; }
static bool wx_q_multiply(void *user, TfpArmQuaternion a, TfpArmQuaternion b, TfpArmQuaternion *out, TfpArmError *error) {
  WX(16); fx_mixq(a); fx_mixq(b); wx.Quaternion_multiply(0, a, b, out, error); fx_mixq(*out); return true; }
static bool wx_q_rotate(void *user, TfpArmQuaternion a, TfpArmVector3 p, TfpArmVector3 *out, TfpArmError *error) {
  WX(17); fx_mixq(a); fx_mixv(p); wx.Quaternion_rotate(0, a, p, out, error); fx_mixv(*out); return true; }
static bool wx_q_lerp(void *user, TfpArmQuaternion a, TfpArmQuaternion b, double t, TfpArmQuaternion *out, TfpArmError *error) {
  WX(18); fx_mixq(a); fx_mixq(b); fx_mixd(t); wx.Quaternion_lerp(0, a, b, t, out, error); fx_mixq(*out); return true; }
static bool wx_q_inverse(void *user, TfpArmQuaternion a, TfpArmQuaternion *out, TfpArmError *error) {
  WX(19); fx_mixq(a); wx.Quaternion_inverse(0, a, out, error); fx_mixq(*out); return true; }
static bool wx_q_to_elements(void *user, TfpArmQuaternion a, double out[4], TfpArmError *error) { WX(20); fx_mixq(a); return wx.Quaternion_to_elements(0, a, out, error); }
static bool wx_q_from_elements(void *user, double x, double y, double z, double w, TfpArmQuaternion *out, TfpArmError *error) {
  WX(21); fx_mixd(x); fx_mixd(y); fx_mixd(z); fx_mixd(w); wx.Quaternion_from_elements(0, x, y, z, w, out, error); fx_mixq(*out); return true; }
static bool wx_m_identity(void *user, TfpArmMatrix4x4 *out, TfpArmError *error) { WX(22); return wx.Matrix4x4_identity(0, out, error); }
static bool wx_m_from(void *user, TfpArmQuaternion a, TfpArmVector3 p, TfpArmMatrix4x4 *out, TfpArmError *error) {
  WX(23); fx_mixq(a); fx_mixv(p); return wx.Matrix4x4_from_quaternion_position(0, a, p, out, error); }
static bool wx_m_multiply(void *user, const TfpArmMatrix4x4 *a, const TfpArmMatrix4x4 *b, TfpArmMatrix4x4 *out, TfpArmError *error) {
  WX(24); fx_mixm(a); fx_mixm(b); wx.Matrix4x4_multiply(0, a, b, out, error); fx_mixm(out); return true; }
static bool wx_m_inverse(void *user, const TfpArmMatrix4x4 *a, TfpArmMatrix4x4 *out, TfpArmError *error) {
  WX(25); fx_mixm(a); wx.Matrix4x4_inverse(0, a, out, error); fx_mixm(out); return true; }
static bool wx_m_translation(void *user, const TfpArmMatrix4x4 *a, TfpArmVector3 *out, TfpArmError *error) { WX(26); fx_mixm(a); return wx.Matrix4x4_translation(0, a, out, error); }
static bool wx_m_rotation(void *user, const TfpArmMatrix4x4 *a, TfpArmQuaternion *out, TfpArmError *error) { WX(27); fx_mixm(a); return wx.Matrix4x4_rotation(0, a, out, error); }

/* The engine of a run, as fx_engine builds it, with each value function it has swapped for the binder's. */
static bool wx_engine(const char *missing, size_t len, TfpArmEngine *engine) {
  if (!fx_engine(missing, len, engine)) return false;
  if (!wx.Vector3_call) tfp_wire_engine_maths(&wx);
#define SWAP(field, function) do { if (engine->field) engine->field = function; } while (0)
  SWAP(Vector3_call, wx_v_call); SWAP(Vector3_op_add, wx_v_add); SWAP(Vector3_op_sub, wx_v_sub); SWAP(Vector3_op_mul, wx_v_mul); SWAP(Vector3_op_unm, wx_v_unm);
  SWAP(Vector3_length, wx_v_length); SWAP(Vector3_dot, wx_v_dot); SWAP(Vector3_cross, wx_v_cross); SWAP(Vector3_normalize, wx_v_normalize);
  SWAP(Vector3_lerp, wx_v_lerp); SWAP(Vector3_x, wx_v_x); SWAP(Vector3_y, wx_v_y); SWAP(Vector3_z, wx_v_z);
  SWAP(Quaternion_identity, wx_q_identity); SWAP(Quaternion_axis_angle, wx_q_axis_angle); SWAP(Quaternion_multiply, wx_q_multiply);
  SWAP(Quaternion_rotate, wx_q_rotate); SWAP(Quaternion_lerp, wx_q_lerp); SWAP(Quaternion_inverse, wx_q_inverse);
  SWAP(Quaternion_to_elements, wx_q_to_elements); SWAP(Quaternion_from_elements, wx_q_from_elements);
  SWAP(Matrix4x4_identity, wx_m_identity); SWAP(Matrix4x4_from_quaternion_position, wx_m_from); SWAP(Matrix4x4_multiply, wx_m_multiply);
  SWAP(Matrix4x4_inverse, wx_m_inverse); SWAP(Matrix4x4_translation, wx_m_translation); SWAP(Matrix4x4_rotation, wx_m_rotation);
#undef SWAP
  return true;
}
#define fx_engine wx_engine
#endif
