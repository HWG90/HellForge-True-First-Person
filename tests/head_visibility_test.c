#include "head_visibility.h"
#include "feature_contract.h"
#include "profile.h"
#include "vectors/head_visibility_vectors.h"
#include "visibility_fake.h"

/* Replays tests/vectors/head_visibility_vectors.h: every operation the retail head_visibility.lua
 * (with the retail model_shadow_source.lua behind it) answered under LuaJIT, against
 * src/head_visibility.c and src/model_shadow_source.c. */
static TfpEngineIface engine = {NULL, vf_main_world, vf_units, vf_set_mesh_visibility, vf_has_mesh, vf_mesh, vf_mesh_name, vf_mesh_node,
                                vf_mesh_materials, vf_mesh_has_material};
static TfpHeadIo hio;
static TfpModelShadowIo mio = {NULL, vf_snapshot, vf_alignment_context, vf_read, vf_ptr, vf_u32, vf_u8, vf_descriptor, vf_foreign_model};
#define PICK(mode, one, two) ((mode) == 0 ? NULL : (mode) == 1 ? (one) : (two))
static void vf_rebind(int code, int mode) {
  switch (code) {
  case VF_MAIN_WORLD: engine.Application_main_world = PICK(mode, vf_main_world, vf_main_world_2); break;
  case VF_UNITS: engine.World_units = PICK(mode, vf_units, vf_units_2); break;
  case VF_SET: engine.Unit_set_mesh_visibility = PICK(mode, vf_set_mesh_visibility, vf_set_mesh_visibility_2); break;
  case VF_HAS_MESH: engine.Unit_has_mesh = PICK(mode, vf_has_mesh, vf_has_mesh_2); break;
  case VF_MESH: engine.Unit_mesh = PICK(mode, vf_mesh, vf_mesh_2); break;
  case VF_MESH_NAME: engine.Mesh_name = PICK(mode, vf_mesh_name, vf_mesh_name_2); break;
  case VF_MESH_NODE: engine.Mesh_node = PICK(mode, vf_mesh_node, vf_mesh_node_2); break;
  case VF_MESH_MATERIALS: engine.Mesh_num_materials = PICK(mode, vf_mesh_materials, vf_mesh_materials_2); break;
  case VF_MESH_HAS_MATERIAL: engine.Mesh_has_material = PICK(mode, vf_mesh_has_material, vf_mesh_has_material_2); break;
  case VF_PLAYER_SNAPSHOT: hio.player_snapshot = PICK(mode, vf_player_snapshot, vf_player_snapshot_2); break;
  case VF_PIN_SNAPSHOT: hio.pin_player_snapshot = PICK(mode, vf_pin_player_snapshot, vf_pin_player_snapshot_2); break;
  case VF_BACKPACK: hio.backpack = PICK(mode, vf_backpack, vf_backpack_2); break;
  case VF_FOREIGN:
    hio.foreign_camera = PICK(mode, vf_foreign_camera, vf_foreign_camera_2);
    mio.foreign_camera = mode ? vf_foreign_model : NULL;
    break;
  case VF_FALLBACK: hio.fallback_ready = mode ? vf_fallback_ready : NULL; break;
  case VF_PINNED: hio.pinned_ready = mode ? vf_pinned_ready : NULL; break;
  case VF_FULL_BODY: hio.full_body = mode ? vf_full_body : NULL; break;
  case VF_HIDE_TORSO: hio.hide_torso = mode ? vf_hide_torso : NULL; break;
  case VF_SHOW_TORSO: hio.show_torso = mode ? vf_show_torso : NULL; break;
  case VF_SEATED_HEAD: hio.seated_head = mode ? vf_seated_head : NULL; break;
  case VF_DESCRIPTOR: mio.descriptor = mode ? vf_descriptor : NULL; break;
  }
}

static TfpHead *head;
static TfpModelShadow model;
static unsigned long long last_table; static bool had_table; static long changes;

static void text(VfBuf *b, const TfpVisText *t) { if (t->present) vf_printf(b, "[%s]", t->text); else vf_put(b, "~", 1); }
static void head_state(VfBuf *out) {
  static VfBuf scratch;
  static char status[1024];
  TfpHeadResult r;
  tfp_head_result(head, &r);
  vf_printf(out, " res=%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,", tfp_head_phase_name(r.phase), vf_num(r.hidden), vf_num(r.hidden_head),
            vf_num(r.hidden_torso), vf_num(r.hidden_shoulders), vf_num(r.hidden_body), vf_num(r.hidden_backpack), vf_num(r.preserved_partial),
            vf_num(r.writes), vf_num(r.restores), vf_num(r.retired), vf_num(r.foreign_masks), r.cleanup_pending ? "T" : "F");
  text(out, &r.error_message);
  if (r.has_model_shadow_error) vf_printf(out, ",[%.*s]", (int)r.model_shadow_error_len, r.model_shadow_error); else vf_put(out, ",~", 2);
  vf_printf(out, ",%s,%s,%s", vf_num(r.shadows_disabled), vf_num(r.steady_observations), vf_num(r.full_observations));
  tfp_head_status(head, status, sizeof status);
  vf_printf(out, " status=[%s] clean=%s", status, tfp_head_is_clean(head) ? "T" : "F");
  const uint8_t *appearance; TfpVisIdentity owner; double world;
  vf_put(out, " ra=", 4);
  if (tfp_head_render_appearance(head, &appearance, &owner, &world)) {
    vf_clear(&scratch); vf_hex(&scratch, appearance, 120); vf_part(out, scratch.data, scratch.len);
    vf_put(out, ",", 1); vf_identity(out, &owner); vf_printf(out, ",%s", vf_num(world));
  } else vf_put(out, "~,~,~", 5);
  size_t count; unsigned long long table;
  bool has = tfp_head_sdk_handles(head, &count, &table);
  if (has != had_table || (has && table != last_table)) { changes++; had_table = has; last_table = table; }
  vf_put(out, " sh=", 4);
  if (has) {
    vf_clear(&scratch);
    for (size_t i = 0; i < count; i++) {
      TfpHeadHandle handle = tfp_head_sdk_handle(head, i);
      vf_printf(&scratch, "%s%s=%s", i ? "," : "", vf_num(handle.ref), handle.unit ? vf_num(vf_address(handle.unit)) : "F");
    }
    vf_part(out, scratch.data ? scratch.data : "", scratch.len);
  } else vf_put(out, "~", 1);
  vf_printf(out, " chg=%ld", changes);
  TfpHeadDebug d;
  tfp_head_debug(head, &d);
  vf_clear(&scratch);
  for (size_t i = 0; i < d.owned; i++) {
    TfpHeadOwnedRow row;
    tfp_head_debug_owned(head, i, &row);
    vf_printf(&scratch, "%s%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s", i ? ";" : "", row.key, vf_num(row.bits), vf_num(row.before), vf_num(row.written),
              row.has_last ? vf_num(row.last) : "~", vf_num(row.current), row.restoring_default ? "T" : "~", row.shadow_suppressed ? "T" : "F",
              row.shadow_only ? "T" : "F", row.part, row.has_reset_epoch ? vf_num(row.reset_epoch) : "~",
              row.has_reset_epoch ? vf_num(row.reset_bits) : "~");
  }
  vf_put(out, " own=", 5); vf_part(out, scratch.data ? scratch.data : "", scratch.len);
  vf_clear(&scratch);
  for (size_t i = 0; i < d.ignored; i++) {
    const char *key;
    double value = tfp_head_debug_ignored(head, i, &key);
    vf_printf(&scratch, "%s%s=%s", i ? ";" : "", key, vf_num(value));
  }
  vf_put(out, " ign=", 5); vf_part(out, scratch.data ? scratch.data : "", scratch.len);
  vf_printf(out, " st=%d ep=", d.steady);
  if (d.has_epoch) vf_printf(out, "%s,%s", vf_num(d.epoch_serial), vf_num(d.epoch_started)); else vf_put(out, "~", 1);
  vf_put(out, " id=", 4); vf_identity(out, &d.identity);
  vf_printf(out, " us=%s cr=", vf_num(d.update_serial));
  vf_clear(&scratch);
  if (d.has_cache_request) vf_printf(&scratch, "[%s]", d.cache_request); else vf_put(&scratch, "~", 1);
  vf_part(out, scratch.data, scratch.len);
  vf_printf(out, " ct=%s ts=%s", d.has_cache_time ? vf_num(d.cache_time) : "~", d.has_torso_steady_since ? vf_num(d.torso_steady_since) : "~");
}

/* What the two game builds hold, written down a second time: the scripted world must not take its numbers from the
 * table under test. Retail: head_visibility.lua:113-142, 393. 25480438: docs/VISIBILITY-TRANSLATION.md "Build data". */
static const TfpHeadBuild world_retail = {
  0x3f4ca0, {0x48, 0x8b, 0xd1, 0xe9, 0xa8, 0xf8, 0xff, 0xff},
  0x51fe80, {0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x44, 0x0f, 0xb6, 0x89, 0xdc, 0x00, 0x00, 0x00},
  0x3f4c23, {0x3b, 0x3d, 0x97, 0x0e, 0x48, 0x02, 0x75, 0x04, 0xb1, 0x01, 0xeb, 0x36, 0x3b, 0x3d, 0x93, 0x0e, 0x48, 0x02, 0x75, 0x04, 0xb1, 0x02, 0xeb, 0x2a},
  0x51feaa, {0x84, 0xd2, 0x74, 0x05, 0x45, 0x0a, 0xc8, 0xeb, 0x06, 0x41, 0xf6, 0xd0, 0x45, 0x22, 0xc8, 0x48, 0x8b, 0x59, 0x08, 0x44, 0x88, 0x89, 0xdc, 0x00, 0x00, 0x00},
  0x2875ac0, 0xF2760503, 0x2875ac8, 0x3100C3D2,
  0xa06d0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x35, 0x12, 0x70, 0xa1, 0x01},
  0x1ab76f8, 0x28d3e0, 0x293600, 0x546ab0};
static const TfpHeadBuild world_25480438 = {
  0x4128e0, {0x48, 0x8b, 0xd1, 0xe9, 0xe8, 0xfb, 0xff, 0xff},
  0x50f620, {0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x44, 0x0f, 0xb6, 0x89, 0xdc, 0x00, 0x00, 0x00},
  0x412875, {0x3b, 0x3d, 0x35, 0xbd, 0x3b, 0x02, 0x75, 0x04, 0xb1, 0x01, 0xeb, 0x36, 0x3b, 0x3d, 0x41, 0xbd, 0x3b, 0x02, 0x75, 0x04, 0xb1, 0x02, 0xeb, 0x2a},
  0x50f64a, {0x84, 0xd2, 0x74, 0x05, 0x45, 0x0a, 0xc8, 0xeb, 0x06, 0x41, 0xf6, 0xd0, 0x45, 0x22, 0xc8, 0x48, 0x8b, 0x59, 0x08, 0x44, 0x88, 0x89, 0xdc, 0x00, 0x00, 0x00},
  0x27ce5b0, 0xF2760503, 0x27ce5c8, 0x3100C3D2,
  0x9d8c0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x35, 0x1a, 0x28, 0x97, 0x01},
  0x1a100f0, 0x2bd890, 0x2c3990, 0x546a70};
/* The project's own record of this build: the four mesh pins of feature_contract.h and profile.h's registry
 * must be the ones in tfp_head_build_25480438. */
static bool hex_is(const char *hex, const uint8_t *bytes, size_t len) {
  if (strlen(hex) != len * 2) return false;
  for (size_t i = 0; i < len; i++) {
    unsigned value;
    sscanf(hex + 2 * i, "%2x", &value);
    if (value != bytes[i]) return false;
  }
  return true;
}
static bool agrees_with_project(void) {
  const TfpHeadBuild *b = &tfp_head_build_25480438;
  const struct { uint32_t rva; const uint8_t *bytes; size_t len; } pins[] = {{b->binding, b->binding_bytes, 8},
    {b->mask_layout, b->mask_layout_bytes, 14}, {b->context_a, b->context_a_bytes, 24}, {b->context_b, b->context_b_bytes, 26}};
  size_t matched = 0, mesh_sites = 0;
  for (size_t i = 0; i < sizeof fp_feature_sites / sizeof fp_feature_sites[0]; i++) {
    const FpFeatureSite *site = &fp_feature_sites[i];
    if (site->feature != FP_FEATURE_MESH) continue;
    mesh_sites++;
    for (size_t k = 0; k < 4; k++) if (site->exe && site->rva == pins[k].rva && hex_is(site->hex, pins[k].bytes, pins[k].len)) matched++;
  }
  /* The context pin compares with the two ids; the registry function loads the registry pointer. */
  const uint8_t *a = b->context_a_bytes, *r = b->registry_code_bytes;
  uint32_t first = b->context_a + 6 + (uint32_t)(a[2] | a[3] << 8 | a[4] << 16 | (uint32_t)a[5] << 24);
  uint32_t second = b->context_a + 18 + (uint32_t)(a[14] | a[15] << 8 | a[16] << 16 | (uint32_t)a[17] << 24);
  uint32_t loaded = b->registry_code + 22 + (uint32_t)(r[18] | r[19] << 8 | r[20] << 16 | (uint32_t)r[21] << 24);
  return mesh_sites == 4 && matched == 4 && b->unit_registry == FP_REGISTRY && first == b->context_default && second == b->context_shadow
    && loaded == b->unit_registry;
}
/* The world laid out as build 25480438 (visibility_fake.h): every place the two builds differ in. */
static void layout_25480438(bool on) {
  const TfpHeadBuild *a = &world_25480438, *b = &world_retail;
  memset(&vf_layout, 0, sizeof vf_layout);
  vf_layout.on = on;
  vf_layout.site[0] = (VfSite){a->binding, b->binding, 8, a->binding_bytes, b->binding_bytes};
  vf_layout.site[1] = (VfSite){a->mask_layout, b->mask_layout, 14, a->mask_layout_bytes, b->mask_layout_bytes};
  vf_layout.site[2] = (VfSite){a->context_a, b->context_a, 24, a->context_a_bytes, b->context_a_bytes};
  vf_layout.site[3] = (VfSite){a->context_b, b->context_b, 26, a->context_b_bytes, b->context_b_bytes};
  vf_layout.site[4] = (VfSite){a->context_default, b->context_default, 4, NULL, NULL};
  vf_layout.site[5] = (VfSite){a->context_shadow, b->context_shadow, 4, NULL, NULL};
  vf_layout.site[6] = (VfSite){a->registry_code, b->registry_code, 22, a->registry_code_bytes, b->registry_code_bytes};
  vf_layout.site[7] = (VfSite){a->unit_registry, b->unit_registry, 8, NULL, NULL};
  vf_layout.count = 8;
  vf_layout.value_here[0] = a->renderer_method_6; vf_layout.value_retail[0] = b->renderer_method_6;
  vf_layout.value_here[1] = a->renderer_method_0; vf_layout.value_retail[1] = b->renderer_method_0;
  vf_layout.appearance_here = a->appearance; vf_layout.appearance_retail = b->appearance;
}
/* here false: the retail build table on the retail layout. here true: build 25480438's table on its layout. The run
 * "edges" is retail-only: it puts the avatar owner at the last readable addresses, where a row that starts 0x40
 * lower is not the same question. */
static size_t replay(bool here) {
  size_t count = sizeof head_visibility_vectors / sizeof head_visibility_vectors[0], failures = 0, done = 0;
  const char *dump = getenv("VIS_DUMP");
  int run = -1;
  VfOptions options;
  VfBuf out = {0};
  static char *tokens[65536];
  layout_25480438(here);
  for (size_t i = 0; i < count; i++) {
    const HeadVisibilityVector *v = &head_visibility_vectors[i];
    if (here && !strcmp(head_visibility_runs[v->run].name, "edges")) continue;
    done++;
    if (v->run != run) {
      run = v->run;
      hio = (TfpHeadIo){NULL, vf_read, vf_ptr, vf_alignment_executable, vf_player_snapshot, vf_pin_player_snapshot, vf_backpack,
                        vf_action_state, vf_on_foot, vf_foreign_camera, vf_userdata_address, vf_clock, vf_log, vf_controller_result,
                        vf_controller_is_enabled, vf_fallback_ready, vf_pinned_ready, vf_full_body, vf_hide_torso, vf_show_torso,
                        vf_seated_head};
      vf_start_run(head_visibility_runs[run].header, &options);
      if (options.nolog) hio.log = NULL;
      tfp_head_free(head);
      tfp_model_shadow_free(&model);
      tfp_model_shadow_init(&model, &mio, here ? TFP_VIS_APPEARANCE_25480438 : TFP_VIS_APPEARANCE_RETAIL);
      head = tfp_head_new(&hio, options.sr ? &engine : NULL, options.models ? &model : NULL, here ? &tfp_head_build_25480438 : &tfp_head_build_retail);
      had_table = false; changes = 0;
    }
    char *line = malloc(strlen(v->script) + 1);
    strcpy(line, v->script);
    vf_ev.dump = dump && (size_t)atol(dump) == i;
    char *call = vf_begin(line, tokens, sizeof tokens / sizeof tokens[0]);
    TfpVisText why = {0};
    bool ok;
    if (!strcmp(call, "update")) ok = tfp_head_update(head, &why);
    else if (!strcmp(call, "stop")) ok = tfp_head_stop(head, &why);
    else if (!strcmp(call, "shutdown")) ok = tfp_head_shutdown(head, &why);
    else { fprintf(stderr, "unknown call %s\n", call); exit(2); }
    vf_clear(&out);
    vf_printf(&out, "%s ret=%s,", call, ok ? "T" : "F");
    text(&out, &why);
    long n = vf_ev.n; uint64_t h1 = vf_ev.h1, h2 = vf_ev.h2;
    head_state(&out);
    if (n != vf_ev.n || h1 != vf_ev.h1 || h2 != vf_ev.h2) { fprintf(stderr, "head visibility: reading the state made calls\n"); exit(1); }
    vf_events(&out);
    failures += (size_t)vf_compare(here ? "head visibility (build 25480438 table and layout)" : "head visibility", i, head_visibility_runs[run].name, v->script, v->expect, &out);
    free(line);
    if (failures >= 3) break;
  }
  tfp_head_free(head);
  head = NULL;
  tfp_model_shadow_free(&model);
  free(out.data);
  return failures ? 0 : done;
}
int main(void) {
  if (!agrees_with_project()) {
    fprintf(stderr, "head visibility: tfp_head_build_25480438 disagrees with feature_contract.h, profile.h or its own displacements\n");
    return 1;
  }
  size_t retail = replay(false), here = retail ? replay(true) : 0;
  if (!retail || !here) return 1;
  printf("head visibility: %zu operations match retail head_visibility.lua (commit %s); %zu of them again with build 25480438's table "
         "on a world laid out as that build\n", retail, HEAD_VISIBILITY_VECTOR_COMMIT, here);
  return 0;
}
