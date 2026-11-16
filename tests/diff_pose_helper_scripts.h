// The scripts of tests/diff_pose_helper.cpp, and the runner (one child process per script).
#pragma once

namespace dp {

struct Script { std::string name; std::function<void(Run &)> run; bool probed = false; bool input_only = false; };
static std::vector<Script> scripts;
static void add(const std::string &name, std::function<void(Run &)> run, bool probed = false, bool input_only = false) { scripts.push_back({name, std::move(run), probed, input_only}); }

// The game's call sites (return addresses), and callers that are not them.
static const Sym RIG_CALLER{A_G_RIG_CALL, 4}, ROTATION_CALLER{A_G_ROTATION_CALL, 4}, OBSERVE_CALLER{A_G_OBSERVE_CALL, 3}, CARRIER_CALLER{A_G_CARRIER_CALL, 4},
    PAIR_CALLER{A_G_CARRIER_PAIR, 4}, INPUT_CALLER{A_G_INPUT_RETURN, 0}, GAME_OTHER{A_GAME, 0x9000}, EXE_OTHER{A_EXE, 0x7000}, OUTSIDE{A_ARG, 0x800};
static const Sym ROOT_POSE{A_STACK, STACK_S + 0x240}, ROOT_SOURCE{A_STACK, STACK_S + 0x230}, ROTATION_POSE{A_ARG, ARG_ROTATION}, POSITION_POSE{A_ARG, ARG_POSITION};

static Run *current_run;
static void print_reached(const Run &r) {
  std::printf("REACHED %s: weapon updates %llu, zero restores %llu, guarded %u, trace rows %u, depth applied %llu (refreshes %llu, checks %llu, reason %u), entry writes %u, "
              "input observed %llu, normalized %llu, unsupported %llu, restored %llu, foreign %llu, quarantined %llu, reason %#x; errors", script_name.c_str(),
              (unsigned long long)::weapon_updates.load(), (unsigned long long)::zero_restores.load(), ::policy.guarded, ::policy.trace_count,
              (unsigned long long)::policy.depth.observed.applied, (unsigned long long)::policy.depth.refreshes, (unsigned long long)::policy.depth.checks, ::policy.depth.observed.reason,
              ::entry_position_writes, (unsigned long long)::input_state.stats.observed, (unsigned long long)::input_state.stats.normalized, (unsigned long long)::input_state.stats.unsupported,
              (unsigned long long)::input_state.stats.restored, (unsigned long long)::input_state.stats.foreign_wins, (unsigned long long)::input_state.stats.quarantined, ::input_state.stats.reason);
  for (int code = 0; code < 19; ++code) if (r.errors[code]) std::printf(" %d:%llu", code, (unsigned long long)r.errors[code]);
  std::printf("\n"); std::fflush(stdout);
}
// A script that did not get where it was written to go is a broken script, not a passing one.
static void need(bool reached, const char *what) { if (!reached && current_run) print_reached(*current_run); if (!reached) fail("the script did not reach what it is for: %s", what); }

struct Q { float x, y, z, w; };
static Q turn(float ax, float ay, float az, float degrees) {
  const float half = degrees * 0.00872664626f, n = std::sqrt(ax * ax + ay * ay + az * az), sine = std::sin(half);
  return {ax / n * sine, ay / n * sine, az / n * sine, std::cos(half)};
}
static void set_rotation(Run &r, Q q) { r.s.rotation[0] = q.x; r.s.rotation[1] = q.y; r.s.rotation[2] = q.z; r.s.rotation[3] = q.w; }
static void set_root(Run &r, Q q, float x, float y, float z) {
  const float values[8] = {q.x, q.y, q.z, q.w, x, y, z, 1}; std::memcpy(r.s.root_source, values, sizeof values);
}

// ---- one frame of the game's first-person evaluator, as far as the helper sees it ----
enum : unsigned { F_GETTER = 1, F_RIG = 2, F_CARRIER = 4, F_PAIR = 8, F_BRANCH = 16, F_INPUT = 32, F_PLAIN = F_GETTER | F_RIG | F_BRANCH, F_FULL = F_GETTER | F_RIG | F_CARRIER | F_PAIR | F_BRANCH };
static void frame(Run &r, int64_t dt, unsigned parts = F_PLAIN) {
  r.tick(dt);
  if (parts & F_INPUT) {
    r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym());
    // The evaluator then filters the look input itself: the history the helper expects to find afterwards.
    if (::input_state.pending.ready) { float history[3]; std::memcpy(history, ::input_state.pending.expected_history, 12); r.req(0x3c, history); }
  }
  if (parts & F_GETTER) r.getter(OBSERVE_CALLER, r.viewmodel, 50, r.thunks, r.thunks ? r.request_sym() : NOWHERE);
  if (parts & F_RIG) r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48));
  if (parts & F_CARRIER) r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE);
  if (parts & F_PAIR) r.rotation(PAIR_CALLER, r.viewmodel, 0, ROOT_SOURCE);
  if (parts & F_BRANCH) r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE);
}
// The rig's native animation: the pose and rotation the game supplies move a little every frame.
static void animate(Run &r, int i, float sway = 1) {
  r.pose(1 + .013f * std::sin(i * .31f) * sway, 2 + .009f * std::cos(i * .17f) * sway, 3 + .011f * std::sin(i * .23f + 1) * sway);
  set_rotation(r, turn(.3f, 1, .2f, 2.0f * std::sin(i * .05f) * sway));
  set_root(r, turn(0, 0, 1, 20 + 3 * std::sin(i * .07f)), 10 + .01f * i, -4, 1.5f);
}
static void frames(Run &r, int count, int64_t dt, unsigned parts = F_PLAIN, int from = 0) { for (int i = 0; i < count; ++i) { animate(r, from + i); frame(r, dt, parts); } }
// kind: 0 begin, 1 begin_steady, 2 begin_input, 3 begin_steady_input. The game then enters its own first-person view.
static void arm(Run &r, int kind = 0) {
  r.normal(); r.begin(kind);
  if (r.result != 1) fail("the script could not arm (begin returned %u, error %u)", r.result, NADSE_Error());
  r.thunks = kind >= 2 && ::input_mode.load();
  r.fpv();
}

// ---- the optic: the request's zoom state and the evaluator's lens locals (after the retail depth fixture) ----
static float lens_of(const Run &r, unsigned level) { return (r.s.depth.degrees / r.s.depth.zoom[level]) * .01745329238474369f; }
static float distance_of(const Run &r, unsigned level) { return .03391552f / (std::tan(lens_of(r, level) * .5f) * r.s.depth.coefficient[level]); }
static void lens_frame(Run &r, float progress) {
  const float prior = r.req_get<float>(0xA0), live = r.optic_target + (prior - r.optic_target) * progress;
  std::memcpy(r.s.frame + 0x18, &r.optic_target, 4); std::memcpy(r.s.frame + 0x1C, &live, 4); r.req(0x84, live);
}
static void optic(Run &r, unsigned level) {
  r.s.depth.packed = 64 | (level << 4); r.optic_target = lens_of(r, level);
  r.req(0x90, distance_of(r, level)); r.req<float>(0x9C, 0); r.req(0xA0, r.optic_target); r.req<uint8_t>(0x8D, 0); r.req<uint8_t>(0x8F, 0);
  std::memset(r.s.frame, 0, sizeof r.s.frame); lens_frame(r, 0);
}
// The player changes the zoom level: the game records the old distance and lens and heads for the new ones.
static void zoom_event(Run &r, unsigned level) {
  const float old = r.req_get<float>(0x90), prior = r.req_get<float>(0x84);
  r.req(0x9C, old); r.req(0xA0, prior); r.req(0x90, distance_of(r, level));
  r.s.depth.packed = 64 | (level << 4); r.optic_target = lens_of(r, level);
}
static void zoom_to(Run &r, unsigned level, int steps, int64_t dt, unsigned parts = F_FULL) {
  zoom_event(r, level);
  for (int i = steps; i >= 0; --i) { lens_frame(r, steps ? static_cast<float>(i) / steps : 0); animate(r, i); frame(r, dt, parts); }
}

// ---- placement set and changed live ----
static void script_placement(Run &r, int kind, int64_t dt) {
  arm(r, kind);
  frames(r, 25, dt);
  r.placement(r.gen, .2f, -.1f, .05f); frames(r, 10, dt, F_PLAIN, 25);
  r.placement(r.gen, .5f, .5f, -.5f); frames(r, 4, dt);            // the bounds themselves
  r.placement(r.gen, .50001f, 0, 0); r.placement(r.gen, 0, -.50001f, 0); r.placement(r.gen, 0, 0, NAN); r.placement(r.gen, INFINITY, 0, 0);
  frames(r, 4, dt);
  r.placement(r.gen, 0, 0, 0); frames(r, 6, dt);                    // nothing to add: the game's own argument goes through
  r.placement(r.gen, -0.0f, 1e-30f, 0); frames(r, 3, dt);
  r.placement(r.gen, .01f, .02f, .03f); frames(r, 6, dt);
  need(::weapon_updates.load() > 30 && r.errors[1] == 4, "placements applied and four refused");
  r.end(r.gen);
}
// ---- motion: bob and sway offsets and the rotation delta, within and outside their bounds ----
static void script_motion(Run &r, int kind) {
  arm(r, kind);
  frames(r, 15, 16);
  for (int i = 0; i < 40; ++i) { r.motion(r.gen, .02f * std::sin(i * .4f), .015f * std::cos(i * .3f), .01f * std::sin(i * .9f)); animate(r, i); frame(r, 16); }
  r.motion(r.gen, .15f, -.15f, .15f); frames(r, 3, 16);
  r.motion(r.gen, .1501f, 0, 0); r.motion(r.gen, 0, 0, -.1501f); r.motion(r.gen, NAN, 0, 0); r.motion(r.gen, 0, -INFINITY, 0);
  frames(r, 3, 16);
  // Bob and sway with a rotation: applied to the rig's branch rotation while the request is alone and uncoupled.
  for (int i = 0; i < 40; ++i) { const Q q = turn(1, .2f * i, .1f, 6 * std::sin(i * .3f)); r.transform(r.gen, .01f * std::sin(i * .5f), 0, .005f, q.x, q.y, q.z, q.w); animate(r, i); frame(r, 16); }
  { const Q q = turn(0, 0, 1, 119.5f); r.transform(r.gen, 0, 0, 0, q.x, q.y, q.z, q.w); frames(r, 3, 16); }          // just inside 120 degrees
  { const Q q = turn(0, 0, 1, 120.5f); r.transform(r.gen, 0, 0, 0, q.x, q.y, q.z, q.w); }                            // outside
  { const Q q = turn(0, 1, 0, 30); r.transform(r.gen, 0, 0, 0, -q.x, -q.y, -q.z, -q.w); frames(r, 3, 16); }          // negative w: taken as its opposite
  { const Q q = turn(0, 1, 0, 30); r.transform(r.gen, 0, 0, 0, q.x * 1.0009f, q.y * 1.0009f, q.z * 1.0009f, q.w * 1.0009f); frames(r, 2, 16); } // norm inside .002: normalised
  { const Q q = turn(0, 1, 0, 30); r.transform(r.gen, 0, 0, 0, q.x * 1.0011f, q.y * 1.0011f, q.z * 1.0011f, q.w * 1.0011f); } // norm outside
  r.transform(r.gen, 0, 0, 0, 0, 0, 0, 0); r.transform(r.gen, 0, 0, 0, NAN, 0, 0, 1); r.transform(r.gen, .16f, 0, 0, 0, 0, 0, 1);
  r.transform(r.gen, 0, 0, 0, 0, 0, 0, 1); frames(r, 4, 16);        // back to the identity: the last shifted rotation is retired
  r.transform(r.gen, 0, 0, 0, 0, 0, 0, -1); frames(r, 2, 16);
  need(::weapon_updates.load() > 100, "offsets and rotations applied");
  r.end(r.gen);
}
// ---- the rotation delta's own gates ----
static void script_rotation_gates(Run &r) {
  arm(r);
  const Q q = turn(1, 0, 0, 10); r.transform(r.gen, 0, 0, 0, q.x, q.y, q.z, q.w);
  frames(r, 5, 16);
  r.req<float>(0x98, .5f); frames(r, 3, 16); r.req<float>(0x98, 0); frames(r, 2, 16);        // sight coupling: passive
  r.req<float>(0x98, -0.0f); frames(r, 2, 16); r.req<float>(0x98, 0);                       // minus zero counts as zero
  r.s.ring[0] = static_cast<uint32_t>((r.s.row + 31) % 32); frames(r, 3, 16); r.s.ring[0] = static_cast<uint32_t>(r.s.row); frames(r, 2, 16); // a fading second request
  r.s.controller[4] = 1; frames(r, 3, 16);                           // full ADS: the ordinary delta yields
  r.transform(r.gen, 0, 0, 0, q.x, q.y, q.z, q.w, true); frames(r, 3, 16); // an authored one does not
  r.s.controller[4] = 0; frames(r, 3, 16);
  r.transform(r.gen, 0, 0, 0, q.x, q.y, q.z, q.w); frames(r, 2, 16);
  // other writers of the same node: another caller retires the cached rotation; other nodes and units pass
  r.branch(GAME_OTHER, r.viewmodel, 5, ROTATION_POSE); frames(r, 2, 16);
  r.branch(ROTATION_CALLER, r.viewmodel, 7, ROTATION_POSE); r.branch(ROTATION_CALLER, OTHER_RIG, 5, ROTATION_POSE); r.branch(ROTATION_CALLER, r.viewmodel, 5, NOWHERE);
  need(NADSE_Error() == POSE_INVALID && !::armed.load(), "a null rotation refused as an invalid pose");
  r.end(r.gen);
  // the game's rotation invalid, or the product out of tolerance
  for (int bad = 0; bad < 4; ++bad) {
    arm(r); r.transform(r.gen, 0, 0, 0, q.x, q.y, q.z, q.w); frames(r, 3, 16);
    if (bad == 0) { r.s.rotation[0] = NAN; }
    if (bad == 1) { const Q n = turn(0, 1, 0, 40); set_rotation(r, {n.x * 1.012f, n.y * 1.012f, n.z * 1.012f, n.w * 1.012f}); } // norm 1.024: refused by the 0.02 tolerance
    if (bad == 2) { const Q n = turn(0, 1, 0, 40); set_rotation(r, {n.x * 1.0095f, n.y * 1.0095f, n.z * 1.0095f, n.w * 1.0095f}); } // norm 1.019: taken
    if (bad == 3) { set_rotation(r, {0, 0, 0, 0}); }
    frame(r, 16); frame(r, 16);
    need((bad == 2) == (::armed.load() != 0), "the rotation tolerance");
    r.end(r.gen);
  }
}
// ---- ADS placement, and the hip placement weight fading into the sights and back ----
static void script_ads(Run &r, int kind, int64_t dt, uint32_t blend) {
  r.blend = blend; arm(r, kind);
  r.motion(r.gen, .01f, .02f, -.01f);
  frames(r, static_cast<int>(400 / dt), dt);
  r.ads_placement(r.gen, .03f, -.02f, .04f);
  r.s.controller[4] = 1; frames(r, static_cast<int>(300 / dt), dt);     // into the sights: the weight eases to 0, the ADS placement is added at once
  r.ads_placement(r.gen, -.5f, .5f, .5f); frames(r, 3, dt);
  r.ads_placement(r.gen, .51f, 0, 0); r.ads_placement(r.gen, 0, NAN, 0);
  r.s.controller[4] = 0; frames(r, static_cast<int>(80 / dt), dt);      // back out, and in again before the fade ends
  r.s.controller[4] = 1; frames(r, static_cast<int>(60 / dt), dt);
  r.s.controller[4] = 0xff; frames(r, 2, dt);
  r.s.controller[4] = 0; frames(r, static_cast<int>(400 / dt), dt);
  r.ads_placement(r.gen, 0, 0, 0);
  r.s.flags[6] = 1; frames(r, 4, dt); r.s.flags[6] = 0;                 // the native-aim flag alone changes nothing for the rig
  r.s.flags[10] = 4; r.s.flags[15] = 32; frames(r, 4, dt); r.s.flags[10] = 0; r.s.flags[15] = 0;
  need(::weapon_updates.load() > 20, "placements applied");
  r.end(r.gen);
}
// ---- the presentation phase packet ----
static void script_transition(Run &r, int kind, int64_t dt) {
  arm(r, kind);
  frames(r, 5, dt);
  uint64_t epoch = 7;
  // a phase for the current state (hip): the weight spends the same remaining share each frame
  for (int i = 0; i <= 10; ++i) { r.transition(r.gen, epoch, i / 10.0f, 0); animate(r, i); frame(r, dt); }
  r.transition(r.gen, epoch, 1, 0); frames(r, 2, dt);                    // repeated
  r.transition(r.gen, epoch, .5f, 0);                                   // progress going back: refused
  r.transition(r.gen, epoch, 1, 1);                                     // same epoch, other state: refused
  r.transition(r.gen, epoch - 1, 0, 0);                                 // an older epoch: refused
  r.transition(r.gen, 0, 0, 0); r.transition(r.gen, epoch + 1, NAN, 0); r.transition(r.gen, epoch + 1, -.01f, 0); r.transition(r.gen, epoch + 1, 1.01f, 0); r.transition(r.gen, epoch + 1, .5f, 2);
  // into the sights with a phase
  r.s.controller[4] = 1; ++epoch;
  for (int i = 0; i <= 8; ++i) { r.transition(r.gen, epoch, i / 8.0f, 1); animate(r, i); frame(r, dt); }
  // a packet for the wrong state: the timed blend runs instead
  r.s.controller[4] = 0; frames(r, 3, dt);
  ++epoch; r.transition(r.gen, epoch, 0, 0); frame(r, dt); r.transition(r.gen, epoch, .25f, 0); frame(r, dt);
  // the packet goes stale: 250 ms is still fresh, 251 is not
  r.tick(250 - dt); frame(r, dt); r.transition(r.gen, epoch, .3f, 0); r.tick(251 - dt); frame(r, dt); frames(r, 3, dt);
  // a packet published "in the future" (the clock stepped back)
  r.transition(r.gen, epoch, .6f, 0); r.tick(-40); frame(r, dt); r.tick(40); frame(r, dt);
  // a packet that skips progress, then a new epoch in mid-fade
  r.transition(r.gen, epoch, .95f, 0); frame(r, dt); ++epoch; r.transition(r.gen, epoch, .1f, 0); frame(r, dt); r.transition(r.gen, epoch, .1f, 0); frame(r, dt);
  r.transition(r.gen, epoch, 1, 0); frame(r, dt); frame(r, dt);
  r.transition(r.gen + 1, epoch + 5, 0, 0); r.transition(0, epoch + 5, 0, 0);
  r.end(r.gen);
  r.transition(r.gen, epoch + 5, 0, 0);
  need(r.errors[1] >= 8 && r.errors[17] >= 3, "packets refused");
}
// ---- entry: the draw animation of a steady lease, and the blended entry ----
static void script_entry(Run &r, int kind, int64_t dt, int variant) {
  if (variant == 1) r.entry_animation(0, .35f, .06f);
  if (variant == 2) r.entry_animation(1000, 1, 1);
  if (variant == 3) { r.entry_animation(1001, .35f, .06f); r.entry_animation(100, -.1f, 0); r.entry_animation(100, 0, 1.1f); r.entry_animation(100, NAN, 0); r.entry_animation(100, 0, INFINITY); r.entry_animation(120, 0, 0); }
  if (variant == 4) { r.offset[0] = r.offset[1] = r.offset[2] = 0; } // nothing but the animation moves the rig
  arm(r, kind);
  frames(r, static_cast<int>(260 / dt) + 2, dt);
  if (variant == 5) { r.s.controller[4] = 1; frames(r, 3, dt); r.s.controller[4] = 0; } // full ADS is never offset
  r.tick(-300); frames(r, 2, dt);            // the clock steps back to before the lease
  r.tick(300); frames(r, 3, dt);
  r.end(r.gen);
  // a second lease for the same weapon: the remembered position is written at begin
  arm(r, kind); frames(r, static_cast<int>(230 / dt) + 2, dt);
  if (variant == 0) { r.entry_animation(50, .2f, 0); r.tick(-200); frames(r, 4, dt); } // changed while a lease runs
  r.end(r.gen);
  need(::weapon_updates.load() > 0 || (variant == 4 && kind == 0), "the rig was moved");
}
// ---- the entry rotation guard ----
static void settle(Run &r, Q q, int count = 9) { set_rotation(r, q); for (int i = 0; i < count; ++i) { r.tick(16); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); } }
static void script_guard(Run &r, int variant) {
  const Q settled = turn(0, 0, 1, 12);
  arm(r, 1); r.tick(400);                     // the first lease on a rig has no reference and is not guarded
  set_rotation(r, turn(0, 0, 1, 80)); r.tick(10); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE);
  settle(r, settled, variant == 6 ? 7 : 9);   // eight unchanged writes make the reference (seven do not)
  r.end(r.gen);
  if (variant == 7) r.rotation_guard(0);
  arm(r, 1);
  // The game's write starts far off and halves its error each frame.
  float error = variant == 1 ? 1.6f : variant == 2 ? 1.4f : 70;
  int held = 0;
  for (int i = 0; i < 14; ++i) {
    set_rotation(r, turn(0, 0, 1, 12 + error)); r.tick(variant == 3 ? 40 : 16);
    if (variant == 8 && i == 2) { r.rotation_guard(0); }
    if (variant == 8 && i == 4) { r.rotation_guard(1); }
    if (variant == 9 && i == 3) { r.s.flags[14] = 4; }   // the owner proof fails inside the window: no hold, and no disarm either
    if (variant == 10 && i >= 2 && i < 6) error *= 2;     // the write stands still: two unchanged writes release the guard
    const uint32_t before = ::policy.guarded;
    r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE);
    r.rotation(GAME_OTHER, r.viewmodel, 5, ROTATION_POSE); r.rotation(EXE_OTHER, r.viewmodel, 5, ROTATION_POSE); r.rotation(OUTSIDE, r.viewmodel, 5, ROTATION_POSE);
    held += ::policy.guarded != before;
    error /= 2;
  }
  if (variant == 4) { r.tick(300 - 14 * 16); set_rotation(r, turn(0, 0, 1, 60)); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); r.tick(1); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); }
  if (variant == 5) { r.tick(-2000); set_rotation(r, turn(0, 0, 1, 60)); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); r.rotation(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); r.tick(2000); }
  settle(r, turn(1, 0, 0, 30), 10);           // a new settled rotation elsewhere, for the lease after this
  r.end(r.gen);
  const bool expect = variant == 0 || variant == 1 || variant == 3 || variant == 4 || variant == 5 || variant == 8 || variant == 9 || variant == 10;
  need((held > 0) == expect, "the guard held exactly when it should");
  arm(r, 1);
  for (int i = 0; i < 30; ++i) { set_rotation(r, turn(1, 0, 0, 30 + (i < 3 ? 40.0f : 0.0f))); r.tick(8); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); r.rotation(PAIR_CALLER, r.viewmodel, 5, ROTATION_POSE); r.rotation(PAIR_CALLER, r.viewmodel, 5, ROTATION_POSE); }
  need(::policy.trace_count == NADSE_ROTATION_TRACE_ROWS, "the trace filled all its rows");
  // a rotation that is not a quaternion is not judged at all; another rig's reference is not this rig's
  set_rotation(r, {0, 0, 0, 1.02f}); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE);
  r.end(r.gen);
}
// ---- the remembered entry position per weapon, and the table filling up ----
static void script_entry_positions(Run &r, int variant) {
  const int weapons = variant == 1 ? 19 : 4;
  if (variant == 2) r.entry_position(0);
  if (variant == 3) r.entry_animation(0, 0, 0);
  for (int round = 0; round < 3; ++round) for (int k = 0; k < weapons; ++k) {
    r.weapon = WEAPON + static_cast<uint32_t>(k); r.offset[0] = .01f * k; r.offset[2] = -.02f * k;
    // steady and blended leases alternate: only a steady one writes; a blended one remembers once its weight is 1
    const int kind = (round == 1 && k % 3 == 0) ? 0 : 1;
    arm(r, kind);
    if (variant == 4 && k == 1) { r.s.controller[4] = 1; }  // armed straight into the sights: nothing to remember until out of them
    r.pose(1 + k, 2, 3 - k); frame(r, 16, F_RIG); r.pose(5, 5, 5); frame(r, 16, F_RIG);
    r.s.controller[4] = 0; frame(r, kind ? 16 : 200, F_RIG); frame(r, 16, F_RIG);
    if (variant == 2 && round == 1 && k == 2) r.entry_position(1);
    if (variant == 5 && round == 1 && k == 1) { r.viewmodel = r.viewmodel; r.end(r.gen); r.weapon = 0x999; arm(r, 1); frame(r, 16, F_RIG); } // one more weapon in between
    r.end(r.gen);
  }
  // With nineteen weapons every place has been given to a newer weapon by the time a weapon returns; the newest ones are still there.
  if (variant == 1) {
    need(::entry_position_writes == 0, "no weapon found again after nineteen");
    for (int k : {18, 17, 2, 16}) { r.weapon = WEAPON + static_cast<uint32_t>(k); arm(r, 1); frame(r, 16, F_RIG); r.end(r.gen); }
    need(::entry_position_writes == 3, "the three newest weapons found");
  }
  need(variant == 2 || ::entry_position_writes > 0, "remembered positions written");
}

// ---- the zoom depth term and the rig-root counter-shift ----
static void script_depth(Run &r, int kind, int64_t dt, int variant) {
  r.offset[0] = r.offset[1] = r.offset[2] = 0;
  if (variant == 1) { r.s.depth.in_overrides = true; }                              // the weapon has an override definition
  if (variant == 2) { r.s.depth.zoom[0] = 3.5f; r.s.depth.zoom[2] = 1; }            // the widest lens last, as on the captured optic
  if (variant == 3) { r.s.depth.near_term[0] = .02f; r.s.depth.near_term[1] = -.03f; r.s.depth.coefficient[1] = .5f; r.s.depth.coefficient[2] = .7f; r.s.depth.degrees = 70; }
  if (variant == 4) { r.offset[0] = .1f; r.offset[1] = .05f; }                      // with a saved placement as well
  if (variant == 7) { r.s.depth.shift = 3; r.s.depth.resource_shift = 21; r.s.depth.resource_di = -1; } // collisions in both tables, the last record of the table
  if (variant == 8) { r.s.resource = 272290ull * 7 - 6; r.s.depth.resource_shift = 9; r.s.depth.resource_di = 5; } // the row search wraps round the end of the table
  if (variant == 9) { r.s.depth.capacity = 128; r.s.depth.multiplier = 0x9E3779B1u; r.s.depth.shift = 40; r.s.depth.in_overrides = true; r.s.depth.override_shift = 63; r.s.depth.di = 4095; r.s.depth.wi = 4095; }
  arm(r, kind);
  optic(r, 0);
  frames(r, 6, dt, F_FULL);
  zoom_to(r, 1, 6, dt); frames(r, 4, dt, F_FULL);
  zoom_to(r, 2, 5, dt); frames(r, 40, dt, F_FULL);          // long enough for the 500 ms revalidation
  zoom_to(r, 0, 4, dt); zoom_to(r, 2, 3, dt);
  if (variant == 5) { r.req<uint8_t>(0x8D, 1); r.req<float>(0x88, .6f); frames(r, 5, dt, F_FULL); r.req<float>(0x88, 1); frames(r, 2, dt, F_FULL); r.req<float>(0x88, 0); frames(r, 2, dt, F_FULL); r.req<uint8_t>(0x8D, 0); }
  if (variant == 6) { r.s.controller[4] = 1; frames(r, 4, dt, F_FULL); r.s.controller[4] = 0; frames(r, 4, dt, F_FULL); }
  need(::policy.depth.observed.applied > 10 && ::zero_restores.load() == 0, "the depth term applied");
  const bool shifted = ::policy.rig_shifted && ::policy.carrier_shifted; // not when the last level is the widest lens, or the mix is 0
  need(shifted == (variant != 2 && variant != 5), "rig and root shifted when the lease ends");
  r.end(r.gen);
  need(!shifted || ::zero_restores.load() >= 2, "rig and root restored at the end");
}
// The request, the lens locals or the definition say something the depth term does not accept. `what` picks it.
static const char *const DEPTH_REJECTS[] = {
    "zoom below 0.1", "zoom above 100", "zoom not a number", "coefficient too small", "coefficient above 100", "near term above 1", "near term not finite", "setting below 10 degrees",
    "setting above 170 degrees", "no parameter kind 1", "two parameter kinds 1", "zoom level 3 selected", "weapon not in the pool", "weapon index 4096", "pool names another descriptor",
    "override index 4096", "no resource row", "resource id 0", "resource row index past the table", "map capacity not a power of two", "map capacity 0", "map capacity above the limit",
    "the weapon id is the map's empty value", "map full of other weapons", "more than 64 probes", "request byte 0xD is 2", "request byte 0xF set", "frame byte 0 set", "frame byte 1 is 2",
    "sight coupling not zero", "lens words differ", "target lens too small", "target lens too large", "current lens not finite", "prior lens negative", "distance beyond 4 m",
    "previous distance not finite", "mix out of range", "correction above 1 m", "zoom policy active", "no lens observation", "depth code changed", "table pointer cells unreadable",
    "angle above 1.48", "reference beyond 4 m", "descriptor table address invalid", "parameter table address invalid", "override data address invalid", "lens observed on another thread",
    "request changed between getter and setter", "getter for another node", "getter from another caller", "no stack frame"};
static void script_depth_reject(Run &r, int what, int when) {
  r.offset[0] = r.offset[1] = r.offset[2] = 0;
  if (what == 39) r.s.depth.in_overrides = false;
  arm(r);
  optic(r, 1);
  const Scene good = r.s; const size_t pokes = r.pokes.size();
  unsigned parts = F_FULL;
  auto apply = [&] {
    Scene::Depth &d = r.s.depth;
    switch (what) {
    case 0: d.zoom[2] = .09f; break; case 1: d.zoom[0] = 100.5f; break; case 2: d.zoom[1] = NAN; break;
    case 3: d.coefficient[1] = .00009f; break; case 4: d.coefficient[2] = 100.5f; break; case 5: d.near_term[0] = 1.01f; break; case 6: d.near_term[1] = INFINITY; break;
    case 7: d.degrees = 9.9f; break; case 8: d.degrees = 170.5f; break; case 9: d.kinds[0] = 0; break; case 10: d.kinds[2] = 1; break; case 11: d.packed = 64 | (3 << 4); break;
    case 12: d.in_primary = false; break; case 13: d.wi = 4096; break; case 14: d.descriptor_ok = false; break; case 15: d.in_overrides = true; d.di = 4096; break;
    case 16: d.in_resources = false; break; case 17: r.s.resource = 0; break; case 18: d.resource_di = -2; break; case 19: d.capacity = 12; break; case 20: d.capacity = 0; break;
    case 21: d.capacity = 0x200000; break; case 22: d.empty = r.weapon; break; case 23: d.in_primary = false; d.shift = 8; break;
    case 24: d.capacity = 128; d.shift = 64; break;
    case 25: r.req<uint8_t>(0x8D, 2); break; case 26: r.req<uint8_t>(0x8F, 1); break; case 27: r.s.frame[0] = 1; break; case 28: r.s.frame[1] = 2; break;
    case 29: r.req<float>(0x98, 1e-20f); break; case 30: { const float other = r.optic_target * 1.0001f; std::memcpy(r.s.frame + 0x1C, &other, 4); } break;
    case 31: { const float v = .0001f; std::memcpy(r.s.frame + 0x18, &v, 4); } break; case 32: { const float v = 2.97f; std::memcpy(r.s.frame + 0x18, &v, 4); } break;
    case 33: { const float v = NAN; std::memcpy(r.s.frame + 0x1C, &v, 4); r.req(0x84, v); } break; case 34: r.req<float>(0xA0, -.01f); break;
    case 35: r.req<float>(0x90, 4.01f); break; case 36: r.req<float>(0x9C, NAN); break; case 37: r.req<uint8_t>(0x8D, 1); r.req<float>(0x88, 1.01f); break;
    case 38: d.coefficient[1] = 40; d.near_term[0] = -1; d.near_term[1] = -1; break;
    case 39: r.zoom_policy(r.gen, 1); break; case 40: parts = F_RIG | F_CARRIER | F_BRANCH; break;
    case 41: r.poke(A_G_DEPTH1 + (when * 4 + 2) % 9, 1, bytes_value(0x90, 1)); break;
    case 42: r.poke(A_G_POOL, 0, bytes_value(1, 8)); break;
    case 43: d.degrees = 170; d.zoom[1] = 1; d.zoom[0] = 2.5f; break; // the selected lens is half of 170 degrees: 1.4835 rad
    case 44: d.coefficient[1] = 90; d.coefficient[0] = .001f; r.req<float>(0x90, 3.9f); break;
    case 45: r.poke(A_POOL, 0x30 + 0x18, bytes_value(0x100, 8)); break; case 46: r.poke(A_POOL, 0x30 + 0x28, bytes_value(0x800000000000ull, 8)); break;
    case 47: d.in_overrides = true; r.poke(A_POOL, 0x30 + 0x80, bytes_value(7, 8)); break;
    default: break;
    }
  };
  auto one = [&](int i) {
    animate(r, i); r.tick(16);
    if (what == 40 || what < 48) { if (parts & F_GETTER) r.getter(OBSERVE_CALLER, r.viewmodel, 50); }
    else if (what == 48) { r.getter(OBSERVE_CALLER, r.viewmodel, 50); shim_thread_id = 2; }
    else if (what == 49) { r.getter(OBSERVE_CALLER, r.viewmodel, 50); r.req<float>(0x90, r.req_get<float>(0x90) + .001f * (i & 1 ? 1 : -1)); }
    else if (what == 50) { r.getter(OBSERVE_CALLER, r.viewmodel, 49); }
    else if (what == 51) { r.getter(GAME_OTHER, r.viewmodel, 50); }
    else if (what == 52) { r.stack_on = false; r.getter(OBSERVE_CALLER, r.viewmodel, 50); }
    r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48));
    r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE);
    r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE);
    shim_thread_id = 1; r.stack_on = true;
  };
  if (when == 1) for (int i = 0; i < 12; ++i) one(i);      // first valid, then the trouble arrives
  const uint64_t applied = ::policy.depth.observed.applied;
  if (what < 48) apply();
  auto trouble = [&](int i) { if (what >= 48) one(i); else { animate(r, i); frame(r, 16, parts); } };
  for (int i = 0; i < 40; ++i) trouble(i);
  if (when == 1 && what < 25) zoom_event(r, 2);              // a new distance event while the definition is unusable
  for (int i = 0; i < 40; ++i) trouble(i);
  // A changed definition is found by the next 500 ms revalidation or the next distance event; the code is proven once per lease.
  const bool definition = what < 25 || what == 38 || (what >= 42 && what <= 47);
  // (the selected zoom level and the pool's descriptor row are read at a distance event only)
  if (what != 49 && what != 39 && !(when == 1 && (what == 41 || what == 11 || what == 14)))
    need(::policy.depth.observed.applied == applied || (when == 1 && definition && ::policy.depth.observed.applied - applied < 35), "the depth term refused");
  // the trouble goes away
  if (what == 39) r.zoom_policy(r.gen, 0);
  const float kept_target = r.optic_target;
  r.s = good; r.s.write_preset = false; r.unpoke(pokes); r.optic_target = kept_target;
  if (when == 1 && what < 25) { r.optic_target = lens_of(r, 1); }
  for (int id : {A_G_POOL}) for (int w = 0; w < 2; ++w) World::put<uint64_t>(W[w].a[id], W[w].a[A_POOL]);
  for (int w = 0; w < 2; ++w) { const uint64_t pool = W[w].a[A_POOL] + 0x30; World::put<uint64_t>(pool + 0x18, W[w].a[A_DESCRIPTORS]); World::put<uint64_t>(pool + 0x28, W[w].a[A_PARAMS]); World::put<uint64_t>(pool + 0x80, W[w].a[A_DATA]);
                                for (int i = 0; i < 9; ++i) World::literal(W[w].a[A_G_DEPTH1 + i], W[w].L->depth_hex[i]); }
  for (int i = 0; i < 40; ++i) one(100 + i);
  zoom_to(r, 0, 3, 16); zoom_to(r, 2, 3, 16); frames(r, 35, 16, F_FULL);
  r.end(r.gen);
}
// ---- the rig-root counter-shift's own gates ----
static void script_carrier(Run &r, int variant) {
  r.offset[0] = r.offset[1] = r.offset[2] = 0;
  arm(r); optic(r, 1);
  frames(r, 4, 16, F_FULL);
  const uint64_t before = ::forwarded[0].load();
  auto rig = [&](int i) { animate(r, i); r.tick(16); r.getter(OBSERVE_CALLER, r.viewmodel, 50); r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); };
  for (int i = 0; i < 6; ++i) {
    rig(i);
    switch (variant) {
    case 0: r.position(CARRIER_CALLER, r.viewmodel, 0, POSITION_POSE); break;                         // not the evaluator's own root
    case 1: r.inner_frame = true; r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); r.inner_frame = false; break; // another stack frame
    case 2: r.position(GAME_OTHER, r.viewmodel, 0, ROOT_POSE); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); break; // another writer of the root first
    case 3: r.position(CARRIER_CALLER, r.viewmodel, 5, ROOT_POSE); r.position(CARRIER_CALLER, OTHER_RIG, 0, ROOT_POSE); break; // the call site, another node or unit
    case 4: r.s.ring[0] = static_cast<uint32_t>((r.s.row + 31) % 32); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); r.s.ring[0] = static_cast<uint32_t>(r.s.row); break; // not alone in the ring
    case 5: set_root(r, {0, 0, 0, 1.0011f}, 1, 2, 3); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); break; // not a unit quaternion (0.002)
    case 6: set_root(r, turn(0, 0, 1, 20), NAN, 2, 3); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); break;
    case 7: shim_thread_id = 3; r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); shim_thread_id = 1; break;
    case 8: r.req<float>(0x9C, 1e-3f * (i + 1)); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); break;   // the request changed since the lens was observed
    case 9: r.s.controller[4] = 1; r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); r.s.controller[4] = 0; break;
    case 10: r.stack_on = false; r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); r.stack_on = true; break;
    case 11: r.s.flags[14] = 4; r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); break;         // owner lost in the carrier call
    case 12: r.s.locals_at = 1; r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); r.s.locals_at = 0; break; // the pose array is another one at the end
    case 13: setter_applies = false; r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); break;   // the queued write never became the node's position
    case 14: r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); break; // consumed once
    case 15: r.poke(A_RIG, 0x80, bytes_value(3, 8)); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); r.unpoke(r.pokes.size() - 1); break; // no pose array
    case 16: r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); r.s.root_local[0] = 99; r.s.write_root_local = true; break; // someone else wrote the root after us
    case 17: set_root(r, {0, 0, 0, 1}, 1e30f, 1e30f, 1e30f); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); break; // huge but finite
    default: break;
    }
    if (variant == 11) break;
  }
  if (variant == 18) { for (int c = 0; c < 3; ++c) { r.poke(A_G_CARRIER1 + c, c, bytes_value(0xCC, 1)); } rig(9); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); r.unpoke(); rig(10); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); }
  need(::forwarded[0].load() > before, "calls forwarded");
  r.end(r.gen);
  setter_applies = true;
  // armed again: the code proof is made once per lease
  arm(r); optic(r, 2); frames(r, 3, 16, F_FULL); r.end(r.gen);
}
// ---- node 0 / node 5 / node 50 calls from the game's call sites and from other callers ----
static void script_callers(Run &r, int kind, bool armed_first) {
  const Sym callers[] = {RIG_CALLER, ROTATION_CALLER, OBSERVE_CALLER, CARRIER_CALLER, PAIR_CALLER, INPUT_CALLER, GAME_OTHER, EXE_OTHER, OUTSIDE, {0xffff, 0x1234}};
  const uint32_t units[] = {VIEWMODEL, OTHER_RIG, WEAPON_UNIT, 0, 0xffffffffu}, nodes[] = {0, 5, 50, 7, 0xffffffffu};
  auto sweep = [&](int pass) {
    int n = 0;
    for (const Sym &caller : callers) for (uint32_t unit : units) for (uint32_t node : nodes) {
      const Sym setter_poses[] = {r.request_sym(0x48), ROOT_POSE, POSITION_POSE, NOWHERE, {0xffff, 0x10}};
      const Sym rotation_poses[] = {ROTATION_POSE, ROOT_SOURCE, NOWHERE};
      ++n; r.tick(n % 5 == 0 ? 3 : 0);
      r.position(caller, unit, node, setter_poses[(n + pass) % 5]);
      r.rotation(caller, unit, node, rotation_poses[(n + pass) % 3]);
      r.branch(caller, unit, node, rotation_poses[(n / 3 + pass) % 3]);
      r.getter(caller, unit, node);
      if (n % 7 == 0) { r.stack_on = false; r.getter(caller, unit, node); r.position(caller, unit, node, ROOT_POSE); r.stack_on = true; }
      if (r.thunks || kind >= 2) { r.getter(caller, unit, node, true, n % 3 ? r.request_sym() : NOWHERE); r.input90(caller, unit, node, n % 4 ? r.request_sym() : Sym{A_CONTEXT, 0x200}); }
      // The rig's ordinary frame in between keeps the lease's caches in use, so a foreign write has something to retire.
      if (n % 11 == 0 && ::armed.load()) { animate(r, n); frame(r, 1, F_FULL); }
    }
  };
  if (!armed_first) sweep(0);               // before any begin: skipped where the original would call a null pointer
  r.offset[0] = .05f; arm(r, kind); optic(r, 1);
  { const Q q = turn(1, 0, 0, 5); r.transform(r.gen, .01f, 0, 0, q.x, q.y, q.z, q.w); }
  frames(r, 3, 16, F_FULL);
  sweep(1);
  need(::armed.load() != 0, "the lease survived every foreign call");
  r.end(r.gen);
  sweep(2);                                  // after the end: everything forwards
}
// ---- the lease duration and its expiry, found by each path that can find it ----
static void script_expiry(Run &r, int where, uint32_t duration) {
  r.duration = duration; r.poll = where == 6;
  arm(r, where == 8 ? 3 : 1); optic(r, 1);
  const Q q = turn(0, 1, 0, 8); r.transform(r.gen, .02f, 0, .01f, q.x, q.y, q.z, q.w);
  const uint64_t deadline = ::policy.deadline;
  need(deadline == ::fixture_now + duration, "a deadline");
  int i = 0;
  while (::fixture_now + 16 < deadline - 1) { animate(r, i++); frame(r, 16, F_FULL); }
  r.tick(static_cast<int64_t>(deadline - 1 - ::fixture_now)); animate(r, i++); frame(r, 0, F_FULL); // one millisecond before: still armed
  need(::armed.load() != 0, "armed until the deadline");
  r.tick(1);
  switch (where) {
  case 0: r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); break;
  case 1: r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); break;
  case 2: r.getter(OBSERVE_CALLER, r.viewmodel, 50); r.tick(-1); r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); r.tick(1); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); break;
  case 3: r.placement(r.gen, .1f, 0, 0); break;
  case 4: r.zoom_policy(r.gen, 1); break;
  case 5: r.transition(r.gen, 3, .5f, 0); r.motion(r.gen, 0, 0, 0); break;   // the phase packet does not look at the deadline
  case 6: r.stats_call(0, r.gen); break;
  case 7: r.s.ring[0] = static_cast<uint32_t>((r.s.row + 31) % 32); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); break; // expired while not alone: no rotation restore counted
  case 8: r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym()); r.getter(OBSERVE_CALLER, r.viewmodel, 50, r.thunks, r.request_sym()); r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); break;
  default: break;
  }
  need(!::armed.load() && ::expired.load(), "expired");
  r.poll = true;
  r.placement(r.gen, 0, 0, 0); r.zoom_policy(r.gen, 0); r.transition(r.gen, 9, 0, 0);
  need(NADSE_Error() == INVALID_CONFIG && r.errors[DEADLINE_EXPIRED] >= 2, "refused as expired");
  frames(r, 3, 16, F_FULL);                 // after expiry everything forwards
  r.end(r.gen); r.end(r.gen);
}
// ---- control calls in right and wrong orders ----
static void script_controls(Run &r) {
  r.end(0); r.end(5); r.placement(1, 0, 0, 0); r.motion(0, 0, 0, 0); r.zoom_policy(1, 1); r.zoom_policy(1, 2); r.transition(1, 1, 0, 0);
  r.stats_call(0, 0); r.stats_call(1, 0); r.stats_call(2, 0); r.stats_call(0, 1); r.stats_call(1, 1); r.stats_call(2, 1);
  for (int header = 1; header <= 5; ++header) for (int which = 0; which < 3; ++which) r.stats_call(which, 0, header);
  r.diagnostics_nowhere(false); r.diagnostics_nowhere(true);
  r.rotation_guard(7); r.rotation_guard(0); r.rotation_guard(1); r.entry_position(9); r.entry_position(0); r.entry_position(1);
  r.validate(); r.validate(true);
  arm(r);
  r.stats_call(0, 0); r.stats_call(1, 0); r.stats_call(2, 0); r.stats_call(0, r.gen + 1); r.stats_call(1, r.gen + 1); r.stats_call(2, r.gen + 1);
  for (int header = 0; header <= 5; ++header) for (int which = 0; which < 3; ++which) r.stats_call(which, r.gen, header);
  r.normal(); r.begin(); r.begin(1); r.validate();           // busy
  need(NADSE_Error() == BUSY, "a second begin refused as busy");
  r.fpv();
  r.placement(r.gen + 1, 0, 0, 0); r.placement(0, 0, 0, 0); r.ads_placement(r.gen - 1, 0, 0, 0); r.zoom_policy(r.gen, 2); r.zoom_policy(r.gen + 1, 1);
  r.zoom_policy(r.gen, 1); r.zoom_policy(r.gen, 1); r.zoom_policy(r.gen, 0);
  frames(r, 5, 16, F_FULL);
  r.end(r.gen + 1); r.end(0);
  const uint64_t first = r.gen;
  r.end(first); r.end(first);                                // the second end of a retired lease is accepted
  r.placement(first, 0, 0, 0); r.transition(first, 1, 1, 0); r.stats_call(0, first); r.stats_call(0, 0);
  arm(r, 1);
  need(r.gen == first + 1, "the next generation");
  r.end(first);                                              // a stale handle cannot end the new lease
  r.stats_call(0, first); r.placement(first, .1f, 0, 0);
  frames(r, 3, 16);
  // a control call after a hook has disarmed the lease on a lost owner
  r.s.flags[14] = 4; frame(r, 16, F_RIG); r.s.flags[14] = 0;
  r.placement(r.gen, .1f, 0, 0); r.zoom_policy(r.gen, 1); r.transition(r.gen, 4, 0, 0);
  need(r.errors[OWNER_MISMATCH] >= 2 && NADSE_Error() == INVALID_CONFIG, "refused on a disarmed lease");
  r.end(r.gen); r.end(first);
  r.validate(); r.validate(true);
}

// ---- refused configurations, images, code bytes and worlds ----
static void script_refusals(Run &r, int kind) {
  int cases = 0;
  uint32_t expected = INVALID_CONFIG;      // the error code the cases that follow must be refused with
  auto refused = [&](const char *what, const std::function<void()> &damage) {
    const Run kept = r;
    damage();
    r.validate(); const uint32_t validated = r.result, validate_error = NADSE_Error(); r.begin(kind);
    // (validate proves the slots after the graph; begin's own first write and its pin come before or after other proofs)
    const bool begin_only = expected == PIN_FAILED || !std::strcmp(what, "no place for the generation");
    if (r.result != 0 || NADSE_Error() != expected || (begin_only ? validated != 1 : validated != 0 || validate_error != expected)) {
      char text[200]; std::snprintf(text, sizeof text, "refused with error %u: %s (validate %u with error %u, begin %u with error %u)", expected, what, validated, validate_error, r.result, NADSE_Error()); need(false, text); }
    const uint64_t gen = r.gen, previous = r.previous_gen;
    r.unpoke(kept.pokes.size());
    r = kept; r.gen = gen; r.previous_gen = previous; r.s.write_preset = true; r.s.write_root_local = true;
    ++cases;
  };
  auto accepted = [&](const char *what, const std::function<void()> &change) {
    const Run kept = r;
    change(); r.validate(); const uint32_t validated = r.result; r.begin(kind);
    if (r.result != 1 || validated != 1) { char text[160]; std::snprintf(text, sizeof text, "accepted: %s (validate %u, begin %u, error %u)", what, validated, r.result, NADSE_Error()); need(false, text); }
    r.end(r.gen);
    const uint64_t gen = r.gen, previous = r.previous_gen;
    r.unpoke(kept.pokes.size());
    r = kept; r.gen = gen; r.previous_gen = previous; r.s.write_preset = true; r.s.write_root_local = true;
  };
  r.normal();
  // the configuration itself
  refused("size", [&] { r.size = 103; }); refused("abi 1", [&] { r.abi = 1; }); refused("abi 3", [&] { r.abi = 3; }); refused("reserved", [&] { r.reserved_field = 1; });
  refused("duration", [&] { r.duration = 30501; }); accepted("duration 30500", [&] { r.duration = 30500; });
  refused("avatar index 8", [&] { r.index = 8; }); refused("avatar 0", [&] { r.avatar = 0; r.s.descriptor[2] = 0; r.s.under[1] = 0; });
  refused("weapon 0", [&] { r.weapon = 0; r.s.wdesc[0] = 0; }); refused("body 0", [&] { r.body = 0; r.s.descriptor[3] = 0; });
  refused("viewmodel 0", [&] { r.viewmodel = 0; r.s.rig_reference = 0; }); refused("viewmodel -1", [&] { r.viewmodel = 0xffffffffu; r.s.rig_reference = 0xffffffffu; });
  refused("weapon unit 0", [&] { r.weapon_unit = 0; r.s.wdesc[1] = 0; }); refused("weapon unit -1", [&] { r.weapon_unit = 0xffffffffu; r.s.wdesc[1] = 0xffffffffu; });
  refused("weapon unit is the rig", [&] { r.weapon_unit = r.viewmodel; r.s.wdesc[1] = r.viewmodel; }); refused("weapon unit is the body", [&] { r.weapon_unit = r.body; r.s.wdesc[1] = r.body; });
  refused("weapon descriptor below 64 KiB", [&] { r.wdesc_absolute = 65535; });
  refused("blend 0", [&] { r.blend = 0; }); refused("blend 1001", [&] { r.blend = 1001; }); accepted("blend 1000", [&] { r.blend = 1000; }); accepted("blend 1", [&] { r.blend = 1; });
  refused("offset not finite", [&] { r.offset[1] = NAN; }); refused("offset x", [&] { r.offset[0] = .5001f; }); refused("offset y", [&] { r.offset[1] = -.5001f; });
  refused("offset z", [&] { r.offset[2] = INFINITY; }); accepted("offsets at the bound", [&] { r.offset[0] = .5f; r.offset[1] = -.5f; r.offset[2] = .5f; });
  refused("no configuration", [&] { r.config_null = true; });
  // the owner
  expected = OWNER_MISMATCH;
  refused("another context", [&] { r.context_delta = 16; }); refused("avatar index 2 of 2", [&] { r.index = 2; });
  refused("another game image", [&] { r.game_delta = 0x1000; }); refused("another executable", [&] { r.exe_delta = -0x1000; }); // (the owner is read through them first)
  refused("weapon descriptor unreadable", [&] { r.wdesc_absolute = 0x800000000000ull - 8; }); refused("weapon descriptor elsewhere", [&] { r.wdesc_delta = 0x40; }); refused("context cell empty", [&] { r.poke(A_G_CONTEXT, 0, bytes_value(0, 8)); });
  refused("no camera", [&] { r.poke(A_CONTEXT, 0, bytes_value(0xffff, 8)); }); refused("another avatar owner", [&] { r.owner_delta = 8; });
  refused("controller cell", [&] { r.poke(A_G_CONTROLLER, 0, bytes_value(0x800000000000ull, 8)); });
  for (int i : {0, 1, 2, 3, 5}) refused("controller byte", [&] { r.s.controller[i] = 1; });
  refused("in the sights already", [&] { r.s.controller[4] = 1; }); refused("native aim flag", [&] { r.s.flags[6] = 1; });
  accepted("shoulder aim alone", [&] { r.s.flags[10] = 4; r.s.flags[15] = 32; r.s.flags[6] = 0xfe; r.s.flags[14] = 0xfb; });
  refused("no avatars", [&] { r.s.avatar_count = 0; }); refused("nine avatars", [&] { r.s.avatar_count = 9; }); accepted("eight avatars", [&] { r.s.avatar_count = 8; });
  refused("descriptor pointer", [&] { r.poke(A_OWNER, 0x110, bytes_value(0, 8)); }); refused("another avatar", [&] { r.s.descriptor[2] = AVATAR + 1; });
  refused("another body", [&] { r.s.descriptor[3] = BODY + 1; }); refused("seated", [&] { r.s.flags[14] = 4; }); refused("another rig", [&] { r.s.rig_reference = OTHER_RIG; });
  refused("rig generation", [&] { r.s.generations[3] = 2; }); refused("weapon unit generation", [&] { r.s.generations[4] = 0; }); refused("registry capacity", [&] { r.s.registry_capacity = 3; });
  refused("registry capacity too large", [&] { r.s.registry_capacity = 0x400001; }); refused("no registry", [&] { r.poke(A_E_REGISTRY, 0, bytes_value(0, 8)); });
  refused("no unit table", [&] { r.poke(A_REGISTRY, 0x88, bytes_value(0, 8)); }); refused("no rig object", [&] { r.poke(A_UNITS, 3 * 8, bytes_value(0, 8)); });
  refused("no weapon object", [&] { r.poke(A_UNITS, 4 * 8, bytes_value(0x1000, 8)); });
  refused("another weapon", [&] { r.s.wdesc[0] = WEAPON + 1; }); refused("another weapon unit", [&] { r.s.wdesc[1] = OTHER_WEAPON; });
  refused("ring head", [&] { r.s.ring[0] = 32; }); refused("ring tail", [&] { r.s.ring[1] = 32; }); refused("newest request not ordinary", [&] { r.s.under[0] = 6; });
  accepted("newest request kind with high bits", [&] { r.s.under[0] = 0x70004; });
  refused("newest request of another avatar", [&] { r.s.under[1] = AVATAR + 1; }); refused("request is not the next row", [&] { r.request_delta = 0xE8; });
  refused("empty ring, idle record of another avatar", [&] { r.s.ring[0] = r.s.ring[1]; r.s.idle[1] = 7; });
  accepted("empty ring, idle record", [&] { r.s.ring[0] = r.s.ring[1]; });
  // the rig's node graph
  expected = GRAPH_MISMATCH;
  refused("62 nodes", [&] { r.s.graph_count = 62; }); refused("64 nodes", [&] { r.s.graph_count = 64; }); refused("another getter", [&] { r.s.getter_delta = 16; });
  refused("getter bytes", [&] { r.poke(A_E_GRAPH_GETTER, 4, bytes_value(0x90, 1)); }); refused("no vtable", [&] { r.poke(A_RIG, 0, bytes_value(0, 8)); });
  refused("no links", [&] { r.poke(A_RIG, 0x98, bytes_value(0, 8)); }); refused("no hashes", [&] { r.poke(A_RIG, 0xA0, bytes_value(0x800000000000ull, 8)); });
  for (const GraphRow &row : GRAPH_ROWS) { refused("node hash", [&] { r.s.hashes[row.node] ^= 0x100; }); refused("node link", [&] { r.s.links[row.node] ^= 0x10000; }); }
  accepted("other nodes differ", [&] { r.s.hashes[6] = 1; r.s.links[62] = 9; r.s.hashes[49] = 0; });
  // the images and the code
  expected = IMAGE_MISMATCH;
  refused("game stamp", [&] { r.poke(A_GAME, 128 + 8, bytes_value(1, 4)); }); refused("game size", [&] { r.poke(A_GAME, 128 + 80, bytes_value(0x3382001, 4)); });
  refused("exe stamp", [&] { r.poke(A_EXE, 128 + 8, bytes_value(1, 4)); }); refused("exe size", [&] { r.poke(A_EXE, 128 + 80, bytes_value(1, 4)); });
  refused("no MZ", [&] { r.poke(A_GAME, 0, bytes_value(0x5A4C, 2)); }); refused("header offset 63", [&] { r.poke(A_GAME, 0x3c, bytes_value(63, 4)); });
  refused("header offset 4097", [&] { r.poke(A_EXE, 0x3c, bytes_value(4097, 4)); }); refused("header offset negative", [&] { r.poke(A_EXE, 0x3c, bytes_value(0xffffff80u, 4)); });
  refused("no PE", [&] { r.poke(A_GAME, 128, bytes_value(0x4551, 4)); }); refused("machine", [&] { r.poke(A_GAME, 132, bytes_value(0x14c, 2)); });
  refused("optional header magic", [&] { r.poke(A_EXE, 128 + 24, bytes_value(0x10b, 2)); });
  expected = CODE_MISMATCH;
  for (int id : {A_G_CARRIER_CALL, A_G_CARRIER_PAIR, A_G_RIG_CALL, A_G_ROTATION_CALL, A_G_OBSERVE_CALL}) for (int byte = 0; byte < 3; byte += 2) refused("call site bytes", [&] { r.poke(id, byte, bytes_value(0x90, 1)); });
  expected = API_MISMATCH;
  refused("no API root", [&] { r.poke(A_G_API_ROOT, 0, bytes_value(0, 8)); }); refused("no Unit API", [&] { r.poke(A_API_ROOT, 0x18, bytes_value(0xfff0, 8)); });
  expected = SLOT_NOT_WRITABLE;
  refused("Unit API table unaligned", [&] { r.poke(A_API_ROOT, 0x18, address_value(A_API, 4)); });
  refused("Unit API table not writable", [&] { ::fixture_slot_readonly = true; }); ::fixture_slot_readonly = false;
  for (int which = 0; which < 4; ++which) {
    expected = API_MISMATCH;
    refused("slot holds something else", [&] { r.poke(A_API, W[0].slot(which) - W[0].a[A_API], slot_value(which, SLOT_FOREIGN + which)); });
    refused("slot holds the helper already", [&] { r.poke(A_API, W[0].slot(which) - W[0].a[A_API], slot_value(which, SLOT_HOOK + which)); });
    refused("slot is empty", [&] { r.poke(A_API, W[0].slot(which) - W[0].a[A_API], bytes_value(0, 8)); });
    expected = CODE_MISMATCH;
    for (int byte : {0, 20, 47}) refused("wrapper bytes", [&] { r.poke(A_E_WRAPPER0 + which, byte, bytes_value(0xCC, 1)); });
  }
  expected = PIN_FAILED;
  refused("module cannot be pinned", [&] { shim_pin_fails = true; }); shim_pin_fails = false;
  expected = INVALID_CONFIG;
  refused("no place for the generation", [&] { r.generation_null = true; });
  need(cases > 150 && r.gen >= 9, "refusals and acceptances");
  // nothing was left behind: an ordinary lease works, with the next generation
  arm(r, kind); frames(r, 3, 16, F_FULL); r.end(r.gen);
}
// ---- the owner is lost while a lease runs: found by each path ----
static const char *const LOST[] = {"seated", "another avatar in the descriptor", "another body", "rig cell names another unit", "rig generation", "rig object replaced", "weapon object replaced",
                                   "weapon descriptor names another weapon", "weapon resource changed", "request kind", "request weapon", "request body", "request of another avatar",
                                   "a newer request above ours", "ring emptied", "camera replaced", "controller replaced", "controller byte 0", "context cell", "avatar count",
                                   "descriptor bytes beyond the two ids", "graph count", "graph hash", "graph link", "links array replaced", "hashes array replaced", "vtable replaced",
                                   "slot 0 foreign", "slot 1 foreign", "slot 2 foreign", "slot 3 foreign", "API table moved", "no sight node", "pose not finite", "pose unreadable"};
static void lose(Run &r, int what) {
  switch (what) {
  case 0: r.s.flags[14] = 4; break; case 1: r.s.descriptor[2] = 9; break; case 2: r.s.descriptor[3] = 9; break; case 3: r.s.rig_reference = OTHER_RIG; break;
  case 4: r.s.generations[3] = 7; break; case 5: r.s.rig_object = 1; break; case 6: r.s.weapon_object = 1; break; case 7: r.s.wdesc[0] = 0x555; break;
  case 8: r.s.resource = 77; break; case 9: r.req<uint32_t>(0, 4); break; case 10: r.req<uint32_t>(8, 0x555); break; case 11: r.req<uint32_t>(12, 0x999); break;
  case 12: r.req<uint32_t>(4, 0x334); break; case 13: r.s.ring[1] = (r.s.ring[1] + 1) % 32; break; case 14: r.s.ring[1] = r.s.ring[0]; break;
  case 15: r.poke(A_CONTEXT, 0, address_value(A_ALT, 0x800)); break; case 16: r.poke(A_G_CONTROLLER, 0, address_value(A_ALT, 0xc00)); break; case 17: r.s.controller[0] = 1; break;
  case 18: r.poke(A_G_CONTEXT, 0, address_value(A_ALT, 0)); break; case 19: r.s.avatar_count = 0; break; case 20: r.s.descriptor[4] = 5; break; case 21: r.s.graph_count = 64; break;
  case 22: r.s.hashes[30] ^= 1; break; case 23: r.s.links[50] ^= 0x10000; break; case 24: r.s.links_at = 1; break; case 25: r.s.hashes_at = 1; break; case 26: r.s.vtable_at = 1; break;
  case 27: case 28: case 29: case 30: r.poke(A_API, W[0].slot(what - 27) - W[0].a[A_API], slot_value(what - 27, SLOT_FOREIGN)); break;
  case 31: r.poke(A_API_ROOT, 0x18, address_value(A_ALT, 0x100)); break; case 32: r.req<uint32_t>(0x18, 0); break; case 33: r.req<float>(0x4c, INFINITY); break;
  default: break;
  }
}
// where: 0 the rig position call, 1 the branch rotation call (with a rotation delta), 2 set_placement, 3 set_zoom_policy,
// 4 the carrier call, 5 end (the restore proof)
static void script_owner_lost(Run &r, int what, int where) {
  r.offset[0] = .05f; arm(r, 1); optic(r, 1);
  const Q q = turn(0, 1, 0, 9); r.transform(r.gen, .01f, 0, 0, q.x, q.y, q.z, q.w);
  frames(r, 4, 16, F_FULL);
  need(::policy.rig_shifted && ::policy.rotation_shifted && ::policy.carrier_shifted, "everything shifted before the loss");
  const Scene good = r.s;
  r.tick(16);
  if (where == 4) { r.getter(OBSERVE_CALLER, r.viewmodel, 50); r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); }
  lose(r, what);
  if (what == 34) r.blocked.push_back({r.request_sym(0x48), 16});
  switch (where) {
  case 0: r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); break;
  case 1: r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); break;
  case 2: r.placement(r.gen, .1f, .1f, .1f); break;
  case 3: r.zoom_policy(r.gen, 1); break;
  case 4: r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); break;
  default: r.end(r.gen); break;
  }
  frames(r, 2, 16, F_FULL);
  r.placement(r.gen, 0, 0, 0);
  r.end(r.gen);
  r.s = good; r.s.write_preset = true; r.unpoke();
  for (int w = 0; w < 2; ++w) { World::put<uint64_t>(W[w].a[A_CONTEXT], W[w].a[A_CAMERA]); World::put<uint64_t>(W[w].a[A_G_CONTROLLER], W[w].a[A_CONTROLLER]);
                                World::put<uint64_t>(W[w].a[A_G_CONTEXT], W[w].a[A_CONTEXT]); World::put<uint64_t>(W[w].a[A_API_ROOT] + 0x18, W[w].a[A_API]);
                                for (int i = 0; i < 4; ++i) World::put<uint64_t>(W[w].slot(i), W[w].native[i]); }
  arm(r, 1); frames(r, 3, 16, F_FULL); r.end(r.gen);
}
// ---- the five dispatch slots ----
static void set_slot(int which, uint64_t cls) { for (int w = 0; w < 2; ++w) World::put<uint64_t>(W[w].slot(which), slot_raw(w, which, cls)); }
static void script_slot(Run &r, int which, int variant) {
  const int kind = which == 4 ? 3 : 1;
  const int order[5] = {0, 1, 2, 3, 4};                         // begin swaps the four pose slots in this order, then the input slot
  (void)order;
  r.normal();
  switch (variant) {
  case 0: // the swap itself faults at begin: everything already swapped is put back
    ::fixture_cas_fail_at = ::fixture_cas_calls + static_cast<uint32_t>(which) + 1; r.begin(kind); ::fixture_cas_fail_at = 0;
    need(r.result == 0 && NADSE_Error() == CAS_FAULT, "begin refused on a faulting swap"); break;
  case 1: // another value arrives between the proof and the swap: kept, the rest put back
    ::fixture_foreign_cas_at = ::fixture_cas_calls + static_cast<uint32_t>(which) + 1; ::fixture_foreign_value = SLOT_FOREIGN + 5; r.begin(kind); ::fixture_foreign_cas_at = 0;
    need(r.result == 0 && NADSE_Error() == CAS_MISMATCH, "begin refused on a foreign value"); set_slot(which, SLOT_NATIVE); break;
  case 2: // the slot changes while the lease runs: found by the next proof; the foreign value survives the end
    arm(r, kind); optic(r, 1); frames(r, 3, 16, F_FULL | (which == 4 ? F_INPUT : 0u));
    set_slot(which, SLOT_FOREIGN + 9); frames(r, 2, 16, F_FULL | (which == 4 ? F_INPUT : 0u));
    need(which == 4 || (!::armed.load() && NADSE_Error() == API_MISMATCH), "disarmed on a foreign slot value");
    r.end(r.gen); set_slot(which, SLOT_NATIVE); break;
  case 3: // the swap faults at end: cleanup stays pending until a later end
    arm(r, kind); frames(r, 2, 16);
    ::fixture_cas_fail_at = ::fixture_cas_calls + (which == 4 ? 1u : static_cast<uint32_t>(which) + (kind == 3 ? 2u : 1u)); r.end(r.gen); ::fixture_cas_fail_at = 0;
    need(r.result == 0 && NADSE_Error() == CLEANUP_PENDING && ::reserved.load(), "cleanup pending");
    frames(r, 2, 16); r.placement(r.gen, 0, 0, 0); r.normal(); r.begin(kind); r.end(r.gen);
    need(r.result == 1 && !::reserved.load(), "the second end finished"); break;
  case 4: // the slot's page is not writable at end
    arm(r, kind); frames(r, 2, 16); ::fixture_slot_readonly = true; r.end(r.gen); ::fixture_slot_readonly = false;
    need(r.result == 0 && NADSE_Error() == CLEANUP_PENDING, "cleanup pending on a read-only table"); r.end(r.gen); break;
  case 5: // the rollback of a failed begin faults too: begin answers 2 and the lease must be ended
    ::fixture_foreign_cas_at = ::fixture_cas_calls + 4; ::fixture_foreign_value = SLOT_FOREIGN + 5; ::fixture_fail_rollback = true; r.begin(kind); ::fixture_foreign_cas_at = 0;
    need(r.result == 2 && NADSE_Error() == CLEANUP_PENDING && ::reserved.load(), "begin answered 2");
    r.fpv(); frames(r, 2, 16); r.normal(); r.end(r.gen); ::fixture_fail_rollback = false; r.end(r.gen); set_slot(3, SLOT_NATIVE);
    need(!::reserved.load(), "ended after the pending rollback"); break;
  case 6: // the helper's own value is taken out of the slot by someone, and the game's put back, before the end
    arm(r, kind); frames(r, 2, 16); set_slot(which, SLOT_NATIVE); r.end(r.gen); break;
  default: break;
  }
  // from here both helpers must agree again
  arm(r, kind); frames(r, 3, 16, F_FULL | (kind == 3 ? F_INPUT : 0u)); r.end(r.gen);
}
// ---- time: gaps, a clock that stands still or steps back, a lease armed at time zero ----
static void script_time(Run &r, int kind, int variant) {
  if (variant == 1) ::fixture_now = 0;
  if (variant == 2) ::fixture_now = 0xfffffffffffffff0ull;
  r.blend = variant == 3 ? 1 : 150;
  arm(r, kind); optic(r, 1);
  { const Q q = turn(1, 0, 0, 4); r.transform(r.gen, .01f, 0, 0, q.x, q.y, q.z, q.w); }
  const int64_t steps[] = {16, 16, 0, 0, 7, 500, 1, -1, -16, -300, 1000, 16, 16, 100000, -99000, 16, 2, 2, 250, 251, 16, 149, 150, 151, -151, 16, 16};
  int i = 0;
  for (int64_t step : steps) {
    animate(r, i); frame(r, step, F_FULL);
    if (i == 6) { r.s.controller[4] = 1; } if (i == 12) { r.s.controller[4] = 0; }
    if (i == 9) zoom_event(r, 2); if (i > 9) lens_frame(r, std::max(0.0f, 1 - (i - 9) * .2f));
    if (i == 15) r.transition(r.gen, 3, .4f, 0);
    ++i;
  }
  r.end(r.gen);
  arm(r, kind); frames(r, 4, 16, F_FULL); r.tick(-5000); frames(r, 4, 16, F_FULL); r.end(r.gen);
}
// ---- the second avatar row; other rows of the ring ----
static void script_rows(Run &r, int variant) {
  if (variant == 0) { r.index = 1; }
  if (variant == 1) { r.s.row = 31; }      // the row after it wraps to 0
  if (variant == 2) { r.s.row = 0; }       // the row before it is 31
  r.offset[1] = .03f;
  arm(r, 1); optic(r, 1);
  frames(r, 6, 16, F_FULL);
  { const Q q = turn(0, 1, 0, 9); r.transform(r.gen, .01f, 0, 0, q.x, q.y, q.z, q.w); }
  frames(r, 6, 16, F_FULL);
  need(::policy.depth.observed.applied > 3 && ::weapon_updates.load() > 10, "the lease works on this row");
  r.end(r.gen);
}
// ---- checks only the translation makes (the fixture build of the original compiles them out) ----
static void script_translation_only(Run &r) {
  r.normal(); r.sync();
  for (int w = 0; w < 2; ++w) { r.write_config(w); World::put<uint64_t>(W[w].a[A_IO] + IO_GENERATION, UINT64_MAX); }
  TfpPoseConfig *c = reinterpret_cast<TfpPoseConfig *>(W[1].a[A_IO] + IO_CONFIG); uint64_t *g = reinterpret_cast<uint64_t *>(W[1].a[A_IO] + IO_GENERATION);
  auto slots_native = [&] { for (int i = 0; i < 5; ++i) { uint64_t v; std::memcpy(&v, reinterpret_cast<void *>(W[1].slot(i)), 8); if (v != W[1].native[i]) return false; } return true; };
  side = 1;
  // graph: retail (cpp:237-239, non-fixture) refuses a getter that is not executable code: GRAPH_MISMATCH.
  call_text = "translation only: begin and validate while the graph getter is not executable";
  answer_getter_executable = false;
  if (tfp_pose_validate(c) != 0 || tfp_pose_error() != TFP_POSE_GRAPH_MISMATCH) fail("validate: expected 0 with error 15, got error %u", tfp_pose_error());
  if (tfp_pose_begin(c, g) != 0 || tfp_pose_error() != TFP_POSE_GRAPH_MISMATCH || *g != 0 || !slots_native()) fail("begin: expected 0 with error 15, generation 0, slots untouched; got error %u", tfp_pose_error());
  answer_getter_executable = true;
  // begin: retail (cpp:1433-1436) pins the executable (PIN_FAILED when it cannot) and, in input mode, the game (input mode off when it cannot).
  call_text = "translation only: begin while the executable cannot be pinned";
  answer_pin_exe = false;
  if (tfp_pose_begin(c, g) != 0 || tfp_pose_error() != TFP_POSE_PIN_FAILED || *g != 0 || !slots_native()) fail("expected 0 with error 9, generation 0, slots untouched; got error %u", tfp_pose_error());
  answer_pin_exe = true;
  call_text = "translation only: begin_input while the game image cannot be pinned";
  answer_pin_game = false;
  TfpPoseInputStats stats{}; stats.size = sizeof stats; stats.abi = 1;
  if (tfp_pose_begin_input(c, g) != 1 || *g != 1) fail("expected an armed lease with generation 1, got error %u, generation %llu", tfp_pose_error(), (unsigned long long)*g);
  { uint64_t v; std::memcpy(&v, reinterpret_cast<void *>(W[1].slot(4)), 8); uint64_t v3; std::memcpy(&v3, reinterpret_cast<void *>(W[1].slot(3)), 8);
    if (v != W[1].native[4] || v3 != W[1].hooks[3]) fail("expected the 0x90 slot untouched and the plain getter hook in slot 3");
    if (tfp_pose_read_input_stats(1, &stats) != 1 || stats.installed || stats.active || stats.reason != TFP_POSE_INPUT_SDK_UNAVAILABLE) fail("expected input statistics: not installed, reason 3; got reason %u", stats.reason); }
  if (tfp_pose_end(1) != 1 || !slots_native()) fail("end failed");
  answer_pin_game = true;
  // The refusals consumed generation 1 in the translation only. The original is given the same lease.
  for (int w = 0; w < 2; ++w) events[w].clear();
  // (its 0x90 slot holds something else meanwhile, so that it too starts input mode without that slot)
  side = 0; { uint64_t retail_generation = 0; NADSE_Config *rc = reinterpret_cast<NADSE_Config *>(W[0].a[A_IO] + IO_CONFIG);
              World::put<uint64_t>(W[0].slot(4), SLOT_FOREIGN);
              if (NADSE_BeginInput(rc, &retail_generation) != 1 || NADSE_End(retail_generation) != 1) fail("the original could not be given the same generation");
              World::put<uint64_t>(W[0].slot(4), W[0].native[4]); }
  translation_cas_calls = ::fixture_cas_calls;
  for (int w = 0; w < 2; ++w) events[w].clear();
  counted.translation_only += 5;
  r.observe();
  arm(r, 1); if (r.gen != 2) fail("generation %llu after the translation-only begins", (unsigned long long)r.gen);
  frames(r, 5, 16, F_FULL);
  // In a hook: the getter stops being executable. Retail (non-fixture) refuses the graph proof: GRAPH_MISMATCH, the
  // game's own argument forwarded, disarmed. Only the translation can be asked; the comparison ends here.
  call_text = "translation only: a rig position call while the graph getter is not executable";
  answer_getter_executable = false; ::fixture_now += 16; events[1].clear(); side = 1;
  game_pose[1] = W[1].request(r.s.row) + 0x48;
  tfp_pose_position_from(W[1].at(RIG_CALLER), r.viewmodel, 5, reinterpret_cast<const float *>(game_pose[1]), W[1].stack());
  const Event *forwarded_call = nullptr; for (const Event &e : events[1]) if (e.kind == EV_FORWARD) forwarded_call = &e;
  TfpPoseStats pose_stats{}; pose_stats.size = sizeof pose_stats; pose_stats.abi = 2;
  if (tfp_pose_error() != TFP_POSE_GRAPH_MISMATCH || !forwarded_call || forwarded_call->v[1] != 0) fail("expected error 15 and the game's own argument forwarded; got error %u", tfp_pose_error());
  if (tfp_pose_read_stats(2, &pose_stats) != 1 || pose_stats.armed != 0) fail("expected the lease disarmed");
  answer_getter_executable = true;
  if (tfp_pose_end(2) != 1) fail("end failed");
  ++counted.translation_only;
}
// ---- tfp_pose_reset_for_tests: after it the translation must be where a fresh process is ----
static void script_reset(Run &r) {
  r.entry_animation(100, .2f, .1f); r.rotation_guard(0); r.entry_position(0); r.entry_position(1);
  r.duration = 5000; arm(r, 3); optic(r, 1);
  { const Q q = turn(0, 1, 0, 9); r.transform(r.gen, .01f, 0, 0, q.x, q.y, q.z, q.w); r.transition(r.gen, 3, .5f, 0); r.zoom_policy(r.gen, 1); r.zoom_policy(r.gen, 0); }
  frames(r, 12, 16, F_FULL | F_INPUT); settle(r, turn(0, 0, 1, 33)); r.s.flags[14] = 4; frame(r, 16, F_RIG);
  // left reserved, with remembered positions, a settled reference, counters, private records and an error code
  std::fflush(stdout);
  if (::input_state.pending.ready) { /* the request keeps a private selector; put the game's own back in both worlds */ }
  for (int w = 0; w < 2; ++w) { uint32_t original = r.req_get<uint32_t>(0x80); World::put<uint32_t>(W[w].request(r.s.row) + 0x80, original); }
  tfp_pose_reset_for_tests();
  // The original has no reset: its start-up values are put back by hand (every static of the retail file).
  for (auto &o : ::originals) o.store(nullptr);
  ::installed.store(0); ::armed.store(0); ::reserved.store(0); ::expired.store(0); ::last_error.store(0);
  for (int i = 0; i < 2; ++i) { ::seen[i].store(0); ::forwarded[i].store(0); }
  ::rig_return.store(0); ::rotation_return.store(0); ::getter_return.store(0); ::active_generation.store(0); ::carrier_return.store(0); ::watched_viewmodel.store(0);
  ::weapon_updates.store(0); ::zero_restores.store(0);
  for (unsigned i = 0; i < SLOT_COUNT; ++i) { ::slots[i] = 0; ::possible[i] = false; }
  ::slot_image = 0; ::input_original.store(nullptr); ::input_slot = 0; ::input_possible = false; ::input_mode.store(false); ::input_return.store(0);
  ::input_watched_request.store(0); ::input_lease_generation.store(0); ::generation_counter = 0; ::owned_generation = 0; ::retired_generation = 0; ::stats_deadline = 0;
  ::policy = ::Policy{}; ::display_cache = ::Display{}; ::display_sequence.store(0); ::display_blend.store(0); ::display_target.store(1);
  for (auto &clone : ::input_clones) clone = ::InputClone{};
  ::input_state = ::InputState{};
  ::entry_animation_ms.store(220); ::entry_animation_drop.store(.35f); ::entry_animation_back.store(.06f); ::rotation_guard_enabled.store(1);
  const float identity_q[4] = {0, 0, 0, 1}; std::memcpy(::guard_reference, identity_q, 16); std::memcpy(::guard_candidate, identity_q, 16);
  ::guard_reference_unit = ::guard_candidate_unit = ::guard_candidate_count = 0;
  for (auto &entry : ::entry_positions) entry = ::EntryPosition{};
  ::entry_position_next = ::entry_position_writes = 0; ::entry_position_enabled.store(1);
  ::fixture_cas_calls = 0; translation_cas_calls = 0;
  for (int which = 0; which < 5; ++which) set_slot(which, SLOT_NATIVE);
  for (int w = 0; w < 2; ++w) { for (uint64_t block : blocks[w]) munmap(reinterpret_cast<void *>(block), 0x10000); blocks[w].clear(); }
  r = Run(); r.observe();
  r.stats_call(0, 0); r.stats_call(1, 0); r.stats_call(2, 0);
  // A hook before any begin: the original would call a null pointer. The translation alone: it must hand the
  // host a null original (nothing left over from the lease before the reset).
  call_text = "translation only: the hooks right after the reset"; expect_null_original = true; events[1].clear(); side = 1; game_pose[1] = W[1].a[A_ARG];
  tfp_pose_position_from(W[1].at(RIG_CALLER), VIEWMODEL, 5, reinterpret_cast<const float *>(W[1].a[A_ARG]), W[1].stack());
  tfp_pose_rotation_from(W[1].at(PAIR_CALLER), VIEWMODEL, 5, reinterpret_cast<const float *>(W[1].a[A_ARG]));
  tfp_pose_branch_rotation_from(W[1].at(ROTATION_CALLER), VIEWMODEL, 5, reinterpret_cast<const float *>(W[1].a[A_ARG]));
  tfp_pose_position_getter_from(W[1].at(OBSERVE_CALLER), VIEWMODEL, 50, W[1].stack(), 0);
  tfp_pose_input90_from(W[1].at(INPUT_CALLER), VIEWMODEL, 0, W[1].request(1), W[1].stack());
  expect_null_original = false;
  int forwards = 0; for (const Event &e : events[1]) forwards += (e.kind == EV_FORWARD && e.v[1] == 0) || e.kind == EV_GETTER;
  if (forwards != 5 || events[1].size() != 5 || tfp_pose_error() != 0) fail("expected five plain forwards to a null original and no error");
  { TfpPoseStats stats{}; stats.size = sizeof stats; stats.abi = 2;
    if (tfp_pose_read_stats(0, &stats) != 1 || stats.seen_position != 1 || stats.seen_rotation != 2 || stats.forwarded_position != 1 || stats.forwarded_rotation != 2 || stats.armed || stats.installed)
      fail("unexpected counters after the unarmed calls");
    ::seen[0].store(1); ::seen[1].store(2); ::forwarded[0].store(1); ::forwarded[1].store(2); } // the original is given the same counts
  ++counted.translation_only;
  arm(r, 3); if (r.gen != 1) fail("after the reset begin gave generation %llu, not 1", (unsigned long long)r.gen);
  optic(r, 1); frames(r, 20, 16, F_FULL | F_INPUT);
  r.end(r.gen); r.end(r.gen);
  arm(r, 1); frames(r, 20, 16, F_FULL); r.end(r.gen);
}

// ======================================================================================================
// Look-gain normalisation (UnitAPI+0x90 and the input variant of the getter hook).
// In the production build of the translation the code proof refuses (INPUT_CODE_UNAVAILABLE); the original is
// then given a game image without retail's pinned code, so it refuses for its own reason, and everything
// else is compared as usual. In the look-gain build both run the normalisation.
// ======================================================================================================
static bool active() { return ::input_state.code_ok; }
// One evaluation as the game makes it: the 0x90 call before the filter, the filter, the 0x00 call after it.
static void evaluate(Run &r, bool game_filters = true, bool complete = true) {
  r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym());
  if (game_filters && ::input_state.pending.ready) { float history[3]; std::memcpy(history, ::input_state.pending.expected_history, 12); r.req(0x3c, history); }
  if (complete) r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym());
}
static void look(Run &r, int i) {
  const float raw[3] = {.2f * std::sin(i * .7f), -.1f * std::cos(i * .4f), .3f}; std::memcpy(r.s.raw, raw, 12);
}
static void input_scene(Run &r) {
  r.offset[0] = r.offset[1] = r.offset[2] = 0;
  const float history[3] = {.07f, -.02f, .13f}; r.req(0x3c, history);
  r.req<uint32_t>(0x80, r.s.preset_row); r.req<float>(0x84, .5235987901687622f); r.req<uint8_t>(0x8c, 0);
  const float scope = 1.25f, pad = .75f; std::memcpy(r.s.input_flags + 0x58, &scope, 4); std::memcpy(r.s.input_flags + 0x4c, &pad, 4);
  r.s.depth.degrees = 90;
}
static void arm_input(Run &r, int kind = 3) { r.normal(); r.begin(kind); if (r.result != 1) fail("the script could not arm (error %u)", NADSE_Error()); r.thunks = ::input_mode.load(); r.fpv(); input_scene(r); }
// The production build: the refusal, and the game's input untouched. Asserted on the translation itself.
static void assert_refusal(Run &r, const char *when) {
  if (input_build) return;
  TfpPoseInputStats stats{}; stats.size = sizeof stats; stats.abi = 1;
  r.sync();
  if (tfp_pose_read_input_stats(::reserved.load() ? r.gen : 0, &stats) != 1) fail("%s: the translation's input statistics could not be read", when);
  if (!::reserved.load()) stats.reason = TFP_POSE_INPUT_CODE_UNAVAILABLE; // (the reason of the lease that ended is not kept apart)
  if (stats.reason != TFP_POSE_INPUT_CODE_UNAVAILABLE || stats.active || stats.normalized || stats.pending || stats.cache_entries || stats.foreign_wins || stats.restored)
    fail("%s: the production translation must refuse with INPUT_CODE_UNAVAILABLE and touch nothing; reason %u, active %u, normalized %llu, pending %u, cache %u", when, stats.reason,
         stats.active, (unsigned long long)stats.normalized, stats.pending, stats.cache_entries);
  uint32_t selector = 0; std::memcpy(&selector, reinterpret_cast<void *>(W[1].request(r.s.row) + 0x80), 4);
  if (selector != r.req_get<uint32_t>(0x80) || !blocks[1].empty()) fail("%s: the production translation changed the game's input (selector %u, %zu private blocks)", when, selector, blocks[1].size());
  if (std::memcmp(reinterpret_cast<void *>(W[1].a[A_G_PRESETS] + r.s.preset_row * 144ull), r.s.preset, 144)) fail("%s: the production translation changed the game's preset table", when);
}
static void script_input_basic(Run &r, int kind, int variant) {
  if (variant == 1) { r.s.device = 1; r.s.input_flags[0] = 1; r.s.input_flags[5] = 1; }          // a pad: its own invert flags and scope gain
  if (variant == 2) { r.s.device = 2; r.s.input_flags[1] = 1; r.s.input_flags[4] = 1; r.s.input_flags[5] = 1; }
  if (variant == 3) { r.s.input_flags[2] = 1; r.s.input_flags[9] = 1; r.s.input_flags[3] = 1; }  // mouse: inverted axes
  if (variant == 4) { r.s.steering[0] = .01f; r.s.steering[1] = -.02f; }                         // steering: the gain depends on the input
  if (variant == 5) { r.s.look[0] = .002f; r.s.look[1] = -.001f; }
  if (variant == 6) { r.req<uint8_t>(0x8c, 1); }
  arm_input(r, kind);
  if (variant == 6) { r.req<uint8_t>(0x8c, 1); r.s.steering[1] = .05f; }
  const float rates[] = {1.0f / 60, 0.0f, 1.0f / 30, 1.0f / 144, 1.0f / 240, 1.0f / 60};
  for (int i = 0; i < 18; ++i) {
    r.s.dt_eval = rates[i % 6]; r.s.dt_global = rates[(i + (i > 8)) % 6]; look(r, i); r.tick(16);
    evaluate(r);
    r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE);
    assert_refusal(r, "after an evaluation");
  }
  if (input_build) need(::input_state.stats.normalized >= 10 && blocks[0].size() >= 1, "evaluations normalised through a private record");
  else need(::input_state.stats.reason == INPUT_CODE_UNAVAILABLE && ::input_state.stats.observed >= 18, "both refuse for lack of the code proof");
  r.end(r.gen);
  need(!::reserved.load(), "ended");
  // a second lease finds the private record of the same preset again
  arm_input(r, kind); for (int i = 0; i < 4; ++i) { look(r, i); r.tick(16); evaluate(r); } r.end(r.gen);
  assert_refusal(r, "after the lease");
}
// What can go wrong around one evaluation. `what` picks it; the retail input fixture's cases are among them.
static const char *const INPUT_CASES[] = {
    "end while a selector is published", "completion from another frame, then another request", "a foreign selector wins the cleanup", "history differs with steering: closed for the lease",
    "history differs without steering", "context inhibits input", "lens not finite", "nested evaluation on another frame", "weapon changed before completion", "second 0x90 call after disarm",
    "selector page stops being writable before the cleanup swap", "selector page unreadable at end", "another thread evaluates in between", "idle axes", "camera object replaced before completion",
    "context cell replaced before completion", "ring emptied before end", "the same frame evaluated twice without completion", "in the sights", "native aim flag", "no stack frame",
    "preset selector above 255", "exp answers nothing usable", "exp leaves the control word changed", "allocation refused twice", "lens equals the third-person setting: nothing to change",
    "player entity is the host's", "player entity found in the world map", "player entity not in the world map", "entity's avatar is another one", "no avatar at all", "avatar not in the owner's map",
    "avatar at another row of the owner's map", "player cell unreadable", "evaluator's context is another one", "evaluator's avatar index is another", "delta time out of range",
    "ratio out of range", "gain out of range", "request state changes between the two samples", "published selector replaced before the swap", "another caller, another request",
    "page not writable when publishing", "entity row beyond the limit", "raw input unbounded", "entity is the map's empty value"};
static void script_input_case(Run &r, int what) {
  arm_input(r, 3);
  const Scene good = r.s;
  auto restore = [&] { const size_t n = r.pokes.size(); (void)n; r.s = good; r.s.write_preset = false; r.unpoke(); shim_thread_id = 1; r.stack_on = true; r.inner_frame = false; exp_mode = 0;
                       for (int w = 0; w < 2; ++w) { World::put<uint64_t>(W[w].a[A_CONTEXT], W[w].a[A_CAMERA]); World::put<uint64_t>(W[w].a[A_G_CONTEXT], W[w].a[A_CONTEXT]); World::put<uint64_t>(W[w].a[A_G_PLAYER], W[w].a[A_PLAYER]); } };
  r.tick(16); evaluate(r); r.tick(16); look(r, 1); evaluate(r);     // two ordinary evaluations first
  r.tick(16); look(r, 2);
  switch (what) {
  case 0: evaluate(r, true, false); r.end(r.gen); need(!input_build || (r.result == 0 && NADSE_Error() == CLEANUP_PENDING && ::reserved.load()), "end deferred"); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 1: evaluate(r, true, false); r.inner_frame = true; r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); r.inner_frame = false;
          r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, {A_CONTEXT, 0x200}); r.getter(OBSERVE_CALLER, r.viewmodel, 50, false); r.getter(GAME_OTHER, r.viewmodel, 50, true, r.request_sym());
          need(!input_build || ::input_state.pending.ready, "still pending"); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 2: evaluate(r, true, false); r.req<uint32_t>(0x80, 42); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); r.req<uint32_t>(0x80, r.s.preset_row); break;
  case 3: r.s.steering[0] = .01f; evaluate(r, false, false); r.req<float>(0x3c, 99); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym());
          need(!input_build || ::input_state.failed_closed, "closed for the lease"); r.tick(300); evaluate(r); break;
  case 4: evaluate(r, false, false); r.req<float>(0x3c, 99); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); need(!input_build || !::input_state.failed_closed, "not closed"); break;
  case 5: r.s.inhibit = 1; evaluate(r); break;
  case 6: r.req<uint32_t>(0x84, 0x7fc00001u); evaluate(r); break;
  case 7: evaluate(r, true, false); r.inner_frame = true; r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym()); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); r.inner_frame = false;
          need(!input_build || ::input_state.pending.ready, "the outer evaluation is still pending"); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 8: evaluate(r, true, false); r.s.wdesc[0] = 0x999; r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 9: evaluate(r, true, false); r.end(r.gen); r.inner_frame = true; r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym()); r.inner_frame = false; r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 10: for (uint32_t mode : {1u, 2u}) { evaluate(r, true, false); r.fault_after_query(mode); r.end(r.gen); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); r.end(r.gen); arm_input(r, 3); r.tick(16); } break;
  case 11: evaluate(r, true, false); r.blocked.push_back({{A_CONTEXT, 0}, 0x3000}); r.end(r.gen);
           r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 12: evaluate(r, true, false); shim_thread_id = 2; r.inner_frame = true; r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym()); r.inner_frame = false;
           r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); shim_thread_id = 1; r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 13: { const float zero[3] = {}; std::memcpy(r.s.raw, zero, 12); r.req(0x3c, zero); evaluate(r); look(r, 5); evaluate(r); } break;
  case 14: evaluate(r, true, false); r.poke(A_CONTEXT, 0, address_value(A_ALT, 0x800)); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 15: evaluate(r, true, false); r.poke(A_G_CONTEXT, 0, address_value(A_ALT, 0)); r.end(r.gen); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 16: evaluate(r, false, false); r.s.ring[0] = r.s.ring[1] = 2; r.end(r.gen); need(!input_build || (::input_state.pending.ready && ::input_clones[::input_state.pending.clone].quarantined), "quarantined");
           { float history[3]; std::memcpy(history, ::input_state.pending.expected_history, 12); r.req(0x3c, history); } r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 17: evaluate(r, true, false); r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym()); need(!input_build || ::input_state.pending.poisoned, "poisoned");
           r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); r.end(r.gen); need(!input_build || (r.result == 0 && ::reserved.load()), "never acknowledged as drained");
           r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym()); break;
  case 18: r.s.controller[4] = 1; evaluate(r); r.s.controller[4] = 0; break;
  case 19: r.s.flags[6] = 1; evaluate(r); r.s.flags[6] = 0; break;
  case 20: r.stack_on = false; evaluate(r); r.stack_on = true; break;
  case 21: r.req<uint32_t>(0x80, 256); evaluate(r); r.req<uint32_t>(0x80, 255); r.s.preset_row = 255; evaluate(r); break;
  case 22: exp_mode = 1; evaluate(r); exp_mode = 2; evaluate(r); exp_mode = 0; r.s.dt_eval = 0; evaluate(r); break;
  case 23: exp_mode = 3; evaluate(r); exp_mode = 0; break;
  case 24: r.s.preset[20] ^= 1; r.allocation_failures = 2; r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym()); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 25: r.req<float>(0x84, 90 / 57.2957763671875f); evaluate(r); break;
  case 26: r.s.player_count = 1; r.s.entity = 0x7fff; evaluate(r); r.s.host_avatar = 5; evaluate(r); break;
  case 27: r.s.player_count = 2; r.s.entity = 0x21; r.s.entity_row = 0x100; evaluate(r); break;
  case 28: r.s.player_count = 1; r.s.entity = 0x21; r.s.entity_in_map = false; evaluate(r); break;
  case 29: r.s.player_count = 1; r.s.entity = 0x21; r.s.entity_avatar = 0x777; evaluate(r); break;
  case 30: r.s.idle_avatar = 0xffffffffu; evaluate(r); r.s.no_avatar = 0x999; evaluate(r); break;
  case 31: r.s.avatar_in_map = false; evaluate(r); break;
  case 32: r.s.avatar_map_index = 1; evaluate(r); break;
  case 33: r.poke(A_G_PLAYER, 0, bytes_value(0x1000, 8)); evaluate(r); break;
  case 34: r.s.stack_context = false; evaluate(r); break;
  case 35: r.s.stack_index = 1; evaluate(r); r.s.stack_index = 0x100000000ull; evaluate(r); break;
  case 36: r.s.dt_eval = 1.5f; evaluate(r); r.s.dt_eval = 1.0f / 60; r.s.dt_global = -.5f; evaluate(r); break;
  case 37: r.req<float>(0x84, 3.0f); r.s.depth.degrees = 10; evaluate(r); r.s.depth.degrees = 1; evaluate(r); break;
  case 38: { const float tiny[2] = {.000002f, 900000.0f}; std::memcpy(r.s.preset + 4, tiny, 8); r.req<float>(0x84, 2.9f); evaluate(r); } break;
  case 39: r.probe_values.clear(); evaluate(r, true, false); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 40: evaluate(r, true, false); r.getter(OBSERVE_CALLER, r.viewmodel, 49, true, r.request_sym()); break;
  case 41: r.input90(GAME_OTHER, r.weapon_unit, 0, r.request_sym()); r.input90(INPUT_CALLER, r.weapon_unit, 0, {A_CONTEXT, 0x200}); r.input90(INPUT_CALLER, 0, 77, NOWHERE); break;
  case 42: r.fault_after_query(1); evaluate(r); r.fault_after_query(2); r.s.preset[21] ^= 1; evaluate(r); break;
  case 43: r.s.player_count = 1; r.s.entity = 0x21; r.s.entity_row = 0x100001; evaluate(r); break;
  case 44: r.s.raw[1] = 2e6f; evaluate(r); r.s.raw[1] = NAN; evaluate(r); break;
  case 45: r.s.player_count = 1; r.s.entity = 0xffffffffu; evaluate(r); break;
  default: break;
  }
  assert_refusal(r, "after the case");
  restore();
  r.tick(16); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym());
  for (int i = 0; i < 3; ++i) { r.tick(16); look(r, 10 + i); evaluate(r); }
  r.end(r.gen); r.end(r.gen);
  if (::reserved.load()) return;             // a poisoned evaluation is never acknowledged: the lease stays until the process ends
  arm_input(r, 2); for (int i = 0; i < 3; ++i) { r.tick(16); look(r, i); evaluate(r); } r.end(r.gen);
}
// More presets than there are private records; records reused, busy and quarantined.
static void script_input_pool(Run &r) {
  arm_input(r, 3);
  for (int i = 0; i < 36; ++i) {
    r.s.preset_row = static_cast<uint32_t>(3 + i % 34); r.s.preset[30] = static_cast<uint8_t>(i); r.req<uint32_t>(0x80, r.s.preset_row);
    r.tick(16); look(r, i); evaluate(r);
  }
  if (input_build) need(blocks[0].size() == 32 && ::input_state.stats.reason == INPUT_POOL, "all 32 private records in use, the next refused");
  r.s.preset_row = 3; r.s.preset[30] = 0; r.req<uint32_t>(0x80, 3); r.tick(16); evaluate(r);  // a known preset again: its record is found
  r.end(r.gen);
  arm_input(r, 2); r.tick(16); evaluate(r); r.end(r.gen);
  assert_refusal(r, "after the pool script");
}
// The code proof made late: the evaluator's page is cold (unreadable) or holds other bytes at first.
static void script_input_cold(Run &r, int variant) {
  r.normal();
  const int cold = variant == 0 ? A_G_PIN0 : A_G_PIN2;   // the first page of the evaluator, or of the third pin
  if (variant <= 1) for (int w = 0; w < 2; ++w) protect(W[w].a[cold], 16, PROT_NONE);
  uint8_t kept[2] = {0, 0};
  if (variant == 2) for (int w = 0; w < 2; ++w) { kept[w] = *reinterpret_cast<uint8_t *>(W[w].a[A_G_PIN4] + 700); *reinterpret_cast<uint8_t *>(W[w].a[A_G_PIN4] + 700) ^= 1; }
  if (variant == 3) for (int w = 0; w < 2; ++w) World::put<uint32_t>(W[w].a[A_G_CONST0 + 3], 0x3fc90fdb);
  if (variant == 4) for (int w = 0; w < 2; ++w) World::put<uint64_t>(W[w].a[A_G_CONST0 + 6], 0x3ff0000000000001ull);
  if (variant == 5) for (int w = 0; w < 2; ++w) *reinterpret_cast<uint8_t *>(W[w].a[A_E_PIN12] + 75) ^= 0x80;
  r.begin(3); need(r.result == 1, "armed although the code is not proven yet"); r.thunks = ::input_mode.load(); r.fpv(); input_scene(r);
  r.tick(16); evaluate(r); need(!::input_state.code_ok && ::armed.load(), "not active, pose still armed");
  r.tick(100); evaluate(r);                                   // within 250 ms: no new attempt
  if (variant <= 1) for (int w = 0; w < 2; ++w) protect(W[w].a[cold], 16, PROT_READ | PROT_WRITE);
  if (variant == 2) for (int w = 0; w < 2; ++w) *reinterpret_cast<uint8_t *>(W[w].a[A_G_PIN4] + 700) = kept[w];
  if (variant == 3) for (int w = 0; w < 2; ++w) World::put<uint32_t>(W[w].a[A_G_CONST0 + 3], 0xbfc90fdb);
  if (variant == 4) for (int w = 0; w < 2; ++w) World::put<uint64_t>(W[w].a[A_G_CONST0 + 6], 0x3ff0000000000000ull);
  if (variant == 5) for (int w = 0; w < 2; ++w) *reinterpret_cast<uint8_t *>(W[w].a[A_E_PIN12] + 75) ^= 0x80;
  r.tick(100); evaluate(r); r.tick(49); evaluate(r);
  r.tick(1); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym());   // 250 ms after the first attempt: the getter hook retries
  if (input_build) need(::input_state.code_ok, "active after the code became readable");
  for (int i = 0; i < 4; ++i) { r.tick(16); look(r, i); evaluate(r); }
  if (input_build) need(::input_state.stats.normalized >= 3, "normalising");
  r.end(r.gen);
}

// ---- one call with every single read failing in turn, and values changing just before each read ----
static void probe_script(Run &r, int which) {
  auto values = [&] {
    r.probe_values = {
        {{A_G_CONTEXT, 0}, address_value(A_ALT, 0)}, {{A_CONTEXT, 0}, address_value(A_ALT, 0x800)}, {{A_G_OWNER, 0}, bytes_value(0, 8)}, {{A_G_CONTROLLER, 0}, address_value(A_ALT, 0xc00)},
        {{A_CONTROLLER, 4}, bytes_value(1, 1)}, {{A_CONTROLLER, 1}, bytes_value(1, 1)}, {{A_OWNER, 0x6C}, bytes_value(0, 4)}, {{A_DESCRIPTOR, 8}, bytes_value(9, 4)},
        {{A_O_FLAGS0, 14}, bytes_value(4, 1)}, {{A_O_FLAGS0, 6}, bytes_value(1, 1)}, {{A_O_RIG0, 0}, bytes_value(OTHER_RIG, 4)}, {{A_GENERATIONS, 3}, bytes_value(9, 1)},
        {{A_UNITS, 3 * 8}, address_value(A_RIG2, 0)}, {{A_WDESC, 8}, bytes_value(0x555, 4)}, {{A_CONTEXT, 0x1F8}, bytes_value((r.s.row + 31) % 32, 4)},
        {{A_CONTEXT, 0x1FC}, bytes_value((r.s.row + 2) % 32, 4)}, {r.request_sym(0), bytes_value(4, 4)}, {r.request_sym(8), bytes_value(0x555, 4)}, {r.request_sym(0x18), bytes_value(0, 4)},
        {r.request_sym(0x48), float_value(NAN)}, {r.request_sym(0x4c), float_value(7.5f)}, {r.request_sym(0x90), float_value(.25f)}, {r.request_sym(0x98), float_value(1)},
        {r.request_sym(0x8D), bytes_value(1, 1)}, {r.request_sym(0x84), float_value(.3f)}, {{A_RIG, 0x70}, bytes_value(62, 4)}, {{A_RIG_HASHES, 5 * 4}, bytes_value(1, 4)},
        {{A_RIG, 0x98}, address_value(A_ALT, 0x1000)}, {{A_RIG, 0x80}, address_value(A_ALT, 0x3000)}, {{A_API_ROOT, 0x18}, address_value(A_ALT, 0x100)},
        {{A_API, 0x38}, slot_value(0, SLOT_FOREIGN)}, {{A_API, 0x40}, slot_value(2, SLOT_NATIVE)}, {{A_API, 0}, slot_value(3, SLOT_FOREIGN)}, {{A_API, 0x90}, slot_value(4, SLOT_FOREIGN)},
        {{A_STACK, STACK_S + 0x230}, float_value(NAN)}, {{A_STACK, STACK_S + 0x44 + 0x1C}, float_value(.31f)}, {{A_RIG_LOCALS, 0x24}, float_value(3)},
        {{A_PACKED, 2 * 12 + 4}, bytes_value(64 | (2 << 4), 4)}, {{A_PARAMS, 2 * 0x3F0 + 0x350}, bytes_value(0, 4)}, {{A_S_DEGREES, 0}, float_value(80)}, {{A_G_POOL, 0}, address_value(A_ALT, 0)},
        {{A_PRIMARY, (r.weapon & 7) * 8}, bytes_value(0xffffffffull, 8)}, {{A_T_RECORDS, 0x74 + 4}, float_value(2.6f)}, {{A_G_DEPTH5, 3}, bytes_value(0x11, 1)},
        {{A_G_CARRIER2, 2}, bytes_value(0x11, 1)}};
    if (which >= 20) {
      const std::vector<Poke> more = {
          {{A_OWNER, 0xa7c24}, float_value(.21f)}, {r.request_sym(0x3c), float_value(.5f)}, {r.request_sym(0x80), bytes_value(42, 4)}, {r.request_sym(0x8c), bytes_value(1, 1)},
          {{A_CONTEXT, 0x1a9}, bytes_value(1, 1)}, {{A_CONTEXT, 0x15c}, float_value(.001f)}, {{A_G_INPUT_OWNER, 0}, bytes_value(0x8000, 8)}, {{A_I_DEVICE, 0}, bytes_value(1, 4)},
          {{A_INPUT_OWNER, 0xa7af8 + 2}, bytes_value(1, 1)}, {{A_INPUT_OWNER, 0xa7af8 + 0x58}, float_value(2)}, {{A_TIME, 0}, float_value(.02f)}, {{A_G_CLOCK, 0}, address_value(A_ALT, 0x40)},
          {{A_STACK, STACK_S + 0x80}, address_value(A_ALT, 0)}, {{A_STACK, STACK_S + 0x48}, bytes_value(1, 8)}, {{A_STACK, STACK_S + 0xa0}, float_value(.02f)},
          {{A_G_PRESETS, 3 * 144 + 4}, float_value(.35f)}, {{A_G_PRESETS, 3 * 144 + 100}, bytes_value(0, 1)}, {{A_PLAYER, 0x88}, bytes_value(1, 4)}, {{A_G_IDLE_AVATAR, 0}, bytes_value(5, 4)},
          {{A_G_NO_AVATAR, 0}, bytes_value(AVATAR, 4)}, {{A_O_STEER0, 0}, float_value(.01f)}, {{A_AVMAP, (AVATAR & 7) * 8}, bytes_value(0xffffffffull, 8)}, {{A_G_PIN0, 600}, bytes_value(0, 1)},
          {{A_G_CONST0, 0}, bytes_value(0, 4)}};
      r.probe_values.insert(r.probe_values.end(), more.begin(), more.end());
    }
  };
  r.offset[0] = .04f;
  const Q q = turn(0, 1, 0, 9);
  auto lease = [&](int kind) { arm(r, kind); optic(r, 1); r.transform(r.gen, .01f, 0, 0, q.x, q.y, q.z, q.w); frames(r, 3, 16, F_FULL); r.tick(16); animate(r, 9); };
  switch (which) {
  case 0: lease(1); r.getter(OBSERVE_CALLER, r.viewmodel, 50); values(); r.probe(); r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); break;        // the rig position call, depth valid
  case 1: lease(1); values(); r.probe(); r.getter(OBSERVE_CALLER, r.viewmodel, 50); r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); break;        // the lens observation
  case 2: lease(1); r.getter(OBSERVE_CALLER, r.viewmodel, 50); r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); values(); r.probe(); r.position(CARRIER_CALLER, r.viewmodel, 0, ROOT_POSE); break;
  case 3: lease(1); values(); r.probe(); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); break;                                                       // the rotation delta
  case 4: lease(1); r.getter(OBSERVE_CALLER, r.viewmodel, 50); zoom_event(r, 2); lens_frame(r, 1); values(); r.probe(); r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); break; // with a definition refresh
  case 5: lease(1); r.tick(600); r.getter(OBSERVE_CALLER, r.viewmodel, 50); values(); r.probe(); r.position(RIG_CALLER, r.viewmodel, 5, r.request_sym(0x48)); break;                       // with the 500 ms revalidation
  case 6: lease(1); values(); r.probe(); r.end(r.gen); break;                                                                                                  // end: the three restores
  case 7: lease(1); values(); r.probe(); r.placement(r.gen, .1f, 0, 0); break;
  case 8: lease(1); values(); r.probe(); r.zoom_policy(r.gen, 1); break;
  case 9: r.normal(); r.sync(); values(); r.probe(); r.begin(1); break;
  case 10: r.normal(); r.sync(); values(); r.probe(); r.validate(); break;
  case 11: r.duration = 40; lease(1); r.tick(30); values(); r.probe(); r.stats_call(0, r.gen); break;                                                          // statistics at the deadline: the restores
  case 12: lease(1); r.end(r.gen); arm(r, 1); set_rotation(r, turn(0, 0, 1, 70)); r.tick(16); values(); r.probe(); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); break; // inside the entry window
  case 13: r.duration = 60; lease(1); r.tick(40); values(); r.probe(); r.branch(ROTATION_CALLER, r.viewmodel, 5, ROTATION_POSE); break;                         // the branch call finds the deadline
  case 14: lease(1); values(); r.probe(); r.stats_call(2, r.gen); break;
  // look gain
  case 20: arm_input(r, 3); r.tick(16); evaluate(r); r.tick(16); look(r, 1); values(); r.probe(); r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym()); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;
  case 21: arm_input(r, 3); r.tick(16); evaluate(r, true, false); values(); r.probe(); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break;  // the completion
  case 22: arm_input(r, 3); r.tick(16); evaluate(r, true, false); values(); r.probe(); r.end(r.gen); r.getter(OBSERVE_CALLER, r.viewmodel, 50, true, r.request_sym()); break; // end with a published selector
  case 23: r.normal(); r.sync(); values(); r.probe(); r.begin(3); break;
  case 24: arm_input(r, 3); r.tick(16); evaluate(r, true, false); values(); r.probe(); r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym()); break;       // re-entry: the abort
  case 25: arm_input(r, 3); r.s.steering[0] = .01f; r.s.player_count = 1; r.s.entity = 0x21; r.tick(16); values(); r.probe(); r.input90(INPUT_CALLER, r.weapon_unit, 0, r.request_sym()); break; // the first evaluation: code proof, steering through the world map
  default: break;
  }
  // Whatever the probe did, the helpers must go on agreeing: the game's frames continue, the lease is ended, a new one works.
  r.unpoke();
  frames(r, 2, 16, F_FULL); r.end(r.gen); r.end(r.gen);
}

// ---- seeded random walks mixing all of it ----
static void script_walk(Run &r, uint32_t seed, int steps, bool with_input) {
  std::mt19937 rng(seed);
  auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<uint32_t>(n)); };
  auto unit = [&] { return static_cast<float>(rng() % 20001) / 10000.0f - 1.0f; };
  int kind = 1, level = 1; bool armed_once = false; float progress = 0;
  const Scene start = r.s;
  auto rearm = [&] {
    if (::reserved.load()) { r.end(r.gen); if (::reserved.load()) return; }
    const float keep[3] = {r.offset[0], r.offset[1], r.offset[2]};
    r.s = start; r.unpoke(); r.s.write_preset = true; r.s.write_root_local = true;
    for (int which = 0; which < 5; ++which) set_slot(which, SLOT_NATIVE);
    r.weapon = WEAPON + static_cast<uint32_t>(pick(20)); r.duration = pick(4) == 0 ? static_cast<uint32_t>(50 + pick(900)) : 0; r.blend = static_cast<uint32_t>(1 + pick(300));
    for (int i = 0; i < 3; ++i) r.offset[i] = pick(3) ? keep[i] : unit() * .3f;
    kind = with_input ? pick(4) : pick(2);
    r.normal(); r.begin(kind); if (r.result != 1) fail("the walk could not arm (error %u)", NADSE_Error());
    r.thunks = kind >= 2 && ::input_mode.load(); r.fpv(); input_scene(r); level = pick(3); optic(r, static_cast<unsigned>(level)); progress = 0; armed_once = true;
  };
  rearm();
  for (int step = 0; step < steps; ++step) {
    const int p = pick(1000);
    const int64_t dt = p < 20 ? 0 : p < 30 ? -pick(50) : p < 40 ? 300 + pick(400) : p < 45 ? 3000 : 4 + pick(30);
    animate(r, step, 1 + unit() * .5f);
    if (progress > 0) { progress = std::max(0.0f, progress - .15f); lens_frame(r, progress); }
    const int action = pick(100);
    if (action < 55) frame(r, dt, F_FULL | (r.thunks && pick(4) ? F_INPUT : 0u));
    else if (action < 60) {
      // Any subset of the frame's calls. A 0x90 call whose evaluation never reaches its 0x00 call leaves a selector that
      // the next evaluation on the same frame poisons for good (the lease then stays until the process ends): only late in the walk.
      unsigned parts = static_cast<unsigned>(pick(64)) & (r.thunks ? 63u : 31u);
      if ((parts & F_INPUT) && step < steps - steps / 8) parts |= F_GETTER;
      frame(r, dt, parts);
    }
    else if (action < 63) { const int next = pick(3); if (next != level) { level = next; zoom_event(r, static_cast<unsigned>(level)); progress = 1; lens_frame(r, 1); } }
    else if (action < 66) r.placement(r.gen, unit() * .52f, unit() * .52f, unit() * .52f);
    else if (action < 69) r.motion(r.gen, unit() * .16f, unit() * .16f, unit() * .16f);
    else if (action < 73) { const Q q = turn(unit(), unit(), unit() + .01f, unit() * 40); r.transform(r.gen, unit() * .1f, unit() * .1f, unit() * .1f, q.x, q.y, q.z, q.w, pick(4) == 0); }
    else if (action < 75) r.transform(r.gen, 0, 0, 0, 0, 0, 0, 1);
    else if (action < 77) r.ads_placement(r.gen, unit() * .1f, unit() * .1f, unit() * .1f);
    else if (action < 80) r.s.controller[4] = static_cast<uint8_t>(pick(2));
    else if (action < 83) r.transition(r.gen, static_cast<uint64_t>(1 + step / 40), std::min(1.0f, (step % 40) / 30.0f), r.s.controller[4] ? 1u : static_cast<uint32_t>(pick(8) == 0));
    else if (action < 85) r.zoom_policy(r.gen, static_cast<uint32_t>(pick(2)));
    else if (action < 87) { r.s.ring[0] = static_cast<uint32_t>(pick(2) ? r.s.row : (r.s.row + 31) % 32); r.req<float>(0x98, pick(5) ? 0.0f : .5f); }
    else if (action < 89) { r.s.flags[6] = static_cast<uint8_t>(pick(4) == 0); r.s.flags[10] = static_cast<uint8_t>(pick(2) * 4); r.s.flags[15] = static_cast<uint8_t>(pick(2) * 32); }
    else if (action < 91) { const Sym callers[] = {RIG_CALLER, ROTATION_CALLER, OBSERVE_CALLER, CARRIER_CALLER, PAIR_CALLER, GAME_OTHER, EXE_OTHER}; const uint32_t units[] = {VIEWMODEL, OTHER_RIG, WEAPON_UNIT}, nodes[] = {0, 5, 50, 9};
                            const Sym caller = callers[pick(7)]; const uint32_t unit_ = units[pick(3)], node = nodes[pick(4)];
                            switch (pick(4)) { case 0: r.position(caller, unit_, node, pick(2) ? r.request_sym(0x48) : pick(2) ? ROOT_POSE : POSITION_POSE); break; case 1: r.rotation(caller, unit_, node, ROTATION_POSE); break;
                                               case 2: r.branch(caller, unit_, node, ROTATION_POSE); break; default: r.getter(caller, unit_, node, r.thunks, r.thunks ? r.request_sym() : NOWHERE); break; } }
    else if (action < 92) { r.poll = pick(3) != 0; }
    else if (action < 93) { r.stats_call(pick(3), pick(4) ? r.gen : r.gen + 1, pick(6) ? 0 : pick(6)); }
    else if (action < 94) { r.rotation_guard(static_cast<uint32_t>(pick(2))); r.entry_position(static_cast<uint32_t>(pick(3) != 0)); if (pick(3) == 0) r.entry_animation(static_cast<uint32_t>(pick(400)), .35f * pick(3), .06f * pick(2)); }
    else if (action < 95) { // one frame of damage: something the owner proof reads, then put back
      const Scene kept = r.s; lose(r, pick(27)); frame(r, dt, F_FULL); r.s = kept; r.s.write_preset = false; r.unpoke();
      for (int w = 0; w < 2; ++w) { World::put<uint64_t>(W[w].a[A_CONTEXT], W[w].a[A_CAMERA]); World::put<uint64_t>(W[w].a[A_G_CONTROLLER], W[w].a[A_CONTROLLER]); World::put<uint64_t>(W[w].a[A_G_CONTEXT], W[w].a[A_CONTEXT]); } }
    else if (action < 96) { r.s.depth.zoom[pick(3)] = pick(3) ? 1.0f + pick(5) : .05f; r.s.depth.coefficient[pick(3)] = .3f + .1f * pick(6); r.s.depth.in_overrides = pick(4) == 0; r.s.depth.degrees = 60.0f + 10 * pick(6); }
    else if (action < 97) { shim_thread_id = pick(5) ? 1u : 2u; r.stack_on = pick(6) != 0; r.inner_frame = pick(8) == 0; }
    else if (action < 98) { r.s.steering[0] = pick(3) ? 0.0f : unit() * .05f; r.s.dt_eval = pick(5) ? 1.0f / (30 + pick(200)) : 0.0f; r.s.dt_global = 1.0f / (30 + pick(200)); look(r, step); r.s.device = static_cast<uint32_t>(pick(3));
                            r.req<float>(0x84, r.optic_target); if (pick(6) == 0) { r.s.preset[40] = static_cast<uint8_t>(pick(3)); } }
    else if (action < 99) { r.end(pick(5) ? r.gen : r.gen + 1); }
    else rearm();
    if (!::armed.load() && pick(12) == 0) rearm();
    shim_thread_id = pick(40) ? shim_thread_id : 1u;
  }
  shim_thread_id = 1; r.stack_on = true; r.inner_frame = false;
  need(armed_once && counted.hooks > static_cast<uint64_t>(steps), "the walk made its calls");
  r.end(r.gen); r.end(r.gen);
}

static void register_scripts() {
  auto named = [](const char *format, ...) { char name[200]; va_list arguments; va_start(arguments, format); std::vsnprintf(name, sizeof name, format, arguments); va_end(arguments); return std::string(name); };
  static const char *const KINDS[] = {"begin", "begin_steady", "begin_input", "begin_steady_input"};
  for (int kind = 0; kind < 4; ++kind) for (int64_t dt : {7, 16, 33}) add(named("placement set and changed live, %s, step %lld ms", KINDS[kind], (long long)dt), [=](Run &r) { script_placement(r, kind, dt); });
  for (int kind = 0; kind < 2; ++kind) add(named("motion: offsets and rotation within and outside their bounds, %s", KINDS[kind]), [=](Run &r) { script_motion(r, kind); });
  add("rotation delta: coupling, fading request, full ADS, authored, other writers, invalid rotations", script_rotation_gates);
  for (int kind = 0; kind < 2; ++kind) for (int64_t dt : {7, 16}) for (uint32_t blend : {150u, 1u, 1000u})
    add(named("ADS placement and the hip weight fading into the sights and back, %s, step %lld ms, blend %u ms", KINDS[kind], (long long)dt, blend), [=](Run &r) { script_ads(r, kind, dt, blend); });
  for (int kind = 0; kind < 2; ++kind) for (int64_t dt : {7, 16, 40}) add(named("presentation phase packets, %s, step %lld ms", KINDS[kind], (long long)dt), [=](Run &r) { script_transition(r, kind, dt); });
  static const char *const ENTRIES[] = {"default animation", "animation off", "1000 ms, 1 m", "settings refused, then 120 ms without offsets", "no placement", "into the sights during it"};
  for (int kind = 0; kind < 2; ++kind) for (int64_t dt : {5, 16}) for (int variant = 0; variant < 6; ++variant)
    add(named("entry (%s: %s), %s, step %lld ms", kind ? "steady, draw animation" : "blended over 150 ms", ENTRIES[variant], KINDS[kind], (long long)dt), [=](Run &r) { script_entry(r, kind, dt, variant); });
  static const char *const GUARDS[] = {"70 degrees off, halving", "1.6 degrees off", "1.4 degrees off", "40 ms steps to the end of the window", "the window's last millisecond", "clock stepped back",
                                       "only seven settled writes", "guard switched off", "switched off and on inside the window", "owner proof fails inside the window", "write standing still"};
  for (int variant = 0; variant < 11; ++variant) add(named("entry rotation guard: %s", GUARDS[variant]), [=](Run &r) { script_guard(r, variant); });
  static const char *const POSITIONS[] = {"four weapons", "nineteen weapons in sixteen places", "switched off, then on", "draw animation off", "armed in the sights", "another weapon in between"};
  for (int variant = 0; variant < 6; ++variant) add(named("remembered entry position: %s", POSITIONS[variant]), [=](Run &r) { script_entry_positions(r, variant); });
  static const char *const DEPTHS[] = {"resource definition", "override definition", "widest lens last", "near terms, 70 degrees", "with a saved placement", "mixed by the request", "through the sights",
                                       "collisions, last record", "row search wraps", "128-row maps, 63 probes"};
  for (int variant = 0; variant < 10; ++variant) for (int kind = 0; kind < 2; ++kind) for (int64_t dt : {16, 7})
    add(named("zoom depth and rig-root counter-shift: %s, %s, step %lld ms", DEPTHS[variant], KINDS[kind], (long long)dt), [=](Run &r) { script_depth(r, kind, dt, variant); });
  for (int what = 0; what < 53; ++what) for (int when = 0; when < 2; ++when)
    add(named("zoom depth refused: %s (%s)", DEPTH_REJECTS[what], when ? "after valid frames" : "from the first frame"), [=](Run &r) { script_depth_reject(r, what, when); });
  for (int variant = 0; variant < 19; ++variant) add(named("rig-root counter-shift, gate %d", variant), [=](Run &r) { script_carrier(r, variant); });
  for (int kind = 0; kind < 4; ++kind) for (int first = 0; first < 2; ++first) add(named("callers, units and nodes swept, %s, %s", KINDS[kind], first ? "armed first" : "unarmed first"), [=](Run &r) { script_callers(r, kind, first != 0); });
  static const char *const EXPIRIES[] = {"rig position call", "branch rotation call", "carrier call", "set_placement", "set_zoom_policy", "phase packet", "read_stats", "branch call, not alone", "input mode"};
  for (int where = 0; where < 9; ++where) for (uint32_t duration : {1u, 40u, 300u, 30500u})
    add(named("lease of %u ms expires, found by: %s", duration, EXPIRIES[where]), [=](Run &r) { script_expiry(r, where, duration); });
  add("control calls in right and wrong orders", script_controls);
  for (int kind = 0; kind < 4; ++kind) add(named("refused configurations and worlds, %s", KINDS[kind]), [=](Run &r) { script_refusals(r, kind); });
  for (int what = 0; what < 35; ++what) for (int where = 0; where < 6; ++where)
    add(named("owner lost (%s), found by %s", LOST[what], where == 0 ? "the rig position call" : where == 1 ? "the branch rotation call" : where == 2 ? "set_placement" : where == 3 ? "set_zoom_policy" : where == 4 ? "the carrier call" : "end"),
        [=](Run &r) { script_owner_lost(r, what, where); });
  static const char *const SLOTS[] = {"swap faults at begin", "foreign value arrives at begin", "foreign value during the lease", "swap faults at end", "table read-only at end", "rollback faults", "game's value back before end"};
  for (int which = 0; which < 5; ++which) for (int variant = 0; variant < 7; ++variant) add(named("dispatch slot %d: %s", which, SLOTS[variant]), [=](Run &r) { script_slot(r, which, variant); });
  for (int kind = 0; kind < 2; ++kind) for (int variant = 0; variant < 4; ++variant)
    add(named("time: gaps, standstill, steps back (%s), %s", variant == 0 ? "from 1000 ms" : variant == 1 ? "lease armed at time 0" : variant == 2 ? "clock about to wrap" : "blend 1 ms", KINDS[kind]), [=](Run &r) { script_time(r, kind, variant); });
  add("second avatar row", [](Run &r) { script_rows(r, 0); }); add("request row 31", [](Run &r) { script_rows(r, 1); }); add("request row 0", [](Run &r) { script_rows(r, 2); });
  add("checks only the translation makes", script_translation_only);
  add("after tfp_pose_reset_for_tests", script_reset);
  // look gain
  static const char *const DEVICES[] = {"mouse", "pad kind 1, inverted", "pad kind 2, inverted", "mouse, inverted", "steering", "look context", "pitch gate"};
  for (int kind = 2; kind < 4; ++kind) for (int variant = 0; variant < 7; ++variant) add(named("look gain: evaluations at five frame rates, %s, %s", DEVICES[variant], KINDS[kind]), [=](Run &r) { script_input_basic(r, kind, variant); });
  for (int what = 0; what < 46; ++what) add(named("look gain: %s", INPUT_CASES[what]), [=](Run &r) { script_input_case(r, what); });
  add("look gain: more presets than private records", script_input_pool);
  static const char *const COLDS[] = {"evaluator page unreadable", "third pin unreadable", "a byte of the solver differs", "a constant differs", "the double constant differs", "an executable pin differs"};
  for (int variant = 0; variant < 6; ++variant) add(named("look gain: code proof made late, %s", COLDS[variant]), [=](Run &r) { script_input_cold(r, variant); });
  static const char *const PROBES[] = {"rig position call", "lens observation", "carrier call", "branch rotation call", "rig position call with a definition refresh", "rig position call with the revalidation", "end",
                                       "set_placement", "set_zoom_policy", "begin_steady", "validate", "read_stats at the deadline", "branch call inside the entry window", "branch call at the deadline", "read_input_stats"};
  for (int which = 0; which < 15; ++which) add(std::string("probe: ") + PROBES[which], [=](Run &r) { probe_script(r, which); }, true);
  static const char *const INPUT_PROBES[] = {"look gain evaluation", "look gain completion", "end with a published selector", "begin_steady_input", "look gain re-entry", "first look gain evaluation"};
  for (int which = 0; which < 6; ++which) add(std::string("probe: ") + INPUT_PROBES[which], [=](Run &r) { probe_script(r, 20 + which); }, true);
  // TFP_DIFF_EXTRA_WALKS=n: n more walks (a soak; not part of the normal run).
  const char *extra = std::getenv("TFP_DIFF_EXTRA_WALKS");
  const uint32_t walks = 8 + (extra ? static_cast<uint32_t>(std::atoi(extra)) : 0u);
  for (uint32_t seed = 1; seed <= walks; ++seed) add(named("random walk, seed %u%s", seed, seed & 1 ? ", with look gain" : ""), [=](Run &r) { script_walk(r, 25480438u + seed, 2500, (seed & 1) != 0); });
}

// ---- the runner ----
struct Job { size_t script; int mode, index, value; };
static int running = 0, failed = 0, finished = 0;
static void reap() {
  int status = 0; const pid_t pid = wait(&status);
  if (pid < 0) return;
  --running; ++finished;
  if (WIFSIGNALED(status)) { ++failed; std::fprintf(stderr, "a pose helper script's process ended on signal %d (rerun with the script's name to isolate)\n", WTERMSIG(status)); }
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
    shim_self = 0;
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
      char text[512]; const int n = std::snprintf(text, sizeof text, "CRASH (signal %d, %s running) in script \"%s\", call %llu: %s\n", signal_number, side ? "the translation" : "the original",
                                                  script_name.c_str(), (unsigned long long)call_index, call_text.c_str());
      if (n > 0) (void)!::write(2, text, static_cast<size_t>(n));
      std::_Exit(4); } };
    signal(SIGSEGV, Report::crash); signal(SIGBUS, Report::crash); signal(SIGFPE, Report::crash); signal(SIGABRT, Report::crash);
    Run r; current_run = &r; r.probe_mode = job.mode; r.probe_index = job.index; r.probe_value = job.value;
    script.run(r);
    if (std::getenv("TFP_DIFF_REACHED")) print_reached(r);
    __sync_fetch_and_add(&shared->calls, counted.calls); __sync_fetch_and_add(&shared->hooks, counted.hooks); __sync_fetch_and_add(&shared->events, counted.events);
    __sync_fetch_and_add(&shared->skipped, counted.skipped); __sync_fetch_and_add(&shared->translation_only, counted.translation_only);
    __sync_fetch_and_add(&shared->retail_only_reads, counted.retail_only_reads);
    std::fflush(stdout);
    std::_Exit(0);
  }
  ++running;
}

} // namespace dp

int main(int argc, char **argv) {
  using namespace dp;
  const char *label = input_build ? "Pose helper (look-gain build)" : "Pose helper";
  shared = static_cast<Shared *>(mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  register_scripts();
  stop_at_first = std::getenv("TFP_DIFF_STOP") != nullptr;
  const char *only = argc > 1 ? argv[1] : nullptr; // a part of a script's name: run just those
  if (only && !std::strcmp(only, "--list")) { for (const Script &s : scripts) std::printf("%s\n", s.name.c_str()); return 0; }
  build(); // the two worlds, untouched by any helper call; every child starts from a copy of them
  if (input_build && !load_pins()) {
    std::printf("%s: skipped (the captured images of the two builds were not found; TFP_INPUT_CAPTURE_DIR, TFP_HERE_IMAGE_DIR)\n", label);
    return 0;
  }
  const int limit = std::max(1L, sysconf(_SC_NPROCESSORS_ONLN));
  size_t plain = 0, probe_runs = 0;
  for (size_t i = 0; i < scripts.size(); ++i) {
    if (only && scripts[i].name.find(only) == std::string::npos) continue;
    if (scripts[i].probed) continue;
    launch({i, INJECT_NONE, -1, 0}, limit); ++plain;
  }
  while (running) reap();
  // TFP_DIFF_JOB="mode,index,value" (mode 1 fails and heals, 2 stays unreadable, 3 a value changes): that one probe run only.
  if (const char *one = std::getenv("TFP_DIFF_JOB")) {
    int mode = 0, index = 0, value = 0; std::sscanf(one, "%d,%d,%d", &mode, &index, &value);
    for (size_t i = 0; i < scripts.size(); ++i) if (scripts[i].probed && only && scripts[i].name.find(only) != std::string::npos) launch({i, mode, index, value}, 1);
    while (running) reap();
    return failed ? 1 : 0;
  }
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
  std::printf("%s: %zu scripts and %zu probe runs; %llu calls compared (%llu of them hook calls), %llu reads, writes, swaps, lock steps, allocations and forwarded calls matched; "
              "%llu translation-only host answers; %llu retail-only reads of pinned code; %llu hook calls skipped (before any begin)\n",
              label, plain, probe_runs, (unsigned long long)shared->calls, (unsigned long long)shared->hooks, (unsigned long long)shared->events,
              (unsigned long long)shared->translation_only, (unsigned long long)shared->retail_only_reads, (unsigned long long)shared->skipped);
  if (failed) { std::printf("%s: %d script run(s) FAILED\n", label, failed); return 1; }
  std::printf("%s: the translation behaves as the retail helper in every compared call\n", label);
  return 0;
}
