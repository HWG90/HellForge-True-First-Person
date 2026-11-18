/* The engine binder's memory of the snapshots it handed to the visibility modules: head visibility
 * and the model shadow source get a TfpVisSnapshot and hand it back (backpack, action state, …);
 * the backend's own TfpOpsSnapshot behind it is found again by what the visible one reads. */
#ifndef TFP_WIRE_RECENT_H
#define TFP_WIRE_RECENT_H
#include <string.h>
#include "backend_ops.h"
#include "model_shadow_source.h"

/* Two rings, one for player snapshots and one for full ones: within one update the modules take many
 * of one kind (the model shadow source's full snapshots) while still holding one of the other (head
 * visibility's player snapshot), and a single ring forgot it. Snapshots of one kind taken in the same
 * frame read the same, so a ring of eight of a kind is enough. */
enum { TFP_RECENT = 8 };
typedef struct TfpWireRecent {
  struct { TfpOpsSnapshot ops; TfpVisSnapshot vis; } kept[2][TFP_RECENT];
  unsigned next[2];
} TfpWireRecent;

static inline void tfp_recent_remember(TfpWireRecent *r, const TfpOpsSnapshot *s, const TfpVisSnapshot *t) {
  unsigned kind = s->full ? 1 : 0;
  r->kept[kind][r->next[kind] % TFP_RECENT].ops = *s;
  r->kept[kind][r->next[kind]++ % TFP_RECENT].vis = *t;
}
/* The newest that reads the same, or NULL. A full snapshot's identity is never a player identity, so
 * the kind is settled by the comparison itself. */
static inline const TfpOpsSnapshot *tfp_recent_recall(const TfpWireRecent *r, const TfpVisSnapshot *t) {
  for (unsigned kind = 0; kind < 2; kind++)
    for (unsigned i = 0; i < TFP_RECENT; i++) {
      unsigned at = (r->next[kind] + TFP_RECENT - 1 - i) % TFP_RECENT;
      const TfpVisSnapshot *v = &r->kept[kind][at].vis;
      if (v->player_identity.present && v->base == t->base && v->record == t->record && v->head == t->head && v->tail == t->tail &&
          v->camera_kind == t->camera_kind && v->body == t->body && v->world == t->world && v->avatar_owner == t->avatar_owner &&
          v->avatar_index == t->avatar_index && v->safe == t->safe && tfp_vis_identity_equal(&v->player_identity, &t->player_identity) &&
          tfp_vis_identity_equal(&v->identity, &t->identity) && !memcmp(v->flags, t->flags, 24))
        return &r->kept[kind][at].ops;
    }
  return NULL;
}
#endif
