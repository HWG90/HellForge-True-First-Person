/* Accepted post-pin camera: native kind6 is ALWAYS forwarded exactly.
 * Only a proven unparented on-foot normal4 camera receives positional eye
 * correction. No raw-input integration, quaternion writes or vehicle pin. */
#include "camera_policy.h"
#include "native_contract.h"
#include "policy_owner.h"
#include "profile.h"
#include "scene.h"
#include <intrin.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef void (*CameraSetter)(void *, uint32_t, const float *);
static _Atomic(FpCameraPolicy *) active;
static _Atomic(CameraSetter) native_setter;
static _Atomic uint64_t callback_generation;
static bool why(char *out, size_t size, const char *text) {
  if (out && size)
    snprintf(out, size, "%s", text);
  return false;
}
static bool finite3(const float *p) {
  return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]);
}
static double camera_now(const FpCameraPolicy *p) {
#if defined(TFP_CAMERA_TEST)
  extern double fp_camera_test_time;
  (void)p;
  return fp_camera_test_time;
#else
  LARGE_INTEGER tick;
  return p->ticks_per_ms > 0 && QueryPerformanceCounter(&tick)
             ? (double)tick.QuadPart / p->ticks_per_ms
             : (double)GetTickCount64();
#endif
}
static void reset(FpCameraPolicy *p, bool history) {
  p->tracking = false;
  p->blend = 0;
  if (!history) {
    p->native_seen = false;
    p->history_valid = false;
    fp_correction_reset(&p->correction);
  }
}
static void publish(FpCameraPolicy *p) {
  float values[10] = {(float)p->last_motion.dx,
                      (float)p->last_motion.dy,
                      (float)p->last_motion.dz,
                      (float)p->last_motion.pitch,
                      (float)p->last_motion.yaw,
                      (float)p->last_motion.roll,
                      p->view.x,
                      p->view.y,
                      p->view.z,
                      p->view.w};
  atomic_fetch_add_explicit(&p->shared_sequence, 1, memory_order_seq_cst);
  for (unsigned i = 0; i < 10; i++)
    atomic_store_explicit(p->shared + i, values[i], memory_order_seq_cst);
  atomic_store_explicit(&p->shared_valid, p->frame_valid, memory_order_seq_cst);
  atomic_fetch_add_explicit(&p->shared_sequence, 1, memory_order_seq_cst);
}
bool fp_camera_policy_read_frame(const FpCameraPolicy *p, float motion[6],
                                 FpQuat *view) {
  if (!p || !motion || !view)
    return false;
  unsigned before =
      atomic_load_explicit(&p->shared_sequence, memory_order_seq_cst);
  if (before & 1)
    return false;
  float values[10];
  for (unsigned i = 0; i < 10; i++)
    values[i] = atomic_load_explicit(p->shared + i, memory_order_seq_cst);
  bool valid = atomic_load_explicit(&p->shared_valid, memory_order_seq_cst);
  if (!valid ||
      before != atomic_load_explicit(&p->shared_sequence, memory_order_seq_cst))
    return false;
  memcpy(motion, values, 24);
  *view = (FpQuat){values[6], values[7], values[8], values[9]};
  return fp_q_valid(*view);
}
bool fp_camera_policy_fallback_ready(const FpCameraPolicy *p) {
  return p && atomic_load(&p->lease.armed) && atomic_load(&p->fallback_active);
}
static bool owner(FpCameraPolicy *p, FpSnapshot *s) {
  return fp_policy_dispatch(&p->memory, &p->lease) &&
         fp_snapshot(&p->memory, false, s, NULL, 0) &&
         fp_same_player(&p->origin, s) &&
         s->camera_unit == p->origin.camera_unit &&
         fp_unit_object(&p->memory, s->body) == p->body_object &&
         fp_unit_object(&p->memory, s->camera_unit) == p->camera_object;
}
static bool same_request(const FpSnapshot *a, const FpSnapshot *b) {
  return fp_same_player(a, b) && a->request == b->request &&
         a->kind == b->kind && a->request_avatar == b->request_avatar &&
         a->request_weapon == b->request_weapon && a->full_ads == b->full_ads &&
         a->mask == b->mask;
}
static bool available(const FpSnapshot *s) {
  return s->local_request && !s->seated && !s->controls[5] &&
         (s->mask == 0 || s->mask == 2 || s->mask == 0x20);
}
/* Observe this invocation's orientation for WEAPON bob/sway only. The game
 * continues to receive its untouched native position pointer and quaternion. */
static void motion(FpCameraPolicy *p, const FpSnapshot *s,
                   const FpCameraSpace *space, const float *input, double now,
                   const float root[3]) {
  p->frame_valid = false;
  uintptr_t at = (uintptr_t)input;
  const NT_TIB *tib = (const NT_TIB *)NtCurrentTeb();
  FpQuat local;
  if (at < (uintptr_t)tib->StackLimit || at > UINTPTR_MAX - 80 ||
      at + 80 > (uintptr_t)tib->StackBase ||
      !fp_read(&p->memory, at + 64, &local, sizeof local) || !fp_q_valid(local))
    return;
  FpQuat view = fp_q_mul(space->rotation, local);
  if (!fp_q_valid(view))
    return;
  p->view = view;
  p->frame_valid = true;
  if (p->sampled == p->sequence)
    return;
  p->sampled = p->sequence;
  memset(&p->last_motion, 0, sizeof p->last_motion);
  float yaw = 0, pitch = 0;
  if (!fp_q_angles(view, &yaw, &pitch))
    return;
  double dt = p->motion_previous && now >= p->motion_previous
                  ? (now - p->motion_previous) / 1000
                  : 0;
  p->motion_previous = now;
  p->view = view;
  FpMotionSample sample = {.owner = s->body,
                           .x = root[0],
                           .y = root[1],
                           .z = root[2],
                           .yaw = yaw,
                           .pitch = pitch,
                           .ads = s->full_ads ? 1 : 0,
                           .shoulder_aim = s->shoulder_aim,
                           .sprint = p->sprint};
  p->last_motion = fp_motion_update(&p->motion, &sample, dt);
}
static void remember_native(FpCameraPolicy *p, const FpSnapshot *s,
                            const float *input, double now) {
  float native[4], root[3], head[3], basis[9], world[3], relative[3] = {0};
  FpCameraSpace space, closing_space;
  FpBodySample body;
  FpSnapshot closing, firearm, closing_firearm;
  uintptr_t gun = 0;
  uint32_t request_body = 0;
  bool sampled =
      available(s) && s->mask != 0x20 && s->request_weapon &&
      s->request_weapon != UINT32_MAX &&
      fp_snapshot(&p->memory, true, &firearm, NULL, 0) &&
      same_request(s, &firearm) && firearm.weapon == s->request_weapon &&
      (gun = fp_unit_object(&p->memory, firearm.weapon_unit)) != 0 &&
      fp_u32(&p->memory, s->request + 12, &request_body) &&
      request_body == s->body &&
      fp_read(&p->memory, (uintptr_t)input, native, 16) && finite3(native) &&
      fp_body_sample(&p->memory, s->body, root, head, basis, &body) &&
      fp_camera_native_parent(&p->memory, s, &space);
#if defined(TFP_CAMERA_TEST)
  extern void fp_camera_before_commit(bool);
  fp_camera_before_commit(true);
#endif
  if (!sampled || !owner(p, &closing) || !same_request(s, &closing) ||
      !fp_body_sample_current(&p->memory, &body) ||
      !fp_snapshot(&p->memory, true, &closing_firearm, NULL, 0) ||
      !same_request(&firearm, &closing_firearm) ||
      firearm.weapon != closing_firearm.weapon ||
      firearm.weapon_descriptor != closing_firearm.weapon_descriptor ||
      memcmp(firearm.weapon_bytes, closing_firearm.weapon_bytes, 20) ||
      firearm.weapon_unit != closing_firearm.weapon_unit ||
      fp_unit_object(&p->memory, closing_firearm.weapon_unit) != gun ||
      !fp_camera_native_parent(&p->memory, &closing, &closing_space) ||
      !fp_camera_space_same(&space, &closing_space)) {
    reset(p, false);
    memset(&p->last_motion, 0, sizeof p->last_motion);
    return;
  }
  fp_q_rotate(space.rotation, native, world);
  for (unsigned a = 0; a < 3; a++)
    for (unsigned b = 0; b < 3; b++)
      relative[a] +=
          (world[b] + space.position[b] - root[b]) * basis[a * 3 + b];
  p->history_valid =
      fp_correction_remember(&p->correction, relative, s->request_weapon, now);
  if (p->history_valid) {
    p->history_weapon = firearm;
    p->history_gun = gun;
    p->history_rig = space.parent;
  }
  reset(p, true);
  p->native_seen = p->history_valid;
  p->native_at = now;
  motion(p, s, &space, input, now, root);
}
static bool unparented(FpCameraPolicy *p, const FpSnapshot *s,
                       FpCameraSpace *space) {
  FpGraph camera;
  float local[3], world[3];
  if (!fp_camera_space(&p->memory, s, space) || space->parent || space->link ||
      !fp_graph(&p->memory, s->camera_unit, &camera) ||
      !fp_read(&p->memory, camera.locals + 36, local, 12) ||
      !fp_read(&p->memory, camera.worlds + 48, world, 12) || !finite3(local) ||
      !finite3(world))
    return false;
  float d = 0;
  for (unsigned i = 0; i < 3; i++) {
    float v = local[i] - world[i];
    d += v * v;
  }
  return d <= .0001f;
}
static bool normal_camera(FpCameraPolicy *p, const FpSnapshot *s,
                          const float *input, double now, float adjusted[4]) {
  FpActions action, closing_action;
  FpBodySample body;
  FpCameraSpace space, closing_space;
  FpSnapshot closing;
  float root[3], head[3], basis[9], frame[16];
  FpQuat q;
  if (!available(s) || !s->local_request || s->kind != 4 ||
      !fp_action_read(&p->action_reader, &p->memory, s, &action) ||
      !action.on_foot ||
      !fp_body_sample(&p->memory, s->body, root, head, basis, &body) ||
      !unparented(p, s, &space) ||
      !fp_read(&p->memory, (uintptr_t)input, adjusted, 16) ||
      !finite3(adjusted) || !fp_read(&p->memory, s->context + 12, frame, 64) ||
      !fp_q_matrix(frame, &q)) {
    reset(p, false);
    return false;
  }
  double distance_head = 0, distance_root = 0;
  for (unsigned i = 0; i < 3; i++) {
    double d = adjusted[i] - head[i];
    distance_head += d * d;
    d = adjusted[i] - root[i];
    distance_root += d * d;
  }
  if (distance_head > 64 || distance_root > 100) {
    reset(p, false);
    return false;
  }
  FpSnapshot firearm;
  uint32_t weapon = fp_snapshot(&p->memory, true, &firearm, NULL, 0) &&
                            same_request(s, &firearm)
                        ? firearm.weapon
                        : 0;
  uintptr_t weapon_object =
      weapon ? fp_unit_object(&p->memory, firearm.weapon_unit) : 0;
  bool carried = p->correction.remembered || p->correction.active;
  if (carried &&
      (!p->history_valid || !weapon_object ||
       p->history_weapon.weapon != weapon ||
       p->history_weapon.weapon_unit != firearm.weapon_unit ||
       p->history_weapon.weapon_descriptor != firearm.weapon_descriptor ||
       memcmp(p->history_weapon.weapon_bytes, firearm.weapon_bytes, 20) ||
       weapon_object != p->history_gun ||
       p->history_weapon.viewmodel != firearm.viewmodel ||
       fp_unit_object(&p->memory, firearm.viewmodel) != p->history_rig)) {
    reset(p, false);
    carried = false;
  }

  float local_head[3] = {0};
  for (unsigned a = 0; a < 3; a++)
    for (unsigned b = 0; b < 3; b++)
      local_head[a] += (head[b] - root[b]) * basis[a * 3 + b];
  double dt = 0;
  if (p->tracking && now >= p->previous && now - p->previous <= 250)
    dt = (now - p->previous) / 1000;
  else {
    bool history = p->native_seen;
    reset(p, history);
    memcpy(p->relative_head, local_head, 12);
  }
  p->tracking = true;
  p->previous = now;
  float weight = (float)(1 - exp(-dt / .1));
  for (unsigned i = 0; i < 3; i++)
    p->relative_head[i] += (local_head[i] - p->relative_head[i]) * weight;
  float lag[3];
  double length = 0;
  for (unsigned b = 0; b < 3; b++) {
    lag[b] = root[b] - head[b];
    for (unsigned a = 0; a < 3; a++)
      lag[b] += p->relative_head[a] * basis[a * 3 + b];
    length += lag[b] * lag[b];
  }
  length = sqrt(length);
  if (length > .06)
    for (unsigned i = 0; i < 3; i++)
      lag[i] *= (float)(.06 / length);
  p->blend = fminf(1, p->blend + (float)(dt * 1000 / 120));
  if (p->native_seen) {
    p->blend = 1;
    p->native_seen = false;
  }
  float e = p->blend * p->blend * (3 - 2 * p->blend),
        horizontal[2] = {frame[4], frame[5]};
  float horizontal_length = hypotf(horizontal[0], horizontal[1]);
  if (horizontal_length < .0001f) {
    horizontal[0] = basis[3];
    horizontal[1] = basis[4];
    horizontal_length = hypotf(horizontal[0], horizontal[1]);
  }
  if (horizontal_length < .0001f) {
    reset(p, false);
    return false;
  }
  horizontal[0] /= horizontal_length;
  horizontal[1] /= horizontal_length;
  for (unsigned i = 0; i < 3; i++) {
    float offset = i < 2 ? horizontal[i] * p->options.forward : 0;
    offset += i == 0   ? horizontal[1] * p->options.right
              : i == 1 ? -horizontal[0] * p->options.right
                       : p->options.up;
    adjusted[i] += (head[i] + lag[i] + offset - adjusted[i]) * e;
  }
  float desired[3] = {0}, relative[3];
  for (unsigned a = 0; a < 3; a++)
    for (unsigned b = 0; b < 3; b++)
      desired[a] += (adjusted[b] - root[b]) * basis[a * 3 + b];
  /* Normal4 carries the last validated weapon's short native6 handoff only.
   * A new native6 observation replaces it; actions/vehicles retire it. */
  if (fp_correction_apply(&p->correction, desired, weapon, now, relative, 150,
                          250))
    for (unsigned b = 0; b < 3; b++) {
      adjusted[b] = root[b];
      for (unsigned a = 0; a < 3; a++)
        adjusted[b] += relative[a] * basis[a * 3 + b];
    }
#if defined(TFP_CAMERA_TEST)
  extern void fp_camera_before_commit(bool);
  fp_camera_before_commit(false);
#endif
  FpSnapshot closing_firearm;
  if (carried &&
      (!weapon || !weapon_object ||
       !fp_snapshot(&p->memory, true, &closing_firearm, NULL, 0) ||
       !same_request(&firearm, &closing_firearm) ||
       firearm.weapon != closing_firearm.weapon ||
       firearm.weapon_unit != closing_firearm.weapon_unit ||
       fp_unit_object(&p->memory, closing_firearm.weapon_unit) !=
           weapon_object ||
       firearm.weapon_descriptor != closing_firearm.weapon_descriptor ||
       memcmp(firearm.weapon_bytes, closing_firearm.weapon_bytes, 20))) {
    reset(p, false);
    return false;
  }
  if (!finite3(adjusted) || !owner(p, &closing) || !same_request(s, &closing) ||
      !available(&closing) ||
      !fp_action_read(&p->action_reader, &p->memory, &closing,
                      &closing_action) ||
      !closing_action.on_foot || closing_action.l14 != action.l14 ||
      !fp_body_sample_current(&p->memory, &body) ||
      !unparented(p, &closing, &closing_space) ||
      !fp_camera_space_same(&space, &closing_space)) {
    reset(p, false);
    return false;
  }
  p->view = q;
  p->frame_valid = true;
  memset(&p->last_motion, 0, sizeof p->last_motion);
  return true;
}
static void camera_call(uintptr_t caller, void *camera, uint32_t unit,
                        const float *input) {
  uint64_t generation =
      atomic_load_explicit(&callback_generation, memory_order_acquire);
  CameraSetter original =
      atomic_load_explicit(&native_setter, memory_order_acquire);
  FpCameraPolicy *p = atomic_load_explicit(&active, memory_order_acquire);
  if (!original)
    return;
  if (!p || !atomic_load_explicit(&p->lease.armed, memory_order_acquire)) {
    original(camera, unit, input);
    return;
  }
#if defined(TFP_CAMERA_TEST)
  extern void fp_camera_before_lock(void);
  fp_camera_before_lock();
#endif
  AcquireSRWLockExclusive(&p->lock);
  const float *output = input;
  float adjusted[4];
  FpSnapshot s;
  if (p == atomic_load_explicit(&active, memory_order_acquire) &&
      generation ==
          atomic_load_explicit(&callback_generation, memory_order_acquire) &&
      !p->opening && !p->closing && atomic_load(&p->lease.armed) &&
      caller == p->memory.game + 0x127a139 &&
      (uintptr_t)camera == p->origin.camera && unit == p->origin.camera_unit) {
    p->frame_valid = false;
    atomic_store(&p->fallback_active, false);
    double now = camera_now(p);
    if (p->native_seen && (now < p->native_at || now - p->native_at > 250))
      p->native_seen = false;
    if (!owner(p, &s)) {
      atomic_store(&p->lease.armed, false);
      reset(p, false);
      memset(&p->last_motion, 0, sizeof p->last_motion);
    } else if (s.kind == 6 && s.local_request) {
      remember_native(p, &s, input, now);
      p->native_forwarded++;
      /* Do not replace output, write native scratch, or filter orientation. */
    } else if (s.kind == 4 && normal_camera(p, &s, input, now, adjusted)) {
      output = adjusted;
      atomic_store(&p->fallback_active, true);
    } else {
      reset(p, false);
      memset(&p->last_motion, 0, sizeof p->last_motion);
    }
    publish(p);
  }
  if (output != input)
    p->adjusted++;
  else
    p->forwarded++;
  original(camera, unit, output);
  ReleaseSRWLockExclusive(&p->lock);
}
static __declspec(noinline) void camera_setter(void *camera, uint32_t unit,
                                               const float *input) {
  camera_call((uintptr_t)_ReturnAddress(), camera, unit, input);
}
void fp_camera_policy_init(FpCameraPolicy *p) {
  memset(p, 0, sizeof *p);
  InitializeSRWLock(&p->lock);
  fp_dispatch_init(&p->lease);
  fp_correction_reset(&p->correction);
  p->view = fp_q_identity();
  fp_motion_init(&p->motion);
  p->options = (FpEyeOptions){
      .forward = .18f, .up = .04f, .kick = true, .motion = p->motion.options};
  LARGE_INTEGER frequency;
  p->ticks_per_ms =
      QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0
          ? (double)frequency.QuadPart / 1000
          : 0;
  atomic_init(&p->shared_valid, false);
  atomic_init(&p->fallback_active, false);
  atomic_init(&p->shared_sequence, 0);
  for (unsigned i = 0; i < 10; i++)
    atomic_init(p->shared + i, i == 9 ? 1 : 0);
}
bool fp_camera_policy_open(FpCameraPolicy *p, const FpMemory *m,
                           const FpSnapshot *s, char *error, size_t size) {
  if (!p || !m || !s || p->token || atomic_load(&active))
    return why(error, size, "Eye-camera owner is busy");
  if (!fp_image(m, m->game, FP_GAME_STAMP, FP_GAME_SIZE) ||
      !fp_image(m, m->exe, FP_EXE_STAMP, FP_EXE_SIZE))
    return why(error, size, "Eye-camera build differs");
  FpProof proof = fp_prove_profile(m);
  if (proof.matched != FP_WITNESS_COUNT)
    return why(error, size, "Native first-person camera code is not ready");
  for (unsigned i = 0; i < FP_NATIVE_SITE_COUNT; i++) {
    const FpNativeSite *site = fp_native_sites + i;
    if (!fp_bytes(m, (site->exe ? m->exe : m->game) + site->rva, site->hex))
      return why(error, size, site->name);
  }
  uintptr_t root = 0, api = 0, target = 0;
  float matrix[16];
  FpQuat q;
  float position[3], head[3];
  if (!fp_pointer(m, m->game + 0x3326308, &root) ||
      !fp_pointer(m, root + 0x20, &api) ||
      !fp_pointer(m, api + 0x20, &target) || target != m->exe + 0x30deb0 ||
      !fp_body_points(m, s->body, position, head, NULL) ||
      !fp_read(m, s->context + 0xc, matrix, 64) || !fp_q_matrix(matrix, &q))
    return why(error, size, "Eye-camera API or body graph differs");
  AcquireSRWLockExclusive(&p->lock);
  p->memory = *m;
  p->origin = *s;
  p->slot = api + 0x20;
  p->platform =
      (FpSlotPlatform){m->exe, GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                              "wine_get_version") != NULL};
  fp_correction_reset(&p->correction);
  p->view = q;
  p->body_object = fp_unit_object(m, s->body);
  p->camera_object = fp_unit_object(m, s->camera_unit);
  if (!p->body_object || !p->camera_object) {
    ReleaseSRWLockExclusive(&p->lock);
    return why(error, size, "Native camera objects unavailable");
  }
  HMODULE pinned = NULL;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_PIN,
                          (LPCWSTR)(uintptr_t)camera_setter, &pinned)) {
    ReleaseSRWLockExclusive(&p->lock);
    return why(error, size, "Cannot retain native callback lifetime");
  }
  p->frame_valid = false;
  atomic_store(&p->fallback_active, false);
  publish(p);
  p->closing = false;
  p->opening = false;
  p->sequence = 1;
  p->sampled = 0;
  p->previous = p->motion_previous = 0;
  p->tracking = p->native_seen = false;
  p->blend = 0;
  memset(&p->last_motion, 0, sizeof p->last_motion);
  atomic_fetch_add(&callback_generation, 1);
  atomic_store_explicit(&native_setter, (CameraSetter)target,
                        memory_order_release);
  atomic_store_explicit(&active, p, memory_order_release);
  FpDispatchSlot slot = {p->slot, target, (uintptr_t)camera_setter, false};
  unsigned result = fp_dispatch_begin(&p->lease, &slot, 1, fp_slot_read,
                                      fp_slot_cas, &p->platform, &p->token);
  if (!result)
    atomic_store(&active, NULL);
  ReleaseSRWLockExclusive(&p->lock);
  if (result != 1)
    return why(error, size,
               result == 2 ? "Eye-camera slot restoration pending"
                           : "Eye-camera dispatch changed");
  return true;
}
bool fp_camera_policy_close(FpCameraPolicy *p) {
  if (!p || !p->token)
    return true;
  AcquireSRWLockExclusive(&p->lock);
  p->closing = true;
  atomic_fetch_add(&callback_generation, 1);
  reset(p, false);
  p->frame_valid = false;
  atomic_store(&p->fallback_active, false);
  memset(&p->last_motion, 0, sizeof p->last_motion);
  publish(p);
  bool done = fp_dispatch_end(&p->lease, p->token, fp_slot_cas, &p->platform);
  if (done) {
    p->token = 0;
    if (atomic_load(&active) == p)
      atomic_store(&active, NULL);
  }
  ReleaseSRWLockExclusive(&p->lock);
  return done;
}
bool fp_camera_policy_options(FpCameraPolicy *p, const FpEyeOptions *options) {
  if (!p || !options || !isfinite(options->right) ||
      !isfinite(options->forward) || !isfinite(options->up) ||
      fabsf(options->right) > .5f || fabsf(options->forward) > .5f ||
      fabsf(options->up) > .5f)
    return false;
  AcquireSRWLockExclusive(&p->lock);
  const FpMotionOptions *a = &p->options.motion, *b = &options->motion;
  bool same =
      a->enabled == b->enabled && a->editor_frozen == b->editor_frozen &&
      a->shoulder_aim_enabled == b->shoulder_aim_enabled &&
      a->scale == b->scale && a->bob_strength == b->bob_strength &&
      a->bob_speed == b->bob_speed && a->sway_strength == b->sway_strength &&
      a->sway_speed == b->sway_speed &&
      a->sprint_strength == b->sprint_strength &&
      a->camera_bob_strength == b->camera_bob_strength &&
      a->camera_bob_speed == b->camera_bob_speed &&
      a->camera_sway_strength == b->camera_sway_strength &&
      a->camera_sway_speed == b->camera_sway_speed;
  bool done = same || fp_motion_configure(&p->motion, &options->motion);
  if (done) {
    p->options = *options;
  }
  ReleaseSRWLockExclusive(&p->lock);
  return done;
}
void fp_camera_policy_frame(FpCameraPolicy *p, uint64_t sequence,
                            bool opening) {
  if (!p)
    return;
  AcquireSRWLockExclusive(&p->lock);
  p->sequence = sequence;
  p->opening = opening;
  ReleaseSRWLockExclusive(&p->lock);
}
bool fp_camera_policy_free_aim(FpCameraPolicy *p, uint32_t milliseconds) {
  /* Parked, matching the accepted native-camera baseline. */
  (void)p;
  (void)milliseconds;
  return false;
}

void fp_camera_policy_actions(FpCameraPolicy *p, bool sprint) {
  if (!p)
    return;
  AcquireSRWLockExclusive(&p->lock);
  p->sprint = sprint;
  ReleaseSRWLockExclusive(&p->lock);
}
