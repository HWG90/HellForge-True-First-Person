/* Native C read-only research module. Writes a bounded SDK census to this
 * session's logs; calls no discovered SDK function and changes no game state.
 */
#define WIN32_LEAN_AND_MEAN
#include "sdk_inventory.h"
#include "engine.h"
#include "native_observer.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
typedef struct Output {
  FILE *file;
  uintptr_t exe, game;
} Output;
#if defined(TFP_READONLY_SDK_CENSUS)
static uint64_t completed;
static bool observer_attempted;
#endif
bool fp_sdk_inventory_pending(void) {
#if defined(TFP_READONLY_SDK_CENSUS)
  uint64_t generation = eng_state_generation();
  return generation && generation != completed;
#else
  return false;
#endif
}
static bool row(void *user, const char *path, int type, uintptr_t function,
                bool alias) {
  Output *o = user;
  const char *module = "none";
  uintptr_t rva = 0;
  if (function >= o->exe && function - o->exe < 0x39e8000) {
    module = "exe";
    rva = function - o->exe;
  } else if (function >= o->game && function - o->game < 0x4744000) {
    module = "game";
    rva = function - o->game;
  } else if (function)
    module = "other";
  return fprintf(o->file, "%s\t%d\t%s\t%llx\t%llx\t%d\n", path, type, module,
                 (unsigned long long)rva, (unsigned long long)function,
                 alias ? 1 : 0) > 0;
}
void fp_sdk_inventory(const HfApi *hf, HfMod *mod, const FpMemory *memory) {
#if defined(TFP_READONLY_SDK_CENSUS)
  uint64_t generation = eng_state_generation();
  if (!generation || generation == completed || !eng_ready())
    return;
  completed = generation;
  if (hf->size < offsetof(HfApi, logs_folder) + sizeof hf->logs_folder ||
      !hf->logs_folder)
    return;
  const char *folder = hf->logs_folder(mod);
  if (!folder || !*folder)
    return;
  char path[1500];
  wchar_t wide[1500], final[1500];
  int n = snprintf(path, sizeof path, "%s/true-first-person-sdk-g%llu.tsv.part",
                   folder, (unsigned long long)generation);
  if (n <= 0 || (size_t)n >= sizeof path ||
      !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 1500))
    return;
  FILE *file = _wfopen(wide, L"wb");
  if (!file)
    return;
  size_t size = 0;
  Output out = {file, hf->module_base("helldivers2.exe", &size), 0};
  out.game = hf->module_base("game.dll", &size);
  fprintf(file,
          "# Build25480438 read-only raw SDK census; no discovered functions "
          "invoked; VM generation %llu\n",
          (unsigned long long)generation);
  fprintf(file, "path\tlua_type\tmodule\trva_hex\taddress_hex\ttable_alias\n");
  EngSdkCensus stats;
  bool ok = eng_sdk_census(row, &out, 32768, &stats);
  uintptr_t root = 0, api = 0, target = 0;
  if (fp_pointer(memory, memory->game + 0x3326308, &root)) {
    static const struct {
      const char *name;
      unsigned table, slot;
    } slots[] = {{"UnitAPI.local_position", 0x18, 0},
                 {"UnitAPI.set_local_position", 0x18, 0x38},
                 {"UnitAPI.set_local_rotation", 0x18, 0x40},
                 {"UnitAPI.pre_input_world_pose", 0x18, 0x90},
                 {"CameraAPI.set_local_position", 0x20, 0x20},
                 {"CameraAPI.set_local_rotation", 0x20, 0x28},
                 {"CameraAPI.near_range", 0x20, 0x58},
                 {"CameraAPI.set_near_range", 0x20, 0x68},
                 {"CameraAPI.vertical_fov", 0x20, 0x78},
                 {"CameraAPI.set_vertical_fov", 0x20, 0x80},
                 {"CameraAPI.set_post_projection_transform", 0x20, 0xa0}};
    for (unsigned i = 0; i < sizeof slots / sizeof *slots; i++) {
      bool readable = fp_pointer(memory, root + slots[i].table, &api) &&
                      fp_pointer(memory, api + slots[i].slot, &target);
      fprintf(file, "# native_pointer\t%s\t%d\t%llx\t%llx\n", slots[i].name,
              readable,
              (unsigned long long)(readable ? api + slots[i].slot : 0),
              (unsigned long long)(readable ? target : 0));
    }
  }
  fprintf(file,
          "# rows=%u tables=%u aliases=%u omitted=%u depth_limited=%u limit=%d "
          "complete=%d\n",
          stats.rows, stats.tables, stats.aliases, stats.omitted,
          stats.depth_limited, stats.limit_reached, stats.complete);
  if (fflush(file) || ferror(file))
    ok = false;
  if (fclose(file))
    ok = false;
  if (!ok) {
    DeleteFileW(wide);
    completed = generation;
    hf->log(mod, "Read-only SDK census failed; no SDK functions were invoked");
    return;
  }
  wcscpy(final, wide);
  final[wcslen(final) - 5] = 0;
  if (!MoveFileExW(wide, final,
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(wide);
    completed = generation;
    return;
  }
  completed = generation;
  hf->log(mod,
          "Read-only SDK census: %u entries, %u tables, %u aliases; bounded "
          "coverage complete=%d. No SDK functions invoked.",
          stats.rows, stats.tables, stats.aliases, stats.complete);
#else
  (void)hf;
  (void)mod;
  (void)row;
  (void)memory;
#endif
}

#if defined(TFP_READONLY_SDK_CENSUS)
static struct {
  uint64_t generation, start, next;
  unsigned count;
  FILE *file;
  wchar_t path[1500];
} observation;
static bool observation_open(const HfApi *hf, HfMod *mod, uint64_t generation) {
  if (hf->size < offsetof(HfApi, logs_folder) + sizeof hf->logs_folder ||
      !hf->logs_folder)
    return false;
  const char *folder = hf->logs_folder(mod);
  char path[1500];
  if (!folder || !*folder)
    return false;
  int n =
      snprintf(path, sizeof path, "%s/true-first-person-native-g%llu.tsv.part",
               folder, (unsigned long long)generation);
  if (n <= 0 || (size_t)n >= sizeof path ||
      !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                           observation.path, 1500))
    return false;
  observation.file = _wfopen(observation.path, L"wb");
  if (!observation.file)
    return false;
  fprintf(
      observation.file,
      "# Build25480438 READ-ONLY game-thread samples. Not "
      "same-native-evaluation proof. No game calls, hooks or state writes. "
      "Bounded20s/512rows/20Hz; partial file retained on interrupted run.\n");
  fprintf(
      observation.file,
      "elapsed_"
      "ms\tenabled\tvalid\tavailable\tunavailable\tfailure\tkind\thead\ttail\tr"
      "equest\trequest_weapon\tmask\tfull_ads\tcontroller5\tbody\trig\tcamera_"
      "unit\tcamera_object\tcamera_parent\tcamera_node\trig_"
      "object\tweapon\tweapon_unit\tweapon_object\tweapon_parent\tweapon_"
      "link\tnear\tfar\tfov\tprojection\tfrustum_mode");
  for (unsigned i = 0; i < 5; i++)
    fprintf(observation.file, "\trequest_word%u", i);
  for (unsigned i = 0; i < 24; i++)
    fprintf(observation.file, "\tflag%u", i);
  for (unsigned i = 0; i < 4; i++)
    fprintf(observation.file, "\tfrustum%u", i);
  const char *names[] = {
      "camera_world", "rig0_world",   "rig5_world",
      "rig50_world",  "weapon_world", "weapon_parent_world",
      "hand13_world", "hand34_world", "weapon_socket8_world"};
  for (unsigned n = 0; n < 9; n++)
    for (unsigned i = 0; i < 16; i++)
      fprintf(observation.file, "\t%s_%u", names[n], i);
  for (unsigned i = 0; i < 3; i++)
    fprintf(observation.file, "\tbody_root%u", i);
  for (unsigned i = 0; i < 3; i++)
    fprintf(observation.file, "\tbody_head%u", i);
  fprintf(observation.file, "\tweapon_entity_resource\tcamera_link");
  fprintf(observation.file, "\n");
  fflush(observation.file);
  return true;
}
static void observation_close(const HfApi *hf, HfMod *mod, const char *reason) {
  if (!observation.file)
    return;
  fprintf(observation.file, "# samples=%u reason=%s; read-only capture ended\n",
          observation.count, reason);
  bool ok = !fflush(observation.file) && !ferror(observation.file);
  if (fclose(observation.file))
    ok = false;
  observation.file = NULL;
  if (ok) {
    wchar_t final[1500];
    wcscpy(final, observation.path);
    final[wcslen(final) - 5] = 0;
    ok = MoveFileExW(observation.path, final,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  }
  hf->log(mod,
          "Read-only native observer ended: %u samples; log finalized=%d. No "
          "game state was changed by the observer.",
          observation.count, ok);
}
#endif
void fp_sdk_observer(const HfApi *hf, HfMod *mod, const FpMemory *memory,
                     bool enabled, uint64_t now) {
#if defined(TFP_READONLY_SDK_CENSUS)
  uint64_t generation = eng_state_generation();
  if (!generation)
    return;
  if (observation.generation != generation) {
    observation_close(hf, mod, "VM changed");
    memset(&observation, 0, sizeof observation);
    observation.generation = generation;
  }
  if (!observer_attempted) {
    if (!enabled || !eng_ready())
      return;
    observer_attempted = true;
    observation.start = observation.next = now;
    if (!observation_open(hf, mod, generation)) {
      hf->log(mod, "Read-only native observer could not open its log");
      return;
    }
    hf->log(mod, "Read-only native pose observation started for20seconds; no "
                 "new game calls or camera controls");
  }
  if (!observation.file)
    return;
  if (now < observation.start || now - observation.start >= 20000 ||
      observation.count >= 512) {
    observation_close(hf, mod, "bounded window");
    return;
  }
  if (now < observation.next)
    return;
  observation.next = now + 50;
  FpNativeObservation o;
  bool valid = fp_native_observe(memory, &o);
  FILE *file = observation.file;
  fprintf(file,
          "%llu\t%d\t%d\t%u\t%u\t%u\t%u\t%u\t%u\t%llx\t%u\t%u\t%d\t%u\t%u\t%"
          "u\t%u\t%llx\t%llx\t%u\t%llx\t%u\t%u\t%llx\t%llx\t%u\t%.9g\t%.9g\t%."
          "9g\t%u\t%u",
          (unsigned long long)(now - observation.start), enabled, valid,
          o.available, o.unavailable, o.failure, o.owner.kind, o.owner.head,
          o.owner.tail, (unsigned long long)o.owner.request,
          o.owner.request_weapon, o.owner.mask, o.owner.full_ads,
          o.owner.controls[5], o.owner.body, o.owner.viewmodel,
          o.owner.camera_unit, (unsigned long long)o.camera_graph.object,
          (unsigned long long)o.camera_parent, o.camera_node,
          (unsigned long long)o.rig_graph.object, o.weapon_owner.weapon,
          o.weapon_owner.weapon_unit, (unsigned long long)o.weapon_graph.object,
          (unsigned long long)o.weapon_parent, o.weapon_link, o.lens.near_range,
          o.lens.far_range, o.lens.vertical_fov, o.lens.projection_type,
          o.lens.frustum_mode);
  for (unsigned i = 0; i < 5; i++)
    fprintf(file, "\t%u", o.request_words[i]);
  for (unsigned i = 0; i < 24; i++)
    fprintf(file, "\t%u", o.owner.flags[i]);
  for (unsigned i = 0; i < 4; i++)
    fprintf(file, "\t%.9g", o.lens.frustum[i]);
  const float *matrices[] = {
      o.camera_world, o.rig_world[0], o.rig_world[1],
      o.rig_world[2], o.weapon_world, o.weapon_parent_world,
      o.rig_hands[0], o.rig_hands[1], o.rig_weapon_attachment};
  for (unsigned n = 0; n < 9; n++)
    for (unsigned i = 0; i < 16; i++)
      fprintf(file, "\t%.9g", matrices[n][i]);
  for (unsigned i = 0; i < 3; i++)
    fprintf(file, "\t%.9g", o.body_root[i]);
  for (unsigned i = 0; i < 3; i++)
    fprintf(file, "\t%.9g", o.body_head[i]);
  uint64_t resource = 0;
  if (o.available & FP_OBSERVE_WEAPON)
    memcpy(&resource, o.weapon_owner.weapon_bytes, sizeof resource);
  fprintf(file, "\t%llx\t%u", (unsigned long long)resource, o.camera_link);
  fprintf(file, "\n");
  observation.count++;
  if (fflush(file) || ferror(file))
    observation_close(hf, mod, "I/O failure");
#else
  (void)hf;
  (void)mod;
  (void)memory;
  (void)enabled;
  (void)now;
#endif
}
