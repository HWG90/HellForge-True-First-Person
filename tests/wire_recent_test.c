/* The binder's snapshot memory (src/wire_recent.h). Found in the game on 2026-11-17: with a backpack on,
 * head visibility asked for the backpack with its player snapshot after the model shadow source had
 * taken more than eight full snapshots in the same update, and the binder no longer knew it. */
#include <stdio.h>
#include <stdlib.h>
#include "wire_recent.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static TfpWireRecent *recent;
static void identity(TfpVisIdentity *id, char kind, unsigned n) {
  id->present = true; id->len = (uint16_t)snprintf((char *)id->bytes, sizeof id->bytes, "%c%u", kind, n);
}
static TfpVisSnapshot vis(bool full, unsigned weapon) {
  TfpVisSnapshot t; memset(&t, 0, sizeof t);
  t.base = 0x1000, t.record = 0x2000, t.camera_kind = 6, t.body = 7, t.world = 0x3000, t.avatar_owner = 0x4000, t.safe = true;
  identity(&t.player_identity, 'P', 1);
  if (full) identity(&t.identity, 'W', weapon); else t.identity = t.player_identity;
  return t;
}
static void take(bool full, unsigned weapon) {
  TfpOpsSnapshot *s = calloc(1, sizeof *s); TfpVisSnapshot t = vis(full, weapon);
  s->full = full, s->weapon_unit = weapon, s->body = 7;
  tfp_recent_remember(recent, s, &t); free(s);
}
int main(void) {
  recent = calloc(1, sizeof *recent);
  TfpVisSnapshot player = vis(false, 0), full = vis(true, 5), other = vis(true, 9);
  CHECK(!tfp_recent_recall(recent, &player));                     /* nothing remembered yet */
  take(false, 0);
  const TfpOpsSnapshot *got = tfp_recent_recall(recent, &player);
  CHECK(got && !got->full);
  for (int i = 0; i < 40; i++) take(true, 5);                     /* one update's worth of full snapshots */
  got = tfp_recent_recall(recent, &player);
  CHECK(got && !got->full);                                       /* the player snapshot is still known */
  got = tfp_recent_recall(recent, &full);
  CHECK(got && got->full && got->weapon_unit == 5);
  CHECK(!tfp_recent_recall(recent, &other));                      /* a weapon never seen is not made up */
  for (int i = 0; i < 40; i++) take(false, 0);                    /* and the other way round */
  got = tfp_recent_recall(recent, &full);
  CHECK(got && got->full && got->weapon_unit == 5);
  player.safe = false;
  CHECK(!tfp_recent_recall(recent, &player));                     /* a snapshot that reads differently is not the same one */
  free(recent);
  if (failures) return 1;
  printf("Binder snapshot memory: 8 checks passed\n");
  return 0;
}
