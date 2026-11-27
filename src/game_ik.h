/* Round 42b: the game's own left-hand IK, its target moved onto a fitted grip. Pure maths and the ease; the
 * engine half (reading the gun, writing its node, putting it back) is src/arm_drive.c (gik_*).
 *
 * What the game does (build 25480438, docs/design/specs/2026-11-25-game-ik.md):
 *   - The first-person rig's state machine (fp_cha_avatar.state_machine) carries two IK chains: 0C92A44D on its
 *     animation bones l_shoulder, l_elbow, l_hand, and 2AB9644B on r_shoulder, r_elbow, r_hand. The third-person body's
 *     state machine has the same two.
 *   - game.dll binds the chains' constraint targets to the weapon's nodes through the engine's script API
 *     (*(game+0x3326308)+0x18): +0x270 finds the target by name on the holder's unit, +0x6d0 / +0x6d8 test for and find
 *     the node by name on the target unit, +0x288 binds the target to (unit, node). game+0x1187a09 binds 0C92A44D to
 *     IK_left (67C94E4B) and 2AB9644B to IK_right (15C8ADE4); game+0x823149 chooses the unit by the weapon's
 *     ik_attach_setting (+0x12f4; None, every primary with an underbarrel slot: the weapon itself).
 *   - So the drawn left hand is solved onto the GUN'S OWN IK_left whatever the drive writes into nodes 10, 12, 13, and a
 *     fitted vertical grip's own IK_left (0.08 to 0.18 m away) is never its target.
 *
 * What is done about it: while the drive holds the hand on an attachment's grip node, the gun's own IK_left node (a leaf
 * locator, no mesh) is given the attachment node's pose in the gun's frame, so the game's IK and hand pose put the hand on
 * the fitted grip. It eases there and back over TFP_GIK_EASE_S, and the game's own record goes back exactly when the hand
 * lets go, the gun or its attachment changes, or the drive stops. */
#ifndef TFP_GAME_IK_H
#define TFP_GAME_IK_H
#include "arm_graft.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define TFP_GIK_EASE_S .25 /* the node's slide there and back. The gate (src/arm_drive.c) lets go of a body's hand moving faster
                            * than 0.9 m/s against the gun, and the body's own IK follows the same node: a 0.124 m slide (the
                            * AR-23's standard vertical grip) peaks at 1.5 x 0.124 / 0.25 = 0.74 m/s, under it. */
#define TFP_GIK_CHAIN UINT32_C(0x0c92a44d)    /* the left-hand IK chain's constraint target (both state machines) */
#define TFP_GIK_CHAIN_R UINT32_C(0x2ab9644b)  /* the right hand's (IK_right 15C8ADE4), not touched */

/* Why the gun's node is not moved (the log's words: tfp_gik_why_name). */
enum {
  TFP_GIK_NONE = 0,   /* moved, or easing */
  TFP_GIK_OFF,        /* switched off (the test file game_ik_off.txt, or the grip off) */
  TFP_GIK_NOT_HELD,   /* the hand is not held on a grip */
  TFP_GIK_GUN_OWN,    /* held on the gun's own node: the game's IK is already there */
  TFP_GIK_NO_OWN,     /* the gun has no IK_left of its own */
  TFP_GIK_NOT_LEAF,   /* its IK_left is not a leaf with a parent in the gun (refused: moving it would move other nodes) */
  TFP_GIK_UNREAD,     /* the gun's or the attachment's matrices could not be read, or are not rigid */
  TFP_GIK_FAULT,      /* the engine's setter or update faulted */
  TFP_GIK_WHYS
};
static inline const char *tfp_gik_why_name(uint32_t why) {
  static const char *const names[TFP_GIK_WHYS] = {
      "moved", "switched off", "the hand is not held on a grip", "the hand holds the gun's own node, where the game's IK already is",
      "the gun has no IK_left of its own", "the gun's IK_left is not a leaf node", "the gun's or the grip's matrices were not readable",
      "an engine call faulted"};
  return why < TFP_GIK_WHYS ? names[why] : "?";
}

/* The ease's progress (0: the game's own node, 1: on the grip) towards `want`, linear in time. */
static inline double tfp_gik_ease(double progress, bool want, double seconds) {
  if (!(seconds > 0)) return progress;
  const double step = seconds / TFP_GIK_EASE_S;
  progress += want ? step : -step;
  return progress < 0 ? 0 : progress > 1 ? 1 : progress;
}
/* The share shown at that progress: a smoothstep, so the node starts and stops at rest. */
static inline double tfp_gik_share(double progress) {
  if (!(progress > 0)) return 0;
  if (progress >= 1) return 1;
  return progress * progress * (3 - 2 * progress);
}
/* The node's record at `share` between the game's own and the target (the attachment's IK_left in the frame of the gun's
 * node the IK_left hangs on): the shorter way round between the two turns, the position straight; the rest of the record
 * (its scale words) the game's. Share 0 is the game's record bit for bit. false: a rotation is not rigid. */
static inline bool tfp_gik_pose(const TfpGraftLocal *game, const TfpGraftMatrix *target, double share, TfpGraftLocal *out) {
  if (!(share > 0)) { *out = *game; return true; }
  return tfp_graft_mix(game, target, (float)share, out);
}
/* The target while the held grip node changes (another node, or the attachment made again): from where the node was aimed
 * (`from`, a record with the game's other words) towards the new target `to` at `share`, so the node slides rather than
 * jumps. false: a rotation is not rigid. */
static inline bool tfp_gik_cross(const TfpGraftLocal *from, const TfpGraftMatrix *to, double share, TfpGraftMatrix *out) {
  if (share >= 1) { *out = *to; return true; }
  TfpGraftLocal mixed;
  if (!tfp_graft_mix(from, to, (float)share, &mixed)) return false;
  *out = tfp_graft_local_matrix(&mixed);
  return true;
}
/* The node where the game keeps it, in the world: its parent's world matrix times the game's own record. */
static inline TfpGraftMatrix tfp_gik_unmoved(const TfpGraftLocal *game, const TfpGraftMatrix *parent_world) {
  const TfpGraftMatrix local = tfp_graft_local_matrix(game);
  return tfp_graft_product(&local, parent_world);
}
#endif
