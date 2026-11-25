/* The hand closed on an attachment's grip (round 36b, 2026-11-25: the BR-14 Adjudicator with a vertical grip, "Hand on
 * foregrip attachments is not working"). Pure maths on the drive's locals: nothing here reads the game.
 *
 * Why. The arm drive copies the body's finger locals (rig nodes 14 to 28, src/arm_graft.h tfp_graft_pose), and the reach
 * (src/offhand_grip.h) puts the wrist on the grip node (name 67C94E4B, IK_left). On a vertical grip that node is the wrist
 * of a hand wrapped round the handle: its X runs forward and to the right across the handle's front, its Y straight down
 * (filediver, standard_verticalgrip on battle_rifle). The body's fingers are shaped for the gun's own handguard, and the
 * round-23 photo showed them nearly flat: with the wrist on the node the hand sat beside the handle and pointed past it.
 *
 * What. The hand is on the grip node, so the handle is where the node's mesh has it in the hand's own frame, whatever the
 * body plays: the round-36b check put standard_verticalgrip's handle (4.6 cm across, along the node's Y) 7.2 cm out along
 * the hand's X and 4.9 cm under the palm. So each finger joint (index, middle, ring, pinky, joints 1 to 3) is set to a
 * curl of the grip's own about its own +Y, the same for the three joints of a finger and chosen per finger
 * (tfp_gf_curl_degrees) so that on fp_cha_avatar's bind pose every fingertip lies 2.7 to 3.1 cm from the handle's axis
 * and no bone comes nearer than 2.6 cm: round the handle's surface (radius 2.3 cm), not into it. The curl is absolute,
 * not added to the body's: the body's fingers are shaped for the gun's own handguard and change with what it plays, and
 * a finger the body already curls further would go through the handle (70 degrees on all three joints put the index
 * and pinky tips 1.5 and 1.7 cm from the axis). `weight` (the caller's grip weight) takes each joint that share of the
 * way from the body's curl to the grip's, so the hand closes and opens with the reach.
 * The thumb is the body's at joint 1 and turned a little further closed at joints 2 and 3 (TFP_GF_THUMB_TURN_DEGREES
 * added to the body's own, by `weight`): an absolute curl measured from the hand's X does not fit thumb joint 1, whose
 * bind pose is -51.6 degrees that way, and turning it to +30 swept the thumb tip across the palm and through the
 * handle past the pinky's knuckle (the check's probe). With the turn the tip stays on the thumb's side, beyond the
 * index knuckle, and round the far side of the handle (tests/offhand_grip_test.c, test_fingers_on_bind).
 * Positions and the rest of each record are kept. On this rig each finger bone's +Y is its curl axis (fp_cha_avatar's
 * bind pose: the axis between finger1 and finger2 has a dot of 1.0 with finger1's Y) and a positive turn about it closes
 * the finger (X towards -Z, where the bind fingers bend). No vertical-grip hand pose of the game's own was found to take
 * the angles from.
 *
 * The joints are found by name in the drive's map (tfp_grip_fingers_find), never by their numbers. */
#ifndef TFP_GRIP_FINGERS_H
#define TFP_GRIP_FINGERS_H
#include "offhand_grip.h"

enum { TFP_GF_PINKY = 0, TFP_GF_RING, TFP_GF_MIDDLE, TFP_GF_INDEX, TFP_GF_THUMB, TFP_GF_FINGERS };
/* the grip's curl of each joint of a finger, about the bone's own +Y (pinky, ring, middle, index: the enum's order) */
static const double tfp_gf_curl_degrees[TFP_GF_THUMB] = {53, 63, 65, 60};
#define TFP_GF_THUMB_TURN_DEGREES 20.0 /* added to the body's own curl of thumb joints 2 and 3 (joint 1 the body's) */

typedef struct TfpGripFingers {
  bool ok;
  unsigned joint[TFP_GF_FINGERS][3]; /* joints of the drive's map (TfpGraft.fp[joint] is the rig node) */
} TfpGripFingers;

/* fp_cha_avatar's left fingers, joints 1 to 3 (the engine's 32-bit name ids; filediver's thin hash list) */
static const uint32_t tfp_gf_names[TFP_GF_FINGERS][3] = {
    {UINT32_C(0x7a19e054), UINT32_C(0xd5371e86), UINT32_C(0x30545cb9)}, /* l_pinky_finger1..3 */
    {UINT32_C(0x7881f640), UINT32_C(0x2db8f964), UINT32_C(0xa2917eea)}, /* l_ring_finger1..3 */
    {UINT32_C(0xe4767875), UINT32_C(0xda6cc994), UINT32_C(0x83e16077)}, /* l_middle_finger1..3 */
    {UINT32_C(0xc7d1ff28), UINT32_C(0xb15bab98), UINT32_C(0x6cd7940d)}, /* l_index_finger1..3 */
    {UINT32_C(0xb7d89ff0), UINT32_C(0xc2235d63), UINT32_C(0xdf1f5d92)}, /* l_thumb_finger1..3 */
};

/* The fifteen joints in the map: each named as above, joint 1 a child of the left hand (the map's TFP_OG_HAND, rig node 13),
 * joints 2 and 3 of the one before. fn: the rig's node names. false: not this rig; out->ok false. */
static inline bool tfp_grip_fingers_find(const TfpGraft *g, const uint32_t *fn, TfpGripFingers *out) {
  memset(out, 0, sizeof *out);
  for (unsigned f = 0; f < TFP_GF_FINGERS; ++f)
    for (unsigned k = 0; k < 3; ++k) {
      const unsigned parent = k ? g->fp[out->joint[f][k - 1]] : g->fp[TFP_OG_HAND];
      unsigned found = TFP_GRAFT_JOINTS;
      for (unsigned i = 0; i < TFP_GRAFT_JOINTS; ++i)
        if (g->fp[i] < TFP_GRAFT_RIG_NODES && fn[g->fp[i]] == tfp_gf_names[f][k] && g->parent[i] == parent) found = i;
      if (found == TFP_GRAFT_JOINTS) return false;
      out->joint[f][k] = found;
    }
  out->ok = true;
  return true;
}
/* Radians a local is curled about its own +Y: from the parent's X (seen across the bone's Y) to the bone's X, positive
 * closing. A turn of the record about its own +Y adds exactly that angle. */
static inline double tfp_gf_curl_rows(const double r[3][3]) {
  const double *y = r[1], *x = r[0];
  double u[3] = {1 - y[0] * y[0], -y[0] * y[1], -y[0] * y[2]}, c[3]; /* the parent's X less its part along Y */
  tfp_og_cross(u, x, c);
  return atan2(tfp_og_dot(c, y), tfp_og_dot(u, x));
}
static inline double tfp_gf_curl(const TfpGraftLocal *l) {
  const TfpGraftMatrix m = tfp_graft_local_matrix(l);
  const TfpOgFrame f = tfp_og_frame(&m);
  return tfp_gf_curl_rows(f.r);
}
/* The hand closed on the grip by `weight` (0..1): each finger joint that share of the way from its curl to the grip's, thumb
 * joints 2 and 3 turned by that share of THUMB_TURN. locals: the 42 joints in the map's order. */
static inline void tfp_grip_fingers_close(const TfpGripFingers *fingers, TfpGraftLocal *locals, double weight) {
  if (!fingers->ok || !(weight > 0)) return;
  if (weight > 1) weight = 1;
  for (unsigned f = 0; f < TFP_GF_FINGERS; ++f)
    for (unsigned k = f == TFP_GF_THUMB ? 1 : 0; k < 3; ++k) {
      TfpGraftLocal *l = &locals[fingers->joint[f][k]];
      TfpGraftMatrix m = tfp_graft_local_matrix(l);
      if (!tfp_graft_rigid(&m)) continue;
      TfpOgFrame fr = tfp_og_frame(&m);
      const double turn = f == TFP_GF_THUMB ? TFP_GF_THUMB_TURN_DEGREES * (TFP_OG_PI / 180)
                                            : tfp_gf_curl_degrees[f] * (TFP_OG_PI / 180) - tfp_gf_curl_rows(fr.r);
      if (turn == 0) continue;
      double axis[3];
      memcpy(axis, fr.r[1], sizeof axis);
      tfp_og_turn_rows(fr.r, axis, turn * weight);
      for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j) l->basis[3 * i + j] = (float)fr.r[i][j];
    }
}
/* Degrees between the X axes of two world matrices (the drawn curl of a finger joint, for the log). */
static inline double tfp_gf_x_degrees(const TfpGraftMatrix *a, const TfpGraftMatrix *b) {
  const double x[3] = {a->values[0], a->values[1], a->values[2]}, y[3] = {b->values[0], b->values[1], b->values[2]};
  double c[3];
  tfp_og_cross(x, y, c);
  return atan2(tfp_og_len(c), tfp_og_dot(x, y)) * 180 / TFP_OG_PI;
}
#endif
