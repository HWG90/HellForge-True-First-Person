/* Replays tests/vectors/arm_pieces_vectors.h against src/arm_pieces.c: every operation must make the
 * engine calls the retail arm_pieces.lua made (same order, same argument bits), do the same value
 * maths and give the same answer and state. See arms_fx.h. */
#include "arm_pieces.h"
#include "arms_fx.h"
#include "vectors/arm_pieces_vectors.h"

static void number(double x, char *out, size_t size) { fx_num(x, out, size); }

int main(int argc, char **argv) {
  const char *commit = "1d0d453";
  static TfpArmPiecesModule module;
  static TfpArmPieces instances[4];
  static bool made[4];
  TfpArmEngine engine;
  unsigned total = sizeof arm_pieces_vectors / sizeof *arm_pieces_vectors, runs = sizeof arm_pieces_runs / sizeof *arm_pieces_runs;
  unsigned calls = 0, maths = 0, raised = 0, updates = 0, active = 0;
  int run = -1;
  unsigned only = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0;
  if (strcmp(ARM_PIECES_VECTOR_COMMIT, commit) || strlen(ARM_PIECES_VECTOR_SHA256) != 64) {
    fprintf(stderr, "arm pieces vectors were cut from retail %s, expected %s\n", ARM_PIECES_VECTOR_COMMIT, commit);
    return 2;
  }
  tfp_arm_pieces_module_init(&module);       /* the kit loads the module once */
  for (unsigned i = 0; i < total; i++) {
    const ArmsVector *v = &arm_pieces_vectors[i];
    FxLine l;
    static FxInputs in;
    TfpArmPieces *a;
    TfpArmPiecesResult r;
    char answer[700], why[1600], n[5][40];
    const char *ret = "~";
    if (v->run != run) {
      run = v->run;
      memset(made, 0, sizeof made);
      if (!fx_engine(arm_pieces_runs[run].missing, strlen(arm_pieces_runs[run].missing), &engine)) return fprintf(stderr, "run %s: unknown engine function\n", arm_pieces_runs[run].name), 2;
    }
    if (!fx_split(v->line, &l)) return fprintf(stderr, "vector %u: malformed\n", i), 2;
    if (fx_is(&l, FX_CALL, "engine")) {        /* from here on the engine lacks these functions */
      if (!fx_engine(l.at[FX_SNAPSHOT], l.len[FX_SNAPSHOT], &engine)) return fprintf(stderr, "vector %u: unknown engine function\n", i), 2;
      continue;
    }
    if (!fx_inputs(&l, &in) || in.instance < 1 || in.instance > 4) return fprintf(stderr, "vector %u: malformed\n", i), 2;
    a = &instances[in.instance - 1];
    if (fx_is(&l, FX_CALL, "new")) { tfp_arm_pieces_init(a, &module, &fx_backend, &engine); made[in.instance - 1] = true; }
    else if (!made[in.instance - 1]) return fprintf(stderr, "vector %u: no instance\n", i), 2;
    else if (fx_is(&l, FX_CALL, "update")) {
      ret = fx_flag(tfp_arm_pieces_update(a, in.has_snapshot ? &in.snapshot : NULL, in.active, in.has_handles ? &in.handles : NULL));
      updates++;
    }
    else if (fx_is(&l, FX_CALL, "stop")) ret = fx_flag(tfp_arm_pieces_stop(a));
    else if (fx_is(&l, FX_CALL, "clean")) ret = fx_flag(tfp_arm_pieces_is_clean(a));
    else if (fx_is(&l, FX_CALL, "result")) ret = "T";
    else return fprintf(stderr, "vector %u: unknown call\n", i), 2;
    tfp_arm_pieces_result(a, &r);
    number(r.pieces, n[0], 40); number(r.scans, n[1], 40); number(r.links, n[2], 40); number(r.frames, n[3], 40); number(r.errors, n[4], 40);
    snprintf(answer, sizeof answer, "%s;%s,%s,%s,%s,%s,%s;%s;%s", ret, n[0], fx_flag(r.hung), n[1], n[2], n[3], n[4],
             fx_flag(tfp_arm_pieces_is_clean(a)), r.reason);
    if (!fx_verdict(&l, answer, why, sizeof why)) {
      fprintf(stderr, "arm pieces vector %u (run %s, %.*s)\n  %s\n", i, arm_pieces_runs[run].name, (int)l.len[FX_CALL], l.at[FX_CALL], why);
      return 1;
    }
    calls += fx.calls; maths += fx.count;
    if (strcmp(r.reason, "active") == 0 && fx_is(&l, FX_CALL, "update")) active++;
    if (*ret == 'F' && fx_is(&l, FX_CALL, "update")) raised++;
    if (only && i + 1 >= only) break;
  }
  printf("Arm pieces: %u operations in %u runs answered as retail's arm_pieces.lua (%s): same results, reasons, counters and engine calls; "
         "%u updates, %u placing, %u raised; %u engine, backend and clock calls with their arguments, %u value-maths calls by checksum\n",
         total, runs, commit, updates, active, raised, calls, maths);
  return 0;
}
