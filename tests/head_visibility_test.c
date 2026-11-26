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
 * table under test. Retail: head_visibility.lua:113-142, 393. 25480438: docs/translation/VISIBILITY-TRANSLATION.md "Build data". */
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
/* Beyond retail: tfp_head_watch, what the last call saw of the camera pass (for the log only). Checked here against
 * the scripted world's own record of the calls: every camera pass switched off is one "set ... F default", every mesh
 * found drawn was either switched off or left drawn, and the three cases the "masks" run plays (drawn again from
 * outside, left drawn, put back by a stop) are all counted somewhere. */
static struct { double again, again_head, left, put_back, off; } watched;
static size_t count_in(const char *text, size_t len, const char *what) {
  size_t n = 0, w = strlen(what);
  for (size_t i = 0; text && i + w <= len; i++) if (!memcmp(text + i, what, w)) n++;
  return n;
}
static bool watch_ok(const char *call, bool ok, size_t i) {
  TfpHeadWatch w;
  TfpHeadResult r;
  tfp_head_watch(head, &w);
  tfp_head_result(head, &r);
  size_t off = count_in(vf_ev.vis.data, vf_ev.vis.len, " F default");
  if (w.turned_off != (double)off) { fprintf(stderr, "head watch: operation %zu (%s): %g camera passes switched off, the world saw %zu\n", i, call, w.turned_off, off); return false; }
  if (!strcmp(call, "update") && ok && r.phase == TFP_HEAD_ACTIVE && w.full
      && w.on_new + w.on_again + w.on_still != w.turned_off + w.left_on) {
    fprintf(stderr, "head watch: operation %zu: found drawn %g new + %g again + %g still, but %g switched off + %g left\n", i, w.on_new, w.on_again, w.on_still, w.turned_off, w.left_on);
    return false;
  }
  if (w.on_again_head > w.on_again || w.left_on > w.camera_rows || (strcmp(call, "update") && (w.camera_rows || w.on_new || w.on_again))) {
    fprintf(stderr, "head watch: operation %zu (%s): counts out of range\n", i, call); return false;
  }
  watched.again += w.on_again; watched.again_head += w.on_again_head; watched.left += w.left_on; watched.put_back += w.put_back; watched.off += w.turned_off;
  return true;
}
/* Round 38: an exosuit seat. The replay hands the module these two answers too, always "not a seat": every retail
 * vector must come out the same with them present. seat_cases() turns them on. Neither is a call the scripted world
 * records (the vectors' checksums count every recorded call). */
static bool seat_on; static double cockpit_unit;
static bool seat_answer(void *user, const TfpVisSnapshot *s) { (void)user; (void)s; return seat_on; }
static bool cockpit_answer(void *user, const TfpVisSnapshot *s, double *unit) { (void)user; (void)s; *unit = cockpit_unit; return cockpit_unit > 0; }
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
                        vf_seated_head, seat_answer, cockpit_answer};   /* round 38: never a seat here; nothing changes */
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
    if (!watch_ok(call, ok, i)) { failures = 3; free(line); break; }
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

/* ---- Round 38: an exosuit seat (src/head_visibility.h TfpHeadIo.seat and .cockpit) ----
 * The scripted world of the run "gates" up to its first seated frame in camera kind 4 (the valve: seated, the seated
 * head on): the module hides the head, torso, shoulders and backpack. Then the seat answer is turned on: every one of
 * the 30 appearance slots, the body's own meshes and the backpack lose their camera and shadow passes; off again,
 * every mask is as it was. Then an exosuit unit is put into the world, and its four body meshes (not its shadow mesh)
 * lose only their camera pass while seated, and get it back on leaving, with the option off, on a stop, and never
 * when the unit is not the suit (other resource, other world, gone). Retail layout and table. */
static int seat_checks, seat_failed;
#define SCHECK(c) do { ++seat_checks; if (!(c)) { ++seat_failed; fprintf(stderr, "seat: FAILED line %d: %s\n", __LINE__, #c); } } while (0)
#define REGISTRY_RETAIL 0x1ab76f8
static double registry_field(size_t offset) { return vf_rawptr(vf_rawptr(V(exe) + REGISTRY_RETAIL) + 0x88 + (double)offset); }
static double object_of(double ref) { return vf_rawptr(registry_field(0) + vf_mod(ref, 0x400000) * 8); }
typedef struct SeatMesh { double ref, mesh; int mask; } SeatMesh;
static size_t meshes_of(double ref, SeatMesh *out, size_t room, size_t n) {
  if (ref == 0 || ref == 4294967295.0) return n;
  for (size_t i = 0; i < n; i++) if (out[i].ref == ref) return n;
  double object = object_of(ref);
  if (!object) return n;
  double count = vf_rawu32(object + 0xC8), array = vf_rawptr(object + 0xD0);
  for (double i = 0; i < count && n < room; i++) {
    double mesh = vf_rawptr(array + i * 16);
    out[n++] = (SeatMesh){ref, mesh, vf_byte(mesh + 0xDC)};
  }
  return n;
}
static double appearance_base(void) { return V(s_avatar_owner) + 0x546ab0 + V(s_avatar_index) * 0x1b8 + 0xdc; }
/* Every mesh of the diver: the 30 slots, the body, the backpack. */
static size_t diver_meshes(SeatMesh *out, size_t room) {
  size_t n = 0;
  for (int at = 0; at < 120; at += 4) n = meshes_of(vf_rawu32(appearance_base() + at), out, room, n);
  n = meshes_of(V(s_body), out, room, n);
  if (V(pack_mode) == 1) n = meshes_of(V(pack_ref), out, room, n);
  return n;
}
static bool same_masks(const SeatMesh *a, size_t n) {
  for (size_t i = 0; i < n; i++) if (vf_byte(a[i].mesh + 0xDC) != a[i].mask) { fprintf(stderr, "seat: mesh %.0f of unit %.0f is %d, was %d\n", a[i].mesh, a[i].ref, vf_byte(a[i].mesh + 0xDC), a[i].mask); return false; }
  return true;
}
static bool all_out(const SeatMesh *a, size_t n) { /* no camera pass and no shadow pass left on any of them */
  for (size_t i = 0; i < n; i++) if (vf_byte(a[i].mesh + 0xDC) & 3) { fprintf(stderr, "seat: mesh %.0f of unit %.0f still %d\n", a[i].mesh, a[i].ref, vf_byte(a[i].mesh + 0xDC)); return false; }
  return true;
}
static char *seat_tokens[65536];
static bool seat_step(const char *script, bool stop) {
  char line[512];
  snprintf(line, sizeof line, "%s", script);
  vf_begin(line, seat_tokens, sizeof seat_tokens / sizeof seat_tokens[0]);
  TfpVisText why = {0};
  bool ok = stop ? tfp_head_stop(head, &why) : tfp_head_update(head, &why);
  if (!ok || why.present) fprintf(stderr, "seat: %s answered %d %s\n", stop ? "stop" : "update", ok, why.present ? why.text : "");
  return ok;
}
static size_t set_calls(const char *unit, const char *pass) { /* "set <unit> <index> F <pass>" in the last call */
  char what[96];
  snprintf(what, sizeof what, "set %s ", unit);
  size_t n = 0;
  for (const char *at = vf_ev.vis.data; at && (at = strstr(at, what)); at++) if (!pass || strncmp(strchr(at + strlen(what), ' ') + 3, pass, strlen(pass)) == 0) n++;
  return n;
}
/* The exosuit: a renderer unit with the suit's resource, in the body's world, not linked to anything, with the four
 * body meshes and the shadow mesh the game's combat_walker.unit has, all drawn (255). */
static const uint8_t WALKER[8] = {0xe3, 0x45, 0x5e, 0xda, 0xd2, 0xb3, 0xe4, 0x79};
static const struct { const char *name; double hash; } WALKER_MESHES[5] = {
  {"g_combat_walker", 0x3f684f2a}, {"g_combat_walker_LOD1", 0x370e4756}, {"g_combat_walker_LOD2", 0x7231cf3d}, {"g_combat_walker_LOD3", 0x8ccfaaa2},
  {"g_combat_walker_shadow", 0x0940e5cb}};
#define WALKER_OBJECT 70368744177664.0   /* 0x400000000000: memory no script uses */
static double walker_ref, walker_mesh[5], walker_index;
static void poke(double at, const void *bytes, size_t n) { for (size_t i = 0; i < n; i++) vf_poke_byte(at + (double)i, ((const uint8_t *)bytes)[i]); }
static void poke64(double at, uint64_t v) { poke(at, &v, 8); }
static void poke32(double at, uint32_t v) { poke(at, &v, 4); }
static void make_walker(void) {
  const double capacity = vf_rawu32(vf_rawptr(V(exe) + REGISTRY_RETAIL) + 0x88 + 16);
  walker_index = capacity - 7;
  walker_ref = walker_index + 0x400000;   /* generation 1 */
  const double body = object_of(V(s_body));
  for (int i = 0; i < 0x400; i++) vf_poke_byte(WALKER_OBJECT + i, vf_byte(body + i));   /* the renderer type and world of the body */
  poke32(WALKER_OBJECT + 8, (uint32_t)walker_ref);
  poke64(WALKER_OBJECT + 0x1D0, 0);
  poke(WALKER_OBJECT + 0x358, WALKER, 8);
  poke32(WALKER_OBJECT + 0xC8, 5);
  poke64(WALKER_OBJECT + 0xD0, (uint64_t)(WALKER_OBJECT + 0x1000));
  for (int i = 0; i < 5; i++) {
    walker_mesh[i] = WALKER_OBJECT + 0x2000 + i * 0x100;
    poke64(WALKER_OBJECT + 0x1000 + i * 16, (uint64_t)walker_mesh[i]);
    vf_poke_byte(walker_mesh[i] + 0xDC, 255);
    char token[160];
    snprintf(token, sizeof token, "M%s,%.0f,%.0f,%d,1,0,%.0f", WALKER_MESHES[i].name, walker_mesh[i], WALKER_MESHES[i].hash, 200 + i, walker_ref * 4);
    char *one[1] = {token};
    vf_apply(one, 1);
  }
  vf_poke_byte(registry_field(24) + walker_index, 1);
  poke64(registry_field(0) + walker_index * 8, (uint64_t)WALKER_OBJECT);
  vf.units = realloc(vf.units, (vf.unit_count + 1) * sizeof *vf.units);
  vf.units[vf.unit_count++] = walker_ref * 4;
}
static int walker_mask(int i) { return vf_byte(walker_mesh[i] + 0xDC); }
static bool walker_drawn(void) { for (int i = 0; i < 5; i++) if (walker_mask(i) != 255) return false; return true; }
static bool walker_seen_through(void) { for (int i = 0; i < 4; i++) if (walker_mask(i) != 254) return false; return walker_mask(4) == 255; }

/* Round 38: once the first of the suit's meshes is written and read back, the suit's resource is changed: the write
 * arms it (flip_walker), the read of that mesh's mask that follows it does it (flip_read). */
static bool flip_after_walker, flip_armed; static int flipped;
static void flip_walker(double unit) { if (flip_after_walker && unit == walker_ref * 4) { flip_after_walker = false; flip_armed = true; } }
static bool flip_read(void *user, double at, size_t n, uint8_t *out, TfpVisText *why) {
  const bool ok = vf_read(user, at, n, out, why);
  for (int i = 0; flip_armed && i < 4; i++) if (at == walker_mesh[i] + 0xDC) { flip_armed = false; flipped += 1; vf_poke_byte(WALKER_OBJECT + 0x358, 0); }
  return ok;
}
static bool seat_step_why(const char *script, TfpVisText *why) {
  char line[512];
  snprintf(line, sizeof line, "%s", script);
  vf_begin(line, seat_tokens, sizeof seat_tokens / sizeof seat_tokens[0]);
  return tfp_head_update(head, why);
}
static bool prepare(const char *name, size_t last) {
  size_t count = sizeof head_visibility_vectors / sizeof head_visibility_vectors[0];
  int run = -1;
  VfOptions options;
  VfBuf out = {0};
  layout_25480438(false);
  for (size_t i = 0; i < count && i <= last; i++) {
    const HeadVisibilityVector *v = &head_visibility_vectors[i];
    if (strcmp(head_visibility_runs[v->run].name, name)) continue;
    if (v->run != run) {
      run = v->run;
      hio = (TfpHeadIo){NULL, vf_read, vf_ptr, vf_alignment_executable, vf_player_snapshot, vf_pin_player_snapshot, vf_backpack,
                        vf_action_state, vf_on_foot, vf_foreign_camera, vf_userdata_address, vf_clock, vf_log, vf_controller_result,
                        vf_controller_is_enabled, vf_fallback_ready, vf_pinned_ready, vf_full_body, vf_hide_torso, vf_show_torso,
                        vf_seated_head, seat_answer, cockpit_answer};
      vf_start_run(head_visibility_runs[run].header, &options);
      if (options.nolog) hio.log = NULL;
      tfp_head_free(head);
      tfp_model_shadow_free(&model);
      tfp_model_shadow_init(&model, &mio, TFP_VIS_APPEARANCE_RETAIL);
      head = tfp_head_new(&hio, options.sr ? &engine : NULL, options.models ? &model : NULL, &tfp_head_build_retail);
    }
    char *line = malloc(strlen(v->script) + 1);
    strcpy(line, v->script);
    char *call = vf_begin(line, seat_tokens, sizeof seat_tokens / sizeof seat_tokens[0]);
    TfpVisText why = {0};
    bool ok = !strcmp(call, "update") ? tfp_head_update(head, &why) : !strcmp(call, "stop") ? tfp_head_stop(head, &why) : tfp_head_shutdown(head, &why);
    vf_clear(&out);
    vf_printf(&out, "%s ret=%s,", call, ok ? "T" : "F");
    text(&out, &why);
    head_state(&out);
    vf_events(&out);
    int differs = vf_compare("head visibility (seat preparation)", i, name, v->script, v->expect, &out);
    free(line);
    if (differs) { free(out.data); return false; }
  }
  free(out.data);
  return true;
}
static bool seat_cases(void) {
  if (!prepare("gates", 318)) return false;
  TfpHeadResult r0, r;
  tfp_head_result(head, &r0);
  SCHECK(r0.phase == TFP_HEAD_ACTIVE && r0.hidden == 8 && r0.hidden_head == 1 && r0.hidden_torso == 1 && r0.hidden_shoulders == 5 && r0.hidden_backpack == 1);
  SCHECK(r0.hidden_limbs == 0 && r0.hidden_cockpit == 0 && !r0.seat);
  static SeatMesh diver[512];
  const size_t n = diver_meshes(diver, 512);
  SCHECK(n >= 10);
  /* The valve, again: a seated frame that is no seat changes nothing. */
  SCHECK(seat_step("Vtick=103700 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden == r0.hidden && r.writes == r0.writes && same_masks(diver, n));

  /* 1. In the seat: everything of the diver goes, camera and shadow. */
  seat_on = true;
  SCHECK(seat_step("Vtick=103720 !update", false));
  tfp_head_result(head, &r);
  SCHECK(r.phase == TFP_HEAD_ACTIVE && r.seat && r.hidden_head >= 1 && r.hidden_torso >= 1 && r.hidden_limbs >= 5 && r.hidden_backpack == 1 && r.hidden_body >= 1);
  SCHECK(all_out(diver, n));
  SCHECK(set_calls("16777224", NULL) == 2);   /* the rig's two shoulder pads given back: the seat does not own the rig */
  const double writes = r.writes;
  SCHECK(seat_step("Vtick=103740 !update", false));
  tfp_head_result(head, &r); SCHECK(r.writes == writes && all_out(diver, n));   /* held: nothing written again */
  /* the glide out of the seat: the diver is no longer seated, the seat answer still yes: the diver stays hidden */
  SCHECK(seat_step("Vs_seated=0 Vtick=103750 !update", false));
  tfp_head_result(head, &r); SCHECK(r.phase == TFP_HEAD_ACTIVE && r.seat && all_out(diver, n));
  /* the glide out running in the native view (camera kind 6, the round 38 check of 2026-11-25): the eye still up to
   * 3 m away in the cockpit, so the diver (and the first-person rig's clothing) stays hidden whatever the camera kind */
  SCHECK(seat_step("Vs_kind=6 Vtick=103751 !update", false));
  tfp_head_result(head, &r); SCHECK(r.phase == TFP_HEAD_ACTIVE && r.seat && all_out(diver, n));
  SCHECK(seat_step("Vs_kind=4 Vtick=103752 !update", false));
  tfp_head_result(head, &r); SCHECK(r.phase == TFP_HEAD_ACTIVE && r.seat && all_out(diver, n));
  SCHECK(seat_step("Vs_seated=1 Vtick=103755 !update", false));
  /* the seated head option off: the seat hides all the same */
  SCHECK(seat_step("Vseated_head=0 Vtick=103760 !update", false));
  tfp_head_result(head, &r); SCHECK(r.phase == TFP_HEAD_ACTIVE && r.seat && all_out(diver, n));

  /* 2. Out of the seat (the valve's frame again): the ordinary set, every mask as it was. */
  seat_on = false;
  SCHECK(seat_step("Vseated_head=1 Vtick=103780 !update", false));
  tfp_head_result(head, &r);
  SCHECK(r.phase == TFP_HEAD_ACTIVE && !r.seat && r.hidden == 8 && r.hidden_head == 1 && r.hidden_torso == 1 && r.hidden_shoulders == 5 && r.hidden_limbs == 0);
  SCHECK(same_masks(diver, n));

  /* 3. Show torso on: drawn in the ordinary view once steady, hidden in the seat, drawn again after it. */
  SCHECK(seat_step("Vshow_torso=1 Vtick=103800 !update", false));
  SCHECK(seat_step("Vtick=104600 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden_torso == 0 && r.hidden_head == 1);
  seat_on = true;
  SCHECK(seat_step("Vtick=104620 !update", false));
  tfp_head_result(head, &r); SCHECK(r.seat && r.hidden_torso >= 1 && all_out(diver, n));
  seat_on = false;
  SCHECK(seat_step("Vtick=104640 !update", false));
  tfp_head_result(head, &r); SCHECK(!r.seat && r.hidden_torso == 0 && r.hidden_head == 1 && r.hidden_limbs == 0);
  SCHECK(seat_step("Vshow_torso=0 Vtick=104660 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden == 8 && same_masks(diver, n));

  /* 4. The exosuit's cockpit seen through: its four body meshes lose the camera pass only, the shadow mesh stays. */
  make_walker();
  char walker[32];
  snprintf(walker, sizeof walker, "%.0f", walker_ref * 4);
  cockpit_unit = walker_ref;
  SCHECK(seat_step("Vtick=104680 !update", false));   /* not seated: the suit is not asked about */
  tfp_head_result(head, &r); SCHECK(!r.seat && r.hidden_cockpit == 0 && walker_drawn() && set_calls(walker, NULL) == 0);
  seat_on = true;
  SCHECK(seat_step("Vtick=104700 !update", false));
  tfp_head_result(head, &r);
  SCHECK(r.seat && r.hidden_cockpit == 4 && r.cockpit_unit == walker_ref && walker_seen_through() && all_out(diver, n));
  SCHECK(set_calls(walker, "default") == 4 && set_calls(walker, "shadow_caster") == 0);
  SCHECK(seat_step("Vtick=104720 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden_cockpit == 4 && walker_seen_through() && set_calls(walker, NULL) == 0);
  /* the option off in the seat: the suit drawn again, the diver still hidden */
  cockpit_unit = 0;
  SCHECK(seat_step("Vtick=104740 !update", false));
  tfp_head_result(head, &r); SCHECK(r.seat && r.hidden_cockpit == 0 && walker_drawn() && all_out(diver, n));
  cockpit_unit = walker_ref;
  SCHECK(seat_step("Vtick=104760 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden_cockpit == 4 && walker_seen_through());
  /* leaving the seat: the suit drawn, the diver as before */
  seat_on = false;
  SCHECK(seat_step("Vtick=104780 !update", false));
  tfp_head_result(head, &r); SCHECK(!r.seat && r.hidden_cockpit == 0 && walker_drawn() && r.hidden == 8 && same_masks(diver, n));
  /* The suit stops being the suit between the look and the hides (its resource gone after its first mesh was written,
   * the check before each hide, beyond retail): refused as an owner that changed before the hide, not left to the
   * mesh lookup after it; nothing more of the suit is written. */
  flip_after_walker = true; vf_after_set = flip_walker; hio.read = flip_read;
  seat_on = true;
  TfpVisText why = {0};
  SCHECK(seat_step_why("Vtick=104790 !update", &why));
  vf_after_set = NULL; hio.read = vf_read;
  tfp_head_result(head, &r);
  SCHECK(flipped == 1 && why.present && strstr(why.text, "Head owner changed before hide") && set_calls(walker, "default") == 1 && r.hidden_cockpit == 0);
  if (!(why.present && strstr(why.text, "Head owner changed before hide"))) fprintf(stderr, "seat: the suit changed before the hide: %s\n", why.present ? why.text : "(nothing)");
  vf_poke_byte(WALKER_OBJECT + 0x358, WALKER[0]);
  for (int i = 0; i < 5; i++) vf_poke_byte(walker_mesh[i] + 0xDC, 255);
  seat_on = false;
  SCHECK(seat_step("Vtick=104795 !update", false));
  tfp_head_result(head, &r); SCHECK(!r.seat && r.hidden_cockpit == 0 && walker_drawn());
  /* switched off (a stop) in the seat: everything given back */
  seat_on = true;
  SCHECK(seat_step("Vtick=104800 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden_cockpit == 4 && walker_seen_through());
  SCHECK(seat_step("Vtick=104820", true));
  tfp_head_result(head, &r); SCHECK(r.phase == TFP_HEAD_IDLE && r.hidden == 0 && walker_drawn() && tfp_head_is_clean(head));
  SCHECK(seat_step("Vtick=104840 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden_cockpit == 4 && walker_seen_through() && all_out(diver, n));
  /* the suit destroyed in the seat (its reference retired): never written again; the diver stays hidden */
  vf_poke_byte(registry_field(24) + walker_index, 2);
  SCHECK(seat_step("Vtick=104860 !update", false));
  tfp_head_result(head, &r); SCHECK(r.seat && r.hidden_cockpit == 0 && set_calls(walker, NULL) == 0 && all_out(diver, n));
  vf_poke_byte(registry_field(24) + walker_index, 1);
  for (int i = 0; i < 5; i++) vf_poke_byte(walker_mesh[i] + 0xDC, 255);
  SCHECK(seat_step("Vtick=105400 !update", false));   /* a suit that failed is looked at again after half a second */
  tfp_head_result(head, &r); SCHECK(r.hidden_cockpit == 4 && walker_seen_through());
  /* a unit that is not the suit (another resource): not touched, the rows held are let go without a write */
  vf_poke_byte(WALKER_OBJECT + 0x358, 0);
  SCHECK(seat_step("Vtick=105420 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden_cockpit == 0 && set_calls(walker, NULL) == 0 && all_out(diver, n));
  for (int i = 0; i < 5; i++) vf_poke_byte(walker_mesh[i] + 0xDC, 255);
  SCHECK(seat_step("Vtick=105440 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden_cockpit == 0 && walker_drawn() && set_calls(walker, NULL) == 0);
  vf_poke_byte(WALKER_OBJECT + 0x358, WALKER[0]);
  /* in another world: not touched */
  const double world = vf_rawptr(WALKER_OBJECT + 0x18);
  poke64(WALKER_OBJECT + 0x18, (uint64_t)(world + 0x1000));
  SCHECK(seat_step("Vtick=105460 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden_cockpit == 0 && walker_drawn() && set_calls(walker, NULL) == 0 && all_out(diver, n));
  poke64(WALKER_OBJECT + 0x18, (uint64_t)world);
  /* the suit's reference is the body's: refused, the diver as in any seat */
  cockpit_unit = V(s_body);
  SCHECK(seat_step("Vtick=105480 !update", false));
  tfp_head_result(head, &r); SCHECK(r.seat && r.hidden_cockpit == 0 && all_out(diver, n));
  cockpit_unit = walker_ref;
  SCHECK(seat_step("Vtick=106000 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden_cockpit == 4 && walker_seen_through());
  /* the seat lost (dead in it: the seat rule says no): all back */
  seat_on = false;
  SCHECK(seat_step("Vtick=107020 !update", false));
  tfp_head_result(head, &r); SCHECK(!r.seat && r.hidden_cockpit == 0 && walker_drawn() && same_masks(diver, n));
  cockpit_unit = 0;
  tfp_head_free(head); head = NULL;
  tfp_model_shadow_free(&model);
  printf("head visibility: seat: %d checks, %d failed\n", seat_checks, seat_failed);
  return seat_failed == 0;
}
/* ---- Round 43: gear hung on the body in an exosuit's seat (TfpHeadIo.seat) ----
 * feel.58: "exosuit interior, helldiver belt equipment like sample holder is still visible". The seat hid the 30
 * appearance slots, the backpack and the body's own meshes, never the units hung on the body's attach nodes. A fake
 * tree is hung on the body of the run "gates" (its child list: unit +0x1D8 first child, +0x1E0 next sibling, +8 the
 * child's reference; +0x1D0 its parent): a pouch at attach_samplepouch, a pistol with a magazine under it, the
 * torso slot's renderer with an attachment under it, the first-person rig, a unit of a foreign type and the exosuit.
 * In the seat every gear mesh loses its camera and shadow passes; the rig, the foreign unit and the suit are left;
 * on foot nothing of it is touched; leaving, a stop, a unit moved to the rig and a unit gone all give back. */
#define GEAR_BASE 71468255805440.0   /* 0x410000000000: memory no script uses */
typedef struct FakeUnit { double ref, object, index, mesh[4]; int meshes; } FakeUnit;
static double gear_registry_capacity(void) { return vf_rawu32(vf_rawptr(V(exe) + REGISTRY_RETAIL) + 0x88 + 16); }
static FakeUnit fake_unit(int slot, double parent, int meshes) {
  FakeUnit u = {0};
  u.index = gear_registry_capacity() - 20 - slot;
  u.ref = u.index + 0x400000;   /* generation 1 */
  u.object = GEAR_BASE + slot * 0x10000;
  u.meshes = meshes;
  const double body = object_of(V(s_body));
  for (int i = 0; i < 0x400; i++) vf_poke_byte(u.object + i, vf_byte(body + i));   /* the renderer type and world of the body */
  poke32(u.object + 8, (uint32_t)u.ref);
  poke64(u.object + 0x1D0, (uint64_t)parent);
  poke64(u.object + 0x1D8, 0); poke64(u.object + 0x1E0, 0); poke64(u.object + 0x98, 0);
  const uint8_t resource[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, (uint8_t)slot};
  poke(u.object + 0x358, resource, 8);
  poke32(u.object + 0xC8, (uint32_t)meshes);
  poke64(u.object + 0xD0, (uint64_t)(u.object + 0x1000));
  for (int i = 0; i < meshes; i++) {
    u.mesh[i] = u.object + 0x2000 + i * 0x100;
    poke64(u.object + 0x1000 + i * 16, (uint64_t)u.mesh[i]);
    vf_poke_byte(u.mesh[i] + 0xDC, 255);
  }
  vf_poke_byte(registry_field(24) + u.index, 1);
  poke64(registry_field(0) + u.index * 8, (uint64_t)u.object);
  vf.units = realloc(vf.units, (vf.unit_count + 1) * sizeof *vf.units);
  vf.units[vf.unit_count++] = u.ref * 4;
  return u;
}
/* Hangs `child` last in the child list of `parent`, at node `node` of the parent (link kind 2). */
static void hang(double parent, double child, uint32_t node) {
  poke64(child + 0x1D0, (uint64_t)parent);
  poke64(child + 0x1E0, 0);
  poke64(child + 0x98, (uint64_t)(child + 0x3000));
  poke32(child + 0x3000, (node << 16) | 2);
  double at = parent + 0x1D8;
  while (vf_rawptr(at)) at = vf_rawptr(at) + 0x1E0;
  poke64(at, (uint64_t)child);
}
static void unhang(double parent, double child) {
  double at = parent + 0x1D8;
  while (vf_rawptr(at) && vf_rawptr(at) != child) at = vf_rawptr(at) + 0x1E0;
  if (vf_rawptr(at) == child) poke64(at, (uint64_t)vf_rawptr(child + 0x1E0));
  poke64(child + 0x1E0, 0);
}
static bool unit_masks(const FakeUnit *u, int mask) {
  for (int i = 0; i < u->meshes; i++) if (vf_byte(u->mesh[i] + 0xDC) != mask) {
    fprintf(stderr, "gear: mesh %d of unit %.0f is %d, wanted %d\n", i, u->ref, vf_byte(u->mesh[i] + 0xDC), mask);
    return false;
  }
  return true;
}
static size_t unit_writes(const FakeUnit *u) { char name[32]; snprintf(name, sizeof name, "%.0f", u->ref * 4); return set_calls(name, NULL); }
static bool logged(const char *text) { return vf_ev.vis.data && strstr(vf_ev.vis.data, text); }
static size_t logged_count(const char *text) { size_t n = 0; for (const char *at = vf_ev.vis.data; at && (at = strstr(at, text)); at++) n++; return n; }
static bool gear_cases(void) {
  if (!prepare("gates", 318)) return false;
  const int before = seat_checks - seat_failed;
  TfpHeadResult r0, r;
  TfpVisText why = {0};
  tfp_head_result(head, &r0);
  static SeatMesh diver[512];
  const size_t n = diver_meshes(diver, 512);
  const double body = object_of(V(s_body)), rig = object_of(V(s_viewmodel));
  const double torso_ref = vf_rawu32(appearance_base() + 2 * 4), torso = object_of(torso_ref);
  SCHECK(vf_rawptr(body + 0x1D8) == 0 && rig && torso && vf_rawptr(torso + 0x1D0) == body);
  /* the body's node names: node 37 is attach_samplepouch (0x000922d7), node 12 the pistol's on the right thigh */
  poke32(body + 0x70, 100); poke64(body + 0xA0, (uint64_t)(GEAR_BASE + 0x200000));
  poke32(GEAR_BASE + 0x200000 + 37 * 4, 0x000922d7); poke32(GEAR_BASE + 0x200000 + 12 * 4, 0x5157a11e);
  FakeUnit pouch = fake_unit(0, body, 2), pistol = fake_unit(1, body, 1), magazine = fake_unit(2, pistol.object, 1);
  FakeUnit attachment = fake_unit(3, torso, 1), odd = fake_unit(4, body, 1);
  hang(body, pouch.object, 37); hang(body, pistol.object, 12); hang(pistol.object, magazine.object, 0);
  hang(body, torso, 2); hang(torso, attachment.object, 5); hang(body, rig, 50); hang(body, odd.object, 9);
  /* the odd one is of another type: its method table +0xC8 holds other functions */
  poke64(odd.object, (uint64_t)(GEAR_BASE + 0x300000));
  for (int i = 0; i < 0x100; i++) vf_poke_byte(GEAR_BASE + 0x300000 + i, 0x41);
  make_walker();
  hang(body, WALKER_OBJECT, 0);   /* the exosuit, were it ever hung on the body, is never gear */
  const FakeUnit *gear[] = {&pouch, &pistol, &magazine, &attachment};

  /* 1. On foot: the tree is there, nothing of it is read into the hide list or written. */
  SCHECK(seat_step("Vtick=103700 !update", false));
  tfp_head_result(head, &r);
  SCHECK(r.phase == TFP_HEAD_ACTIVE && !r.seat && r.hidden == r0.hidden && r.hidden_gear == 0 && r.writes == r0.writes);
  for (size_t i = 0; i < 4; i++) SCHECK(unit_masks(gear[i], 255) && unit_writes(gear[i]) == 0);
  SCHECK(!logged("seat gear"));

  /* 2. In the seat, the cockpit option off: every gear mesh goes (camera and shadow), the rest is left; the log lists
   * every unit under the body once, with its node, why it was or was not taken, and how many of its rows are hidden. */
  seat_on = true; cockpit_unit = 0;
  SCHECK(seat_step_why("Vtick=103720 !update", &why) && !why.present);
  tfp_head_result(head, &r);
  SCHECK(r.phase == TFP_HEAD_ACTIVE && r.seat && r.hidden_gear == 5 && all_out(diver, n));
  for (size_t i = 0; i < 4; i++) SCHECK(unit_masks(gear[i], 252));
  SCHECK(unit_masks(&odd, 255) && unit_writes(&odd) == 0 && walker_drawn());
  SCHECK(logged("seat gear: 8 units under the body, 4 hidden as gear"));
  SCHECK(logged("node 37 (hash 0x000922d7)") && logged("node 12 (hash 0x5157a11e)"));
  SCHECK(logged("resource 0x0070605040302010") && logged("2 rows hidden: gear"));
  SCHECK(logged("already hidden: appearance slot") && logged("left: the first-person rig") && logged("left: not a renderer unit")
         && logged("left: the exosuit"));
  SCHECK(logged_count("log:FPV head visibility: seat gear: unit ") == 8);
  if (!logged("seat gear: 8 units") || getenv("GEAR_LOG")) fprintf(stderr, "gear: the log was: %s\n", vf_ev.vis.data ? vf_ev.vis.data : "");
  /* held: nothing written or logged again */
  SCHECK(seat_step("Vtick=103740 !update", false));
  tfp_head_result(head, &r);
  SCHECK(r.hidden_gear == 5 && !logged("seat gear"));
  for (size_t i = 0; i < 4; i++) SCHECK(unit_masks(gear[i], 252) && unit_writes(gear[i]) == 0);

  /* 3. The cockpit seen through: the suit loses its camera pass, the gear stays hidden; off again, the same. */
  cockpit_unit = walker_ref;
  SCHECK(seat_step("Vtick=103760 !update", false));
  tfp_head_result(head, &r);
  SCHECK(r.hidden_cockpit == 4 && walker_seen_through() && r.hidden_gear == 5 && all_out(diver, n));
  for (size_t i = 0; i < 4; i++) SCHECK(unit_masks(gear[i], 252));
  cockpit_unit = 0;
  SCHECK(seat_step("Vtick=103780 !update", false));
  tfp_head_result(head, &r);
  SCHECK(r.hidden_cockpit == 0 && walker_drawn() && r.hidden_gear == 5);

  /* 4. The magazine gone while seated (its reference retired): never written, no check fails, the rest stays hidden. */
  vf_poke_byte(registry_field(24) + magazine.index, 2);
  const double retired = r.retired;
  SCHECK(seat_step_why("Vtick=103800 !update", &why) && !why.present);
  tfp_head_result(head, &r);
  SCHECK(r.phase == TFP_HEAD_ACTIVE && r.hidden_gear == 4 && r.retired == retired + 1 && unit_writes(&magazine) == 0);
  SCHECK(unit_masks(&pouch, 252) && unit_masks(&pistol, 252) && unit_masks(&attachment, 252));
  SCHECK(logged("seat gear: 8 units under the body, 3 hidden as gear") && logged("left: reference not current"));
  vf_poke_byte(registry_field(24) + magazine.index, 1);
  vf_poke_byte(magazine.mesh[0] + 0xDC, 255);   /* a new magazine, drawn */
  SCHECK(seat_step("Vtick=103820 !update", false));
  tfp_head_result(head, &r);
  SCHECK(r.hidden_gear == 5 && unit_masks(&magazine, 252));

  /* 5. The pistol (and its magazine) moved to the first-person rig while still in the seat: both given back at once,
   * never left hidden on the rig; then the seat left with the pistol there: nothing of it written again. */
  unhang(body, pistol.object); hang(rig, pistol.object, 8);
  SCHECK(seat_step_why("Vtick=103840 !update", &why) && !why.present);
  tfp_head_result(head, &r);
  SCHECK(r.phase == TFP_HEAD_ACTIVE && r.seat && r.hidden_gear == 3 && unit_masks(&pistol, 255) && unit_masks(&magazine, 255));
  SCHECK(unit_masks(&pouch, 252) && unit_masks(&attachment, 252) && all_out(diver, n));
  seat_on = false;
  SCHECK(seat_step_why("Vtick=103860 !update", &why) && !why.present);
  tfp_head_result(head, &r);
  SCHECK(!r.seat && r.hidden_gear == 0 && r.hidden == r0.hidden && same_masks(diver, n));
  for (size_t i = 0; i < 4; i++) SCHECK(unit_masks(gear[i], 255));
  SCHECK(unit_writes(&pistol) == 0 && unit_writes(&magazine) == 0);

  /* 6. Back on the body and into the seat again: hidden, and listed again (a new seat entry). Leaving: all back. */
  unhang(rig, pistol.object); hang(body, pistol.object, 12);
  seat_on = true;
  SCHECK(seat_step("Vtick=103880 !update", false));
  tfp_head_result(head, &r);
  SCHECK(r.hidden_gear == 5 && logged("seat gear: 8 units under the body, 4 hidden as gear"));
  for (size_t i = 0; i < 4; i++) SCHECK(unit_masks(gear[i], 252));
  seat_on = false;
  SCHECK(seat_step("Vtick=103900 !update", false));
  tfp_head_result(head, &r);
  SCHECK(r.hidden_gear == 0 && r.hidden == r0.hidden && same_masks(diver, n));
  for (size_t i = 0; i < 4; i++) SCHECK(unit_masks(gear[i], 255));

  /* 7. Switched off (a stop) in the seat: everything given back, clean. */
  seat_on = true;
  SCHECK(seat_step("Vtick=103920 !update", false));
  tfp_head_result(head, &r); SCHECK(r.hidden_gear == 5);
  SCHECK(seat_step("Vtick=103940", true));
  tfp_head_result(head, &r);
  SCHECK(r.phase == TFP_HEAD_IDLE && r.hidden == 0 && r.hidden_gear == 0 && tfp_head_is_clean(head));
  for (size_t i = 0; i < 4; i++) SCHECK(unit_masks(gear[i], 255));
  SCHECK(unit_masks(&odd, 255) && walker_drawn());
  seat_on = false; cockpit_unit = 0;
  tfp_head_free(head); head = NULL;
  tfp_model_shadow_free(&model);
  printf("head visibility: seat gear: %d checks passed\n", seat_checks - seat_failed - before);
  return seat_failed == 0;
}
int main(void) {
  if (!agrees_with_project()) {
    fprintf(stderr, "head visibility: tfp_head_build_25480438 disagrees with feature_contract.h, profile.h or its own displacements\n");
    return 1;
  }
  size_t retail = replay(false), here = retail ? replay(true) : 0;
  if (!retail || !here) return 1;
  if (!seat_cases()) return 1;
  if (!gear_cases()) return 1;
  if (!(watched.again > 0 && watched.again_head > 0 && watched.left > 0 && watched.put_back > 0 && watched.off > 0)) {
    fprintf(stderr, "head watch: the replay never counted one of: drawn again %g (head %g), left drawn %g, put back %g, switched off %g\n",
            watched.again, watched.again_head, watched.left, watched.put_back, watched.off);
    return 1;
  }
  printf("head visibility: %zu operations match retail head_visibility.lua (commit %s); %zu of them again with build 25480438's table "
         "on a world laid out as that build\n", retail, HEAD_VISIBILITY_VECTOR_COMMIT, here);
  return 0;
}
