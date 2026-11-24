/* The "Head:" line: what head visibility did in a tick, where the tick is worth a line. For the log only; nothing here
 * changes what the mod does (docs/design/specs/2026-11-24-helmet-in-view.md).
 *
 * Seen in the game on 0.6.0-feel.49: in a weapon swap the player's own helmet, shoulders and back filled the screen
 * for a moment. The eye was where the lock holds it through a swap (the outgoing weapon's eye, behind the head), so
 * the body was drawn in that frame; the log of that build cannot say why, because it carries one number of head
 * visibility a second. This line says, on the tick it happens:
 *   - what the module owns of the head, the torso and the shoulders, when that changes;
 *   - meshes it had seen hidden and now finds drawn (the game, or another mod, switched their camera pass back on
 *     between two ticks; a frame may have been drawn in between), and what it did about them;
 *   - meshes it leaves drawn (retail's rule: an outside change is left alone until the view, weapon or aim changes);
 *   - the module stopping or starting while the view is on, and rows it gave back (said once while the number
 *     stays: rows that cannot be given back are tried again every tick).
 *
 * The binder fills a TfpHeadTick after every update and writes the text where tfp_head_log_step says so. */
#ifndef TFP_HEAD_LOG_H
#define TFP_HEAD_LOG_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct TfpHeadTick {
  bool bound;        /* head visibility is bound (its in-game checks passed) */
  bool view;         /* the first-person view is switched on */
  bool active;       /* the module's phase is "active" */
  const char *phase; /* its name */
  uint32_t kind;     /* the camera kind of the game's top request this tick, as the tick read it */
  unsigned rows, head, torso, shoulders;   /* owned now: all rows, and the camera rows by part (TfpHeadResult) */
  /* The last update's TfpHeadWatch (src/head_visibility.h). */
  bool full;
  unsigned camera_rows, on_new, on_again, on_again_head, on_still, turned_off, left_on, put_back;
} TfpHeadTick;
typedef struct TfpHeadLog {
  bool known, was_view, was_active;
  unsigned head, torso, shoulders, left_on, put_back;
  uint64_t again_ticks, again_meshes, off_ticks;   /* since the start: ticks with meshes drawn again, those meshes, ticks of the view without hiding */
} TfpHeadLog;

/* One tick. true: `text` holds a line to write (cut to `size`). */
static inline bool tfp_head_log_step(TfpHeadLog *g, const TfpHeadTick *t, char *text, size_t size) {
  if (!t->bound) return false;
  if (t->on_again) { g->again_ticks++; g->again_meshes += t->on_again; }
  if (t->view && !t->active) g->off_ticks++;
  const unsigned was_head = g->known ? g->head : 0, was_torso = g->known ? g->torso : 0, was_shoulders = g->known ? g->shoulders : 0;
  /* A steady tick reads no row: it says nothing about what is left drawn, and what was left stands. */
  const unsigned left = t->full || !t->active ? t->left_on : g->left_on;
  const bool worth = t->on_again || (t->put_back && t->put_back != g->put_back) || left != g->left_on || t->head != was_head || t->torso != was_torso || t->shoulders != was_shoulders
    || (g->known && t->active != g->was_active && (t->view || g->was_view));
  g->known = true; g->was_view = t->view; g->was_active = t->active;
  g->head = t->head; g->torso = t->torso; g->shoulders = t->shoulders; g->left_on = left; g->put_back = t->put_back;
  if (!worth || !size) return false;
  snprintf(text, size, "Head: kind=%u %s: owns head %u, torso %u, shoulders %u (%u rows; was head %u, torso %u, shoulders %u); found drawn %u new, %u again (%u of "
           "the head), %u still; switched off %u, left drawn %u, gave back %u; so far drawn again on %llu ticks (%llu meshes), view on without hiding on %llu ticks",
           t->kind, t->phase ? t->phase : "-", t->head, t->torso, t->shoulders, t->rows, was_head, was_torso, was_shoulders, t->on_new, t->on_again, t->on_again_head,
           t->on_still, t->turned_off, t->left_on, t->put_back, (unsigned long long)g->again_ticks, (unsigned long long)g->again_meshes,
           (unsigned long long)g->off_ticks);
  return true;
}
#endif
