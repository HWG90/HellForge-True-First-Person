/* Runtime and every engine call run on HellForge's owned game-thread tick.
 * Intent survives firearm changes; a request/rig lease never crosses them. */
#include "runtime.h"
#include "policy_owner.h"
#include "profile.h"
#include "scene.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static bool weapon_same(const FpSnapshot *a, const FpSnapshot *b) {
  return fp_same_player(a, b) && a->weapon == b->weapon &&
         a->weapon_descriptor == b->weapon_descriptor &&
         a->viewmodel == b->viewmodel &&
         !memcmp(a->weapon_bytes, b->weapon_bytes, 20);
}
static bool release_policies(FpRuntime *r, bool camera, bool handoff) {
  bool done = handoff ? fp_pose_policy_handoff(&r->pose)
                      : fp_pose_policy_close(&r->pose);
  if (!fp_blur_policy_close(&r->blur))
    done = false;
  r->blur_absent = false;
  if (camera) {
    if (!fp_visibility_policy_close(&r->visibility))
      done = false;
    if (!fp_fov_policy_close(&r->fov))
      done = false;
    if (!fp_near_policy_close(&r->clip))
      done = false;
    if (!fp_camera_policy_close(&r->camera))
      done = false;
  }
  return done;
}
static bool policies(FpRuntime *r, bool camera) {
  return release_policies(r, camera, false);
}
void fp_runtime_init(FpRuntime *r, const FpMemory *m, const FpSettings *s) {
  memset(r, 0, sizeof *r);
  r->memory = *m;
  r->settings = *s;
  fp_camera_policy_init(&r->camera);
  fp_pose_policy_init(&r->pose);
  fp_fov_policy_init(&r->fov);
  fp_near_policy_init(&r->clip);
  fp_blur_policy_init(&r->blur);
  fp_visibility_policy_init(&r->visibility);
  fp_effects_init(&r->effects);
  fp_models_init(&r->models);
  snprintf(r->status, sizeof r->status, "First person off");
}
void fp_runtime_enable(FpRuntime *r, bool enabled) {
  if (r->stopping)
    enabled = false;
  r->enabled = enabled;
  if (!enabled) {
    r->editor = false;
    r->compare_until = 0;
  }
  r->next_entry = 0;
}
bool fp_runtime_clean(const FpRuntime *r) {
  return !r->native.held && !r->camera.token && !r->pose.token &&
         !r->fov.token && !r->clip.token && !r->blur.token && !r->aim.held &&
         !r->visibility.token &&
         !r->movement.held && !r->markers.held[0] && !r->markers.held[1] &&
         !r->models.count && !r->effects.count && !r->effects.setting[0].held &&
         !r->effects.setting[1].held;
}
static bool cleanup(FpRuntime *r, uint64_t now, bool foreign) {
  bool done = policies(r, true);
  if (foreign)
    return false;
  if (!fp_aim_preference(&r->aim, &r->memory, false, fp_private_byte, NULL))
    done = false;
  if (!fp_movement_preference(&r->movement, &r->memory, false, fp_private_byte,
                              NULL))
    done = false;
  if (!fp_markers_update(&r->markers, &r->memory, false, fp_markers_native,
                         &r->memory))
    done = false;
  if (!fp_models_update(&r->models, &r->memory, NULL, false, now))
    done = false;
  if (!fp_effects_update(&r->effects, &r->memory, false, now))
    done = false;
  if (!r->pose.token && !r->blur.token &&
      !fp_native_release(&r->memory, &r->native, fp_native_platform, NULL,
                         r->issue, sizeof r->issue))
    done = false;
  if (done) {
    r->warming = false;
    r->placement_ready = false;
    r->weapon_key = 0;
  }
  return done;
}
static void clip_options(FpRuntime *r) {
  float clip = r->settings.near_cm >= 5 && r->settings.near_cm <= 40
                   ? (float)r->settings.near_cm / 100
                   : 0;
  if (r->clip.token)
    fp_near_policy_set(&r->clip, r->clip.token, clip,
                       r->settings.near_aim ? clip : 0);
}
static bool open_camera(FpRuntime *r, const FpSnapshot *s) {
  if (!r->camera.token && !fp_camera_policy_open(&r->camera, &r->memory, s,
                                                 r->issue, sizeof r->issue))
    return false;
  if (!r->fov.token &&
      !fp_fov_policy_open(&r->fov, &r->memory, s, r->issue, sizeof r->issue))
    return false;
  if (!r->clip.token)
    fp_near_policy_open(&r->clip, &r->memory, s, r->issue, sizeof r->issue);
  /* Native entry can evaluate immediately, before the rest of this tick. */
  clip_options(r);
  if (!r->visibility.token && s->weapon_ready &&
      (s->kind == 4 || s->kind == 6) &&
      !fp_visibility_policy_open(&r->visibility, &r->memory, s,
                                 r->issue, sizeof r->issue))
    return false;
  fp_camera_policy_options(&r->camera, &r->settings.eye);
  return true;
}
static bool prepare(FpRuntime *r, const FpSnapshot *s, uint64_t now) {
  if (now < r->next_entry)
    return false;
  r->next_entry = now + 100;
  if (!s->safe || s->kind != 4 || s->aiming || s->full_ads)
    return false;
  FpProof proof = fp_prove_profile(&r->memory);
  FpGraph graph;
  /* The original mode also supports the known melee model at the eye camera,
   * with its native third-person carry animation and no fabricated FPS rig. */
  if (r->weapon_key == UINT64_C(0x5385bda2bdc93e5f) &&
      proof.matched == FP_WITNESS_COUNT)
    return open_camera(r, s);
  if (proof.matched != FP_WITNESS_COUNT ||
      !fp_graph_rig(&r->memory, s->viewmodel, &graph)) {
    if (r->native.held)
      return false;
    if (!r->visibility.token &&
        !fp_visibility_policy_open(&r->visibility, &r->memory, s,
                                   r->issue, sizeof r->issue))
      return false;
    r->warming = true;
    if (!fp_native_begin(&r->memory, s, &r->native, fp_native_platform, NULL,
                         r->issue, sizeof r->issue) &&
        !r->native.held)
      r->warming = false;
    return false;
  }
  if (!open_camera(r, s))
    return false;
  if (!r->pose.token &&
      !fp_pose_policy_open(&r->pose, &r->memory, s, &r->camera, r->issue,
                           sizeof r->issue))
    return false;
  FpPlacement hip = fp_placement_resolve(&r->settings.placements, r->weapon_key,
                                         FP_IDLE, NULL),
              ads = fp_placement_resolve(&r->settings.placements, r->weapon_key,
                                         FP_ADS, NULL);
  /* Native entry evaluates immediately; use the existing interpolated
   * authored offset for its first callback during a weapon handoff. */
  if (r->placement_ready)
    hip = r->current_placement;
  fp_pose_policy_place(&r->pose, r->pose.token, &hip, &ads);
  fp_camera_policy_frame(&r->camera, r->look_sequence, true);
  bool entered = fp_native_begin(&r->memory, s, &r->native, fp_native_platform,
                                 NULL, r->issue, sizeof r->issue);
  fp_camera_policy_frame(&r->camera, r->look_sequence, false);
  if (!entered)
    fp_pose_policy_close(&r->pose);
  return entered;
}
static void place(FpRuntime *r, double dt) {
  if (!r->pose.token)
    return;
  FpAction action = r->action.verified         ? r->action.selected
                    : r->snapshot.full_ads     ? FP_ADS
                    : r->snapshot.reloading    ? FP_RELOAD
                    : r->snapshot.shoulder_aim ? FP_AIM
                                               : FP_IDLE;
  FpPlacement target =
      fp_placement_resolve(&r->settings.placements, r->weapon_key,
                           action == FP_ADS ? FP_IDLE : action, NULL);
  FpPlacement ads = fp_placement_resolve(&r->settings.placements, r->weapon_key,
                                         FP_ADS, NULL);
  if (r->editor && r->weapon_key == r->editor_weapon) {
    if (r->editor_action == FP_ADS)
      ads = r->draft;
    else
      target = r->draft;
  }
  double weight = dt > 0 && dt < .25 ? 1 - exp(-dt / .10) : 1;
  if (!r->placement_ready) {
    r->current_placement = target;
    r->placement_ready = true;
  } else {
    double *current[] = {
        &r->current_placement.x,     &r->current_placement.y,
        &r->current_placement.z,     &r->current_placement.yaw,
        &r->current_placement.pitch, &r->current_placement.roll};
    const double goal[] = {target.x,   target.y,     target.z,
                           target.yaw, target.pitch, target.roll};
    for (unsigned i = 0; i < 6; i++)
      *current[i] += (goal[i] - *current[i]) * weight;
  }
  fp_pose_policy_place(&r->pose, r->pose.token, &r->current_placement, &ads);
}
void fp_runtime_tick(FpRuntime *r, double dt, uint64_t now, bool foreign,
                     bool look_allowed, bool backward) {
  r->sequence++;
  if (look_allowed)
    r->look_sequence++;
  fp_camera_policy_frame(&r->camera, r->look_sequence, false);
  if (r->compare_until) {
    if (now >= r->compare_until || r->stopping) {
      r->compare_until = 0;
      fp_native_release(&r->memory, &r->native, fp_native_platform, NULL,
                        r->issue, sizeof r->issue);
    } else {
      snprintf(r->status, sizeof r->status,
               "Native ADS comparison: %llu seconds",
               (unsigned long long)((r->compare_until - now + 999) / 1000));
      return;
    }
  }
  if (!r->enabled || r->stopping || foreign) {
    bool done = cleanup(r, now, foreign);
    snprintf(r->status, sizeof r->status, "%s",
             foreign ? "Paused while another camera owns input"
             : done  ? "First person off"
                     : "Restoring owned state");
    return;
  }
  bool dispatch_lost =
      (r->camera.token && !fp_policy_dispatch(&r->memory, &r->camera.lease)) ||
      (r->fov.token && !fp_policy_dispatch(&r->memory, &r->fov.lease)) ||
      (r->pose.token && !fp_policy_dispatch(&r->memory, &r->pose.lease));
  dispatch_lost = dispatch_lost ||
      (r->visibility.token &&
       !fp_policy_dispatch(&r->memory, &r->visibility.lease));
  bool partial = (r->camera.token && !atomic_load(&r->camera.lease.armed)) ||
                 (r->fov.token && !atomic_load(&r->fov.lease.armed)) ||
                 (r->pose.token && !atomic_load(&r->pose.lease.armed));
  partial = partial ||
            (r->visibility.token && !atomic_load(&r->visibility.lease.armed));
  if (dispatch_lost || partial) {
    cleanup(r, now, false);
    snprintf(r->status, sizeof r->status,
             "Restoring a replaced or partial camera dispatch");
    return;
  }
  if (r->clip.token && !atomic_load(&r->clip.lease.armed))
    fp_near_policy_close(&r->clip);
  if (r->blur.token && !atomic_load(&r->blur.lease.armed))
    fp_blur_policy_close(&r->blur);
  FpSnapshot player;
  char why[160];
  if (!fp_snapshot(&r->memory, false, &player, why, sizeof why)) {
    snprintf(r->status, sizeof r->status, "Waiting: %s", why);
    return;
  }
  if (r->camera.token && (!fp_same_player(&r->camera.origin, &player) ||
                          r->camera.origin.camera_unit != player.camera_unit)) {
    cleanup(r, now, false);
    snprintf(r->status, sizeof r->status,
             "Waiting for the replacement player camera");
    return;
  }
  bool firearm = fp_snapshot(&r->memory, true, &r->snapshot, why, sizeof why);
  if (!firearm)
    r->snapshot = player;
  if (r->editor && !weapon_same(&r->editor_origin, &r->snapshot))
    r->editor = false;
  uint64_t weapon_key = 0;
  /* An optional placement key must not decide whether a real equipped
   * weapon can enter FP or have its rendering restored. */
  if (firearm)
    fp_weapon_key(&r->memory, &r->snapshot, &weapon_key);
  if (r->weapon_key != weapon_key) {
    r->editor = false;
    r->weapon_key = weapon_key;
    r->blur_absent = false;
    r->next_blur = 0;
  }
  if (r->warming) {
    if (fp_native_release(&r->memory, &r->native, fp_native_platform, NULL,
                          r->issue, sizeof r->issue)) {
      r->warming = false;
      r->next_entry = 0;
    }
    snprintf(r->status, sizeof r->status,
             "Initializing the native first-person rig");
    return;
  }
  if (r->native.held &&
      (player.request != r->native.request || player.kind != 6 ||
       player.request_weapon != r->native.origin.weapon))
    fp_native_release(&r->memory, &r->native, fp_native_platform, NULL,
                      r->issue, sizeof r->issue);
  if (r->pose.token &&
      (!firearm || !weapon_same(&r->pose.origin, &r->snapshot) ||
       player.request != r->pose.request || player.kind != 6)) {
    bool handoff = firearm && weapon_same(&r->pose.origin, &r->snapshot) &&
                   player.kind == 4 && player.safe && !player.full_ads;
    release_policies(r, false, handoff);
  }
  if (r->blur.token &&
      (!firearm || !weapon_same(&r->blur.origin, &r->snapshot) ||
       player.request != r->blur.origin.request || player.kind != 6))
    fp_blur_policy_close(&r->blur);
  if ((!r->camera.token || !r->pose.token) && r->native.held) {
    fp_native_release(&r->memory, &r->native, fp_native_platform, NULL,
                      r->issue, sizeof r->issue);
    snprintf(r->status, sizeof r->status,
             "Restoring the previous native camera");
    return;
  }
  if (!r->camera.token && !r->native.held &&
      (!firearm || player.kind != 4 || !player.safe || player.aiming ||
       player.full_ads)) {
    FpProof proof = fp_prove_profile(&r->memory);
    if (proof.matched == FP_WITNESS_COUNT)
      open_camera(r, firearm ? &r->snapshot : &player);
  }
  if (firearm && !r->native.held && !r->pose.token && player.kind == 4)
    prepare(r, &r->snapshot, now);
  if (firearm && r->camera.token && !r->visibility.token &&
      (player.kind == 4 || player.kind == 6))
    fp_visibility_policy_open(&r->visibility, &r->memory, &r->snapshot,
                              r->issue, sizeof r->issue);
  bool active = r->camera.token && atomic_load(&r->camera.lease.armed) &&
                fp_same_player(&r->camera.origin, &player);
  if (!active) {
    snprintf(r->status, sizeof r->status, "Waiting for a released firearm%s%s",
             *r->issue ? ": " : "", r->issue);
    return;
  }
  FpEyeOptions eye = r->settings.eye;
  eye.motion.editor_frozen = r->editor || !look_allowed;
  fp_camera_policy_options(&r->camera, &eye);
  clip_options(r);
  if (!fp_aim_preference(&r->aim, &r->memory, r->settings.native_ads,
                         fp_private_byte, NULL))
    snprintf(r->issue, sizeof r->issue,
             "Native ADS preference restoration pending");
  memset(&r->action, 0, sizeof r->action);
  fp_action_read(&r->actions, &r->memory, &player, &r->action);
  if (r->action.verified && r->action.selected == FP_IDLE)
    for (unsigned i = 0; i < r->settings.mapping_count && i < 128; i++)
      if (r->settings.action_mapping[i].l1 == r->action.l1 &&
          r->settings.action_mapping[i].l3 == r->action.l3) {
        r->action.selected = r->settings.action_mapping[i].action;
        break;
      }
  fp_camera_policy_actions(&r->camera, r->action.verified && r->action.sprint);
  bool visible = player.local_request && !player.seated && !player.controls[5] &&
                 ((player.kind == 6 && (player.mask == 0 || player.mask == 2)) ||
                  (player.kind == 4 && (player.mask == 0 || player.mask == 2 || player.mask == 0x20) &&
                   r->action.on_foot &&
                   fp_camera_policy_fallback_ready(&r->camera)));
  if (visible && player.kind == 6) {
    FpGraph attached_rig;
    visible = firearm && fp_rig_attachments(&r->memory, &r->snapshot, &attached_rig);
  }
  /* Visibility ownership follows the actual FPS attachment even while a
   * weapon/menu action temporarily changes the input mask. */
  bool protect_weapon = firearm && player.kind == 6 && player.local_request &&
                        !player.seated && !player.controls[5];
  if (protect_weapon) {
    FpGraph attached_rig;
    protect_weapon =
        fp_rig_attachments(&r->memory, &r->snapshot, &attached_rig);
  }
  visible = visible || protect_weapon;
  if (r->visibility.token)
    fp_visibility_policy_update(&r->visibility, r->visibility.token,
                                 &r->snapshot, protect_weapon);
  fp_movement_preference(&r->movement, &r->memory,
                         backward && look_allowed && !r->editor &&
                             r->action.on_foot && player.safe,
                         fp_private_byte, NULL);
  place(r, dt);
  if (firearm && player.kind == 6 && !r->blur.token && !r->blur_absent &&
      now >= r->next_blur) {
    r->next_blur = now + 500;
    if (!fp_blur_policy_open(&r->blur, &r->memory, &r->snapshot,
                             &r->blur_absent, r->issue, sizeof r->issue))
      fp_blur_policy_close(&r->blur);
  }
  bool aiming = player.full_ads || player.aiming || player.shoulder_aim;
  bool show_markers = r->settings.show_markers;
  if (r->settings.marker_legacy) {
    show_markers = !firearm || !r->snapshot.safe ||
                   (player.kind != 4 && player.kind != 6) || weapon_key == 0;
    for (unsigned i = 0; i < r->settings.marker_count && i < 256; i++)
      if (r->settings.marker_allow[i] == weapon_key)
        show_markers = true;
  }
  fp_markers_update(&r->markers, &r->memory, visible && !show_markers && aiming,
                    fp_markers_native, &r->memory);
  fp_models_update(&r->models, &r->memory, &r->snapshot, visible, now);
  fp_effects_update(&r->effects, &r->memory, visible, now);
  snprintf(r->status, sizeof r->status, "True First Person on%s",
           !visible        ? " — native action camera"
           : player.full_ads ? " — native ADS"
           : r->editor     ? " — editing placement"
                           : "");
}
bool fp_runtime_editor(FpRuntime *r, bool enabled) {
  if (!enabled) {
    r->editor = false;
    return true;
  }
  if (!r->enabled || !r->camera.token || !r->pose.token || !r->weapon_key)
    return false;
  r->editor = true;
  r->editor_weapon = r->weapon_key;
  r->editor_origin = r->snapshot;
  r->editor_action = FP_IDLE;
  r->draft = fp_placement_resolve(&r->settings.placements, r->weapon_key,
                                  FP_IDLE, NULL);
  return true;
}
bool fp_runtime_draft(FpRuntime *r, FpAction action, const FpPlacement *draft) {
  if (!r->editor || r->editor_weapon != r->weapon_key ||
      action >= FP_ACTION_COUNT || !fp_placement_valid(draft))
    return false;
  r->editor_action = action;
  r->draft = *draft;
  return true;
}
bool fp_runtime_free_aim(FpRuntime *r) {
  return r->enabled && fp_camera_policy_free_aim(&r->camera, 30000);
}
bool fp_runtime_compare(FpRuntime *r, uint64_t now) {
  if (r->enabled || !fp_runtime_clean(r))
    return false;
  FpSnapshot s;
  if (!fp_snapshot(&r->memory, true, &s, NULL, 0))
    return false;
  if (!fp_native_begin(&r->memory, &s, &r->native, fp_native_platform, NULL,
                       r->issue, sizeof r->issue))
    return false;
  r->compare_until = now + 30000;
  return true;
}
