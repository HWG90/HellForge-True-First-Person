#include "model_shadow_source.h"
#include "vectors/model_shadow_source_vectors.h"
#include "visibility_fake.h"

/* Replays tests/vectors/model_shadow_source_vectors.h: every operation the retail
 * model_shadow_source.lua answered under LuaJIT, against src/model_shadow_source.c. */
static TfpModelShadowIo io = {NULL, vf_snapshot, vf_alignment_context, vf_read, vf_ptr, vf_u32, vf_u8, vf_descriptor, vf_foreign_model};
static void vf_rebind(int code, int mode) {
  if (code == VF_DESCRIPTOR) io.descriptor = mode ? vf_descriptor : NULL;
  if (code == VF_FOREIGN) io.foreign_camera = mode ? vf_foreign_model : NULL;
}
static bool fake_inspect(void *context, double id, double expected, double *object, uint8_t header[32], TfpVisText *why) {
  (void)context;
  vf_en(7, 2, id, expected, 0);
  double registry = vf_rawptr(V(exe) + 0x1ab76f8);
  double objects = vf_rawptr(registry + 0x88), generations = vf_rawptr(registry + 0xA0);
  double index = vf_mod(id, 0x400000), generation = vf_mod(floor(id / 0x400000), 256);
  if (vf_byte(generations + index) != generation) return vf_fail(why, "inspect: generation retired");
  *object = vf_rawptr(objects + index * 8);
  if (V(insp_mode) == 1) *object += 8;
  if (expected != *object) return vf_fail(why, "inspect: object changed");
  vf_raw(*object, 32, header);
  return true;
}

static TfpModelShadow model;
static TfpModelRow *rows; static size_t row_count; static bool have_rows;
static uint8_t appearance[120]; static size_t appearance_len; static bool have_appearance;

static void owner_text(VfBuf *b, const TfpModelOwner *o) {
  vf_identity(b, &o->identity);
  vf_printf(b, ",%s,%s,%s,%s,%s,%s,%s,%s", vf_num(o->rig), vf_num(o->rig_object), vf_num(o->weapon), vf_num(o->weapon_object),
            vf_num(o->world), vf_num(o->parent), vf_num(o->unit), vf_num(o->object));
}
static void model_call(const char *call, double a1, double a2, VfBuf *out) {
  static VfBuf scratch, hexed;
  TfpVisSnapshot s;
  uint8_t fresh[120];
  if (!strcmp(call, "probe")) {
    const uint8_t *text; size_t text_len;
    vf_snapshot_table(&s, false);
    vf_custom_appearance(fresh);
    if (!(a1 == 1 && have_appearance)) { memcpy(appearance, fresh, 120); appearance_len = a1 == 2 ? 100 : 120; }
    have_appearance = true;
    if (tfp_model_shadow_probe(&model, &s, appearance, appearance_len, &text, &text_len)) {
      free(rows);
      row_count = model.row_count; have_rows = true;
      rows = malloc((row_count + 1) * sizeof *rows);
      memcpy(rows, model.rows, row_count * sizeof *rows);
      vf_clear(&scratch);
      for (size_t i = 0; i < row_count; i++) {
        const TfpModelRow *row = &rows[i];
        if (i) vf_put(&scratch, ";", 1);
        if (row->rig) vf_printf(&scratch, "%s,shoulder,T,T,~,~,0,~", vf_num(row->unit));
        else { vf_printf(&scratch, "%s,model,~,~,T,%s,0,", vf_num(row->unit), vf_num(row->model_parent)); owner_text(&scratch, &row->owner); }
      }
      vf_put(out, " rows=", 6); vf_part(out, scratch.data ? scratch.data : "", scratch.len);
      vf_clear(&hexed); vf_hex(&hexed, text, text_len);
      vf_put(out, " sig=", 5); vf_part(out, hexed.data ? hexed.data : "", hexed.len);
    } else vf_printf(out, " rows=~ err=[%.*s]", (int)text_len, (const char *)text);
  } else if (!strcmp(call, "unchanged")) {
    TfpVisText why = {0};
    vf_snapshot_table(&s, false);
    vf_custom_appearance(fresh);
    bool stale = a1 == 1 && have_appearance;
    bool ok = tfp_model_shadow_unchanged(&model, &s, stale ? appearance : fresh, stale ? appearance_len : 120,
                                         V(insp_mode) != 2 ? fake_inspect : NULL, NULL, &why);
    if (why.present) vf_printf(out, " unchanged=%s,[%s]", ok ? "T" : "F", why.text);
    else vf_printf(out, " unchanged=%s,~", ok ? "T" : "F");
  } else if (!strcmp(call, "valid")) {
    const TfpModelRow *row = NULL;
    for (size_t i = 0; have_rows && i < row_count; i++) if (rows[i].unit == a1 && !rows[i].rig) row = &rows[i];
    if (!row) vf_put(out, " valid=norow", 12);
    else {
      TfpModelOwner owner = row->owner;
      double *fields[] = {NULL, NULL, &owner.rig, &owner.rig_object, &owner.weapon, &owner.weapon_object, &owner.world, &owner.parent,
                          &owner.unit, &owner.object};
      if (a2 == 1) owner.identity.bytes[owner.identity.len++] = '!';
      else if (a2 >= 2 && a2 <= 9) *fields[(int)a2] += 8;
      vf_snapshot_table(&s, false);
      vf_printf(out, " valid=%s", tfp_model_shadow_valid(&model, &s, &owner) ? "T" : "F");
    }
  } else if (strcmp(call, "has")) { fprintf(stderr, "unknown call %s\n", call); exit(2); }
  vf_printf(out, " has=%s", tfp_model_shadow_has_observation(&model) ? "T" : "F");
  if (model.has_fingerprint) {
    vf_clear(&scratch);
    vf_identity(&scratch, &model.fingerprint.owner);
    vf_printf(&scratch, ",%s,%s,%s,%s,%s,%s,", vf_num(model.fingerprint.body), vf_num(model.fingerprint.rig), vf_num(model.fingerprint.weapon),
              vf_num(model.fingerprint.weapon_unit), vf_num(model.fingerprint.record), vf_num(model.fingerprint.world));
    vf_hex(&scratch, model.fingerprint.descriptor, 20);
    vf_printf(&scratch, ",%s,", vf_num(model.fingerprint.component));
    vf_hex(&scratch, model.fingerprint.appearance, model.fingerprint.appearance_len);
    vf_put(&scratch, " ", 1);
    for (size_t i = 0; i < model.fingerprint.inspected_count; i++) {
      const TfpModelInspected *row = &model.fingerprint.inspected[i];
      vf_printf(&scratch, "%s%s:%s,%s,%s,", i ? ";" : "", vf_num(row->id), vf_num(row->object), row->has_parent ? vf_num(row->parent) : "~",
                vf_num(row->world));
      if (row->has_links) vf_hex(&scratch, row->links, 16); else vf_put(&scratch, "~", 1);
    }
    vf_put(out, " fp=", 4); vf_part(out, scratch.data, scratch.len);
  }
}

/* here false: the retail layout. here true: the appearance rows at build 25480438's place (visibility_fake.h). */
static size_t replay(bool here) {
  size_t count = sizeof model_shadow_source_vectors / sizeof model_shadow_source_vectors[0], failures = 0;
  const char *dump = getenv("VIS_DUMP");
  int run = -1;
  VfOptions options;
  VfBuf out = {0};
  static char *tokens[65536];
  memset(&vf_layout, 0, sizeof vf_layout);
  vf_layout.on = here; vf_layout.appearance_here = TFP_VIS_APPEARANCE_25480438; vf_layout.appearance_retail = TFP_VIS_APPEARANCE_RETAIL;
  for (size_t i = 0; i < count; i++) {
    const ModelShadowSourceVector *v = &model_shadow_source_vectors[i];
    if (v->run != run) {
      run = v->run;
      vf_start_run(model_shadow_source_runs[run].header, &options);
      tfp_model_shadow_free(&model);
      tfp_model_shadow_init(&model, &io, here ? TFP_VIS_APPEARANCE_25480438 : TFP_VIS_APPEARANCE_RETAIL);
      have_rows = have_appearance = false;
    }
    char *line = malloc(strlen(v->script) + 1);
    strcpy(line, v->script);
    vf_ev.dump = dump && (size_t)atol(dump) == i;
    char *call = vf_begin(line, tokens, sizeof tokens / sizeof tokens[0]);
    double a1 = 0, a2 = 0;
    char *comma = strchr(call, ',');
    if (comma) { *comma = 0; a1 = strtod(comma + 1, &comma); if (*comma == ',') a2 = strtod(comma + 1, NULL); }
    vf_clear(&out);
    vf_put(&out, call, strlen(call));
    model_call(call, a1, a2, &out);
    vf_events(&out);
    failures += (size_t)vf_compare(here ? "model shadow source (build 25480438 layout)" : "model shadow source", i, model_shadow_source_runs[run].name, v->script, v->expect, &out);
    free(line);
    if (failures >= 5) break;
  }
  tfp_model_shadow_free(&model);
  free(out.data);
  return failures ? 0 : count;
}
int main(void) {
  size_t retail = replay(false), here = retail ? replay(true) : 0;
  if (!retail || !here) return 1;
  printf("model shadow source: %zu operations match retail model_shadow_source.lua (commit %s); the same %zu with the appearance rows where "
         "build 25480438 has them\n", retail, MODEL_SHADOW_SOURCE_VECTOR_COMMIT, here);
  return 0;
}
