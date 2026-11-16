#include "transition.h"
#include "vectors/transition_vectors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  /* vectors_name_their_source: refuse vectors cut from another retail commit. */
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(TRANSITION_VECTOR_COMMIT, commit) || strlen(TRANSITION_VECTOR_SHA256) != 64) {
    fprintf(stderr, "transition vectors were cut from retail %s, expected %s\n", TRANSITION_VECTOR_COMMIT, commit);
    return 2;
  }
  TfpTransition t;
  tfp_transition_init(&t);
  unsigned count = sizeof transition_vectors / sizeof *transition_vectors, packets = 0;
  for (unsigned i = 0; i < count; i++) {
    const TransitionVector *v = transition_vectors + i;
    TfpTransitionPacket p = {0};
    bool has = false;
    if (v->op == TV_NATIVE_ADS) {
      if (tfp_transition_native_ads(v->active, v->seated, v->b) != (v->flag != 0)) {
        fprintf(stderr, "transition vector %u: native_ads differs\n", i);
        return 1;
      }
      continue;
    }
    if (v->op == TV_UPDATE) {
      TfpTransitionInput s = {.player_identity = (uint64_t)v->player, .identity = (uint64_t)v->identity,
          .controller_active = v->active, .seated = v->seated, .shoulder_aim = v->shoulder,
          .camera_kind = (uint32_t)v->kind, .action = v->action, .zoom_identity = v->zoom};
      has = tfp_transition_update(&t, v->available ? &s : NULL, v->now, v->enabled, v->blocked, &p);
    } else if (v->op == TV_ACTION) has = tfp_transition_action(&t, v->action, v->now, &p);
    else if (v->op == TV_RESUME) has = tfp_transition_resume(&t, (uint64_t)v->identity, (uint64_t)v->player, v->now, &p);
    else if (v->op == TV_RESULT) has = tfp_transition_result(&t, &p);
    else if (v->op == TV_RESET) tfp_transition_reset(&t);
    if (has != (v->has_packet != 0) ||
        (has && (p.epoch != v->epoch || fabs(p.progress - v->progress) > 1e-12 || p.ads != (v->ads != 0) ||
                 (int)p.reason != v->reason || (int)p.merged != v->merged || p.continuous != (v->continuous != 0) ||
                 fabs(p.started - v->started) > 1e-12))) {
      fprintf(stderr, "transition vector %u (op %d, now %.6f): C has=%d epoch=%llu progress=%.17g ads=%d reason=%d merged=%d cont=%d started=%.6f\n"
                      "  retail has=%d epoch=%llu progress=%.17g ads=%d reason=%d merged=%d cont=%d started=%.6f\n",
              i, v->op, v->now, has, (unsigned long long)p.epoch, p.progress, p.ads, p.reason, p.merged, p.continuous, p.started,
              v->has_packet, v->epoch, v->progress, v->ads, v->reason, v->merged, v->continuous, v->started);
      return 1;
    }
    packets += has;
  }
  printf("Transition clock: %u operations answered as retail's transition.lua (%s), %u with a packet\n", count,
         TRANSITION_VECTOR_COMMIT, packets);
  return 0;
}
