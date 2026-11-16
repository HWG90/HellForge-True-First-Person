/* Replays tests/vectors/grip_hand_vectors.h against src/grip_hand.c: every operation must make the
 * engine calls the retail grip_hand.lua made (same order, same argument bits), do the same value
 * maths and give the same answer and state. See arms_fx.h. */
#include "grip_hand.h"
#include "arms_fx.h"
#include "vectors/grip_hand_vectors.h"

static const char *opt(bool has, double x, char *out) { if (has) fx_double_hex(x, out); else snprintf(out, 17, "~"); return out; }
static TfpArmVector3 point(const char *hex) { return (TfpArmVector3){fx_float(hex), fx_float(hex + 8), fx_float(hex + 16)}; }
static void point_hex(TfpArmVector3 p, char *out) { snprintf(out, 25, "%08x%08x%08x", fx_bits(p.x), fx_bits(p.y), fx_bits(p.z)); }

int main(int argc, char **argv) {
  const char *commit = "1d0d453";
  static TfpGripHand instances[2];
  static bool made[2];
  TfpArmEngine engine;
  unsigned total = sizeof grip_hand_vectors / sizeof *grip_hand_vectors, runs = sizeof grip_hand_runs / sizeof *grip_hand_runs;
  unsigned calls = 0, maths = 0, raised = 0, updates = 0, left = 0, right = 0, solves = 0;
  int run = -1;
  unsigned only = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0;
  if (strcmp(GRIP_HAND_VECTOR_COMMIT, commit) || strlen(GRIP_HAND_VECTOR_SHA256) != 64) {
    fprintf(stderr, "grip hand vectors were cut from retail %s, expected %s\n", GRIP_HAND_VECTOR_COMMIT, commit);
    return 2;
  }
  for (unsigned i = 0; i < total; i++) {
    const ArmsVector *v = &grip_hand_vectors[i];
    FxLine l;
    static FxInputs in;
    TfpGripHand *g;
    TfpGripHandResult r;
    char answer[1200], why[1600], n[16][40], h[3][17], root[40];
    const char *ret = "~";
    if (v->run != run) {
      run = v->run;
      memset(made, 0, sizeof made);
      if (!fx_engine(grip_hand_runs[run].missing, strlen(grip_hand_runs[run].missing), &engine)) return fprintf(stderr, "run %s: unknown engine function\n", grip_hand_runs[run].name), 2;
    }
    if (!fx_split(v->line, &l)) return fprintf(stderr, "vector %u: malformed\n", i), 2;
    if (fx_is(&l, FX_CALL, "engine")) {          /* from here on the engine lacks these functions */
      if (!fx_engine(l.at[FX_SNAPSHOT], l.len[FX_SNAPSHOT], &engine)) return fprintf(stderr, "vector %u: unknown engine function\n", i), 2;
      continue;
    }
    if (fx_is(&l, FX_CALL, "solve") || fx_is(&l, FX_CALL, "weight")) {
      FxLine plain = l;
      plain.at[FX_SNAPSHOT] = "~"; plain.len[FX_SNAPSHOT] = 1;
      if (!fx_inputs(&plain, &in)) return fprintf(stderr, "vector %u: malformed\n", i), 2;
      if (fx_is(&l, FX_CALL, "weight")) fx_double_hex(tfp_grip_hand_weight(fx_double(l.at[FX_SNAPSHOT])), answer);
      else {
        const char *p = l.at[FX_SNAPSHOT];
        TfpArmVector3 elbow, reached;
        TfpArmError error;
        TfpGripSolve solved = tfp_grip_hand_solve(&engine, point(p), point(p + 25), point(p + 50), point(p + 75), &elbow, &reached, &error);
        if (solved == TFP_GRIP_SOLVE_RAISED) snprintf(answer, sizeof answer, "!%s", error.text);
        else if (solved == TFP_GRIP_SOLVE_NIL) snprintf(answer, sizeof answer, "~");
        else { point_hex(elbow, answer); answer[24] = ','; point_hex(reached, answer + 25); }
        solves++;
      }
    } else {
      if (!fx_inputs(&l, &in) || in.instance < 1 || in.instance > 2) return fprintf(stderr, "vector %u: malformed\n", i), 2;
      g = &instances[in.instance - 1];
      if (fx_is(&l, FX_CALL, "new")) { tfp_grip_hand_init(g, &fx_backend, &engine); made[in.instance - 1] = true; }
      else if (!made[in.instance - 1]) return fprintf(stderr, "vector %u: no instance\n", i), 2;
      else if (fx_is(&l, FX_CALL, "update")) {
        ret = fx_flag(tfp_grip_hand_update(g, in.has_snapshot ? &in.snapshot : NULL, in.active, in.has_handles ? &in.handles : NULL));
        updates++;
        if (*ret == 'F') raised++;
      }
      else if (fx_is(&l, FX_CALL, "stop")) ret = fx_flag(tfp_grip_hand_stop(g));
      else if (fx_is(&l, FX_CALL, "clean")) ret = fx_flag(tfp_grip_hand_is_clean(g));
      else if (fx_is(&l, FX_CALL, "result")) ret = "T";
      else return fprintf(stderr, "vector %u: unknown call\n", i), 2;
      tfp_grip_hand_result(g, &r);
      double numbers[] = {r.frames, r.scans, r.weapons, r.pieces, r.bones, r.fingers, r.errors, r.steady, r.handled, r.turning, r.sliding, r.swaps,
                          r.recalled, r.partial, r.spun};
      for (int k = 0; k < 15; k++) fx_num(numbers[k], n[k], 40);
      fx_opt(r.has_arm_root, r.arm_root, root, sizeof root);
      snprintf(answer, sizeof answer, "%s;%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s;%s;%s;%s", ret, n[0], n[1], n[2], n[3], n[4],
               n[5], opt(r.has_distance, r.distance, h[0]), opt(r.has_off, r.off, h[1]), n[6], fx_flag(r.rig_screen_hidden), n[7], n[8], n[9],
               n[10], n[11], n[12], n[13], n[14], fx_flag(r.gun), root, fx_flag(r.gun_turned), opt(r.has_right_distance, r.right_distance, h[2]),
               fx_flag(tfp_grip_hand_is_clean(g)), r.reason, r.right ? r.right : "nil");
      if (fx_is(&l, FX_CALL, "update") && !strcmp(r.reason, "active")) left++;
      if (fx_is(&l, FX_CALL, "update") && r.right && !strcmp(r.right, "active")) right++;
    }
    if (!fx_verdict(&l, answer, why, sizeof why)) {
      fprintf(stderr, "grip hand vector %u (run %s, %.*s)\n  %s\n", i, grip_hand_runs[run].name, (int)l.len[FX_CALL], l.at[FX_CALL], why);
      return 1;
    }
    calls += fx.calls; maths += fx.count;
    if (only && i + 1 >= only) break;
  }
  printf("Grip hand: %u operations in %u runs answered as retail's grip_hand.lua (%s): same results, reasons, counters and engine calls; "
         "%u updates, %u with the left arm corrected, %u with the right, %u raised; %u calls of solve; %u engine, backend and clock calls "
         "with their arguments, %u value-maths calls by checksum\n", total, runs, commit, updates, left, right, raised, solves, calls, maths);
  return 0;
}
