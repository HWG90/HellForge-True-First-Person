/* Round 38 (docs/design/specs/2026-11-25-exosuit-seat.md): the "Seat:" line, once a second while the camera lock shows
 * first person from an exosuit's seat or glides out of one. Pure formatting: the caller gathers the numbers.
 *
 * Seat: +<frames> frames from the seat (+<refused> refused, +<glided> gliding), eye (<right> <forward> <up>) m from the
 * suit seat's root, anchor settled|settling, glide in|out|none; hands from the eye L (x y z) R (x y z) | not read;
 * diver: <rows> rows hidden (<limbs> of the limb slots); cockpit: seen through (suit <ref> by <how>, <rows> meshes) |
 * drawn (<why>); arms: rig not drawn by the game here, +<driven> frames driven, +<other> in another camera
 *
 * The hands answer the open question of round 37b (what the shape in front of the console was): the body's own
 * l_hand and r_hand nodes along the root's axes from the eye. The arm counts show the first-person rig staying the
 * game's: nothing is driven outside camera kind 6. */
#ifndef TFP_SEAT_LOG_H
#define TFP_SEAT_LOG_H
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

typedef struct TfpSeatLine {
  uint64_t frames, refused, glided;   /* camera seat frames in the second (TfpCameraSeatStats deltas) */
  uint32_t settled, gliding, hands;   /* TfpCameraSeatStats: anchor frozen; 0 none, 1 in, 2 out; bit 0 left, bit 1 right */
  float offset[3], left[3], right[3];
  bool see_through;                   /* the option */
  uint32_t suit; int how;             /* the suit seen through (0: none), src/cockpit_find.h TfpCockpitHow */
  uint32_t searches;
  double hidden, limbs, cockpit;      /* head visibility's rows: all, limb slots, the suit's meshes */
  uint64_t driven, other;             /* arm drive deltas: frames driven, frames in another camera */
} TfpSeatLine;

static inline size_t tfp_seat_line(char *out, size_t size, const TfpSeatLine *l) {
  char hands[96], cockpit[120];
  if ((l->hands & 3) == 3)
    snprintf(hands, sizeof hands, "L (%.2f %.2f %.2f) R (%.2f %.2f %.2f)", (double)l->left[0], (double)l->left[1], (double)l->left[2],
             (double)l->right[0], (double)l->right[1], (double)l->right[2]);
  else snprintf(hands, sizeof hands, "not read");
  if (!l->see_through) snprintf(cockpit, sizeof cockpit, "drawn (the option is off)");
  else if (l->suit && l->cockpit > 0)
    snprintf(cockpit, sizeof cockpit, "seen through (suit %#x by %s, %.0f meshes)", l->suit, l->how == 1 ? "link" : "seat", l->cockpit);
  else if (l->suit) snprintf(cockpit, sizeof cockpit, "drawn (suit %#x found, its meshes not taken yet)", l->suit);
  else snprintf(cockpit, sizeof cockpit, "drawn (no suit proven yet, %u searches)", l->searches);
  const int n = snprintf(out, size,
                         "Seat: +%llu frames from the seat (+%llu refused, +%llu gliding), eye (%.2f %.2f %.2f) m from the seat's root, anchor %s, glide %s; "
                         "hands from the eye %s; diver: %.0f rows hidden (%.0f of the limb slots); cockpit: %s; arms: rig not drawn by the game here, "
                         "+%llu frames driven, +%llu in another camera",
                         (unsigned long long)l->frames, (unsigned long long)l->refused, (unsigned long long)l->glided, (double)l->offset[0],
                         (double)l->offset[1], (double)l->offset[2], l->settled ? "settled" : "settling",
                         l->gliding == 1 ? "in" : l->gliding == 2 ? "out" : "none", hands, l->hidden, l->limbs, cockpit,
                         (unsigned long long)l->driven, (unsigned long long)l->other);
  return n < 0 ? 0 : (size_t)n;
}
#endif
