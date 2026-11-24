// The frame scripts of tests/diff_camera_helper.cpp, and the runner (one child process per script).
#pragma once

namespace dt {

struct Script { std::string name; std::function<void(Run &)> run; bool probed = false; };
static std::vector<Script> scripts;
static void add(const std::string &name, std::function<void(Run &)> run, bool probed = false) { scripts.push_back({name, std::move(run), probed}); }

enum : uint32_t { KEY_A = 0x400006, KEY_B = 0x400007 }; // the two weapon units of the scene

static void set_slot(uint64_t which) { for (int w = 0; w < 2; ++w) World::put<uint64_t>(W[w].slot(), slot_raw(w, which)); }
static void walk_pose(Run &r, float t, bool moving) {
  if (moving) {
    r.pose(1.5f * t, .4f * std::sin(t * 2), .02f * std::sin(t * 9), .5f * std::sin(t), 1.5f + .03f * std::sin(t * 11), .05f * std::sin(t * 3), .06f + .05f * std::cos(t * 2.3f));
    r.look(.3f + .8f * std::sin(t * .7f), .4f * std::sin(t * 1.3f));
  } else { r.pose(3, -2, .5f, .2f); r.look(.3f, -.1f); }
}
struct Clock { float t = 0; bool moving = true; };
static void print_reached(const Run &r) {
  const Run::Reached &g = r.got;
  std::printf("REACHED %s: replaced %llu, native %llu, eye hold %llu, glides %llu, anchor %llu (learned %llu), exit holds %llu, seated %llu, grace %llu, correction %llu, entry ease %llu/%llu, "
              "rotation ease %llu/%llu, pending %llu; errors", script_name.c_str(), (unsigned long long)g.replaced, (unsigned long long)g.native, (unsigned long long)g.eye_hold,
              (unsigned long long)g.glides, (unsigned long long)g.anchor, (unsigned long long)g.learned, (unsigned long long)g.exit_holds, (unsigned long long)g.seated, (unsigned long long)g.grace,
              (unsigned long long)g.correction, (unsigned long long)g.entry_started, (unsigned long long)g.entry_frames, (unsigned long long)g.turn_started, (unsigned long long)g.turn_frames, (unsigned long long)g.pending);
  for (int code = 0; code < 19; ++code) if (g.errors[code]) std::printf(" %d:%llu", code, (unsigned long long)g.errors[code]);
  std::printf("\n"); std::fflush(stdout);
}
static Run *current_run;
// A script that did not get where it was written to go is a broken script, not a passing one.
static void need(bool reached, const char *what) { if (!reached && current_run) print_reached(*current_run); if (!reached) fail("the script did not reach what it is for: %s", what); }
static void hip_frames(Run &r, Clock &c, int count, double dt, uint32_t mask = 0) {
  for (int i = 0; i < count; ++i) { c.t += static_cast<float>(dt / 1000); walk_pose(r, c.t, c.moving); r.hip(mask); r.frame(dt); }
}
static void native_frames(Run &r, Clock &c, int count, double dt, bool aim = false, uint32_t mask = 0) {
  for (int i = 0; i < count; ++i) { c.t += static_cast<float>(dt / 1000); walk_pose(r, c.t, c.moving); r.native(aim); r.s.mask = mask; r.frame(dt); }
}
static void start(Run &r, Clock &c, uint32_t mode = 15, bool present = true) {
  walk_pose(r, c.t, c.moving); r.hip();
  if (present) r.arm(mode); else { r.begin(); if (!r.result) fail("the script could not arm (error %u)", NADSC_Error()); }
}

// ---- hip view on the normal camera ----
static void script_hip(Run &r, double dt, bool moving, bool present) {
  Clock c; c.moving = moving; r.smoothing = 100; r.blend = 120; start(r, c, 15, present);
  hip_frames(r, c, 300, dt);
  // The head jerks about faster than the 100 ms smoothing follows: the lag is clamped to 6 cm.
  for (int i = 0; i < 120; ++i) {
    c.t += static_cast<float>(dt / 1000);
    r.pose(1.5f * c.t, .3f * c.t, 0, .4f * c.t, 1.5f + .2f * std::sin(i * 1.7f), .15f * std::sin(i * 2.9f), .15f * std::cos(i * 1.3f)); r.look(.3f + c.t, .2f); r.hip(); r.frame(dt);
  }
  need(r.got.replaced == 420, "every frame replaced");
  r.end(r.gen);
}
// ---- the game's own first-person request, forwarded untouched ----
static void script_native(Run &r, bool present) {
  Clock c; start(r, c, 0, present);
  hip_frames(r, c, 20, 16); native_frames(r, c, 40, 16); hip_frames(r, c, 30, 16);
  native_frames(r, c, 12, 16, true); hip_frames(r, c, 30, 16);
  need(r.got.native == 52 && (r.got.correction > 10) == present, "native frames forwarded; the 150 ms correction only with presentation");
  r.end(r.gen);
}
// ---- kind 6 -> kind 4 exits, presentation on, cut-hold mode 15 ----
// 0: a remembered native eye (eye hold); 1: the exit frame comes 300 ms later; 2: the eye is further than 1 m;
// 3: no eye on record (the native stretch ran under an action mask); 4: the eye on record is older than 250 ms;
// 5: a refused normal frame (full reset) and one unsampled native frame come between the eye record and the exit;
// 6: the eye hold is switched off and on again (set_cut_hold) while it runs.
static void script_exit(Run &r, int variant, uint32_t mode, uint32_t exit_mask) {
  Clock c; start(r, c, mode); r.weapon_key(r.gen, KEY_A);
  hip_frames(r, c, 20, 16);
  if (variant == 2) r.eye[2] = 1.6f;
  if (variant == 3) native_frames(r, c, 8, 16, false, 0x40);
  else native_frames(r, c, 8, 16);
  if (variant == 4) native_frames(r, c, 20, 16, false, 0x40);
  if (variant == 5) { c.t += .016f; walk_pose(r, c.t, true); r.hip(); r.s.argument[0] = NAN; r.frame(16); native_frames(r, c, 1, 16, false, 0x40); }
  hip_frames(r, c, 1, variant == 1 ? 300 : 16, exit_mask);
  if (variant == 6) { hip_frames(r, c, 3, 16, exit_mask); r.cut_hold(r.gen, mode & ~2u); r.cut_hold(r.gen, mode); }
  hip_frames(r, c, 40, 16, exit_mask); hip_frames(r, c, 40, 16);
  native_frames(r, c, 6, 16); hip_frames(r, c, 10, 16);
  if (variant == 0) need((mode & 2) ? r.got.eye_hold > 40 : r.got.correction > 5, "eye hold with mode bit 1, else the 150 ms correction");
  r.end(r.gen);
}
// ---- weapon swap with the eye hold ----
// 0: to a weapon whose eye is known (70 ms glide); 1: to an unknown one; 2: an empty-handed action (80 ms fade);
// 3: the 2.5 s limit; 4: the 300 ms no-action tolerance, then back to the native view.
static void script_swap(Run &r, int variant, double dt) {
  Clock c; start(r, c); r.weapon_key(r.gen, KEY_A);
  hip_frames(r, c, 10, dt); native_frames(r, c, 8, dt); hip_frames(r, c, 5, dt, 2);
  r.weapon_key(r.gen, KEY_B); r.eye[0] = .05f; r.eye[1] = .14f; r.eye[2] = .01f;
  native_frames(r, c, 8, dt);
  if (variant == 0) { hip_frames(r, c, 2, dt, 2); r.weapon_key(r.gen, KEY_A); hip_frames(r, c, 40, dt, 2); r.eye[0] = 0; r.eye[1] = .1f; r.eye[2] = .05f; }
  if (variant == 1) { hip_frames(r, c, 2, dt, 2); r.weapon_key(r.gen, 9); hip_frames(r, c, 40, dt, 2); r.weapon_key(r.gen, 0); hip_frames(r, c, 10, dt, 2); }
  if (variant == 2) hip_frames(r, c, 70, dt);
  if (variant == 3) hip_frames(r, c, static_cast<int>(3400 / dt), dt, 2);
  if (variant == 4) { hip_frames(r, c, 20, dt, 2); hip_frames(r, c, static_cast<int>(280 / dt), dt); }
  native_frames(r, c, 8, dt); hip_frames(r, c, 20, dt);
  need(r.got.eye_hold > 20 && (r.got.glides > 5) == (variant == 0), "the eye hold; a glide only towards a known weapon");
  r.end(r.gen);
}
// ---- aim-release anchor ----
// 0: learned over stable aimed frames, used after release; 1: not learned (4 frames); 2: the weapon key changes
// just before release; 3: the weapon is too far from the ordinary eye; 4: the weapon hangs from no body node;
// 5: the aimed weapon wobbles 4.5 mm (inside the 5 mm that still counts as holding still); 6: the weapon's link
// names a node one past the body's last, after a good one was seen; 7: the smallest frame gap (16 ms), where the
// native view is forgotten between two frames while the anchor is still running; 8: the anchor is switched off and
// on again (set_cut_hold) while it runs.
static void script_anchor(Run &r, int variant, uint32_t mode) {
  Clock c; if (variant == 7) r.max_gap = 16;
  start(r, c, mode); r.weapon_key(r.gen, KEY_A);
  if (variant == 5) {
    hip_frames(r, c, 10, 10);
    for (int i = 0; i < 12; ++i) { r.grip[0][0] = i & 1 ? .0045f : 0.0f; native_frames(r, c, 1, 10, true); }
    r.grip[0][0] = 0; hip_frames(r, c, 10, 10);
    need(r.got.learned == 1 && r.got.anchor >= 5, "the anchor learned from a weapon that wobbles less than 5 mm");
    r.end(r.gen); return;
  }
  if (variant == 9) { // the reported weapon is the body, the camera or the rig itself: never a weapon position
    for (uint32_t key : {0x400003u, 0x400004u, 0x400005u, 0xffffffffu}) { r.weapon_key(r.gen, key); hip_frames(r, c, 4, 10); native_frames(r, c, 14, 10, true); hip_frames(r, c, 6, 10); }
    need(r.got.anchor <= 6, "no anchor from the body or the camera");
    r.end(r.gen); return;
  }
  if (variant == 10) { // release after release, the third-person weapon a little further from where the sights were: 0.8 m is the limit
    c.moving = false; hip_frames(r, c, 10, 10); native_frames(r, c, 12, 10, true);
    for (int step = 0; step < 220; ++step) {
      walk_pose(r, c.t, false); r.hip(); r.s.node42[2] += .9f + .002f * step; r.frame(10);
      native_frames(r, c, 2, 10, true);
    }
    need(r.got.learned == 1 && r.got.anchor >= 20 && r.got.anchor <= 200, "the anchor accepted near and refused far");
    r.end(r.gen); return;
  }
  if (variant == 7) {
    hip_frames(r, c, 10, 10); native_frames(r, c, 12, 10, true); hip_frames(r, c, 1, 10); native_frames(r, c, 1, 10, true);
    hip_frames(r, c, 1, 17); hip_frames(r, c, 5, 10);
    native_frames(r, c, 7, 10, true); hip_frames(r, c, 2, 10); hip_frames(r, c, 1, 16.5); hip_frames(r, c, 10, 10);
    need(r.got.learned == 1 && r.got.anchor >= 3, "the anchor at the smallest gap");
    r.end(r.gen); return;
  }
  bool later = false;
  auto released = [&](int count) {
    for (int i = 0; i < count; ++i) {
      c.t += .01f; walk_pose(r, c.t, c.moving); r.hip();
      if (variant == 3) for (int k = 0; k < 3; ++k) r.s.node42[k] += 2;
      if (variant == 4) { r.s.weapon[0].parent = 0; for (int k = 0; k < 3; ++k) r.s.weapon[0].position[k] += .05f; }
      if (variant == 6 && later) { r.s.weapon[0].link = (static_cast<uint32_t>(BODY_NODES) << 16) | 2u; for (int k = 0; k < 3; ++k) r.s.weapon[0].position[k] += .05f; }
      r.frame(10);
    }
  };
  released(10); later = true;
  native_frames(r, c, variant == 1 ? 4 : 12, 10, true);
  if (variant == 2) { r.weapon_key(r.gen, KEY_B); native_frames(r, c, 2, 10, true); }
  if (variant == 8) { released(1); r.cut_hold(r.gen, mode & ~4u); r.cut_hold(r.gen, mode); released(3); r.cut_hold(r.gen, mode & ~4u); released(2); r.cut_hold(r.gen, mode); }
  released(14);
  native_frames(r, c, 7, 10, true); released(3); native_frames(r, c, 3, 10, true); released(20);
  native_frames(r, c, 7, 10, false); released(5);
  if (variant == 0 || variant == 4 || variant == 6) need(r.got.learned >= 1 && r.got.anchor >= 8, "the anchor learned and applied");
  if (variant == 3) need(r.got.learned >= 1 && r.got.anchor == 0, "the anchor learned but refused");
  r.end(r.gen);
}
// ---- exit hold: kind 4 while the camera is still linked to the first-person rig ----
static void script_exit_hold(Run &r, uint32_t mode) {
  Clock c; start(r, c, mode);
  hip_frames(r, c, 10, 16); native_frames(r, c, 8, 16);
  for (int i = 0; i < 3; ++i) { c.t += .016f; walk_pose(r, c.t, true); r.hip(); r.s.linked = true; r.frame(16); }
  hip_frames(r, c, 10, 16); native_frames(r, c, 5, 16);
  // the clock steps back, but not behind the last native frame: the hold counts from its own last frame
  for (double dt : {16.0, 16.0, -10.0, 4.0, 16.0}) { walk_pose(r, c.t, true); r.hip(); r.s.linked = true; r.frame(dt); }
  hip_frames(r, c, 6, 16); native_frames(r, c, 5, 16);
  for (int i = 0; i < 20; ++i) { c.t += .016f; walk_pose(r, c.t, true); r.hip(); r.s.linked = true; r.frame(i == 4 ? 300 : 16); }
  // linked to something that is not the proven rig
  native_frames(r, c, 5, 16);
  for (int i = 0; i < 3; ++i) { walk_pose(r, c.t, true); r.hip(); r.s.linked = true; r.s.camera_link = 0x00320003; r.frame(16); }
  r.s.camera_link = 0x00320002;
  hip_frames(r, c, 5, 16);
  need((r.got.exit_holds >= 8) == ((mode & 1) != 0), "exit-hold frames with mode bit 0 only");
  r.end(r.gen);
}
// ---- seated view ----
static void script_seated(Run &r, uint32_t mode, double dt) {
  Clock c; start(r, c, mode);
  hip_frames(r, c, 20, dt); native_frames(r, c, 5, dt);
  for (int i = 0; i < 60; ++i) {
    c.t += static_cast<float>(dt / 1000); walk_pose(r, c.t, true); r.hip(); r.s.flags[14] = 4;
    // the game's own camera: far off, turning towards what the interaction is about
    for (int k = 0; k < 3; ++k) r.s.argument[k] += 30;
    Run turned = r; turned.look(1.7f + .01f * i, .9f); turned.rotation_from_look(); std::memcpy(r.s.argument_rotation, turned.s.argument_rotation, 16);
    if (i == 30) r.s.argument_rotation[3] = NAN;
    if (i == 40) r.s.mask = 2;
    if (i == 45) r.s.guard = 1;
    r.frame(i == 50 ? 0.0 : dt);
    r.s.guard = 0;
  }
  r.s.flags[14] = 0; hip_frames(r, c, 20, dt);
  need((r.got.seated > 40) == ((mode & 8) != 0), "seated frames with mode bit 3 only");
  r.end(r.gen);
}
// ---- objections: guard byte, masks, not on foot, arguments that are not ours ----
static void script_objections(Run &r, uint32_t mode, bool present) {
  Clock c; start(r, c, mode, present);
  hip_frames(r, c, 20, 16);
  r.s.guard = 1; hip_frames(r, c, 1, 16); r.s.guard = 0; hip_frames(r, c, 10, 16);
  r.s.guard = 1; hip_frames(r, c, 1, 0); hip_frames(r, c, 2, 0); r.s.guard = 0; hip_frames(r, c, 5, 16); // the guard arrives on a repeated timestamp
  r.s.guard = 1; hip_frames(r, c, 30, 16); r.s.guard = 0; hip_frames(r, c, 10, 16);
  r.s.guard = 1; hip_frames(r, c, 1, 250); hip_frames(r, c, 1, .5); r.s.guard = 0; hip_frames(r, c, 5, 16);
  hip_frames(r, c, 30, 16, 0x20); hip_frames(r, c, 5, 16);
  r.s.guard = 1; hip_frames(r, c, 3, 16, 0x20); r.s.guard = 0;
  for (uint32_t mask : {1u, 3u, 4u, 0x40u, 0x22u, 0x10020u}) { hip_frames(r, c, 4, 16, mask); hip_frames(r, c, 3, 16); }
  r.s.state = 5; hip_frames(r, c, 5, 16); r.s.state = 36; hip_frames(r, c, 5, 16);
  r.s.state = 3; hip_frames(r, c, 3, 16); r.s.state = 36; hip_frames(r, c, 3, 16);
  r.camera_argument_delta = 8; hip_frames(r, c, 3, 16); r.camera_argument_delta = 0;
  r.unit_argument_xor = 1; hip_frames(r, c, 3, 16); r.unit_argument_xor = 0;
  r.caller_ours = false; hip_frames(r, c, 3, 16); native_frames(r, c, 2, 16); r.caller_ours = true;
  r.argument_null = true; hip_frames(r, c, 2, 16); native_frames(r, c, 2, 16); r.argument_null = false;
  hip_frames(r, c, 5, 16);
  // an unsupported request kind, an empty ring, a request for another avatar
  r.s.request[0] = 5; r.frames(3, 16); r.s.request[0] = 4;
  r.s.ring[1] = 0; r.frames(3, 16); r.s.idle[0] = 6; r.frames(2, 16); r.s.idle[3] = 0x400003; r.frames(2, 16); r.s.idle[0] = 4; r.frames(2, 16); r.s.idle[3] = 0; r.s.ring[1] = 1;
  r.s.ring[0] = 32; r.frames(2, 16); r.s.ring[0] = 0;
  r.s.request[1] = 0x334; r.frames(2, 16); r.s.request[1] = 0x333;
  r.s.ring[0] = 5; r.s.ring[1] = 9; hip_frames(r, c, 3, 16); r.s.ring[0] = 0; r.s.ring[1] = 1;
  hip_frames(r, c, 5, 16);
  need(r.got.grace >= 10 && r.got.errors[18] >= 5, "guard grace frames and not-on-foot refusals");
  r.end(r.gen);
}
// ---- gates ----
static void script_gates(Run &r, bool present) {
  Clock c; start(r, c, 15, present);
  hip_frames(r, c, 15, 16);
  auto one = [&](std::function<void()> change, int count = 2) {
    for (int i = 0; i < count; ++i) { c.t += .016f; walk_pose(r, c.t, true); r.hip(); const Scene saved = r.s; change(); r.frame(16); r.s = saved; }
    hip_frames(r, c, 4, 16);
  };
  one([&] { r.s.argument[0] = r.s.head[0] + 8.5f; r.s.argument[1] = r.s.head[1]; r.s.argument[2] = r.s.head[2]; });                 // head further than 8 m
  one([&] { r.s.argument[0] = r.s.head[0] + 7.9f; r.s.argument[1] = r.s.head[1]; r.s.argument[2] = r.s.head[2]; });                 // just inside
  one([&] { r.s.head[2] = r.s.root[14] + 2.9f; r.s.argument[0] = r.s.root[12]; r.s.argument[1] = r.s.root[13]; r.s.argument[2] = r.s.root[14] + 10.5f; }); // root further than 10 m
  one([&] { r.s.argument[1] = NAN; });
  one([&] { r.s.argument[2] = INFINITY; });
  one([&] { r.s.argument[3] = NAN; });                                                                                             // the fourth float is not tested
  one([&] { for (int k = 0; k < 3; ++k) r.s.context[k] *= 1.1f; });                                                                // basis not orthonormal
  one([&] { r.s.context[4] = NAN; });
  one([&] { for (int k = 0; k < 11; ++k) r.s.context[k] *= .99f; });                                                               // each axis 1% short: lengths pass, handedness .9703 does not
  one([&] { for (int k = 0; k < 11; ++k) r.s.context[k] *= .995f; });
  one([&] { for (int k = 0; k < 12; ++k) r.s.root[k] *= .99f; });
  one([&] { for (int k = 0; k < 3; ++k) { r.s.context[8 + k] = -r.s.context[8 + k]; } });                                          // left-handed
  one([&] { r.s.graph_count = BODY_NODES - 1; });                                                                                  // graph changed
  one([&] { r.s.graph_count = 0; });
  one([&] { r.s.head_hash = 0x8C5570C8; });                                                                                        // head node hash changed
  one([&] { r.s.head_link = (HEAD_PARENT << 16) | 2u; });
  one([&] { r.s.head_link = (600u << 16) | 1u; });
  one([&] { r.s.parent_hash = 1; });
  one([&] { r.s.head[0] = r.s.root[12]; r.s.head[1] = r.s.root[13]; r.s.head[2] = r.s.root[14] + .04f; });                                                                                 // head closer than 0.05 m to the root
  one([&] { r.s.head[2] = r.s.root[14] + 3.1f; });
  one([&] { r.s.head[0] = r.s.root[12]; r.s.head[1] = r.s.root[13]; r.s.head[2] = r.s.root[14] + .055f; });                                                                                // just far enough
  one([&] { r.s.head[2] = r.s.root[14] + 2.95f; });
  one([&] { r.s.head[0] = NAN; });
  one([&] { r.s.root[0] *= 1.2f; });                                                                                               // body basis not orthonormal
  one([&] { r.s.root[13] = NAN; });
  one([&] { r.look(.4f, 1.5707963267948966f); r.hip(); });                                                                         // looking straight up: body yaw is used
  one([&] { r.look(.4f, 1.5707963267948966f); r.hip(); const float m[12] = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0}; std::memcpy(r.s.root, m, sizeof m); }); // and the body has no yaw either
  one([&] { r.s.camera_split = true; for (int k = 0; k < 3; ++k) { r.s.camera_local[k] = 1; r.s.camera_world[k] = 1.02f; } });     // local and world apart
  one([&] { r.s.camera_split = true; for (int k = 0; k < 3; ++k) { r.s.camera_local[k] = 1; r.s.camera_world[k] = 1.004f; } });
  one([&] { r.s.camera_split = true; r.s.camera_local[0] = NAN; });
  one([&] { r.s.camera_index = 1; });
  one([&] { r.s.camera_count = 0; });
  one([&] { r.s.camera_count = 513; });
  one([&] { r.s.parent_raw = 0x10; });
  one([&] { r.s.parent_raw = W[0].a[A_FPS] ? 0x7fff00000000ull : 0; });
  hip_frames(r, c, 5, 16);
  // pointers that change under an armed helper
  auto poked = [&](int id, uint64_t offset, Value v, int count = 2) {
    r.poke(id, offset, v);
    for (int i = 0; i < count; ++i) { c.t += .016f; walk_pose(r, c.t, true); r.hip(); r.frame(16); }
    r.pokes.clear();
    // put the builders' values back
    for (int w = 0; w < 2; ++w) {
      World::put<uint64_t>(W[w].a[A_BODY] + 0xA0, W[w].a[A_HASHES]); World::put<uint64_t>(W[w].a[A_BODY] + 0x98, W[w].a[A_LINKS]);
      World::put<uint64_t>(W[w].a[A_BODY] + 0x88, W[w].a[A_BODY_WORLD]); World::put<uint64_t>(W[w].a[A_BODY_VT] + 0xE8, W[w].a[A_EXE] + W[w].L->graph_getter);
      World::put<uint64_t>(W[w].a[A_BODY_VT] + 0x1B0, W[w].a[A_EXE] + W[w].L->anim_getter); World::put<uint64_t>(W[w].a[A_BODY] + 0x358, 0x4D1C334D294DFA97ULL);
      World::put<uint64_t>(W[w].a[A_CAMERA] + 0x18, W[w].a[A_CAMOBJ] + 0x60); World::put<uint32_t>(W[w].a[A_ANIM_GRAPH] + 4, 31);
      World::literal(W[w].a[A_EXE] + W[w].L->graph_getter, "488d4160c3"); World::literal(W[w].a[A_EXE] + W[w].L->anim_getter, "488b8178010000c3");
      World::put<uint64_t>(W[w].a[A_CAMOBJ] + 0x88, W[w].a[A_CAM_WORLDS]); World::put<uint64_t>(W[w].a[A_CAMOBJ] + 0x80, W[w].a[A_CAM_LOCALS]);
    }
    hip_frames(r, c, 4, 16);
  };
  poked(A_BODY, 0xA0, address_value(A_FPS_HASHES));          // another hash table
  poked(A_BODY, 0x98, address_value(A_FPS_LINKS));
  poked(A_BODY, 0x88, address_value(A_FPS_WORLD));           // another world table: the commit refuses
  poked(A_BODY_VT, 0xE8, address_value(A_E_ANIM_GETTER));    // the graph getter is another function
  poked(A_E_GRAPH_GETTER, 0, bytes_value(0x90, 1));          // its code changed
  poked(A_BODY_VT, 0x1B0, address_value(A_E_GRAPH_GETTER));
  poked(A_E_ANIM_GETTER, 3, bytes_value(0x79, 1));
  poked(A_BODY, 0x358, bytes_value(0x4D1C334D294DFA98ULL, 8));
  poked(A_ANIM_GRAPH, 4, bytes_value(30));
  poked(A_CAMERA, 0x18, address_value(A_CAMOBJ, 0x68));
  poked(A_CAMOBJ, 0x88, bytes_value(0, 8));
  poked(A_CAMOBJ, 0x80, bytes_value(8, 8));
  hip_frames(r, c, 5, 16);
  need(r.got.errors[14] >= 10 && r.got.errors[15] >= 8 && r.got.errors[16] >= 10 && r.got.errors[18] >= 4, "pose, space, graph and on-foot refusals");
  r.end(r.gen);
}
// ---- owner lost: the helper disarms; the adapter ends and arms again ----
static void script_owner_lost(Run &r, int what) {
  Clock c; start(r, c);
  hip_frames(r, c, 10, 16); if (what & 1) native_frames(r, c, 5, 16);
  const Scene saved = r.s;
  switch (what >> 1) {
  case 0: r.s.generations[3] = 2; break;                       // the body unit's generation
  case 1: r.s.generations[4] = 2; break;                       // the camera unit's
  case 2: r.poke(A_DESCRIPTOR, 12, bytes_value(0x400009)); break;
  case 3: r.poke(A_CONTEXT, 8, bytes_value(0x400009)); break;
  case 4: set_slot(SLOT_FOREIGN); break;                        // someone else took the dispatch slot
  case 5: set_slot(SLOT_NATIVE); break;
  case 6: r.poke(A_REGISTRY, 0x98, bytes_value(3)); break;      // the registry shrank
  case 7: r.s.avatar_count = 0; break;
  case 8: r.poke(A_G_CONTEXT, 0, address_value(A_CAMERA)); break;
  case 9: r.poke(A_G_API_ROOT, 0, bytes_value(0, 8)); break;
  case 10: r.poke(A_UNITS, 3 * 8, address_value(A_FPS)); break; // the body reference names another object
  case 11: r.poke(A_G_CONTROLLER, 0, address_value(A_DESCRIPTOR)); break;
  case 12: r.poke(A_DESCRIPTOR, 0, bytes_value(1, 1)); break;      // a descriptor byte the helper does not interpret
  case 13: r.poke(A_DESCRIPTOR, 19, bytes_value(1, 1)); break;
  case 14: r.poke(A_OWNER, 0x110, address_value(A_DESCRIPTOR, 0x100)); for (int w = 0; w < 2; ++w) { World::put<uint32_t>(W[w].a[A_DESCRIPTOR] + 0x108, 0x333); World::put<uint32_t>(W[w].a[A_DESCRIPTOR] + 0x10c, 0x400003); } break; // the same descriptor at another address
  case 15: r.poke(A_REGISTRY, 0x88, address_value(A_ANIM_GRAPH)); break; // another unit table
  case 16: r.poke(A_REGISTRY, 0xA0, address_value(A_ANIM_GRAPH)); break;
  case 17: r.poke(A_E_REGISTRY, 0, address_value(A_ANIM_GRAPH)); break;
  case 18: r.poke(A_UNITS, 4 * 8, address_value(A_FPS)); break;   // the camera reference names another object
  case 19: r.poke(A_API_ROOT, 0x20, address_value(A_API, 8)); break; // the API table moved
  }
  if (what & 1) native_frames(r, c, 3, 16); else hip_frames(r, c, 3, 16);
  r.s = saved; r.pokes.clear();
  for (int w = 0; w < 2; ++w) {
    World::put<uint32_t>(W[w].a[A_DESCRIPTOR] + 12, 0x400003); World::put<uint32_t>(W[w].a[A_CONTEXT] + 8, 0x400004); World::put<uint32_t>(W[w].a[A_REGISTRY] + 0x98, 16);
    World::put<uint64_t>(W[w].a[A_GAME] + W[w].L->g_context, W[w].a[A_CONTEXT]); World::put<uint64_t>(W[w].a[A_GAME] + W[w].L->g_api_root, W[w].a[A_API_ROOT]);
    World::put<uint64_t>(W[w].a[A_UNITS] + 3 * 8, W[w].a[A_BODY]); World::put<uint64_t>(W[w].a[A_GAME] + W[w].L->g_controller, W[w].a[A_CONTROLLER]);
    World::put<uint8_t>(W[w].a[A_DESCRIPTOR], 0); World::put<uint8_t>(W[w].a[A_DESCRIPTOR] + 19, 0); World::put<uint64_t>(W[w].a[A_OWNER] + 0x110, W[w].a[A_DESCRIPTOR]);
    World::put<uint64_t>(W[w].a[A_REGISTRY] + 0x88, W[w].a[A_UNITS]); World::put<uint64_t>(W[w].a[A_REGISTRY] + 0xA0, W[w].a[A_GENERATIONS]); World::put<uint64_t>(W[w].a[A_EXE] + W[w].L->e_registry, W[w].a[A_REGISTRY]);
    World::put<uint64_t>(W[w].a[A_UNITS] + 4 * 8, W[w].a[A_CAMOBJ]); World::put<uint64_t>(W[w].a[A_API_ROOT] + 0x20, W[w].a[A_API]);
  }
  hip_frames(r, c, 3, 16);
  r.end(r.gen);
  set_slot(SLOT_NATIVE);
  hip_frames(r, c, 2, 16);
  r.arm(); hip_frames(r, c, 10, 16); native_frames(r, c, 5, 16); hip_frames(r, c, 5, 16);
  r.end(r.gen);
}
// ---- time ----
static void script_time(Run &r, uint32_t gap) {
  Clock c; r.max_gap = gap; start(r, c);
  const double g = gap;
  const double steps[] = {16, 16, g + 50, 16, 16, -50, 16, 16, 0, 0, 0, 16, g, 16, g + .001, 16, g - .001, 5000, 16, -5000, 16, 16, 1e7, 16, .001, 16};
  for (double dt : steps) hip_frames(r, c, 1, dt);
  for (double dt : steps) native_frames(r, c, 1, dt);
  for (double dt : steps) { native_frames(r, c, 6, 16); hip_frames(r, c, 1, dt, 2); hip_frames(r, c, 3, 16, 2); hip_frames(r, c, 1, dt, 2); }
  for (double dt : steps) { hip_frames(r, c, 6, 16); native_frames(r, c, 1, dt); }
  r.s.guard = 1; for (double dt : steps) hip_frames(r, c, 1, dt); r.s.guard = 0;
  hip_frames(r, c, 5, 16);
  r.end(r.gen);
}
// ---- the control calls, in the adapter's order and in wrong orders ----
static void script_controls(Run &r) {
  Clock c; walk_pose(r, 0, true); r.hip();
  r.end(0); r.end(1); r.presentation(0); r.presentation(1); r.cut_hold(0, 1); r.cut_hold(0, 0); r.handoff(0, 0, 0); r.handoff(1, 10, 10); r.weapon_key(0, 5);
  r.read_stats(0); r.read_stats(1); r.read_stats(0, true); r.handoff_stats(0); r.handoff_stats(1); r.stats_nowhere(0); r.frames(2, 16);
  if (r.validate() != 1) fail("validate refused the plain scene (error %u)", NADSC_Error());
  r.begin(); const uint64_t g = r.gen;
  r.begin(); r.validate();                                   // busy
  r.read_stats(0); r.read_stats(g); r.read_stats(g + 1); r.read_stats(g, true); r.handoff_stats(g + 1); r.handoff_stats(0); r.handoff_stats(g); r.stats_nowhere(g);
  r.cut_hold(g, 1); r.cut_hold(g, 15); r.handoff(g, 100, 0); r.handoff(g, 0, 100);   // before set_presentation
  r.handoff(g, 0, 0); r.handoff(g, -1, 0); r.handoff(g, 501, 0); r.handoff(g, NAN, 0); r.handoff(g, 0, INFINITY); r.handoff(g + 1, 0, 0); r.handoff(g + 1, 900, 0);
  r.cut_hold(g, 0); r.cut_hold(g, 16);
  hip_frames(r, c, 5, 16);
  r.presentation(g + 1); r.presentation(0); r.presentation(g); r.presentation(g);
  r.cut_hold(g, 16); r.cut_hold(g + 1, 3); r.cut_hold(g, 15); r.cut_hold(g, 0xffffffffu);
  r.handoff(g, 100, 200); r.handoff(g, 500, 500); r.handoff(g, 500.5f, 0);
  r.weapon_key(g + 1, 5); r.weapon_key(g, 5); r.weapon_key(g, KEY_A);
  hip_frames(r, c, 10, 16); native_frames(r, c, 8, 16); hip_frames(r, c, 8, 16);
  r.cut_hold(g, 2); hip_frames(r, c, 3, 16); r.cut_hold(g, 4); native_frames(r, c, 6, 16, true); r.cut_hold(g, 0); hip_frames(r, c, 3, 16); r.cut_hold(g, 15);
  r.handoff(g, 0, 0); hip_frames(r, c, 3, 16);
  r.end(g + 1); r.end(0); r.end(g); r.end(g); r.end(g + 1); r.end(g - 1);
  r.presentation(g); r.cut_hold(g, 0); r.weapon_key(g, 1); r.handoff(g, 0, 0); r.read_stats(g); r.read_stats(0); r.handoff_stats(g);
  hip_frames(r, c, 3, 16); native_frames(r, c, 2, 16);        // the hook after end
  r.begin(); r.end(g); r.cut_hold(r.gen, 15); hip_frames(r, c, 5, 16); r.presentation(g); r.presentation(r.gen); r.cut_hold(r.gen, 15);
  native_frames(r, c, 8, 16); hip_frames(r, c, 5, 16, 2);
  r.end(r.gen); r.end(g);
  r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen);
}
// ---- set_presentation again in mid-lease drops the remembered native eye and a running correction ----
static void script_presentation_again(Run &r) {
  Clock c; start(r, c, 1);
  hip_frames(r, c, 10, 16); native_frames(r, c, 8, 16); r.presentation(r.gen); hip_frames(r, c, 10, 16);
  need(r.got.correction == 0, "no correction after the second set_presentation");
  native_frames(r, c, 8, 16); hip_frames(r, c, 4, 16); need(r.got.correction >= 3, "a running correction");
  r.presentation(r.gen); const uint64_t ran = r.got.correction; hip_frames(r, c, 10, 16);
  need(r.got.correction == ran, "the correction stopped by set_presentation");
  r.end(r.gen);
}
// ---- configurations and worlds that must be refused ----
static void script_refusals(Run &r) {
  Clock c; walk_pose(r, 0, true); r.hip();
  auto refused = [&](std::function<void()> change) {
    const Run saved = r; change(); r.validate(); r.begin();
    if (r.result == 1) r.end(r.gen);
    const uint64_t gen = r.gen, previous = r.previous_gen; r = saved; r.gen = gen; r.previous_gen = previous;
    r.sync();
    for (int w = 0; w < 2; ++w) { const float zero[3] = {}; World::put(W[w].a[A_CAM_LOCALS] + 0x24, zero); World::put(W[w].a[A_CAM_WORLDS] + 0x30, zero); }
  };
  refused([&] { r.size = 87; }); refused([&] { r.abi = 2; }); refused([&] { r.reserved_field = 1; }); refused([&] { r.index = 8; }); refused([&] { r.index = 2; });
  refused([&] { r.avatar = 0; }); refused([&] { r.avatar = 0xffffffffu; }); refused([&] { r.body_unit = 0; }); refused([&] { r.body_unit = 0xffffffffu; });
  refused([&] { r.camera_unit = 0; }); refused([&] { r.camera_unit = 0xffffffffu; });
  refused([&] { r.forward = .51f; }); refused([&] { r.forward = -.5f; }); refused([&] { r.forward = NAN; }); refused([&] { r.up = -.51f; }); refused([&] { r.up = INFINITY; });
  refused([&] { r.smoothing = -1; }); refused([&] { r.smoothing = 500.5f; }); refused([&] { r.smoothing = NAN; }); refused([&] { r.blend = 1001; }); refused([&] { r.blend = -.5f; });
  refused([&] { r.max_gap = 15; }); refused([&] { r.max_gap = 1001; }); refused([&] { r.max_gap = 16; }); refused([&] { r.max_gap = 1000; });
  refused([&] { r.config_null = true; }); refused([&] { r.generation_null = true; });
  refused([&] { r.context_absolute = 65535; }); refused([&] { r.camera_absolute = 65535; }); refused([&] { r.context_absolute = 0x10000; }); refused([&] { r.camera_absolute = 0x10000; });
  refused([&] { r.context_delta = 8; }); refused([&] { r.camera_delta = 16; }); refused([&] { r.owner_delta = 8; }); refused([&] { r.game_delta = 0x1000; }); refused([&] { r.exe_delta = 0x1000; });
  refused([&] { r.avatar = 0x334; }); refused([&] { r.body_unit = 0x400005; }); refused([&] { r.body_unit = 0x800003; }); refused([&] { r.camera_unit = 0x400003; });
  refused([&] { r.s.avatar_count = 0; }); refused([&] { r.s.avatar_count = 9; }); refused([&] { r.s.avatar_count = 1; r.index = 1; });
  refused([&] { r.s.mask = 4; }); refused([&] { r.s.mask = 0x20; }); refused([&] { r.s.mask = 2; }); refused([&] { r.s.guard = 1; }); refused([&] { r.s.flags[14] = 4; }); refused([&] { r.s.aim = 1; });
  refused([&] { r.s.request[0] = 5; }); refused([&] { r.s.request[0] = 6; }); refused([&] { r.native(); }); refused([&] { r.s.ring[1] = 0; }); refused([&] { r.s.ring[1] = 0; r.s.idle[0] = 6; }); refused([&] { r.s.ring[1] = 0; r.s.idle[0] = 6; r.s.idle[3] = 0x400003; });
  refused([&] { r.s.linked = true; }); refused([&] { r.s.camera_split = true; r.s.camera_world[0] = 1; });
  refused([&] { r.s.context[0] = 2; }); refused([&] { r.s.head_hash = 5; });
  refused([&] { r.poke(A_HASHES, 10 * 4, bytes_value(0x8C5570C9)); }); for (int w = 0; w < 2; ++w) World::put<uint32_t>(W[w].a[A_HASHES] + 10 * 4, 0x1000 + 10); /* two head nodes */
  refused([&] { r.s.graph_count = 513; }); refused([&] { r.s.graph_count = 512; }); refused([&] { r.s.camera_count = 512; }); refused([&] { r.s.camera_count = 513; }); refused([&] { r.s.head[2] = r.s.root[14] + 5; });
  refused([&] { r.s.state = 5; });                           // not on foot does not stop arming
  // the images and the code
  auto poked = [&](int id, uint64_t offset, Value v) { refused([&] { r.poke(id, offset, v); }); for (int w = 0; w < 2; ++w) { World::pe_header(W[w].a[A_GAME], W[w].L->game_stamp, W[w].L->game_size); World::pe_header(W[w].a[A_EXE], W[w].L->exe_stamp, W[w].L->exe_size);
      World::literal(W[w].a[A_GAME] + W[w].L->call_pair, W[w].L->hex_call_pair); World::literal(W[w].a[A_EXE] + W[w].L->setter, W[w].L->hex_setter); World::literal(W[w].a[A_EXE] + W[w].L->unit_world, W[w].L->hex_unit_world);
      World::literal(W[w].a[A_EXE] + W[w].L->graph_getter, "488d4160c3"); World::literal(W[w].a[A_EXE] + W[w].L->anim_getter, "488b8178010000c3");
      World::put<uint64_t>(W[w].a[A_GAME] + W[w].L->g_api_root, W[w].a[A_API_ROOT]); World::put<uint64_t>(W[w].a[A_API_ROOT] + 0x20, W[w].a[A_API]);
      World::put<uint64_t>(W[w].a[A_EXE] + W[w].L->e_registry, W[w].a[A_REGISTRY]); World::put<uint64_t>(W[w].a[A_GAME] + W[w].L->g_owner, W[w].a[A_OWNER]); } };
  poked(A_GAME, 0, bytes_value(0x5A4E, 2)); poked(A_GAME, 0x3c, bytes_value(60)); poked(A_GAME, 0x3c, bytes_value(4097)); poked(A_GAME, 128, bytes_value(0x4551));
  poked(A_GAME, 132, bytes_value(0x8665, 2)); poked(A_GAME, 136, bytes_value(0x6af322c2)); poked(A_GAME, 152, bytes_value(0x10b, 2)); poked(A_GAME, 208, bytes_value(0x3383000));
  poked(A_EXE, 136, bytes_value(1)); poked(A_EXE, 208, bytes_value(1)); poked(A_EXE, 0, bytes_value(0, 2));
  poked(A_G_CALL_PAIR, 14, bytes_value(0x40, 1)); poked(A_G_CALL_PAIR, 17, bytes_value(0x28, 1));
  poked(A_E_SETTER, 0, bytes_value(0x49, 1)); poked(A_E_SETTER, 47, bytes_value(0x21, 1)); poked(A_E_SETTER, 20, bytes_value(0, 1));
  poked(A_E_UNIT_WORLD, 46, bytes_value(0xc2, 1)); poked(A_E_UNIT_WORLD, 10, bytes_value(0, 1)); poked(A_E_GRAPH_GETTER, 4, bytes_value(0xc2, 1)); poked(A_E_ANIM_GETTER, 7, bytes_value(0xc2, 1));
  poked(A_G_API_ROOT, 0, bytes_value(0, 8)); poked(A_API_ROOT, 0x20, bytes_value(0x100, 8)); poked(A_API_ROOT, 0x20, address_value(A_API, 4));
  poked(A_E_REGISTRY, 0, bytes_value(0, 8)); poked(A_G_OWNER, 0, address_value(A_CONTEXT));
  // the slot
  set_slot(SLOT_FOREIGN); r.validate(); r.begin(); set_slot(SLOT_HELPER); r.validate(); r.begin(); set_slot(0); r.validate(); r.begin(); set_slot(SLOT_NATIVE);
  if (r.validate() != 1) fail("validate refused the plain scene at the end (error %u)", NADSC_Error());
  r.arm(); hip_frames(r, c, 5, 16); r.end(r.gen);
}
// ---- the dispatch slot ----
static void script_slot(Run &r, int variant) {
  Clock c; walk_pose(r, 0, true); r.hip();
  switch (variant) {
  case 0: // a foreign value at begin, then the game's again
    set_slot(SLOT_FOREIGN); r.begin(); r.validate(); set_slot(SLOT_NATIVE); r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  case 1: // the swap faults at begin
    fixture_cas_fail_at = 1; r.begin(); r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  case 2: // someone else stores a value between the proof and the swap
    fixture_foreign_cas_at = 1; fixture_foreign_value = SLOT_FOREIGN; r.begin(); r.frames(2, 16); r.begin(); set_slot(SLOT_NATIVE); r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  case 3: // a foreign value at end: left alone, the lease still ends
    r.arm(); hip_frames(r, c, 3, 16); set_slot(SLOT_FOREIGN); hip_frames(r, c, 2, 16); r.end(r.gen); r.end(r.gen); r.begin(); set_slot(SLOT_NATIVE); r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  case 4: // the swap faults at end: cleanup pending, a second end finishes
    fixture_cas_fail_at = 2; r.arm(); hip_frames(r, c, 3, 16); r.end(r.gen); r.frames(2, 16); r.begin(); r.read_stats(r.gen); r.end(r.gen); r.frames(2, 16); r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  case 5: // a foreign value arrives just before the restoring swap
    fixture_foreign_cas_at = 2; fixture_foreign_value = SLOT_FOREIGN; r.arm(); hip_frames(r, c, 3, 16); r.end(r.gen); r.begin(); set_slot(SLOT_NATIVE); r.begin(); r.end(r.gen); break;
  case 6: // the slot's page is read-only at begin
    for (int w = 0; w < 2; ++w) protect(W[w].slot(), 8, PROT_READ);
    r.validate(); r.begin();
    for (int w = 0; w < 2; ++w) protect(W[w].slot(), 8, PROT_READ | PROT_WRITE);
    r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  case 7: // read-only at end
    r.arm(); hip_frames(r, c, 3, 16);
    for (int w = 0; w < 2; ++w) protect(W[w].slot(), 8, PROT_READ);
    r.end(r.gen); hip_frames(r, c, 2, 16); r.begin(); r.end(r.gen + 1);
    for (int w = 0; w < 2; ++w) protect(W[w].slot(), 8, PROT_READ | PROT_WRITE);
    r.end(r.gen); r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  case 8: // the owner changes right after the swap: begin undoes it
    fixture_mutate_after_cas = W[0].a[A_CONTROLLER]; fixture_mutation_value = 4; r.begin(); fixture_mutate_after_cas = 0; r.frames(2, 16); r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  case 9: // ... and undoing it faults too: begin answers 2, end finishes later
    fixture_mutate_after_cas = W[0].a[A_CONTROLLER]; fixture_mutation_value = 4; fixture_cas_fail_at = 2; r.begin(); fixture_mutate_after_cas = 0;
    r.frames(2, 16); r.begin(); r.read_stats(r.gen); r.presentation(r.gen); r.end(r.gen); r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  case 10: // module pinning fails
    shim_pin_fails = true; r.begin(); r.validate(); shim_pin_fails = false; r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  case 11: { // the slot's target is not executable code
    const uint64_t page = W[0].native_value & ~4095ull; shim_region(page, 4096, MEM_IMAGE, PAGE_READWRITE);
    r.validate(); r.begin(); --shim_region_count; r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break; }
  case 12: // the slot is not 8-aligned
    r.poke(A_API_ROOT, 0x20, address_value(A_API, 4)); set_slot(0); for (int w = 0; w < 2; ++w) World::put<uint64_t>(W[w].a[A_API] + 0x24, SLOT_FOREIGN);
    r.validate(); r.begin(); r.pokes.clear(); for (int w = 0; w < 2; ++w) World::put<uint64_t>(W[w].a[A_API_ROOT] + 0x20, W[w].a[A_API]);
    set_slot(SLOT_NATIVE); r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  case 13: // the generation cannot be written
    r.generation_null = true; r.begin(); r.generation_null = false; r.begin(); hip_frames(r, c, 3, 16); r.end(r.gen); break;
  }
}
// ---- the ship's bridge: state 3 counts as on foot only with the combat role and scene 3 ----
static void script_bridge(Run &r) {
  Clock c; start(r, c);
  hip_frames(r, c, 5, 16);
  r.s.state = 3; r.s.flags[0] = 2; r.s.scene = 3; hip_frames(r, c, 10, 16);
  r.s.scene = 4; hip_frames(r, c, 3, 16); r.s.scene = 3; hip_frames(r, c, 3, 16);
  r.s.flags[0] = 4; hip_frames(r, c, 3, 16); r.s.flags[0] = 6; hip_frames(r, c, 3, 16); r.s.flags[0] = 7; hip_frames(r, c, 3, 16); r.s.flags[0] = 0x12; hip_frames(r, c, 3, 16); r.s.flags[0] = 3; hip_frames(r, c, 3, 16); r.s.flags[0] = 0; hip_frames(r, c, 3, 16); r.s.flags[0] = 2;
  r.s.state = 4; hip_frames(r, c, 3, 16); r.s.state = 2; hip_frames(r, c, 3, 16); r.s.state = 37; hip_frames(r, c, 3, 16); r.s.state = 3;
  // an earlier state aliases the bridge state's offset
  r.poke(A_ANIM_GRAPH, 0x200 + 12 + 1 * 4, bytes_value(0x1000 + 3 * 16)); hip_frames(r, c, 3, 16); r.pokes.clear();
  for (int w = 0; w < 2; ++w) World::put<uint32_t>(W[w].definition() + 12 + 4, 0x1000 + 16);
  r.s.state = 36; r.poke(A_ANIM_GRAPH, 0x200 + 12 + 7 * 4, bytes_value(0x1000 + 36 * 16)); hip_frames(r, c, 3, 16); r.pokes.clear();
  for (int w = 0; w < 2; ++w) World::put<uint32_t>(W[w].definition() + 12 + 7 * 4, 0x1000 + 7 * 16);
  r.poke(A_ANIM_GRAPH, 0x200 + 8, bytes_value(36)); hip_frames(r, c, 2, 16); r.pokes.clear();
  r.poke(A_ANIM_GRAPH, 0x200 + 8, bytes_value(2049)); hip_frames(r, c, 2, 16); r.pokes.clear();
  for (int w = 0; w < 2; ++w) World::put<uint32_t>(W[w].definition() + 8, 128);
  r.poke(A_ANIM_GRAPH, 8, bytes_value(8)); hip_frames(r, c, 2, 16); r.pokes.clear();
  for (int w = 0; w < 2; ++w) World::put<uint32_t>(W[w].a[A_ANIM_GRAPH] + 8, 0x100);
  hip_frames(r, c, 5, 16); native_frames(r, c, 5, 16); r.s.state = 3; hip_frames(r, c, 5, 16, 2);
  need(r.got.replaced >= 25 && r.got.errors[18] >= 20, "bridge frames accepted and refused");
  r.end(r.gen);
}
// ---- the opt-in eases (set_handoff with nonzero values) ----
static void script_handoff(Run &r, float entry, float rotation, double dt) {
  Clock c; start(r, c); r.handoff(r.gen, entry, rotation); r.weapon_key(r.gen, KEY_A);
  // The rotation proof needs twelve matching, clearly rotated frames in a row; a set_handoff in between costs one.
  for (int i = 0; i < 16; ++i) { r.pose(1, 2, 0, .3f); r.look(1.2f + .001f * i, .3f); r.hip(); if (i == 6) r.handoff(r.gen, entry, rotation); r.frame(i == 3 ? 250.0 : dt); }
  hip_frames(r, c, 30, dt);
  float turned_from = 0;
  for (int round = 0; round < 8; ++round) {
    // the first native frame arrives before the camera is linked (rounds 0, 3), or with another parent (round 4)
    c.t += static_cast<float>(dt / 1000); walk_pose(r, c.t, true); r.native(round == 2, round != 0 && round != 3);
    if (round == 4) { r.s.linked = false; r.s.parent_raw = 0x20; }
    if (round == 5) r.s.rig_reference = 0;
    r.frame(dt); r.s.parent_raw = 0; r.s.rig_reference = 0x400005;
    native_frames(r, c, 10, dt, round == 2);
    if (round == 7) { turned_from = 1.0f; for (int k = 0; k < 3; ++k) { c.t += static_cast<float>(dt / 1000); walk_pose(r, c.t, true); r.look(turned_from, 0); r.native(); r.frame(dt); } }
    if (round == 1) { r.s.rig[0] = 2; r.frame(dt); }
    // leave: the game's rotation keeps turning
    for (int i = 0; i < 25; ++i) {
      c.t += static_cast<float>(dt / 1000); walk_pose(r, c.t, true); r.look(.3f + .8f * std::sin(c.t * .7f) + .02f * i, .4f * std::sin(c.t * 1.3f)); r.hip(round == 3 ? 2 : 0);
      // round 6: on the first frame out, the camera's own matrix is no rotation (the ease still starts from the recorded view)
      if (round == 6 && i == 0) { r.s.camera_rotation_given = true; r.s.camera_rotation[5] = NAN; }
      // round 7: the game's view is 120.5 degrees of yaw from the native one: a different view, presented at once
      if (round == 7) { r.look(turned_from + 2.1031f + .01f * i, 0); r.hip(); }
      if (round == 1 && i == 10) r.s.argument_rotation[0] = NAN;
      if (round == 2 && i == 3) { r.frame(400); continue; }
      r.frame(dt);
      // back into the native view for two frames in which the camera's matrix is no rotation, then out again
      if (round == 5 && i == 3) for (int k = 0; k < 2; ++k) { r.native(); r.s.camera_rotation[0] = NAN; r.frame(dt); }
    }
  }
  r.handoff(r.gen, 0, 0); native_frames(r, c, 5, dt); hip_frames(r, c, 5, dt);
  if (entry > 0) need(r.got.entry_started >= 3 && r.got.entry_frames >= 20 && r.got.pending >= 2, "the entry ease, also from a camera not linked yet");
  if (rotation > 0) need(r.got.turn_started >= 3 && r.got.turn_frames >= 20, "the rotation ease");
  r.end(r.gen);
}
// ---- a native view that lasts one frame, entered before the camera was linked: no history from it ----
static void script_pending_exit(Run &r, uint32_t mode) {
  Clock c; start(r, c, mode); r.handoff(r.gen, 120, 0);
  hip_frames(r, c, 20, 10);
  for (int round = 0; round < 3; ++round) {
    c.t += .01f; walk_pose(r, c.t, true); r.native(false, false); r.frame(10);
    if (round == 1) native_frames(r, c, 1, 10);
    if (round == 2) native_frames(r, c, 4, 10);
    hip_frames(r, c, 20, 10);
  }
  need(r.got.pending >= 3 && r.got.entry_frames >= 3, "the entry ease from a camera not linked yet");
  r.end(r.gen);
}
// ---- the second avatar row (the row stride of this build) ----
static void script_second_avatar(Run &r) {
  Clock c; r.index = 1;
  for (int w = 0; w < 2; ++w) { std::memset(reinterpret_cast<void *>(W[w].a[A_O_FLAGS0]), 0xff, 24); World::put<uint32_t>(W[w].a[A_O_RIG0], 0x400009); }
  start(r, c); r.weapon_key(r.gen, KEY_A);
  hip_frames(r, c, 10, 16); native_frames(r, c, 10, 16, true); hip_frames(r, c, 10, 16, 2);
  r.s.state = 3; r.s.flags[0] = 2; r.s.scene = 3; hip_frames(r, c, 5, 16); r.s.state = 36;
  r.s.flags[14] = 4; hip_frames(r, c, 5, 16); r.s.flags[14] = 0; hip_frames(r, c, 5, 16);
  need(r.got.replaced >= 30 && r.got.eye_hold >= 5 && r.got.seated >= 4 && r.got.learned == 1, "the second row's flags and rig reference in use");
  r.end(r.gen);
}
// ---- more weapons than the eye memory holds (16) ----
static void script_many_weapons(Run &r) {
  Clock c; start(r, c);
  // Twenty-one weapons' eyes are learned in turn; the memory keeps sixteen.
  for (uint32_t round = 0; round < 21; ++round) {
    r.weapon_key(r.gen, round == 0 ? KEY_A : round == 12 ? KEY_B : 200 + round);
    r.eye[0] = .01f * (round % 7); r.eye[1] = .1f + .01f * (round % 5); r.eye[2] = .05f - .004f * round;
    // the first weapon's aimed position and body node are learned too; its place is the first to be reused
    hip_frames(r, c, 2, 10); native_frames(r, c, round == 0 ? 14 : 7, 10, round % 2 == 0); hip_frames(r, c, 3, 10, 2);
    if (round == 0) need(r.got.learned == 1, "the first weapon's aimed position learned");
    if (round % 8 == 5) { r.end(r.gen); hip_frames(r, c, 1, 10); r.arm(); } // the memory is kept across leases
  }
  // Which of them are still known: the held eye glides towards a known weapon's, and stays for an unknown one.
  const uint64_t before = r.got.glides;
  for (uint32_t round = 0; round < 21; ++round) {
    native_frames(r, c, 6, 10); hip_frames(r, c, 2, 10, 2);
    r.weapon_key(r.gen, round == 0 ? KEY_A : round == 12 ? KEY_B : 200 + round); hip_frames(r, c, 5, 10, 2);
    r.weapon_key(r.gen, 220);
  }
  need(r.got.glides - before >= 40 && r.got.glides - before <= 16 * 5, "glides towards the weapons still remembered, none towards the forgotten");
  r.end(r.gen);
}

// ---- probes: every read of one call fails once (and heals, or stays unreadable); values change before every read ----
static void probe_values(Run &r) {
  auto v = [&](int id, uint64_t offset, Value value) { r.probe_values.push_back({{id, offset}, value}); };
  v(A_CONTEXT, 0x200, bytes_value(6)); v(A_CONTEXT, 0x200, bytes_value(4)); v(A_CONTEXT, 0x200, bytes_value(5));
  v(A_CONTROLLER, 0, bytes_value(0x40)); v(A_CONTROLLER, 0, bytes_value(2)); v(A_CONTROLLER, 5, bytes_value(1, 1)); v(A_CONTROLLER, 4, bytes_value(1, 1)); v(A_CONTROLLER, 4, bytes_value(0, 1));
  v(A_GENERATIONS, 3, bytes_value(2, 1)); v(A_GENERATIONS, 4, bytes_value(2, 1)); v(A_GENERATIONS, 5, bytes_value(2, 1)); v(A_GENERATIONS, 6, bytes_value(2, 1));
  v(A_CAMOBJ, 0x1D0, address_value(A_FPS)); v(A_CAMOBJ, 0x1D0, bytes_value(0, 8));
  v(A_API, 0x20, slot_value(SLOT_FOREIGN)); v(A_API, 0x20, slot_value(SLOT_NATIVE));
  v(A_BODY, 0x70, bytes_value(BODY_NODES - 1)); v(A_HASHES, HEAD_NODE * 4, bytes_value(0));
  v(A_ANIM_CURRENT, 13 * 8, address_value(A_ANIM_GRAPH, 0x200 + 0x1000 + 5 * 16)); v(A_BODY, 0x88, address_value(A_FPS_WORLD));
  v(A_O_FLAGS0, 14, bytes_value(4, 1)); v(A_REGISTRY, 0x98, bytes_value(3)); v(A_O_RIG0, 0, bytes_value(0)); v(A_CONTEXT, 0x208, bytes_value(0x445));
  v(A_UNITS, 3 * 8, address_value(A_FPS)); v(A_UNITS, 5 * 8, address_value(A_BODY)); v(A_DESCRIPTOR, 0, bytes_value(1, 1));
  v(A_CONTROLLER, 5, bytes_value(0, 1)); v(A_CONTROLLER, 0, bytes_value(0x20)); v(A_CONTROLLER, 0, bytes_value(0));
  v(A_CAM_LINKS, 0, bytes_value(0x00320003)); v(A_FPS, 0x70, bytes_value(62)); v(A_CAMOBJ, 0x78, address_value(A_BODY_WORLD)); v(A_CONTEXT, 0, address_value(A_CAMOBJ));
}
static void probe_tail(Run &r, Clock &c) { hip_frames(r, c, 2, 10); native_frames(r, c, 2, 10); hip_frames(r, c, 2, 10); r.end(r.gen); r.frames(1, 10); }
static void probe_script(Run &r, int which) {
  Clock c; probe_values(r);
  switch (which) {
  case 0: // a hip frame with everything on: eye hold just started, aim-release anchor active
    start(r, c); r.weapon_key(r.gen, KEY_A); hip_frames(r, c, 8, 10); native_frames(r, c, 12, 10, true); hip_frames(r, c, 1, 10, 2);
    r.probe(); hip_frames(r, c, 1, 10, 2); break;
  case 1: // a native frame whose view is sampled
    start(r, c); r.weapon_key(r.gen, KEY_A); hip_frames(r, c, 8, 10); native_frames(r, c, 6, 10, true);
    r.probe(); native_frames(r, c, 1, 10, true); break;
  case 2: // the first frame after the native view
    start(r, c); r.weapon_key(r.gen, KEY_A); hip_frames(r, c, 8, 10); native_frames(r, c, 12, 10, true);
    r.probe(); hip_frames(r, c, 1, 10, 2); break;
  case 3: // begin
    walk_pose(r, 0, true); r.hip(); r.probe(); r.begin(); if (r.result == 1) { r.presentation(r.gen); r.cut_hold(r.gen, 15); } break;
  case 4: // an exit-hold frame
    start(r, c); hip_frames(r, c, 5, 10); native_frames(r, c, 6, 10);
    walk_pose(r, c.t, true); r.hip(); r.s.linked = true; r.probe(); r.frame(10); r.frame(10); break;
  case 5: // a seated frame
    start(r, c); hip_frames(r, c, 8, 10); r.s.flags[14] = 4; hip_frames(r, c, 3, 10);
    r.probe(); hip_frames(r, c, 1, 10); hip_frames(r, c, 2, 10); r.s.flags[14] = 0; break;
  case 6: // a frame on the ship's bridge
    start(r, c); r.s.state = 3; r.s.flags[0] = 2; r.s.scene = 3; hip_frames(r, c, 5, 10);
    r.probe(); hip_frames(r, c, 1, 10); break;
  case 7: // validate
    walk_pose(r, 0, true); r.hip(); r.probe(); r.validate(); r.begin(); if (r.result == 1) { r.presentation(r.gen); r.cut_hold(r.gen, 15); } break;
  case 8: // end
    start(r, c); hip_frames(r, c, 5, 10); r.probe(); r.end(r.gen); r.frames(2, 10); r.end(r.gen); r.begin(); if (r.result != 1) { r.end(r.gen); r.begin(); } break;
  case 9: // set_presentation
    walk_pose(r, 0, true); r.hip(); r.begin(); r.probe(); r.presentation(r.gen); r.cut_hold(r.gen, 15); break;
  case 10: // the first native frame with the entry ease, before the camera is linked
    start(r, c); r.handoff(r.gen, 120, 150); hip_frames(r, c, 10, 10);
    walk_pose(r, c.t, true); r.native(false, false); r.probe(); r.frame(10); native_frames(r, c, 3, 10); break;
  case 11: // a native frame while the entry ease runs
    start(r, c); r.handoff(r.gen, 120, 150); hip_frames(r, c, 10, 10); native_frames(r, c, 2, 10);
    r.probe(); native_frames(r, c, 1, 10); break;
  case 12: // the first normal frame with the rotation ease
    start(r, c); r.handoff(r.gen, 120, 150); hip_frames(r, c, 10, 10); native_frames(r, c, 6, 10); hip_frames(r, c, 6, 10); native_frames(r, c, 6, 10);
    r.probe(); hip_frames(r, c, 1, 10); break;
  case 13: // a plain hip frame without presentation
    start(r, c, 0, false); hip_frames(r, c, 5, 10); r.probe(); hip_frames(r, c, 1, 10); break;
  case 14: // read_stats
    start(r, c); hip_frames(r, c, 3, 10); r.probe(); r.read_stats(r.gen); break;
  case 15: // a frame tolerated under the guard byte's 250 ms grace
    start(r, c); hip_frames(r, c, 6, 10); r.s.guard = 1; hip_frames(r, c, 1, 10);
    r.probe(); hip_frames(r, c, 1, 10); r.s.guard = 0; break;
  case 16: // the frame after such a one
    start(r, c); hip_frames(r, c, 6, 10); r.s.guard = 1; hip_frames(r, c, 2, 10); r.s.guard = 0;
    r.probe(); hip_frames(r, c, 1, 10); break;
  }
  probe_tail(r, c);
}

// ---- seeded random walks mixing all of the above ----
static void script_walk(Run &r, uint32_t seed, int count, bool checked) {
  std::mt19937 rng(seed);
  auto U = [&](float a, float b) { return a + (b - a) * static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f); };
  auto P = [&](float p) { return U(0, 1) < p; };
  auto I = [&](int a, int b) { return a + static_cast<int>(rng() % static_cast<uint32_t>(b - a + 1)); };
  const double rates[] = {1000.0 / 30, 1000.0 / 60, 1000.0 / 144, 10, 16, 4};
  const uint32_t keys[7] = {KEY_A, KEY_B, 9, 0, 0x400005, 0x400003, 0x400004}; // two weapons, a plain number, none, the rig, the body, the camera
  const float eyes[7][3] = {{0, .1f, .05f}, {.05f, .14f, .01f}, {-.03f, .08f, .1f}, {0, 0, 0}, {.2f, .4f, .3f}, {0, .1f, .05f}, {.01f, .1f, .04f}};
  if (P(.5f)) { r.forward = U(-.5f, .5f); r.up = U(-.5f, .5f); }
  r.smoothing = P(.5f) ? 100.0f : P(.3f) ? 0.0f : U(0, 500);
  r.blend = P(.5f) ? 120.0f : P(.3f) ? 0.0f : U(0, 1000);
  r.max_gap = P(.6f) ? 250u : static_cast<uint32_t>(I(16, 1000));
  r.index = P(.2f) ? 1u : 0u;
  float x = U(-50, 50), y = U(-50, 50), z = U(0, 5), yaw = U(-3, 3), look_yaw = yaw, pitch = 0, speed = 0, phase = 0;
  double rate = rates[I(0, 5)];
  int mode = 0, dwell = I(5, 60), weapon = 0, mask_timer = 0, guard_timer = 0, seat_timer = 0, link_lag = 0, unlinked = 0, state_timer = 0, bridge_timer = 0, down = 0;
  uint32_t mask = 0, hold_mode = 15; bool aim = false; float entry_ms = 0, rotation_ms = 0;
  auto configure = [&] {
    if (P(.92f)) r.presentation(r.gen);
    r.cut_hold(r.gen, hold_mode); r.weapon_key(r.gen, keys[weapon]);
    if (entry_ms > 0 || rotation_ms > 0) r.handoff(r.gen, entry_ms, rotation_ms);
  };
  if (P(.3f)) { entry_ms = P(.7f) ? U(1, 500) : 0; rotation_ms = P(.7f) ? U(1, 500) : 0; }
  if (P(.2f)) hold_mode = static_cast<uint32_t>(I(0, 15));
  r.pose(x, y, z, yaw); r.look(look_yaw, pitch); r.hip();
  r.begin(); if (r.result == 1) configure();
  for (int i = 0; i < count; ++i) {
    // time
    if (P(.01f)) rate = rates[I(0, 5)];
    double dt = rate * (P(.3f) ? U(.6f, 1.6f) : 1.0f);
    const float q = U(0, 1);
    if (q < .006f) dt = 0; else if (q < .010f) dt = -U(1, 80); else if (q < .016f) dt = static_cast<double>(r.max_gap) + U(-3, 3); else if (q < .019f) dt = U(300, 4000);
    else if (q < .021f) dt = static_cast<double>(r.max_gap);
    // movement
    const float seconds = dt > 0 && dt < 100 ? static_cast<float>(dt / 1000) : 0;
    speed = std::min(6.0f, std::max(0.0f, speed + U(-.3f, .3f))); yaw += U(-.05f, .05f); look_yaw += U(-.08f, .08f);
    pitch = std::min(1.4f, std::max(-1.4f, pitch + U(-.05f, .05f))); phase += seconds * 9;
    x += -std::sin(yaw) * speed * seconds; y += std::cos(yaw) * speed * seconds;
    if (P(.002f)) { x = U(-900, 900); y = U(-900, 900); z = U(-20, 60); }
    r.pose(x, y, z, yaw, 1.5f + .04f * std::sin(phase)); r.look(look_yaw, P(.004f) ? (P(.5f) ? 1.5707963267948966f : -1.5707963267948966f) : pitch);
    // what the player is doing
    if (--dwell <= 0) {
      if (mode == 0) { mode = 1; dwell = I(1, 60); aim = P(.5f); unlinked = P(.4f) ? I(1, 2) : 0; if (P(.7f)) { mask = 0; mask_timer = 0; } }
      else {
        mode = 0; dwell = I(2, 120);
        const float e = U(0, 1);
        if (e < .35f) { mask = 2; mask_timer = I(2, 220); }
        else if (e < .45f) { mask = 0x20; mask_timer = I(3, 60); }
        else if (e < .6f) dwell = I(1, 5);
        else if (e < .7f) link_lag = I(1, 4);
        else if (e < .75f) seat_timer = I(5, 80);
        else if (e < .8f) guard_timer = I(1, 3);
      }
    }
    if (mode == 1 && P(.03f)) aim = !aim;
    if (mask_timer > 0 && --mask_timer == 0) mask = 0;
    if (P(.006f)) guard_timer = P(.5f) ? I(1, 2) : I(8, 60);
    if (P(.002f)) seat_timer = I(5, 80);
    if (P(.004f)) { const uint32_t masks[] = {1, 3, 4, 0x40, 0x20, 2}; mask = masks[I(0, 5)]; mask_timer = I(1, 25); }
    if (P(.002f)) state_timer = I(1, 12);
    if (P(.002f)) bridge_timer = I(5, 60);
    std::memcpy(r.eye, eyes[weapon], 12);
    if (mode == 1) { r.native(aim, unlinked-- <= 0); r.s.mask = mask; }
    else { r.hip(mask); if (link_lag > 0) { r.s.linked = true; --link_lag; } }
    r.s.guard = guard_timer > 0 ? 1 : 0; if (guard_timer > 0) --guard_timer;
    r.s.flags[14] = seat_timer > 0 ? 4 : 0;
    if (seat_timer > 0) {
      --seat_timer;
      if (mode == 0) { for (int k = 0; k < 3; ++k) r.s.argument[k] += 30; Run turned = r; turned.look(look_yaw + 1.3f, .9f); turned.rotation_from_look(); std::memcpy(r.s.argument_rotation, turned.s.argument_rotation, 16); }
    }
    r.s.state = state_timer > 0 ? 5 : 36; if (state_timer > 0) --state_timer;
    r.s.flags[0] = 0; r.s.scene = 0;
    if (bridge_timer > 0) { --bridge_timer; r.s.state = 3; r.s.flags[0] = 2; r.s.scene = bridge_timer == 20 ? 4 : 3; }
    r.s.request[2] = 0x444 + static_cast<uint32_t>(weapon);
    if (P(.2f)) r.s.request[0] |= 0x10000;
    // the adapter's calls
    if (P(.012f)) { weapon = P(.8f) ? I(0, 3) : I(4, 6); r.weapon_key(r.gen, keys[weapon]); }
    if (P(.003f)) { hold_mode = P(.5f) ? 15u : static_cast<uint32_t>(I(0, 15)); r.cut_hold(r.gen, hold_mode); }
    if (P(.002f)) { entry_ms = P(.6f) ? U(0, 500) : 0; rotation_ms = P(.6f) ? U(0, 500) : 0; r.handoff(r.gen, entry_ms, rotation_ms); }
    if (P(.001f)) r.validate();
    if (P(.001f)) { r.end(r.gen); if (P(.5f)) r.frame(dt); r.begin(); if (r.result == 1) configure(); }
    // rare one-frame trouble
    const Scene saved = r.s; bool slot_touched = false;
    const float t = U(0, 1);
    if (t < .002f) r.s.argument[I(0, 2)] = P(.5f) ? NAN : INFINITY;
    else if (t < .004f) for (int k = 0; k < 3; ++k) r.s.argument[k] += U(4, 12);
    else if (t < .0055f) r.s.generations[I(3, 7)] = 2;
    else if (t < .0065f) r.s.head_hash = 7;
    else if (t < .0075f) r.s.graph_count = BODY_NODES - 1;
    else if (t < .0085f) for (int k = 0; k < 11; ++k) r.s.context[k] *= 1.1f;
    else if (t < .0095f) { r.s.camera_split = true; for (int k = 0; k < 3; ++k) { r.s.camera_local[k] = U(-1, 1); r.s.camera_world[k] = r.s.camera_local[k] + U(-.01f, .01f); } }
    else if (t < .0105f) r.s.request[0] = P(.5f) ? 5u : 6u;
    else if (t < .0115f) r.s.ring[1] = 0;
    else if (t < .0125f) r.s.rig[I(0, 15)] *= 1.3f;
    else if (t < .0135f) r.s.rig_reference = P(.5f) ? 0u : 0x400006u;
    else if (t < .0145f) r.s.camera_link = 0x00320001;
    else if (t < .0155f) r.s.argument_rotation[I(0, 3)] = P(.5f) ? NAN : 3.0f;
    else if (t < .0165f) r.s.linked = !r.s.linked;
    else if (t < .0175f) for (int w = 0; w < 2; ++w) { r.s.weapon[w].parent = I(0, 2); r.s.weapon[w].link = (static_cast<uint32_t>(I(0, 600)) << 16) | static_cast<uint32_t>(I(1, 2)); for (int k = 0; k < 3; ++k) r.s.weapon[w].position[k] += U(-1, 1); }
    else if (t < .0185f) { r.s.head[2] += U(-2, 3); }
    else if (t < .0195f) { r.s.aim = !r.s.aim; }
    else if (t < .0205f) { r.s.request[3] = 0x400009; }
    else if (t < .0212f) { set_slot(P(.5f) ? SLOT_FOREIGN : SLOT_NATIVE); slot_touched = true; }
    else if (t < .0222f) r.caller_ours = false;
    else if (t < .0232f) r.camera_argument_delta = 16;
    else if (t < .0242f) r.unit_argument_xor = 2;
    else if (t < .0252f) { r.s.camera_rotation_given = true; r.s.camera_rotation[I(0, 11)] = P(.5f) ? NAN : 2.0f; }
    else if (t < .0262f) r.s.root[I(0, 11)] += .3f;
    else if (t < .0272f) r.s.avatar_count = P(.5f) ? 0u : 1u;
    r.frame(dt);
    r.s = saved; r.caller_ours = true; r.camera_argument_delta = 0; r.unit_argument_xor = 0;
    // the adapter notices a lost lease, ends it and arms again when the scene allows
    if (!::armed.load() && ++down > 3) {
      down = 0;
      if (::reserved.load()) r.end(r.gen);
      if (slot_touched || !::reserved.load()) set_slot(SLOT_NATIVE);
      r.begin(); if (r.result == 1) configure();
    }
  }
  if (checked) need(r.got.replaced > static_cast<uint64_t>(count) / 3 && r.got.native > static_cast<uint64_t>(count) / 5 && r.got.eye_hold > 50 && r.got.seated > 0 && r.got.grace > 0,
       "a walk that is mostly armed and visits the features");
  r.end(r.gen);
}

// ---- checks only the translation makes (the original's fixture build compiles them out) ----
// Expected results are read off the original's non-fixture lines (cpp:330-332, 1055, 1095-1097).
static void script_translation_only(Run &r) {
  Clock c; walk_pose(r, 0, true); r.hip(); r.sync();
  for (int w = 0; w < 2; ++w) { r.write_config(w); World::put<uint64_t>(W[w].a[A_IO] + IO_GENERATION, UINT64_MAX); }
  call_text = "translation only: begin while the graph getter is not executable";
  answer_getter_executable = false;
  uint32_t result = tfp_camera_begin(reinterpret_cast<TfpCameraConfig *>(W[1].a[A_IO] + IO_CONFIG), reinterpret_cast<uint64_t *>(W[1].a[A_IO] + IO_GENERATION));
  if (result != 0 || tfp_camera_error() != TFP_CAMERA_GRAPH_MISMATCH) fail("expected 0 with error 16, got %u with error %u", result, tfp_camera_error());
  result = tfp_camera_validate(reinterpret_cast<TfpCameraConfig *>(W[1].a[A_IO] + IO_CONFIG));
  if (result != 0 || tfp_camera_error() != TFP_CAMERA_GRAPH_MISMATCH) fail("validate: expected 0 with error 16, got %u with error %u", result, tfp_camera_error());
  answer_getter_executable = true;
  call_text = "translation only: begin while the executable cannot be pinned";
  answer_pin_executable = false;
  result = tfp_camera_begin(reinterpret_cast<TfpCameraConfig *>(W[1].a[A_IO] + IO_CONFIG), reinterpret_cast<uint64_t *>(W[1].a[A_IO] + IO_GENERATION));
  uint64_t generation = 1, slot_now = 0; std::memcpy(&generation, reinterpret_cast<void *>(W[1].a[A_IO] + IO_GENERATION), 8); std::memcpy(&slot_now, reinterpret_cast<void *>(W[1].slot()), 8);
  if (result != 0 || tfp_camera_error() != TFP_CAMERA_PIN_FAILED || generation != 0 || slot_now != W[1].native_value)
    fail("expected 0 with error 9, generation 0 and the slot untouched; got %u, error %u, generation %llu", result, tfp_camera_error(), (unsigned long long)generation);
  answer_pin_executable = true;
  for (int w = 0; w < 2; ++w) events[w].clear();
  counted.translation_only += 3;
  // Neither refusal may have left anything behind: from here both helpers must agree again, generation 1 included.
  r.arm(); if (r.gen != 1) fail("the refused begins consumed a generation (now %llu)", (unsigned long long)r.gen);
  hip_frames(r, c, 10, 16); native_frames(r, c, 5, 16); hip_frames(r, c, 5, 16, 2);
  // In the hook: the getter stops being executable. The original (non-fixture) refuses the graph: error 16,
  // the game's own argument forwarded, still armed. Only the translation can be asked; the comparison ends here.
  call_text = "translation only: a frame while the graph getter is not executable";
  answer_getter_executable = false;
  fixture_time += 16; events[1].clear();
  tfp_camera_setter_from(W[1].a[A_GAME] + HERE.call + 4, reinterpret_cast<void *>(W[1].a[A_CAMERA]), r.camera_unit, reinterpret_cast<const float *>(W[1].native()));
  TfpCameraStats stats{}; stats.size = sizeof stats; stats.abi = 1;
  if (tfp_camera_error() != TFP_CAMERA_GRAPH_MISMATCH || events[1].empty()) fail("expected error 16, got %u", tfp_camera_error());
  const Event *forwarded_call = nullptr; for (const Event &e : events[1]) if (e.kind == EV_FORWARD) forwarded_call = &e;
  if (!forwarded_call || forwarded_call->v[1] != 0) fail("expected the game's own argument to be forwarded");
  if (tfp_camera_read_stats(r.gen, &stats) != 1 || stats.armed != 1) fail("expected the helper to stay armed");
  answer_getter_executable = true;
  if (tfp_camera_end(r.gen) != 1) fail("end failed");
  ++counted.translation_only;
}

// ---- tfp_camera_reset_for_tests: after it the translation must be where a fresh process is ----
// The original has no reset; its start-up values are put back here by hand (every static of the retail file).
static void script_reset(Run &r) {
  { Clock c; start(r, c); r.handoff(r.gen, 120, 150); r.weapon_key(r.gen, KEY_A);
    hip_frames(r, c, 20, 10); native_frames(r, c, 12, 10, true); hip_frames(r, c, 6, 10, 2); r.weapon_key(r.gen, KEY_B); native_frames(r, c, 8, 10); hip_frames(r, c, 3, 10, 2);
    r.s.guard = 1; hip_frames(r, c, 2, 10); r.s.flags[14] = 4; hip_frames(r, c, 2, 10); r.read_stats(r.gen); }
  // left armed, mid eye hold, with remembered weapons, counters and an error code
  tfp_camera_reset_for_tests();
  ::original.store(nullptr); ::installed.store(0); ::armed.store(0); ::reserved.store(0); ::last_error.store(0); ::callback_generation.store(0);
  ::seen.store(0); ::forwarded.store(0); ::replaced.store(0); ::native6_forwarded.store(0); ::resets.store(0); ::return_address.store(0);
  ::slot = 0; ::slot_image = 0; ::generation_counter = 0; ::owned_generation = 0; ::retired_generation = 0; ::possible = false;
  ::policy = ::Policy{}; for (auto &entry : ::eye_memory) entry = ::EyeMemory{}; ::eye_memory_next = 0;
  ::display_cache = ::Display{}; ::display_sequence.store(0); ::display_blend.store(0); ::display_head_distance.store(0); ::display_last_dt.store(0);
  fixture_cas_calls = 0; translation_cas_calls = 0;
  set_slot(SLOT_NATIVE);
  for (int w = 0; w < 2; ++w) { const float zero[3] = {}; World::put(W[w].a[A_CAM_LOCALS] + 0x24, zero); World::put(W[w].a[A_CAM_WORLDS] + 0x30, zero); }
  r = Run(); r.observe();
  r.read_stats(0); r.frames(2, 10);
  // The hook before any begin: the original would call a null pointer. The translation alone: it must hand the
  // host a null original (nothing left over from the lease before the reset).
  call_text = "translation only: the hook right after the reset"; expect_null_original = true; events[1].clear();
  tfp_camera_setter_from(W[1].a[A_GAME] + HERE.call + 4, reinterpret_cast<void *>(W[1].a[A_CAMERA]), r.camera_unit, reinterpret_cast<const float *>(W[1].native()));
  expect_null_original = false;
  bool forwarded_once = false; for (const Event &e : events[1]) if (e.kind == EV_FORWARD && e.v[1] == 0) forwarded_once = true;
  if (!forwarded_once || tfp_camera_error() != 0) fail("expected the game's own argument forwarded to a null original and no error");
  { TfpCameraStats stats{}; stats.size = sizeof stats; stats.abi = 1;
    if (tfp_camera_read_stats(0, &stats) != 1 || stats.seen != 1 || stats.forwarded != 1 || stats.replaced || stats.resets || stats.armed || stats.installed) fail("unexpected counters after one unarmed call");
    ::seen.store(1); ::forwarded.store(1); } // the original is given the same two counts
  ++counted.translation_only;
  { Clock c; start(r, c); if (r.gen != 1) fail("after the reset begin gave generation %llu, not 1", (unsigned long long)r.gen);
    r.weapon_key(r.gen, KEY_B); hip_frames(r, c, 3, 10); native_frames(r, c, 2, 10, true); hip_frames(r, c, 6, 10, 2);
    native_frames(r, c, 8, 10, true); hip_frames(r, c, 3, 10, 2); r.weapon_key(r.gen, KEY_A); hip_frames(r, c, 10, 10, 2);
    native_frames(r, c, 12, 10, true); hip_frames(r, c, 10, 10); r.end(r.gen); r.end(r.gen); }
}


// ---- beyond retail: the three opt-in values of tfp_camera_set_action_tuning (docs/design/ACTION-CAMERA.md) ----
// Retail is shown the mask it accepts, the translation the one seen on build 25480438; with the value set the
// translation must answer exactly as retail does. Without it, it must not.
static void hip_frames_as(Run &r, Clock &c, int count, double dt, uint32_t retail_mask, uint32_t translation_mask) {
  r.translation_mask = translation_mask; hip_frames(r, c, count, dt, retail_mask); r.translation_mask = UINT32_MAX;
}
static float apart(Run &r) {
  float a[3] = {}, b[3] = {}; if (!r.forwarded_position(0, a) || !r.forwarded_position(1, b)) fail("a frame forwarded no position");
  return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}
// The high clamber (mask 0x22; read as a pick-up until round 24) is given the head camera, as retail gives it to a vault (mask 0x20).
static void script_extra_mask(Run &r, bool set) {
  Clock c; start(r, c); r.weapon_key(r.gen, KEY_A);
  tfp_camera_set_action_tuning(set ? 0x22 : 0, 300, false);
  hip_frames(r, c, 10, 16); native_frames(r, c, 8, 16);
  if (set) { hip_frames_as(r, c, 40, 16, 0x20, 0x22); need(r.got.replaced >= 50, "the head camera through the pick-up"); }
  else { // retail's own answer to 0x22: the game's camera is forwarded untouched
    const uint64_t before = r.got.replaced; hip_frames(r, c, 40, 16, 0x22); need(r.got.replaced == before, "no head camera for mask 0x22 without the value");
  }
  hip_frames(r, c, 10, 16, 2); native_frames(r, c, 6, 16); hip_frames(r, c, 10, 16);
  r.end(r.gen);
}
// A stim on build 25480438 is mask 2, then 0 for 650 ms, then 2: with a 900 ms tolerance the eye is held as
// through one unbroken swap.
static void script_eye_idle(Run &r, double dt) {
  Clock c; start(r, c); r.weapon_key(r.gen, KEY_A);
  tfp_camera_set_action_tuning(0, 900, false);
  hip_frames(r, c, 10, dt); native_frames(r, c, 8, dt);
  hip_frames(r, c, static_cast<int>(400 / dt), dt, 2);
  hip_frames_as(r, c, static_cast<int>(650 / dt), dt, 2, 0);
  hip_frames(r, c, static_cast<int>(300 / dt), dt, 2);
  // The tail stays inside retail's own 300 ms: beyond it the two tolerances differ by design.
  native_frames(r, c, 8, dt); hip_frames(r, c, static_cast<int>(200 / dt), dt);
  need(r.got.eye_hold > static_cast<uint64_t>(1300 / dt), "the eye held through the whole stim");
  r.end(r.gen);
}
// A thrown grenade: the eye lets go while the hands are empty (retail), and with the value set it glides back to
// the weapon's remembered eye while the weapon is drawn, so the native view returns without a cut. Retail is
// shown one unbroken swap (its eye never lets go): that is where the glide has to end up.
static void script_eye_return(Run &r, bool set, double dt) {
  Clock c; c.moving = false; start(r, c); r.weapon_key(r.gen, KEY_A);
  tfp_camera_set_action_tuning(0, 300, set);
  hip_frames(r, c, 10, dt); native_frames(r, c, 8, dt);
  hip_frames(r, c, static_cast<int>(200 / dt), dt, 2);
  r.compare = false;
  hip_frames_as(r, c, static_cast<int>(1200 / dt), dt, 2, 0);
  const float let_go = apart(r);
  if (let_go < .04f) fail("the eye did not let go while the hands were empty (%.4f m from the held eye)", let_go);
  hip_frames(r, c, static_cast<int>(500 / dt), dt, 2);
  const float back = apart(r);
  if (set && back > .002f) fail("the eye is %.4f m from the weapon's remembered eye after the glide back", back);
  if (!set && back < .04f) fail("without the value the eye must stay let go (retail); it is %.4f m from the held eye", back);
  // No step of the glide is a cut: the eye never moves more than 2 cm in a frame.
  r.end(r.gen);
}
static void script_eye_return_steps(Run &r, double dt) {
  Clock c; c.moving = false; start(r, c); r.weapon_key(r.gen, KEY_A);
  tfp_camera_set_action_tuning(0, 300, true);
  hip_frames(r, c, 10, dt); native_frames(r, c, 8, dt); hip_frames(r, c, static_cast<int>(200 / dt), dt, 2);
  r.compare = false;
  hip_frames_as(r, c, static_cast<int>(1200 / dt), dt, 2, 0);
  float last[3] = {}, now_at[3] = {}, largest = 0; r.forwarded_position(1, last);
  for (int i = 0; i < static_cast<int>(500 / dt); ++i) {
    hip_frames(r, c, 1, dt, 2); r.forwarded_position(1, now_at);
    const float step = std::sqrt((now_at[0] - last[0]) * (now_at[0] - last[0]) + (now_at[1] - last[1]) * (now_at[1] - last[1]) + (now_at[2] - last[2]) * (now_at[2] - last[2]));
    if (step > largest) largest = step;
    std::memcpy(last, now_at, 12);
  }
  if (largest > .03f) fail("the glide back moved the eye %.4f m in one frame", largest);
  if (largest < .001f) fail("the eye did not glide back at all");
  r.end(r.gen);
}
static void register_scripts() {
  add("beyond retail: the high clamber mask 0x22 given the head camera", [](Run &r) { script_extra_mask(r, true); });
  add("beyond retail: mask 0x22 without the value is retail's", [](Run &r) { script_extra_mask(r, false); });
  for (double dt : {16.0, 1000.0 / 144}) {
    char name[96]; std::snprintf(name, sizeof name, "beyond retail: eye held through a stim, step %.2f ms", dt); add(name, [=](Run &r) { script_eye_idle(r, dt); });
    std::snprintf(name, sizeof name, "beyond retail: eye glides back before the weapon returns, step %.2f ms", dt); add(name, [=](Run &r) { script_eye_return(r, true, dt); });
    std::snprintf(name, sizeof name, "beyond retail: no glide back without the value, step %.2f ms", dt); add(name, [=](Run &r) { script_eye_return(r, false, dt); });
    std::snprintf(name, sizeof name, "beyond retail: the glide back has no cut, step %.2f ms", dt); add(name, [=](Run &r) { script_eye_return_steps(r, dt); });
  }
  for (double fps : {30.0, 60.0, 144.0}) for (int moving = 0; moving < 2; ++moving) for (int present = 0; present < 2; ++present) {
    char name[96]; std::snprintf(name, sizeof name, "hip view, %g fps, head %s, %s", fps, moving ? "moving" : "still", present ? "presentation and mode 15" : "plain");
    add(name, [=](Run &r) { script_hip(r, 1000.0 / fps, moving != 0, present != 0); });
  }
  add("native view forwarded, no presentation", [](Run &r) { script_native(r, false); });
  add("native view forwarded, history remembered, presentation on", [](Run &r) { script_native(r, true); });
  const char *exits[] = {"remembered native eye", "exit frame 300 ms late", "eye further than 1 m", "no eye on record", "eye on record older than the gap",
                         "full reset between the eye record and the exit", "eye hold switched off and on"};
  for (int v = 0; v < 7; ++v) for (uint32_t mask : {2u, 0u}) for (uint32_t mode : {15u, 1u, 2u}) {
    char name[128]; std::snprintf(name, sizeof name, "exit from the native view: %s, mode %u, mask %u", exits[v], mode, mask);
    add(name, [=](Run &r) { script_exit(r, v, mode, mask); });
  }
  const char *swaps[] = {"to a known weapon (glide)", "to an unknown weapon", "empty-handed action (fade)", "2.5 s limit", "300 ms no-action tolerance"};
  for (int v = 0; v < 5; ++v) for (double dt : {16.0, 1000.0 / 144}) {
    char name[128]; std::snprintf(name, sizeof name, "weapon swap: %s, step %.2f ms", swaps[v], dt);
    add(name, [=](Run &r) { script_swap(r, v, dt); });
  }
  const char *anchors[] = {"learned and used", "not learned", "weapon key changed", "weapon too far", "weapon on no body node", "weapon wobbling 4.5 mm",
                           "weapon link one past the last node", "16 ms gap", "switched off and on while running", "body, camera or rig as the weapon", "distance swept through 0.8 m"};
  for (int v = 0; v < 11; ++v) for (uint32_t mode : {15u, 4u, 7u}) {
    char name[128]; std::snprintf(name, sizeof name, "aim-release anchor: %s, mode %u", anchors[v], mode);
    add(name, [=](Run &r) { script_anchor(r, v, mode); });
  }
  for (uint32_t mode : {15u, 1u, 14u}) { char name[64]; std::snprintf(name, sizeof name, "exit hold, mode %u", mode); add(name, [=](Run &r) { script_exit_hold(r, mode); }); }
  for (uint32_t mode : {15u, 8u, 7u}) for (double dt : {16.0, 33.0}) { char name[64]; std::snprintf(name, sizeof name, "seated view, mode %u, step %g ms", mode, dt); add(name, [=](Run &r) { script_seated(r, mode, dt); }); }
  add("objections (guard, masks, not on foot, foreign arguments), mode 15", [](Run &r) { script_objections(r, 15, true); });
  add("objections, mode 0", [](Run &r) { script_objections(r, 0, true); });
  add("objections, no presentation", [](Run &r) { script_objections(r, 0, false); });
  add("gates, presentation on", [](Run &r) { script_gates(r, true); });
  add("gates, plain", [](Run &r) { script_gates(r, false); });
  for (int what = 0; what < 40; ++what) { char name[64]; std::snprintf(name, sizeof name, "owner lost, case %d on a %s frame", what >> 1, what & 1 ? "native" : "hip"); add(name, [=](Run &r) { script_owner_lost(r, what); }); }
  for (uint32_t gap : {250u, 16u, 1000u}) { char name[64]; std::snprintf(name, sizeof name, "time, max gap %u ms", gap); add(name, [=](Run &r) { script_time(r, gap); }); }
  add("control calls in right and wrong orders", script_controls);
  add("set_presentation again in mid-lease", script_presentation_again);
  add("refused configurations and worlds", script_refusals);
  for (int v = 0; v < 14; ++v) { char name[64]; std::snprintf(name, sizeof name, "dispatch slot, case %d", v); add(name, [=](Run &r) { script_slot(r, v); }); }
  add("ship's bridge", script_bridge);
  add("eases: entry 120 ms, rotation 150 ms", [](Run &r) { script_handoff(r, 120, 150, 16); });
  add("eases: entry only", [](Run &r) { script_handoff(r, 200, 0, 1000.0 / 144); });
  add("eases: rotation only", [](Run &r) { script_handoff(r, 0, 300, 33); });
  for (uint32_t mode : {1u, 15u, 0u}) { char name[96]; std::snprintf(name, sizeof name, "one native frame entered before the camera was linked, mode %u", mode); add(name, [=](Run &r) { script_pending_exit(r, mode); }); }
  add("second avatar row", script_second_avatar);
  add("more weapons than the eye memory holds", script_many_weapons);
  add("checks only the translation makes", script_translation_only);
  add("after tfp_camera_reset_for_tests", script_reset);
  const char *probes[] = {"hip frame, eye hold and anchor active", "sampled native frame", "first frame after the native view", "begin", "exit-hold frame", "seated frame", "bridge frame",
                          "validate", "end", "set_presentation", "first native frame, entry ease, camera not linked", "native frame during the entry ease",
                          "first normal frame with the rotation ease", "plain hip frame", "read_stats", "guard-grace frame", "frame after a guard-grace frame"};
  for (int which = 0; which < 17; ++which) add(std::string("probe: ") + probes[which], [=](Run &r) { probe_script(r, which); }, true);
  // TFP_DIFF_EXTRA_WALKS=n: n more walks (a soak; not part of the normal run).
  const char *extra = std::getenv("TFP_DIFF_EXTRA_WALKS");
  const uint32_t walks = 8 + (extra ? static_cast<uint32_t>(std::atoi(extra)) : 0u);
  for (uint32_t seed = 1; seed <= walks; ++seed) { char name[64]; std::snprintf(name, sizeof name, "random walk, seed %u", seed); add(name, [=](Run &r) { script_walk(r, 25480438u + seed, 4000, seed <= 8); }); }
}

// ---- the runner ----
struct Job { size_t script; int mode, index, value; };
static int running = 0, failed = 0, finished = 0;
static void reap() {
  int status = 0; const pid_t pid = wait(&status);
  if (pid < 0) return;
  --running; ++finished;
  if (WIFSIGNALED(status)) { ++failed; std::fprintf(stderr, "a camera helper script's process ended on signal %d (see the last script named above; rerun with its name to isolate)\n", WTERMSIG(status)); }
  else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) ++failed;
}
static bool stop_at_first = false; // TFP_DIFF_STOP: for mutation runs, where one difference is the answer
static void launch(const Job &job, int limit) {
  while (running >= limit) reap();
  if (stop_at_first && failed) return;
  std::fflush(stdout); std::fflush(stderr);
  const pid_t pid = fork();
  if (pid < 0) { std::perror("fork"); std::exit(2); }
  if (pid == 0) {
    const Script &script = scripts[job.script];
    script_name = script.name;
    if (job.mode != INJECT_NONE) {
      char suffix[96];
      if (job.mode == INJECT_VALUE) std::snprintf(suffix, sizeof suffix, " [value %d changes before read %d]", job.value, job.index);
      else std::snprintf(suffix, sizeof suffix, " [read %d fails and %s]", job.index, job.mode == INJECT_FAIL ? "heals" : "stays unreadable");
      script_name += suffix;
    }
    discovering = script.probed && job.mode == INJECT_NONE;
    struct Report { static void crash(int signal_number) {
      char text[256]; const int n = std::snprintf(text, sizeof text, "CRASH (signal %d) in script \"%s\", call %llu: %s\n", signal_number, script_name.c_str(), (unsigned long long)call_index, call_text.c_str());
      if (n > 0) (void)!::write(2, text, static_cast<size_t>(n));
      std::_Exit(4); } };
    signal(SIGSEGV, Report::crash); signal(SIGBUS, Report::crash); signal(SIGFPE, Report::crash); signal(SIGABRT, Report::crash);
    Run r; current_run = &r; r.probe_mode = job.mode; r.probe_index = job.index; r.probe_value = job.value;
    script.run(r);
    if (std::getenv("TFP_DIFF_REACHED")) print_reached(r);
    __sync_fetch_and_add(&shared->calls, counted.calls); __sync_fetch_and_add(&shared->frames, counted.frames); __sync_fetch_and_add(&shared->events, counted.events);
    __sync_fetch_and_add(&shared->skipped_frames, counted.skipped_frames); __sync_fetch_and_add(&shared->translation_only, counted.translation_only);
    std::fflush(stdout);
    std::_Exit(0);
  }
  ++running;
}

} // namespace dt

int main(int argc, char **argv) {
  using namespace dt;
  shared = static_cast<Shared *>(mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  register_scripts();
  stop_at_first = std::getenv("TFP_DIFF_STOP") != nullptr;
  build(); // the two worlds, untouched by any helper call; every child starts from a copy of them
  const char *only = argc > 1 ? argv[1] : nullptr; // a part of a script's name: run just those
  const bool list = only && !std::strcmp(only, "--list");
  if (list) { for (const Script &s : scripts) std::printf("%s\n", s.name.c_str()); return 0; }
  const int limit = std::max(1L, sysconf(_SC_NPROCESSORS_ONLN));
  size_t plain = 0, probe_runs = 0;
  for (size_t i = 0; i < scripts.size(); ++i) {
    if (only && scripts[i].name.find(only) == std::string::npos) continue;
    if (scripts[i].probed) continue;
    launch({i, INJECT_NONE, -1, 0}, limit); ++plain;
  }
  while (running) reap();
  // Probes: one run counts the probed call's reads; then one run per read and kind of trouble.
  for (size_t i = 0; i < scripts.size() && failed < 20; ++i) {
    if (!scripts[i].probed || (only && scripts[i].name.find(only) == std::string::npos)) continue;
    shared->probe_reads = shared->probe_values = 0;
    const int before = failed;
    launch({i, INJECT_NONE, -1, 0}, 1); while (running) reap(); ++probe_runs;
    if (failed != before) continue;
    const int reads = static_cast<int>(shared->probe_reads), values = static_cast<int>(shared->probe_values);
    for (int index = 0; index < reads && failed < 20; ++index) {
      launch({i, INJECT_FAIL, index, 0}, limit); launch({i, INJECT_FAIL_STAY, index, 0}, limit); probe_runs += 2;
      for (int value = 0; value < values; ++value) { launch({i, INJECT_VALUE, index, value}, limit); ++probe_runs; }
    }
    while (running) reap();
  }
  while (running) reap();
  std::printf("Camera helper: %zu scripts and %zu probe runs; %llu calls compared (%llu of them frames), %llu reads, writes, swaps, lock steps and forwarded calls matched; "
              "%llu translation-only host answers; %llu frames skipped (hook before any begin)\n",
              plain, probe_runs, (unsigned long long)shared->calls, (unsigned long long)shared->frames, (unsigned long long)shared->events,
              (unsigned long long)shared->translation_only, (unsigned long long)shared->skipped_frames);
  if (failed) { std::printf("Camera helper: %d script run(s) FAILED\n", failed); return 1; }
  std::printf("Camera helper: the translation behaves as the retail helper in every compared call\n");
  return 0;
}
