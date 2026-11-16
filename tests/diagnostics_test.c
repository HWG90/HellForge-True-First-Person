/* Replays tests/vectors/diagnostics_vectors.h (the retail diagnostics.lua's own answers) against
 * src/diagnostics.c. The sample is the world: slot f_<field> holds that field's value; the text
 * "<table>" stands for a Lua table, as in tools/retail_ref/run_diagnostics.lua. */
#include "diagnostics.h"
#include "effects_fx.h"
#include "vectors/diagnostics_vectors.h"

_Static_assert((int)W_COUNT == (int)TFP_DIAGNOSTICS_FIELDS, "one world slot per field");
static TfpDiagnostics d;
static bool live;

static bool is_table(FxValue v) { return v.t == 's' && v.len == 7 && memcmp(v.s, "<table>", 7) == 0; }
static TfpDiagnosticsValue value(FxValue v) {
  TfpDiagnosticsValue out = {TFP_DIAGNOSTICS_NIL, false, 0, NULL, 0};
  if (v.t == 'f' || v.t == 't') { out.kind = TFP_DIAGNOSTICS_BOOLEAN; out.boolean = v.t == 't'; }
  else if (v.t == 'd') { out.kind = TFP_DIAGNOSTICS_NUMBER; out.number = v.n; }
  else if (is_table(v)) out.kind = TFP_DIAGNOSTICS_OTHER;
  else if (v.t == 's') { out.kind = TFP_DIAGNOSTICS_STRING; out.text = v.s; out.length = (size_t)v.len; }
  return out;
}
/* The sample argument: true = the world's fields, "<table>" = an empty table, anything else = not a table. */
static const TfpDiagnosticsSample *sample(FxValue pick, TfpDiagnosticsSample *storage) {
  memset(storage, 0, sizeof *storage);
  if (pick.t == 't') {
    for (int i = 0; i < TFP_DIAGNOSTICS_FIELDS; i++) {
      if (strcmp(diagnostics_slots[i] + 2, tfp_diagnostics_field_name((TfpDiagnosticsField)i)) != 0) { fprintf(stderr, "field order\n"); exit(2); }
      storage->field[i] = value(fx_world[i]);
    }
    return storage;
  }
  return is_table(pick) ? storage : NULL;
}
static const char *show(TfpDiagnosticsValue v) {
  switch (v.kind) {
    case TFP_DIAGNOSTICS_BOOLEAN: return fx_bool(v.boolean);
    case TFP_DIAGNOSTICS_NUMBER: return fx_num(v.number);
    case TFP_DIAGNOSTICS_STRING: return fx_strn(v.text, v.length);
    default: return "~";
  }
}
static void put_row(const TfpDiagnosticsRow *row) {
  fx_put("{%s", fx_num(row->frame));
  for (int i = 0; i < TFP_DIAGNOSTICS_FIELDS; i++) {
    TfpDiagnosticsValue v = tfp_diagnostics_row_value(row, (TfpDiagnosticsField)i);
    if (v.kind != TFP_DIAGNOSTICS_NIL) fx_put(" %s=%s", tfp_diagnostics_field_name((TfpDiagnosticsField)i), show(v));
  }
  fx_put("}");
}
static void fresh(void) {
  if (live) tfp_diagnostics_free(&d);
  live = false;
}
static void dispatch(const FxOp *op) {
  const char *call = op->call, *error = "(unset)";
  TfpDiagnosticsSample storage;
  const TfpDiagnosticsCounters *c;
  if (!strcmp(call, "new")) {
    TfpDiagnostics made;
    memset(&made, 0xA5, sizeof made);                 /* init must not rely on what was there */
    if (!tfp_diagnostics_init(&made, fx_truthy(op->a[0]), value(op->a[1]), &error)) { fx_put("raised %s", fx_str(error)); return; }
    fresh();
    d = made; live = true;
    fx_put("new");
  } else if (!strcmp(call, "record") || !strcmp(call, "event")) {
    TfpDiagnosticsAnswer r = !strcmp(call, "record") ? tfp_diagnostics_record(&d, value(op->a[0]), sample(op->a[1], &storage), &error)
                                                    : tfp_diagnostics_event(&d, value(op->a[0]), value(op->a[1]), sample(op->a[2], &storage), &error);
    if (r == TFP_DIAGNOSTICS_RAISED) fx_put("raised %s", fx_str(error));
    else fx_put("%s", fx_bool(r == TFP_DIAGNOSTICS_TRUE));
    if (r == TFP_DIAGNOSTICS_TRUE) { fx_put(" "); put_row(tfp_diagnostics_row(&d, tfp_diagnostics_count(&d) - 1)); }
  } else if (!strcmp(call, "clear")) { tfp_diagnostics_clear(&d); fx_put("clear"); }
  else if (!strcmp(call, "snapshot")) {
    fx_put("snapshot %u %u ", tfp_diagnostics_capacity(&d), tfp_diagnostics_count(&d));
    for (uint32_t i = 0; i < tfp_diagnostics_count(&d); i++) { if (i) fx_put(" "); put_row(tfp_diagnostics_row(&d, i)); }
    if (tfp_diagnostics_row(&d, tfp_diagnostics_count(&d))) { fprintf(stderr, "a row past the count\n"); exit(2); }
  } else { fprintf(stderr, "unknown call %s\n", call); exit(2); }
  c = tfp_diagnostics_counters(&d);
  fx_put(" | %u %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %s |", tfp_diagnostics_count(&d),
         (unsigned long long)c->frames, (unsigned long long)c->duplicate_frames, (unsigned long long)c->out_of_order_frames,
         (unsigned long long)c->events, (unsigned long long)c->overwritten, (unsigned long long)c->player_changes,
         (unsigned long long)c->weapon_changes, (unsigned long long)c->request_changes, (unsigned long long)c->camera_changes,
         (unsigned long long)c->transition_restarts, (unsigned long long)c->mixed_publication_frames,
         (unsigned long long)c->unexpected_view_frames, (unsigned long long)c->look_duplicates, (unsigned long long)c->cleanup_frames,
         d.has_last_frame ? fx_num(d.last_frame) : "~");
  for (int i = 0; i < TFP_DIAGNOSTICS_KEPT; i++) {
    TfpDiagnosticsValue v = {(TfpDiagnosticsKind)d.previous[i].cell.kind, d.previous[i].cell.boolean, d.previous[i].cell.number,
                             d.previous[i].text, d.previous[i].cell.length};
    fx_put(" %s", d.has_previous ? show(v) : "-");
  }
}
int main(void) {
  int code = fx_main("diagnostics", DIAGNOSTICS_VECTOR_COMMIT, diagnostics_runs, (int)(sizeof diagnostics_runs / sizeof diagnostics_runs[0]),
                     diagnostics_ops, diagnostics_events, diagnostics_slots, W_COUNT, fresh, dispatch);
  fresh();
  return code;
}
