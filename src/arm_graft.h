/* The arm graft's maths: the third-person body's arm animation expressed as local poses of the game's
 * first-person arm rig. Build 25480438.
 *
 * Lifted from the retired First Person Controller (true-first-person-controller-body14-20261118):
 *   src/arm_graft.h        the joint map, fpc_graft_bind, fpc_graft_pose, fpc_graft_anchor
 *   src/rigid_pose.h       fpc_rigid_matrix_valid, fpc_rigid_product, fpc_relative_pose
 *   src/native_view.c      fpc_native_pose_from_world (matrix to quaternion)
 *   src/draw_eye.h         fpc_rotate_vector; src/hud_projection.h fpc_hud_matrix (quaternion to matrix)
 *   src/native_arm_graft.h graft_underlying, graft_local_matrix, graft_set_transform, graft_mix_local,
 *                          graft_align_action
 * Every expression, its types and its order are the retired ones, so the results are the same bits
 * (tests/arm_graft_test.c compares against values the retired functions produced). Only the names
 * changed. Nothing here reads the game or keeps a pointer. Do not compile with fast-math or FMA
 * contraction.
 *
 * Both rigs name their arm joints alike. The map is 42 joints of the rig's 63 nodes: nodes 10..49
 * (two arms with their fingers), 58, and 8 (the weapon socket). Matrices are the engine's: a 4x4 of
 * row axes with the position in row 3. */
#ifndef TFP_ARM_GRAFT_H
#define TFP_ARM_GRAFT_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum { TFP_GRAFT_RIG_NODES = 63, TFP_GRAFT_BODY_NODES = 107, TFP_GRAFT_JOINTS = 42 };
enum { TFP_GRAFT_RIG_ROOT = 7, TFP_GRAFT_RIG_SOCKET = 8, TFP_GRAFT_RIG_EYE = 50 };
/* Node name hashes (the engine's 32-bit name ids). */
#define TFP_GRAFT_NAME_HEAD       UINT32_C(0x8c5570c9) /* head: the body's anchor */
#define TFP_GRAFT_NAME_RIG_ROOT   UINT32_C(0x6f3bd021) /* rig node 7: the presentation root both arms and the socket hang from */
#define TFP_GRAFT_NAME_SOCKET     UINT32_C(0x4fdd3337) /* rig node 8: the weapon socket */
#define TFP_GRAFT_NAME_L_HAND     UINT32_C(0x5c0c68d3) /* rig node 13: l_hand */
#define TFP_GRAFT_NAME_R_HAND     UINT32_C(0x248004f5) /* rig node 34: r_hand */
#define TFP_GRAFT_NAME_L_SHOULDER UINT32_C(0x03752c98) /* rig node 10: l_shoulder */
#define TFP_GRAFT_NAME_RIG_EYE    UINT32_C(0x60ed8c39) /* rig node 50: where the game's camera is linked */

typedef struct TfpGraftMatrix {
  _Alignas(16) float values[16];
} TfpGraftMatrix;
/* A node's local record in a unit's scene graph. It is not a world matrix: the engine's rotation
 * setters replace bytes 0..35, its position setters bytes 36..47; bytes 48..63 (scale and one word)
 * are kept from the unit's own record. */
typedef struct TfpGraftLocal {
  _Alignas(16) float basis[9];
  float position[3], preserved[4];
} TfpGraftLocal;
_Static_assert(sizeof(TfpGraftMatrix) == 64 && sizeof(TfpGraftLocal) == 64, "engine record sizes");

typedef struct TfpGraft {
  unsigned body[TFP_GRAFT_JOINTS], fp[TFP_GRAFT_JOINTS], parent[TFP_GRAFT_JOINTS];
  unsigned head;
  TfpGraftMatrix alignment, grip;
} TfpGraft;

/* ---- rigid_pose.h ---- */
static inline bool tfp_graft_rigid(const TfpGraftMatrix *matrix) {
  if (!matrix) return false;
  const float *m = matrix->values;
  for (int i = 0; i < 16; ++i)
    if (!isfinite(m[i])) return false;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j <= i; ++j) {
      float dot = 0;
      for (int k = 0; k < 3; ++k) dot += m[4 * i + k] * m[4 * j + k];
      if (fabsf(dot - (i == j)) > .002f) return false;
    }
  }
  float determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
  if (determinant < .998f) return false;
  return true;
}
static inline TfpGraftMatrix tfp_graft_product(const TfpGraftMatrix *a, const TfpGraftMatrix *b) {
  TfpGraftMatrix out = {0};
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = 0; j < 3; ++j) {
      double v = i == 3 ? b->values[12 + j] : 0;
      for (unsigned k = 0; k < 3; ++k) v += (double)a->values[4 * i + k] * b->values[4 * k + j];
      out.values[4 * i + j] = (float)v;
    }
  out.values[15] = 1;
  return out;
}
/* child expressed in parent: child * inverse(parent), for rigid matrices. */
static inline TfpGraftMatrix tfp_graft_relative(const TfpGraftMatrix *child, const TfpGraftMatrix *parent) {
  TfpGraftMatrix out = {0};
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = 0; j < 3; ++j) {
      double v = 0;
      for (unsigned k = 0; k < 3; ++k) v += ((double)child->values[4 * i + k] - (i == 3 ? parent->values[12 + k] : 0)) * parent->values[4 * j + k];
      out.values[4 * i + j] = (float)v;
    }
  out.values[15] = 1;
  return out;
}

/* ---- native_view.c fpc_native_pose_from_world: the rotation of a rigid matrix as x, y, z, w ---- */
static inline bool tfp_graft_quaternion(const TfpGraftMatrix *matrix, float q[4]) {
  if (!tfp_graft_rigid(matrix)) return false;
  const float *m = matrix->values;
  float trace = m[0] + m[5] + m[10];
  if (trace > 0) {
    float scale = 2 * sqrtf(1 + trace);
    q[3] = scale / 4;
    q[0] = (m[6] - m[9]) / scale;
    q[1] = (m[8] - m[2]) / scale;
    q[2] = (m[1] - m[4]) / scale;
  } else {
    int i = m[0] >= m[5] ? 0 : 1;
    if (m[10] > m[5 * i]) i = 2;
    int j = (i + 1) % 3, k = (i + 2) % 3;
    float scale = 2 * sqrtf(1 + m[5 * i] - m[5 * j] - m[5 * k]);
    q[i] = scale / 4;
    q[j] = (m[4 * i + j] + m[4 * j + i]) / scale;
    q[k] = (m[4 * i + k] + m[4 * k + i]) / scale;
    q[3] = (m[4 * j + k] - m[4 * k + j]) / scale;
  }
  return true;
}
/* ---- draw_eye.h fpc_rotate_vector, hud_projection.h fpc_hud_matrix ---- */
static inline void tfp_graft_rotate(const float q[4], const float v[3], float out[3]) {
  float cross[3];
  for (unsigned i = 0; i < 3; ++i) {
    unsigned j = (i + 1) % 3, k = (i + 2) % 3;
    cross[i] = 2 * (q[j] * v[k] - q[k] * v[j]);
  }
  for (unsigned i = 0; i < 3; ++i) {
    unsigned j = (i + 1) % 3, k = (i + 2) % 3;
    out[i] = v[i] + q[3] * cross[i] + q[j] * cross[k] - q[k] * cross[j];
  }
}
static inline void tfp_graft_axes(const float q[4], TfpGraftMatrix *matrix) { /* the three row axes; position 0 */
  *matrix = (TfpGraftMatrix){0};
  for (unsigned axis = 0; axis < 3; axis++) {
    float basis[3] = {0};
    basis[axis] = 1;
    tfp_graft_rotate(q, basis, &matrix->values[axis * 4]);
  }
  matrix->values[15] = 1;
}

/* ---- arm_graft.h ---- */
/* The body's frame the arm roots are expressed in: the root node's axes at the head's position. */
static inline TfpGraftMatrix tfp_graft_anchor(const TfpGraft *g, const TfpGraftMatrix *body) {
  TfpGraftMatrix anchor = body[0];
  memcpy(anchor.values + 12, body[g->head].values + 12, 3 * sizeof(float));
  return anchor;
}
/* bn, bl: the body's 107 node names and links; fn, fl: the rig's 63; body, fp: their world matrices
 * now. A link is (parent << 16) | kind, kind 1 = a parent in the same unit. false: these are not the
 * two rigs of build 25480438, or a matrix is not rigid; *out is then untouched. */
static inline bool tfp_graft_bind(TfpGraft *out, const uint32_t *bn, const uint32_t *bl, const uint32_t *fn, const uint32_t *fl,
                                  const TfpGraftMatrix *body, const TfpGraftMatrix *fp) {
  TfpGraft g = {0};
  if (fn[7] != TFP_GRAFT_NAME_RIG_ROOT || fn[8] != TFP_GRAFT_NAME_SOCKET || fn[13] != TFP_GRAFT_NAME_L_HAND || fn[34] != TFP_GRAFT_NAME_R_HAND)
    return false;
  for (unsigned i = 0; i < TFP_GRAFT_BODY_NODES; ++i)
    if (bn[i] == TFP_GRAFT_NAME_HEAD) g.head = i;
  if (!g.head || !tfp_graft_rigid(&body[0]) || !tfp_graft_rigid(&fp[7])) return false;
  for (unsigned i = 0; i < TFP_GRAFT_JOINTS; ++i) {
    unsigned node = i < 40 ? 10 + i : i == 40 ? 58 : 8;
    unsigned match = TFP_GRAFT_BODY_NODES;
    for (unsigned j = 0; j < TFP_GRAFT_BODY_NODES; ++j)
      if (bn[j] == fn[node]) {
        if (match != TFP_GRAFT_BODY_NODES) return false;
        match = j;
      }
    if (match == TFP_GRAFT_BODY_NODES || (fl[node] & 65535) != 1 || (fl[node] >> 16) >= node || !tfp_graft_rigid(&body[match]) ||
        !tfp_graft_rigid(&fp[node]))
      return false;
    unsigned parent = fl[node] >> 16;
    if (parent == 7) {
      if (node != 8 && node != 10 && node != 30) return false;
    } else if (parent < 10 || parent >= 50 || i <= parent - 10 || bl[match] != ((g.body[parent - 10] << 16) | 1u))
      return false;
    g.body[i] = match;
    g.fp[i] = node;
    g.parent[i] = parent;
  }
  unsigned palm = g.body[34 - 10], socket = g.body[TFP_GRAFT_JOINTS - 1];
  if (bl[socket] != ((palm << 16) | 1u)) return false;
  TfpGraftMatrix anchor = tfp_graft_anchor(&g, body);
  TfpGraftMatrix hand = tfp_graft_relative(&body[palm], &anchor);
  TfpGraftMatrix dest = tfp_graft_relative(&fp[34], &fp[7]);
  TfpGraftMatrix identity = {.values = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  TfpGraftMatrix inverse = tfp_graft_relative(&identity, &hand);
  g.alignment = tfp_graft_product(&inverse, &dest);
  TfpGraftMatrix body_grip = tfp_graft_relative(&body[socket], &body[palm]);
  TfpGraftMatrix fp_grip = tfp_graft_relative(&fp[8], &fp[34]);
  g.grip = tfp_graft_relative(&fp_grip, &body_grip);
  *out = g;
  return true;
}
/* The 42 local poses for the rig from the body's world matrices now. The rig's eye and its
 * presentation root are never destinations; the gun and the optic keep the engine's socket chain. */
static inline bool tfp_graft_pose(const TfpGraft *g, const TfpGraftMatrix *body, TfpGraftMatrix locals[TFP_GRAFT_JOINTS]) {
  TfpGraftMatrix anchor = tfp_graft_anchor(g, body);
  if (!tfp_graft_rigid(&anchor)) return false;
  for (unsigned i = 0; i < TFP_GRAFT_JOINTS; ++i) {
    const TfpGraftMatrix *bone = &body[g->body[i]];
    if (!tfp_graft_rigid(bone)) return false;
    if (g->parent[i] == 7) {
      TfpGraftMatrix relative = tfp_graft_relative(bone, &anchor);
      locals[i] = tfp_graft_product(&relative, &g->alignment);
      if (g->fp[i] == 8) locals[i] = tfp_graft_product(&g->grip, &locals[i]);
    } else
      locals[i] = tfp_graft_relative(bone, &body[g->body[g->parent[i] - 10]]);
    if (!tfp_graft_rigid(&locals[i])) return false;
  }
  return true;
}

/* ---- native_arm_graft.h ---- */
static inline TfpGraftMatrix tfp_graft_local_matrix(const TfpGraftLocal *p) { /* graft_local_matrix */
  TfpGraftMatrix m = {0};
  for (unsigned i = 0; i < 3; ++i) memcpy(m.values + 4 * i, p->basis + 3 * i, 3 * sizeof(float));
  memcpy(m.values + 12, p->position, sizeof p->position);
  m.values[15] = 1;
  return m;
}
static inline void tfp_graft_set_local(TfpGraftLocal *p, const TfpGraftMatrix *m) { /* graft_set_transform */
  for (unsigned i = 0; i < 3; ++i) memcpy(p->basis + 3 * i, m->values + 4 * i, 3 * sizeof(float));
  memcpy(p->position, m->values + 12, sizeof p->position);
}
/* graft_underlying: the game's own value of a joint this mod wrote earlier. Where the record still
 * holds what was written, the value saved before the write; where the game has written since, what
 * is there now. The rotation is taken as a whole, the position one component at a time. */
static inline TfpGraftLocal tfp_graft_underlying(bool owned, const TfpGraftLocal *original, const TfpGraftLocal *applied, TfpGraftLocal p) {
  if (owned) {
    if (!memcmp(p.basis, applied->basis, sizeof p.basis)) memcpy(p.basis, original->basis, sizeof p.basis);
    for (unsigned j = 0; j < 3; ++j)
      if (p.position[j] == applied->position[j]) p.position[j] = original->position[j];
  }
  return p;
}
/* graft_mix_local: `native` with its rotation and position moved towards `animated` by `weight`.
 * 0: native untouched. 1: animated's rotation and position exactly, the rest of the record native's.
 * Between: the shorter way round between the two rotations (normalised blend), the position
 * straight. false: a rotation is not rigid. */
static inline bool tfp_graft_mix(const TfpGraftLocal *native, const TfpGraftMatrix *animated, float weight, TfpGraftLocal *out) {
  if (weight <= 0) { *out = *native; return true; }
  if (weight >= 1) {
    *out = *native;
    tfp_graft_set_local(out, animated);
    return true;
  }
  TfpGraftMatrix from = {.values = {0}};
  for (unsigned i = 0; i < 3; ++i) memcpy(from.values + 4 * i, native->basis + 3 * i, 3 * sizeof(float));
  from.values[15] = 1;
  float a[4], b[4];
  if (!tfp_graft_quaternion(&from, a) || !tfp_graft_quaternion(animated, b)) return false;
  float dot = 0, len = 0;
  for (unsigned i = 0; i < 4; ++i) dot += a[i] * b[i];
  for (unsigned i = 0; i < 4; ++i) {
    a[i] = (1 - weight) * a[i] + weight * (dot < 0 ? -b[i] : b[i]);
    len += a[i] * a[i];
  }
  if (!(len > .01f)) return false;
  len = 1 / sqrtf(len);
  for (unsigned i = 0; i < 4; ++i) a[i] *= len;
  tfp_graft_axes(a, &from);
  *out = *native;
  for (unsigned i = 0; i < 3; ++i) {
    memcpy(out->basis + 3 * i, from.values + 4 * i, 3 * sizeof(float));
    out->position[i] += weight * (animated->values[12 + i] - out->position[i]);
  }
  return true;
}
/* graft_align_action (0.3.37): makes the body's weapon socket, as it is now, land on `socket`, the
 * rig's own socket local being shown now. Everything else the body does afterwards is then seen
 * relative to the weapon placement the player already had. */
static inline bool tfp_graft_align(TfpGraft *g, const TfpGraftMatrix *body, const TfpGraftLocal *socket) {
  TfpGraftMatrix anchor = tfp_graft_anchor(g, body);
  TfpGraftMatrix source = tfp_graft_relative(&body[g->body[TFP_GRAFT_JOINTS - 1]], &anchor);
  source = tfp_graft_product(&g->grip, &source);
  TfpGraftMatrix shown = tfp_graft_local_matrix(socket);
  if (!tfp_graft_rigid(&source) || !tfp_graft_rigid(&shown)) return false;
  const TfpGraftMatrix identity = {.values = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  TfpGraftMatrix inverse = tfp_graft_relative(&identity, &source);
  g->alignment = tfp_graft_product(&inverse, &shown);
  return true;
}
/* The test bend (arm_bend.txt): the record turned by `degrees` about its own Z axis. */
static inline TfpGraftLocal tfp_graft_bend_z(TfpGraftLocal p, float degrees) {
  const float c = cosf(degrees * .01745329252f), s = sinf(degrees * .01745329252f);
  for (unsigned k = 0; k < 3; ++k) {
    const float x = p.basis[k], y = p.basis[3 + k];
    p.basis[k] = c * x + s * y;
    p.basis[3 + k] = c * y - s * x;
  }
  return p;
}
#endif
