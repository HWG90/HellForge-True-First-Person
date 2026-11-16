/* src/motion_math.c replayed against the retail motion.lua's own answers
 * (tests/vectors/motion_vectors.h). Every number is a double in both and must
 * be the same double, bit for bit (a NaN must be a NaN; its payload is not
 * compared). Texts, flags and the model's whole state must be equal. */
#include "motion_math.h"
#include "vectors/motion_vectors.h"
#include <stdio.h>
#include <string.h>

static bool same(double c, double retail) {
  if (c != c || retail != retail) return c != c && retail != retail;
  return !memcmp(&c, &retail, sizeof c);
}
static TfpMotionOptions options_of(const MotionOptionsVector *v) {
  TfpMotionOptions o;
  memset(&o, 0, sizeof o);
  o.enabled = (TfpMotionFlag)v->enabled;
  o.editor_frozen = (TfpMotionFlag)v->editor_frozen;
  o.shoulder_aim_enabled = (TfpMotionFlag)v->shoulder_aim_enabled;
  o.scale.kind = (TfpMotionKind)v->scale_kind;
  o.scale.value = v->scale;
  for (int i = 0; i < TFP_MOTION_SETTING_COUNT; i++) {
    o.setting[i].kind = (TfpMotionKind)v->kind[i];
    o.setting[i].value = v->value[i];
  }
  return o;
}
static const char *text_of(const char *error, int setting, char *buffer, size_t size) {
  if (!error) return "";
  if (setting < 0) return error;
  snprintf(buffer, size, "%s%s", error, tfp_motion_setting_names[setting]);
  return buffer;
}

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(MOTION_VECTOR_COMMIT, commit) || strlen(MOTION_VECTOR_SHA256) != 64) {
    fprintf(stderr, "motion vectors were cut from retail %s, expected %s\n", MOTION_VECTOR_COMMIT, commit);
    return 2;
  }
  TfpMotionMath m;
  memset(&m, 0xa5, sizeof m); /* init must not depend on what the memory held */
  tfp_motion_math_init(&m, NULL, NULL);
  if (*tfp_motion_reset_name((TfpMotionReset)99) || *tfp_motion_rebase_name((TfpMotionRebase)99)) return 3;
  unsigned count = sizeof motion_vectors / sizeof *motion_vectors, updates = 0, numbers = 0, failures = 0;
  for (unsigned i = 0; i < count; i++) {
    const MotionVector *v = motion_vectors + i;
    char buffer[96], what[160] = "";
    bool bad = false;
    if (v->op == MV_NEW || v->op == MV_CONFIGURE) {
      const MotionOptionsVector *ov = motion_options + v->index;
      TfpMotionOptions o = options_of(ov);
      const char *error = "not cleared"; /* a call that succeeds must leave NULL and -1 */
      int setting = 99;
      bool ok;
      if (v->op == MV_NEW) {
        ok = tfp_motion_math_init(&m, ov->table ? &o : NULL, &error);
        setting = -1;
        /* retail's message names the setting; init reports only the text, so ask configure for the name */
        if (!ok) {
          TfpMotionMath scratch;
          tfp_motion_math_init(&scratch, NULL, NULL);
          tfp_motion_math_configure(&scratch, &o, &error, &setting);
          tfp_motion_math_init(&m, NULL, NULL); /* as the runner: a refused Motion.new leaves a fresh default model */
        }
      } else {
        TfpMotionMath twin; /* the same call without the optional answers must do the same */
        memcpy(&twin, &m, sizeof m);
        bool twin_ok = tfp_motion_math_configure(&twin, ov->table ? &o : NULL, NULL, NULL);
        ok = tfp_motion_math_configure(&m, ov->table ? &o : NULL, &error, &setting);
        if (twin_ok != ok || memcmp(&twin, &m, sizeof m)) { bad = true; snprintf(what, sizeof what, "configure without answers differs"); }
      }
      if ((ok && (error || setting != -1)) || (!ok && !error) || (setting != -1 && (!error || strcmp(error, "Invalid motion setting: ")))) {
        bad = true;
        snprintf(what, sizeof what, "error / setting answers inconsistent (setting %d)", setting);
      }
      const char *text = text_of(error, setting, buffer, sizeof buffer);
      if (ok != (v->ok != 0) || strcmp(text, motion_texts[v->text])) {
        bad = true;
        snprintf(what, sizeof what, "ok=%d \"%s\"", ok, text);
      }
    } else if (v->op == MV_UPDATE) {
      TfpMotionSample s = {.has_owner = v->has_owner != 0, .owner = v->owner, .x = v->x, .y = v->y, .z = v->z, .yaw = v->yaw,
          .pitch = v->pitch, .ads_kind = (TfpMotionAds)v->ads_kind, .ads = v->ads, .shoulder_aim = v->shoulder_aim != 0,
          .sprint = v->sprint != 0, .editor_frozen = v->editor_frozen != 0, .frozen = v->frozen != 0};
      TfpMotionOutput o = tfp_motion_math_update(&m, v->has_sample ? &s : NULL, v->dt);
      const double c[18] = {o.dx, o.dy, o.dz, o.pitch, o.yaw, o.roll, o.camera_dx, o.camera_dy, o.camera_dz, o.camera_pitch,
          o.camera_yaw, o.camera_roll, o.weight, o.speed, o.phase, o.sprint_weight, o.shoulder_weight, o.bob_weight};
      updates++;
      if ((int)o.reset != v->reset || (int)o.rebased != v->rebased ||
          strcmp(tfp_motion_reset_name(o.reset), motion_reset_texts[v->reset]) ||
          strcmp(tfp_motion_rebase_name(o.rebased), motion_rebase_texts[v->rebased]) || o.has_camera != (v->has_camera != 0) ||
          o.sprint_target != (v->sprint_target != 0) || o.sprint_held != (v->sprint_held != 0)) {
        bad = true;
        snprintf(what, sizeof what, "reset=%s rebased=%s camera=%d target=%d held=%d", tfp_motion_reset_name(o.reset),
                 tfp_motion_rebase_name(o.rebased), o.has_camera, o.sprint_target, o.sprint_held);
      }
      for (int k = 0; k < 18 && !bad; k++) {
        numbers++;
        if (!same(c[k], v->out[k])) {
          bad = true;
          snprintf(what, sizeof what, "number %d: C %.17g retail %.17g", k, c[k], v->out[k]);
        }
      }
    } else if (v->op == MV_RESET) tfp_motion_math_reset(&m);
    else if (v->op == MV_CLEAR_ADS) tfp_motion_math_clear_ads(&m);
    else if (v->op == MV_STATE) {
      const MotionStateVector *sv = motion_states + v->index;
      const double dynamic[13] = {m.phase, m.camera_phase, m.camera_look_x, m.camera_look_z, m.amplitude, m.sprint,
          m.sprint_hold, m.shoulder, m.aim, m.look_x, m.look_z, m.right_speed, m.forward_speed};
      const double previous[5] = {m.previous.x, m.previous.y, m.previous.z, m.previous.yaw, m.previous.pitch};
      if (m.enabled != (sv->enabled != 0) || m.editor_frozen != (sv->editor_frozen != 0) ||
          m.shoulder_aim_enabled != (sv->shoulder_aim_enabled != 0) || m.has_previous != (sv->has_previous != 0) ||
          !same(m.scale, sv->scale) || (m.has_previous && m.previous.owner != sv->owner)) {
        bad = true;
        snprintf(what, sizeof what, "state flags, scale or owner");
      }
      for (int k = 0; k < TFP_MOTION_SETTING_COUNT && !bad; k++)
        if (!same(m.setting[k], sv->setting[k])) { bad = true; snprintf(what, sizeof what, "setting %s", tfp_motion_setting_names[k]); }
      for (int k = 0; k < 5 && !bad && m.has_previous; k++)
        if (!same(previous[k], sv->previous[k])) { bad = true; snprintf(what, sizeof what, "previous %d", k); }
      for (int k = 0; k < 13 && !bad; k++) {
        numbers++;
        if (!same(dynamic[k], sv->dynamic[k])) {
          bad = true;
          snprintf(what, sizeof what, "state %d: C %.17g retail %.17g", k, dynamic[k], sv->dynamic[k]);
        }
      }
    }
    if (bad && failures++ < 8) fprintf(stderr, "motion vector %u (op %d): %s\n", i, v->op, what);
  }
  if (failures) {
    fprintf(stderr, "%u of %u operations differ from retail\n", failures, count);
    return 1;
  }
  printf("Motion math: %u operations answered as retail's motion.lua (%s), %u updates, %u doubles equal bit for bit\n", count,
         MOTION_VECTOR_COMMIT, updates, numbers);
  return 0;
}
