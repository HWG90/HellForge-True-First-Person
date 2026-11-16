// The scripts of tests/diff_fov_helper.cpp, and the runner (one child process per script).
#pragma once

namespace dt {

struct Script { std::string name; std::function<void(Run &)> run; bool probed = false; };
static std::vector<Script> scripts;
static void add(const std::string &name, std::function<void(Run &)> run, bool probed = false) { scripts.push_back({name, std::move(run), probed}); }

static void print_reached(const Run &r) {
  const Run::Reached &g = r.got;
  std::printf("REACHED %s: replaced %llu, ads forwarded %llu, immediate %llu, binds %llu, calls with: lens ease %llu, shared phase %llu, shoulder tween %llu, lens ledger %llu; begin=2 %llu; errors",
              script_name.c_str(), (unsigned long long)g.replaced, (unsigned long long)g.ads, (unsigned long long)g.immediate, (unsigned long long)g.binds, (unsigned long long)g.blending,
              (unsigned long long)g.shared, (unsigned long long)g.tween, (unsigned long long)g.ledger, (unsigned long long)g.cleanup_begins);
  for (int code = 0; code < 20; ++code) if (g.errors[code]) std::printf(" %d:%llu", code, (unsigned long long)g.errors[code]);
  std::printf("\n"); std::fflush(stdout);
}
static Run *current_run;
// A script that did not get where it was written to go is a broken script, not a passing one.
static void need(bool reached, const char *what) { if (!reached && current_run) print_reached(*current_run); if (!reached) fail("the script did not reach what it is for: %s", what); }
static void now(double t) { fixture_now = t; }
static void tick(double dt) { fixture_now += dt; }
static void frames(Run &r, int count, double dt, float native) { for (int i = 0; i < count; ++i) { tick(dt); r.frame(native); } }
static void arm(Run &r, uint64_t reserved_ms) {
  r.fp(); r.reserved_ms = reserved_ms;
  if (r.begin() != 1) fail("the script could not arm (begin returned %u, error %u)", r.result, NADSF_Error());
}
static const float CAPTURED = 1.0471976f; // 60 degrees
static void arm_fird(Run &r, float field = 1.2f) {
  r.normal(); r.lens(r.request_row, field); r.reserved_ms = 0;
  if (r.begin_fird(CAPTURED) != 1) fail("the script could not arm the fird policy (begin returned %u, error %u)", r.result, NADSF_Error());
}
static void repair(Run &r) {
  for (int w = 0; w < 2; ++w) W[w].write_static();
  Run fresh;
  r.size = fresh.size; r.abi = fresh.abi; r.index = fresh.index; r.avatar = fresh.avatar; r.body = fresh.body; r.weapon = fresh.weapon;
  r.request_absolute = 0; r.request_is_absolute = false; r.request_delta = 0; r.context_delta = r.camera_delta = r.owner_delta = r.game_delta = r.exe_delta = 0;
  r.context_absolute = r.camera_absolute = 0; r.config_null = r.generation_null = false;
  shim_pin_fails = false; fixture_slot_readonly = false; fixture_clock_ok = true;
}

// ---- hip: the third-person setting on the weapon camera ----
static void script_hip(Run &r, double fps, uint64_t reserved_ms) {
  now(100); arm(r, reserved_ms);
  for (int i = 0; i < 240; ++i) { tick(1 / fps); if (i % 40 == 0) r.degrees(60 + (i / 40) * 9.5f); r.frame(.6f + .001f * i); }
  need(r.got.replaced == 240 && r.got.immediate == 1, "every hip frame replaced");
  for (float d : {1.0f, 179.0f, .99f, 179.5f, NAN, INFINITY, -5.0f, 0.0f, 90.0f, 110.0f}) {
    r.degrees(d); tick(1 / fps); r.frame(.7f); r.frame(-1.0f); r.frame(NAN); r.frame(4.0f); r.frame(3.14159265f);
  }
  r.end(r.gen); frames(r, 2, 1 / fps, .5f);
}
// ---- the 5 % shoulder magnification and its 120 ms tween ----
static void script_shoulder(Run &r, double fps, uint64_t reserved_ms) {
  const double dt = 1 / fps; now(50); arm(r, reserved_ms); frames(r, 5, dt, .8f);
  r.shoulder(true); frames(r, 30, dt, .8f);
  r.shoulder(false); frames(r, 30, dt, .8f);
  r.shoulder(true); frames(r, 3, dt, .8f); r.shoulder(false); frames(r, 2, dt, .8f); r.shoulder(true); frames(r, 30, dt, .8f);
  need(r.got.tween > 3 && r.got.replaced > 90, "shoulder tween frames");
  // a pause in mid-tween is not integrated
  r.shoulder(false); frames(r, 2, dt, .8f); tick(.3); r.frame(.8f); frames(r, 30, dt, .8f);
  // time going backwards in mid-tween
  r.shoulder(true); frames(r, 2, dt, .8f); tick(-.05); r.frame(.8f); frames(r, 3, dt, .8f); tick(-10); r.frame(.8f); frames(r, 30, dt, .8f);
  // the clock failing, and answering nonsense, in mid-tween
  r.shoulder(false); frames(r, 2, dt, .8f); fixture_clock_ok = false; frames(r, 2, dt, .8f); fixture_clock_ok = true; frames(r, 20, dt, .8f);
  r.shoulder(true); frames(r, 2, dt, .8f);
  { const double keep = fixture_now; now(NAN); r.frame(.8f); now(keep); frames(r, 3, dt, .8f); now(INFINITY); r.frame(.8f); now(-INFINITY); r.frame(.8f); now(keep + 1); }
  frames(r, 20, dt, .8f);
  // one flag of the pair, and other bits of the two bytes
  r.shoulder(false); frames(r, 20, dt, .8f);
  set_u8(r.flags_id(), 10, 4); frames(r, 4, dt, .8f); set_u8(r.flags_id(), 10, 0); set_u8(r.flags_id(), 15, 32); frames(r, 4, dt, .8f);
  set_u8(r.flags_id(), 10, 0xfb); set_u8(r.flags_id(), 15, 0xdf); frames(r, 4, dt, .8f);
  set_u8(r.flags_id(), 10, 0xff); set_u8(r.flags_id(), 15, 0xff); frames(r, 20, dt, .8f);
  // the setting changes under the shoulder; full sights win while the flags linger
  r.shoulder(true); r.degrees(70); frames(r, 6, dt, .8f); r.degrees(105.5f); frames(r, 6, dt, .8f);
  r.aim(true); frames(r, 6, dt, .3f); r.aim(false); frames(r, 30, dt, .8f);
  r.end(r.gen);
}
// Exact limits of the tween clock: steps of exactly 250 ms and 120 ms, and one just over.
static void script_shoulder_exact(Run &r) {
  now(0); arm(r, 0); r.frame(.8f);
  r.shoulder(true); r.frame(.8f); now(.0625); r.frame(.8f); now(.125); r.frame(.8f);  // from time 0: the first sample is 0.0
  r.shoulder(false); now(1); r.frame(.8f); now(1.25); r.frame(.8f);                    // exactly 0.25 s: still integrated
  r.shoulder(true); now(2); r.frame(.8f); now(2.25 + 1e-9); r.frame(.8f); now(2.3); r.frame(.8f); // just over: rebased
  r.shoulder(false); now(3); r.frame(.8f); now(3.03125); r.frame(.8f); now(3.03125); r.frame(.8f); now(3.0625); r.frame(.8f); now(3.5); r.frame(.8f);
  r.shoulder(true); now(4); r.frame(.8f); now(4.12); r.frame(.8f); now(4.24); r.frame(.8f);
  need(r.got.tween >= 3, "partial weights");
  r.end(r.gen);
}
// ---- the ease into and out of the sight's lens (reserved != 0), and its refusals ----
static void script_ads(Run &r, double fps, uint64_t reserved_ms, int variant) {
  const double dt = 1 / fps; now(variant == 7 ? -3 : 10); arm(r, reserved_ms); frames(r, 5, dt, .9f);
  const int hold = static_cast<int>(reserved_ms / 1000.0 * fps) + 8;
  r.aim(true);
  for (int i = 0; i < hold; ++i) { tick(dt); r.frame(i >= hold / 3 ? .30f : .35f); } // the zoom changes in mid-ease
  need(r.got.blending >= 2 && r.got.ads >= static_cast<uint64_t>(hold), "the ease into the sight");
  switch (variant) {
  case 0: // out on the weapon camera: the target is the setting
    r.aim(false); frames(r, hold, dt, .35f); frames(r, 10, dt, .9f); break;
  case 1: // out with the third-person request on top: the target is the game's lens; then the request is not ours
    r.aim(false); r.ring(0, 1); r.row(0, 4, AVATAR, 0, BODY); frames(r, hold + 4, dt, .9f); r.ring(0, 2); frames(r, 10, dt, .9f);
    r.aim(true); frames(r, 3, dt, .35f); r.aim(false); r.ring(0, 0); r.row(IDLE_ROW, 4, AVATAR, 0, BODY); frames(r, hold, dt, .9f); r.ring(0, 2); frames(r, 4, dt, .9f); break;
  case 2: // in again in the middle of the way out, and out again
    r.aim(false); frames(r, 3, dt, .35f); r.aim(true); frames(r, 3, dt, .35f); r.aim(false); frames(r, 2, dt, .35f); r.aim(true); frames(r, hold, dt, .33f);
    r.aim(false); frames(r, hold + 3, dt, .35f); break;
  case 3: // objections on the way out and on the way in
    r.aim(false); frames(r, 2, dt, .35f); r.controls(1); frames(r, 2, dt, .35f); r.controls(); frames(r, hold, dt, .35f);
    r.aim(true); frames(r, 2, dt, .35f); r.controls(0, true, true); frames(r, 2, dt, .35f); r.controls(0, true); frames(r, hold, dt, .35f);
    r.seated(true); frames(r, 2, dt, .35f); r.seated(false); frames(r, 3, dt, .35f);
    r.controls(2, true); frames(r, 3, dt, .35f); r.controls(0x20, true); frames(r, 2, dt, .35f); r.controls(2, false); frames(r, hold, dt, .35f); break;
  case 4: // another weapon on the request in full sights, then bound
    r.row(1, 6, AVATAR, WEAPON_B, BODY); frames(r, 3, dt, .35f); r.bind(r.gen, 1, WEAPON_B); need(r.result == 1, "bind in full sights");
    frames(r, 4, dt, .35f); r.aim(false); frames(r, hold, dt, .35f);
    r.aim(true); frames(r, 3, dt, .35f); r.row(1, 6, AVATAR, WEAPON_B, 0x400009); frames(r, 2, dt, .35f); r.row(1, 6, AVATAR, WEAPON_B, BODY);
    r.ring(0, 0); r.row(IDLE_ROW, 6, AVATAR, WEAPON_B, BODY); frames(r, 2, dt, .35f); r.ring(0, 2); frames(r, 3, dt, .35f);
    r.aim(false); r.row(1, 5, AVATAR, WEAPON_B, BODY); frames(r, 3, dt, .35f); r.row(1, 6, AVATAR, WEAPON_B, BODY); frames(r, hold, dt, .35f); break;
  case 5: // lenses the ease must refuse, in and out
    for (float odd : {NAN, 0.0f, -1.0f, 3.14159265f, 3.2f, INFINITY, 1e-6f, 3.1415f, .35f}) { tick(dt); r.frame(odd); tick(dt); r.frame(.35f); }
    r.aim(false); tick(dt); r.frame(.35f); r.degrees(NAN); frames(r, 2, dt, .35f); r.degrees(179); frames(r, 2, dt, NAN); r.degrees(90);
    for (float odd : {NAN, 0.0f, 3.2f, .9f}) { tick(dt); r.frame(odd); }
    frames(r, hold, dt, .9f); break;
  case 6: // clock trouble at the switch and in the ease; time backwards; a jump past the end
    fixture_clock_ok = false; r.aim(false); tick(dt); r.frame(.35f); fixture_clock_ok = true; frames(r, 3, dt, .35f);
    r.aim(true); tick(dt); r.frame(.35f); fixture_clock_ok = false; tick(dt); r.frame(.35f); fixture_clock_ok = true; frames(r, 3, dt, .35f);
    r.aim(false); tick(dt); r.frame(.35f); tick(-1); r.frame(.35f); frames(r, 3, dt, .35f);
    r.aim(true); tick(dt); r.frame(.35f); tick(reserved_ms / 1000.0); r.frame(.35f); frames(r, 2, dt, .35f);
    r.aim(false); tick(dt); r.frame(.35f); { const double keep = fixture_now; now(NAN); r.frame(.35f); now(keep); } frames(r, hold, dt, .35f);
    r.aim(true); tick(dt); r.frame(.35f); tick(dt); r.frame(.35f); tick(1e9); r.frame(.35f); break;
  case 7: // the switch sampled at exactly time 0 (the clock is then asked twice), in both directions
    now(-.5); r.frame(.35f); now(0); r.aim(false); r.frame(.35f); now(.01); r.frame(.35f); now(.4); r.frame(.35f);
    now(-.2); r.frame(.9f); now(0); r.aim(true); r.frame(.35f); now(0); r.frame(.35f); now(.05); r.frame(.35f); now(1); r.frame(.35f); break;
  case 8: // exactly the end of the ease, and one step before it
    r.aim(false); now(64); r.frame(.35f); now(64 + reserved_ms / 1000.0 - 1e-9); r.frame(.35f); now(64 + reserved_ms / 1000.0); r.frame(.35f); now(64.75); r.frame(.35f);
    r.aim(true); now(128); r.frame(.35f); now(128.0625); r.frame(.35f); now(128 + reserved_ms / 1000.0); r.frame(.35f); break;
  }
  r.end(r.gen); frames(r, 2, dt, .5f);
}
// ---- reserved 0: full sights pass through ----
static void script_passthrough(Run &r) {
  now(10); arm(r, 0); frames(r, 5, .016, .9f);
  r.aim(true); frames(r, 20, .016, .35f); need(r.got.ads == 20 && r.got.blending == 0, "full sights forwarded");
  r.aim(false); frames(r, 10, .016, .9f); r.shoulder(true); frames(r, 10, .016, .9f); r.aim(true); frames(r, 5, .016, .3f); r.aim(false); r.shoulder(false); frames(r, 12, .016, .9f);
  // a lease that begins in full sights: nothing is applied at once
  r.end(r.gen); r.fp(); r.aim(true); r.begin(); need(r.result == 1 && r.got.immediate == 1, "begin in full sights applies nothing");
  frames(r, 3, .016, .35f); r.aim(false); frames(r, 3, .016, .9f); r.end(r.gen);
  r.fp(); r.aim(true); r.reserved_ms = 180; r.begin(); frames(r, 3, .016, .35f); r.aim(false); frames(r, 20, .016, .9f); r.end(r.gen);
}
// ---- the presentation phase packet ----
static void script_phase(Run &r, double fps, int variant) {
  const double dt = 1 / fps; now(20); arm(r, 180); frames(r, 5, dt, .9f);
  uint64_t epoch = 1; const int steps = std::max(2, static_cast<int>(.18 * fps));
  auto phase = [&](uint32_t ads, float native, float native_late) {
    for (int i = 0; i <= steps + 3; ++i) {
      tick(dt); r.transition(r.gen, epoch, std::min(1.0f, static_cast<float>(i) / steps), ads);
      r.frame(i > steps / 2 ? native_late : native); if (variant == 4) r.frame(native_late);
    }
    ++epoch;
  };
  r.aim(true); phase(1, .35f, .35f);
  r.aim(false); phase(0, .9f, .9f);
  r.shoulder(true); phase(0, .9f, .9f); r.shoulder(false); phase(0, .9f, .9f);
  need(r.got.shared > 10 && r.got.blending > 2, "shared phase frames");
  switch (variant) {
  case 0: case 4: break;
  case 1: // the token still names the old mode when the sight comes up, and when it goes down
    tick(dt); r.transition(r.gen, epoch, .2f, 0); r.aim(true); frames(r, 3, dt, .35f); ++epoch; phase(1, .35f, .35f);
    tick(dt); r.transition(r.gen, epoch, .5f, 1); r.aim(false); frames(r, 3, dt, .9f); ++epoch; phase(0, .9f, .9f); break;
  case 2: // a producer that stops: 250 ms exactly is still live, more is not
    now(64); r.transition(r.gen, epoch, .3f, 0); now(64.25); r.frame(.9f); now(64.25 + 1e-9); r.frame(.9f); now(64.3); r.frame(.9f); ++epoch;
    r.aim(true); now(65); r.transition(r.gen, epoch, .3f, 1); now(65.0625); r.frame(.35f); now(65.5); r.frame(.35f); frames(r, 14, dt, .35f); ++epoch;
    r.aim(false); phase(0, .9f, .9f); break;
  case 3: // the clock steps back behind the packet; then fails; then answers nonsense
    r.aim(true); tick(dt); r.transition(r.gen, epoch, .4f, 1); r.frame(.35f); tick(-.01); r.frame(.35f); frames(r, 3, dt, .35f); ++epoch;
    tick(dt); r.transition(r.gen, epoch, .4f, 1); fixture_clock_ok = false; r.frame(.35f); r.transition(r.gen, epoch, .5f, 1); need(NADSF_Error() == SOURCE_INVALID, "no clock for the packet");
    fixture_clock_ok = true; frames(r, 3, dt, .35f); ++epoch;
    tick(dt); r.transition(r.gen, epoch, .4f, 1); { const double keep = fixture_now; now(NAN); r.frame(.35f); r.transition(r.gen, epoch, .6f, 1); now(keep); } frames(r, 3, dt, .35f);
    r.aim(false); ++epoch; phase(0, .9f, .9f); break;
  case 5: // the zoom and the setting move during the phase
    r.aim(true); phase(1, .35f, .25f); r.aim(false); r.degrees(75); phase(0, .9f, .9f); r.degrees(100); r.shoulder(true); phase(0, .9f, .9f);
    r.aim(true); for (int i = 0; i <= steps; ++i) { tick(dt); r.transition(r.gen, epoch, static_cast<float>(i) / steps, 1); r.frame(i & 1 ? .2f : NAN); r.frame(i & 1 ? 3.2f : .3f); } break;
  case 6: // packets that must be refused
    tick(dt); r.transition(r.gen, epoch, .5f, 1);
    r.transition(r.gen, epoch, .4f, 1); r.transition(r.gen, epoch, .5f, 0); r.transition(r.gen, epoch - 1, .9f, 1); r.transition(r.gen, 0, .5f, 1);
    r.transition(r.gen, epoch, NAN, 1); r.transition(r.gen, epoch, -.01f, 1); r.transition(r.gen, epoch, 1.01f, 1); r.transition(r.gen, epoch, INFINITY, 1); r.transition(r.gen, epoch, .6f, 2);
    r.transition(r.gen + 1, epoch, .6f, 1); r.transition(0, epoch, .6f, 1); r.transition(r.gen, epoch, .5f, 1); r.transition(r.gen, epoch, 1.0f, 1); r.transition(r.gen, epoch, 1.0f, 1);
    r.transition(r.gen, epoch + 5, 0.0f, 0); r.transition(r.gen, epoch + 5, 0.0f, 1); r.transition(r.gen, UINT64_MAX, 1.0f, 0); r.transition(r.gen, UINT64_MAX, 1.0f, 0); r.transition(r.gen, 1, 0, 0);
    r.frame(.9f);
    r.controls(1); r.transition(r.gen, UINT64_MAX, 1.0f, 0); r.controls(); // an objection does not refuse the packet
    r.slot_holds(SLOT_FOREIGN); r.transition(r.gen, UINT64_MAX, 1.0f, 0); need(NADSF_Error() == API_MISMATCH, "packet with a foreign slot"); r.slot_holds(SLOT_HELPER);
    set_u32(A_DESCRIPTOR, 8, 0x999); r.transition(r.gen, UINT64_MAX, 1.0f, 0); need(NADSF_Error() == OWNER_MISMATCH, "packet with a foreign owner"); set_u32(A_DESCRIPTOR, 8, AVATAR);
    r.transition(r.gen, UINT64_MAX, 1.0f, 0); r.frame(.9f); break;
  case 7: // the sight comes up in the middle of an epoch for the hip, and a new epoch starts in the middle of an ease
    tick(dt); r.transition(r.gen, epoch, .3f, 0); r.frame(.9f); r.aim(true); tick(dt); r.transition(r.gen, epoch, .5f, 0); r.frame(.35f); tick(dt); r.frame(.35f);
    ++epoch; tick(dt); r.transition(r.gen, epoch, .1f, 1); r.frame(.35f); tick(dt); r.transition(r.gen, epoch, .1f, 1); r.frame(.35f); tick(dt); r.transition(r.gen, epoch, .5f, 1); r.frame(.33f);
    ++epoch; r.aim(false); tick(dt); r.transition(r.gen, epoch, .2f, 0); r.frame(.9f); tick(dt); r.transition(r.gen, epoch, .9999999f, 0); r.frame(.9f); tick(dt); r.transition(r.gen, epoch, 1.0f, 0); r.frame(.9f);
    // the producer stops in mid-ease: the local ease takes over from where the lens is
    ++epoch; r.aim(true); tick(dt); r.transition(r.gen, epoch, .3f, 1); r.frame(.35f); tick(.3); r.frame(.35f); frames(r, 14, dt, .35f);
    r.aim(false); frames(r, 3, dt, .35f); ++epoch; tick(dt); r.transition(r.gen, epoch, 0.0f, 0); frames(r, 3, dt, .35f); tick(dt); r.transition(r.gen, epoch, .7f, 0); frames(r, 3, dt, .35f); break;
  case 8: // objections and a foreign request during the phase
    r.aim(true); tick(dt); r.transition(r.gen, epoch, .3f, 1); r.frame(.35f); r.controls(1, true); tick(dt); r.transition(r.gen, epoch, .5f, 1); r.frame(.35f);
    r.controls(0, true); tick(dt); r.transition(r.gen, epoch, .7f, 1); r.frame(.35f); r.row(1, 6, AVATAR, WEAPON_B, BODY); tick(dt); r.frame(.35f); r.row(1, 6, AVATAR, WEAPON, BODY);
    tick(dt); r.transition(r.gen, epoch, 1.0f, 1); r.frame(.35f); r.aim(false); ++epoch; r.ring(0, 1); r.row(0, 4, AVATAR, 0, BODY); phase(0, .9f, .9f); r.ring(0, 2); frames(r, 3, dt, .9f); break;
  }
  r.end(r.gen); r.transition(r.gen, epoch + 9, .5f, 0);
}
// ---- weapon and request changes: bind ----
static void script_bind(Run &r, uint64_t reserved_ms) {
  now(30); arm(r, reserved_ms); frames(r, 4, .016, .9f);
  r.ring(0, 3); r.row(2, 6, AVATAR, WEAPON_B, BODY); frames(r, 3, .016, .9f);           // a new weapon camera request: not ours yet
  r.bind(0, 2, WEAPON_B); r.bind(r.gen + 1, 2, WEAPON_B); r.bind(r.gen, 2, 0); r.bind(r.gen, 2, 0xffffffff); r.bind(r.gen, 2, WEAPON_B, 0, true, 100); r.bind(r.gen, 2, WEAPON_B, 0, true, 65535);
  r.bind(r.gen, 1, WEAPON_B); r.bind(r.gen, 2, WEAPON); r.bind(r.gen, 2, WEAPON_B, 4);
  r.controls(1); r.bind(r.gen, 2, WEAPON_B); r.controls(0, false, true); r.bind(r.gen, 2, WEAPON_B); r.controls(); r.seated(true); r.bind(r.gen, 2, WEAPON_B); r.seated(false);
  r.degrees(0); r.bind(r.gen, 2, WEAPON_B); need(NADSF_Error() == OWNER_MISMATCH, "bind without a readable setting"); r.degrees(90);
  set_u8(A_G_CALL, 0, 0x00); r.bind(r.gen, 2, WEAPON_B); need(NADSF_Error() == API_MISMATCH, "bind with changed code"); set_u8(A_G_CALL, 0, 0xff);
  set_u32(A_EXE, 136, 1); r.bind(r.gen, 2, WEAPON_B); for (int w = 0; w < 2; ++w) W[w].write_static();
  set_u32(A_DESCRIPTOR, 12, 0x400009); r.bind(r.gen, 2, WEAPON_B); set_u32(A_DESCRIPTOR, 12, BODY);
  forward_pokes.push_back({1, {{A_CONTROLLER, 0}, raw_value(1)}}); r.bind(r.gen, 2, WEAPON_B); need(r.result == 0, "an objection arriving during the bind's own setter call"); r.controls();
  forward_pokes.push_back({1, {{A_GENERATIONS, 3}, raw_value(9, 1)}}); r.bind(r.gen, 2, WEAPON_B); set_u8(A_GENERATIONS, 3, 1);
  frames(r, 2, .016, .9f);
  r.bind(r.gen, 2, WEAPON_B); need(r.result == 1 && r.got.binds == 1, "bind"); frames(r, 5, .016, .9f);
  r.shoulder(true); frames(r, 3, .016, .9f); r.ring(0, 2); r.row(1, 6, AVATAR, WEAPON, BODY); r.bind(r.gen, 1, WEAPON); frames(r, 12, .016, .9f); // bound in mid-tween
  r.aim(true); r.ring(0, 3); r.bind(r.gen, 2, WEAPON_B); frames(r, 3, .016, .35f); r.aim(false); frames(r, 14, .016, .9f);                  // bound in full sights: nothing applied
  r.slot_holds(SLOT_FOREIGN); r.bind(r.gen, 2, WEAPON_B); need(NADSF_Error() == API_MISMATCH, "bind with a foreign slot"); r.slot_holds(SLOT_HELPER);
  r.row(2, 4, AVATAR, WEAPON_B, BODY); r.bind(r.gen, 2, WEAPON_B); frames(r, 2, .016, .9f); r.row(2, 6, AVATAR, WEAPON_B, BODY);
  set_u32(A_CONTEXT, 0x1F8, 32); r.bind(r.gen, 2, WEAPON_B); r.frame(.9f); set_u32(A_CONTEXT, 0x1F8, 0);
  r.slot_holds(SLOT_FOREIGN); r.frame(.9f); need(NADSF_Error() == API_MISMATCH && !::armed.load(), "disarmed by a foreign slot"); r.slot_holds(SLOT_HELPER);
  r.bind(r.gen, 2, WEAPON_B); need(NADSF_Error() == OWNER_MISMATCH, "bind on a disarmed lease"); r.bind_fird(r.gen, 2);
  r.end(r.gen); r.bind(r.gen, 2, WEAPON_B); need(NADSF_Error() == TOKEN_MISMATCH, "bind after the end");
}

// ---- everything validate and begin refuse ----
struct Break { const char *name; std::function<void(Run &)> on; };
static std::vector<Break> breaks() {
  std::vector<Break> b;
  auto world = [&](const char *name, int id, uint64_t offset, Value v) { b.push_back({name, [=](Run &) { set({id, offset}, v); }}); };
  b.push_back({"nothing", [](Run &) {}});
  b.push_back({"config size", [](Run &r) { r.size = 79; }}); b.push_back({"config size 88", [](Run &r) { r.size = 88; }}); b.push_back({"config abi", [](Run &r) { r.abi = 2; }});
  for (uint64_t ms : {1ull, 79ull, 80ull, 500ull, 501ull, 0x100000000ull}) b.push_back({"config reserved", [=](Run &r) { r.reserved_ms = ms; }});
  b.push_back({"config index 8", [](Run &r) { r.index = 8; }}); b.push_back({"config index 2 of 2", [](Run &r) { r.index = 2; }});
  b.push_back({"config index 1, avatar 0's ids", [](Run &r) { r.index = 1; }});
  b.push_back({"config index 1", [](Run &r) { r.index = 1; r.avatar = AVATAR1; r.body = BODY1; set_u32(A_CONTEXT, row_offset(r.request_row) + 4, AVATAR1); set_u32(A_CONTEXT, row_offset(r.request_row) + 12, BODY1); }});
  b.push_back({"config avatar 0", [](Run &r) { r.avatar = 0; }}); b.push_back({"config avatar -1", [](Run &r) { r.avatar = 0xffffffff; }}); b.push_back({"config avatar other", [](Run &r) { r.avatar = 0x999; }});
  b.push_back({"config body 0", [](Run &r) { r.body = 0; }}); b.push_back({"config body -1", [](Run &r) { r.body = 0xffffffff; }});
  b.push_back({"config body generation", [](Run &r) { r.body = 0x800003; }}); b.push_back({"config body past capacity", [](Run &r) { r.body = 0x400010; }});
  b.push_back({"config weapon 0", [](Run &r) { r.weapon = 0; }}); b.push_back({"config weapon -1", [](Run &r) { r.weapon = 0xffffffff; }}); b.push_back({"config weapon other", [](Run &r) { r.weapon = WEAPON_B; }});
  b.push_back({"config context small", [](Run &r) { r.context_absolute = 65535; }}); b.push_back({"config context elsewhere", [](Run &r) { r.context_delta = 8; }});
  b.push_back({"config camera small", [](Run &r) { r.camera_absolute = 65535; }}); b.push_back({"config camera elsewhere", [](Run &r) { r.camera_delta = 16; }});
  b.push_back({"config request small", [](Run &r) { r.request_is_absolute = true; r.request_absolute = 65535; }}); b.push_back({"config request elsewhere", [](Run &r) { r.request_delta = 0xE8; }});
  b.push_back({"config request idle", [](Run &r) { r.request_row = IDLE_ROW; }});
  b.push_back({"config owner elsewhere", [](Run &r) { r.owner_delta = 8; }}); b.push_back({"config game elsewhere", [](Run &r) { r.game_delta = 4096; }}); b.push_back({"config exe elsewhere", [](Run &r) { r.exe_delta = 4096; }});
  b.push_back({"config null", [](Run &r) { r.config_null = true; }}); b.push_back({"generation null", [](Run &r) { r.generation_null = true; }});
  for (int image : {A_GAME, A_EXE}) {
    world("image e_magic", image, 0, raw_value(0x5A4E, 2)); world("image e_lfanew 63", image, 0x3c, raw_value(63)); world("image e_lfanew 4097", image, 0x3c, raw_value(4097));
    world("image e_lfanew negative", image, 0x3c, raw_value(0xffffff80)); world("image signature", image, 128, raw_value(0x00004551)); world("image machine", image, 132, raw_value(0x8665, 2));
    world("image magic", image, 152, raw_value(0x10b, 2)); world("image stamp", image, 136, raw_value(0x11111111)); world("image size", image, 208, raw_value(0x1000));
    // A header at the far end of the accepted range (4096): accepted when it carries this build's identity.
    b.push_back({"image e_lfanew 4096, good header there", [=](Run &) { for (int w = 0; w < 2; ++w) {
      const uint64_t base = W[w].a[image], at = base + 4096; World::put<int32_t>(base + 0x3c, 4096);
      World::put<uint32_t>(at, 0x00004550); World::put<uint16_t>(at + 4, 0x8664); World::put<uint32_t>(at + 8, image == A_GAME ? W[w].L->game_stamp : W[w].L->exe_stamp);
      World::put<uint16_t>(at + 24, 0x20b); World::put<uint32_t>(at + 80, image == A_GAME ? W[w].L->game_size : W[w].L->exe_size); } }});
    b.push_back({"image e_lfanew 64, no header there", [=](Run &) { set_u32(image, 0x3c, 64); }});
  }
  world("call first byte", A_G_CALL, 0, raw_value(0xfe, 1)); world("call last byte", A_G_CALL, 5, raw_value(1, 1));
  world("setter first byte", A_E_SETTER, 0, raw_value(0xf2, 1)); world("setter last byte", A_E_SETTER, 16, raw_value(1, 1));
  world("getter first byte", A_E_GETTER, 0, raw_value(0xf2, 1)); world("getter last byte", A_E_GETTER, 5, raw_value(0xc2, 1));
  static const int lengths[6] = {5, 17, 7, 33, 17, 39};
  for (int i = 0; i < 6; ++i) { world("fird pattern first byte", A_G_FIRD0 + i, 0, raw_value(0x90, 1)); world("fird pattern last byte", A_G_FIRD0 + i, lengths[i] - 1, raw_value(0x90, 1)); }
  world("api root null", A_G_API_ROOT, 0, raw_value(0, 8)); world("api root small", A_G_API_ROOT, 0, raw_value(65535, 8)); world("api root high", A_G_API_ROOT, 0, raw_value(0x800000000000ull, 8));
  world("api null", A_API_ROOT, 0x20, raw_value(0, 8)); world("api other table", A_API_ROOT, 0x20, address_value(A_API_OTHER));
  world("api unaligned", A_API_ROOT, 0x20, address_value(A_API_OTHER, 0x804)); // a table whose slot is not 8-aligned (prepared below)
  world("getter slot wrong", A_API, 0x78, address_value(A_E_SETTER)); world("getter slot null", A_API, 0x78, raw_value(0, 8));
  world("slot foreign", A_API, 0x80, slot_value(SLOT_FOREIGN)); world("slot null", A_API, 0x80, raw_value(0, 8)); world("slot helper", A_API, 0x80, slot_value(SLOT_HELPER));
  b.push_back({"slot read-only", [](Run &) { fixture_slot_readonly = true; }});
  b.push_back({"pin fails", [](Run &) { shim_pin_fails = true; }});
  world("context pointer elsewhere", A_G_CONTEXT, 0, address_value(A_SPARE)); world("context pointer null", A_G_CONTEXT, 0, raw_value(0, 8));
  world("camera pointer elsewhere", A_CONTEXT, 0, address_value(A_SPARE)); world("camera pointer small", A_CONTEXT, 0, raw_value(4096, 8));
  world("owner pointer elsewhere", A_G_OWNER, 0, address_value(A_SPARE)); world("controller pointer null", A_G_CONTROLLER, 0, raw_value(0, 8));
  world("controller unreadable", A_G_CONTROLLER, 0, address_value(A_CONTROLLER, 0xffd));
  world("count 0", A_OWNER, 0x6C, raw_value(0)); world("count 9", A_OWNER, 0x6C, raw_value(9)); world("count 8", A_OWNER, 0x6C, raw_value(8)); world("count 1", A_OWNER, 0x6C, raw_value(1));
  world("descriptor null", A_OWNER, 0x110, raw_value(0, 8)); world("descriptor other", A_OWNER, 0x110, address_value(A_DESCRIPTOR1));
  world("descriptor avatar", A_DESCRIPTOR, 8, raw_value(0x999)); world("descriptor body", A_DESCRIPTOR, 12, raw_value(0x400009));
  world("registry null", A_E_REGISTRY, 0, raw_value(0, 8)); world("capacity 0", A_REGISTRY, 0x98, raw_value(0)); world("capacity 3", A_REGISTRY, 0x98, raw_value(3)); world("capacity 4", A_REGISTRY, 0x98, raw_value(4));
  world("capacity 0x400000", A_REGISTRY, 0x98, raw_value(0x400000)); world("capacity 0x400001", A_REGISTRY, 0x98, raw_value(0x400001));
  world("generations null", A_REGISTRY, 0xA0, raw_value(0, 8)); world("generation byte", A_GENERATIONS, 3, raw_value(2, 1));
  world("units null", A_REGISTRY, 0x88, raw_value(0, 8)); world("unit null", A_UNITS, 24, raw_value(0, 8)); world("unit high", A_UNITS, 24, raw_value(0x800000000000ull, 8));
  for (uint32_t mask : {1u, 2u, 3u, 0x20u, 0x100u}) world("mask", A_CONTROLLER, 0, raw_value(mask));
  world("guard", A_CONTROLLER, 5, raw_value(1, 1)); world("full sights", A_CONTROLLER, 4, raw_value(1, 1)); world("full sights 0x80", A_CONTROLLER, 4, raw_value(0x80, 1));
  world("seated", A_O_FLAGS0, 14, raw_value(4, 1)); world("flag byte 14 other bits", A_O_FLAGS0, 14, raw_value(0xfb, 1)); world("shoulder a", A_O_FLAGS0, 10, raw_value(4, 1));
  b.push_back({"shoulder", [](Run &r) { r.shoulder(true); }});
  b.push_back({"request kind 4", [](Run &r) { set_u32(A_CONTEXT, row_offset(r.request_row), 4); }}); b.push_back({"request kind 6", [](Run &r) { set_u32(A_CONTEXT, row_offset(r.request_row), 6); }});
  b.push_back({"request kind 5", [](Run &r) { set_u32(A_CONTEXT, row_offset(r.request_row), 5); }}); b.push_back({"request kind high bits", [](Run &r) { set_u32(A_CONTEXT, row_offset(r.request_row), 0xABCD0000u | (r.request_row == 0 ? 4u : 6u)); }});
  b.push_back({"request avatar", [](Run &r) { set_u32(A_CONTEXT, row_offset(r.request_row) + 4, 0x999); }}); b.push_back({"request weapon", [](Run &r) { set_u32(A_CONTEXT, row_offset(r.request_row) + 8, WEAPON_B); }});
  b.push_back({"request body", [](Run &r) { set_u32(A_CONTEXT, row_offset(r.request_row) + 12, 0x400009); }});
  b.push_back({"ring head 32", [](Run &) { set_u32(A_CONTEXT, 0x1F8, 32); }}); b.push_back({"ring tail 32", [](Run &) { set_u32(A_CONTEXT, 0x1FC, 32); }});
  b.push_back({"ring moved on", [](Run &) { set_u32(A_CONTEXT, 0x1FC, 5); }}); b.push_back({"ring empty", [](Run &) { set_u32(A_CONTEXT, 0x1F8, 9); set_u32(A_CONTEXT, 0x1FC, 9); }});
  b.push_back({"ring empty, idle record ours", [](Run &r) { set_u32(A_CONTEXT, 0x1F8, 9); set_u32(A_CONTEXT, 0x1FC, 9); r.row(IDLE_ROW, r.request_row == 0 ? 4 : 6, AVATAR, WEAPON, BODY); r.lens(IDLE_ROW, 1.1f); r.request_row = IDLE_ROW; }});
  b.push_back({"ring wraps", [](Run &r) { const uint32_t kind = r.request_row == 0 ? 4 : 6; r.ring(30, 1); r.row(30, 5, AVATAR, 0, BODY); r.row(31, 4, 0x999, 0, BODY); r.row(0, kind, AVATAR, WEAPON, BODY); r.lens(0, 1.1f); r.request_row = 0; }});
  b.push_back({"ring wraps, first normal request is ours", [](Run &r) { const uint32_t kind = r.request_row == 0 ? 4 : 6; r.ring(30, 1); r.row(30, 5, AVATAR, 0, BODY); r.row(31, 4, AVATAR, 0, BODY); r.lens(31, .7f); r.row(0, kind, AVATAR, WEAPON, BODY); r.lens(0, 1.1f); r.request_row = 0; }});
  for (float d : {.5f, .99f, 1.0f, 179.0f, 179.01f, 0.0f, -90.0f}) world("setting", A_S_FOV, 0, float_value(d));
  world("setting NaN", A_S_FOV, 0, raw_value(0x7fc00000)); world("setting inf", A_S_FOV, 0, raw_value(0x7f800000)); world("settings pointer null", A_G_STATE, 0, raw_value(0, 8));
  for (uint32_t bits : {0x7fc00000u, 0x7f800000u, 0u, 0x80000000u, 0xbf800000u, 0x40490fdbu, 0x40490fdau, 0x00000001u, 0x40800000u})
    b.push_back({"request lens field", [=](Run &r) { r.lens_bits(r.request_row, bits); }});
  b.push_back({"request records read-only", [](Run &) { protect_region(A_CONTEXT, PROT_READ); }});
  b.push_back({"slot table read-only", [](Run &) { protect_region(A_API, PROT_READ); }});
  b.push_back({"output read-only after the first write", [](Run &) { write_fail_after = 1; }});
  return b;
}
static void script_refusals(Run &r, int part, int parts) {
  now(5);
  const std::vector<Break> list = breaks();
  // The unaligned table of "api unaligned": its getter and setter slots hold the right values.
  for (int w = 0; w < 2; ++w) { World::put<uint64_t>(W[w].a[A_API_OTHER] + 0x804 + 0x78, W[w].a[A_EXE] + W[w].L->getter); World::put<uint64_t>(W[w].a[A_API_OTHER] + 0x804 + 0x80, W[w].native_value); }
  auto settle = [&] {
    protect_region(A_CONTEXT, PROT_READ | PROT_WRITE); protect_region(A_API, PROT_READ | PROT_WRITE);
    if (::reserved.load()) { repair(r); r.end(r.gen); }
    if (::reserved.load()) { r.slot_holds(SLOT_HELPER); r.end(r.gen); }
    if (::reserved.load()) fail("the refusals script could not end its lease");
    repair(r); r.slot_holds(SLOT_NATIVE);
  };
  const std::string base_name = script_name;
  for (size_t i = 0; i < list.size(); ++i) {
    if (static_cast<int>(i % parts) != part) continue;
    const Break &b = list[i];
    script_name = base_name + " [" + b.name + "]";
    // the weapon camera at the hip: validate, begin
    r.reserved_ms = 0; r.fp(); b.on(r); r.validate(); r.begin(); settle();
    r.reserved_ms = 180; r.fp(); b.on(r); r.begin(); if (r.result == 1) { tick(.016); r.frame(.9f); } settle();
    // the third-person camera: validate (accepts it), and the fird policy's validate and begin
    r.reserved_ms = 0; r.normal(); b.on(r); r.validate(); r.validate_fird(); r.begin(); settle();
    r.reserved_ms = 0; r.normal(); b.on(r); r.begin_fird(CAPTURED); if (r.result == 1) { tick(.016); r.frame(.9f); } settle();
  }
  script_name = base_name;
  if (part != 0) return;
  // lenses the fird policy cannot capture
  for (float captured : {10 * RAD, 170 * RAD, 9.99f * RAD, 170.01f * RAD, NAN, INFINITY, 0.0f, -1.0f}) { r.normal(); r.begin_fird(captured); settle(); }
  // busy: a second lease while one is held, in every flavour
  arm(r, 0); r.begin(); need(NADSF_Error() == BUSY, "busy"); r.validate(); r.normal(); r.begin_fird(CAPTURED); r.validate_fird(); r.fp(); r.end(r.gen);
}

// ---- the dispatch slot ----
static void script_slot(Run &r, int v) {
  now(7); r.fp();
  switch (v) {
  case 0: // the swap faults at begin
    fixture_cas_fail_at = fixture_cas_calls + 1; r.begin(); need(r.result == 0 && NADSF_Error() == CAS_FAULT, "swap fault at begin"); fixture_cas_fail_at = 0; r.begin(); r.end(r.gen); break;
  case 1: // a foreign value arrives between the proof and the swap
    fixture_foreign_value = SLOT_FOREIGN; fixture_foreign_cas_at = fixture_cas_calls + 1; r.begin(); need(r.result == 0 && NADSF_Error() == CAS_MISMATCH, "foreign value at the swap");
    fixture_foreign_cas_at = 0; r.begin(); r.slot_holds(SLOT_NATIVE); r.begin(); frames(r, 2, .016, .9f); r.end(r.gen); break;
  case 2: // a foreign value in the slot when the lease ends: left alone
    arm(r, 0); r.slot_holds(SLOT_FOREIGN); r.end(r.gen); need(r.result == 1, "end with a foreign slot"); r.end(r.gen); r.frame(.9f); r.slot_holds(SLOT_NATIVE); r.begin(); r.end(r.gen); break;
  case 3: // the restoring swap faults: cleanup stays pending, a second end completes it
    arm(r, 0); fixture_cas_fail_at = fixture_cas_calls + 1; r.end(r.gen); need(r.result == 0 && NADSF_Error() == CLEANUP_PENDING, "pending cleanup");
    r.frame(.9f); r.begin(); r.validate(); r.read_stats(r.gen); r.bind(r.gen, 1, WEAPON); r.transition(r.gen, 1, .5f, 0);
    fixture_cas_fail_at = 0; r.end(r.gen); need(r.result == 1, "second end"); r.end(r.gen); break;
  case 4: // the slot cannot be read, then is not writable, when the lease ends
    arm(r, 0); protect_region(A_API, PROT_NONE); r.end(r.gen); need(NADSF_Error() == CLEANUP_PENDING, "unreadable slot at the end"); r.frame(.9f);
    protect_region(A_API, PROT_READ | PROT_WRITE); fixture_slot_readonly = true; r.end(r.gen); need(NADSF_Error() == CLEANUP_PENDING, "read-only slot at the end");
    fixture_slot_readonly = false; r.end(r.gen); need(r.result == 1, "end"); break;
  case 5: // the game's setter changes the world during begin's own call: begin fails and restores
    forward_pokes.push_back({1, {{A_CONTROLLER, 0}, raw_value(1)}}); r.begin(); need(r.result == 0 && NADSF_Error() == OWNER_MISMATCH, "objection during begin"); r.controls(); r.begin(); r.end(r.gen);
    forward_pokes.push_back({1, {{A_DESCRIPTOR, 8}, raw_value(0x999)}}); r.begin(); set_u32(A_DESCRIPTOR, 8, AVATAR);
    forward_pokes.push_back({1, {{A_CONTEXT, row_offset(1) + 8}, raw_value(WEAPON_B)}}); r.begin(); r.row(1, 6, AVATAR, WEAPON, BODY);
    forward_pokes.push_back({1, {{A_API, 0x80}, slot_value(SLOT_FOREIGN)}}); r.begin(); r.slot_holds(SLOT_NATIVE);
    forward_pokes.push_back({1, {{A_CONTROLLER, 4}, raw_value(1, 1)}}); r.begin(); need(r.result == 1, "full sights during begin is no objection"); r.end(r.gen); r.aim(false); break;
  case 6: // the same, and the restoring swap faults too: begin answers 2 and the lease must be ended
    forward_pokes.push_back({1, {{A_CONTROLLER, 0}, raw_value(1)}}); fixture_cas_fail_at = fixture_cas_calls + 2; r.begin();
    need(r.result == 2 && NADSF_Error() == CLEANUP_PENDING && r.gen != 0, "begin answers 2"); fixture_cas_fail_at = 0; r.controls();
    r.frame(.9f); r.begin(); r.read_stats(r.gen); r.end(r.gen + 1); r.end(r.gen); need(r.result == 1, "end after 2"); r.begin(); frames(r, 2, .016, .9f); r.end(r.gen); break;
  case 7: // pinning fails; the generation cannot be written
    shim_pin_fails = true; r.begin(); need(NADSF_Error() == PIN_FAILED, "pin"); shim_pin_fails = false;
    r.generation_null = true; r.begin(); need(NADSF_Error() == INVALID_CONFIG, "generation null"); r.generation_null = false;
    write_fail_after = 1; r.begin(); need(r.result == 0 && NADSF_Error() == INVALID_CONFIG, "second generation write fails"); r.begin(); need(r.result == 1 && r.gen == 2, "a generation was consumed"); r.end(r.gen); break;
  case 8: // the slot is not writable right before the swap
    arm(r, 0); r.end(r.gen); fixture_slot_readonly = true; r.begin(); need(NADSF_Error() == SLOT_NOT_WRITABLE, "slot read-only"); fixture_slot_readonly = false; r.begin(); r.end(r.gen); break;
  case 9: // frames while someone else's value sits in the slot; frames through the helper after the end
    arm(r, 180); frames(r, 3, .016, .9f); r.slot_holds(SLOT_FOREIGN); r.frame(.9f); need(!::armed.load() && NADSF_Error() == API_MISMATCH, "disarmed");
    frames(r, 2, .016, .9f); r.slot_holds(SLOT_HELPER); frames(r, 2, .016, .9f); r.transition(r.gen, 1, 0, 0); r.end(r.gen); frames(r, 2, .016, .9f); break;
  case 10: // the table pointer or the getter changes under an armed helper
    arm(r, 0); set({A_API_ROOT, 0x20}, address_value(A_API_OTHER)); r.frame(.9f); need(NADSF_Error() == API_MISMATCH, "table moved"); set({A_API_ROOT, 0x20}, address_value(A_API));
    r.end(r.gen); r.begin(); set({A_API, 0x78}, address_value(A_E_SETTER)); r.frame(.9f); r.end(r.gen); need(r.result == 1, "end with the getter changed");
    r.begin(); for (int w = 0; w < 2; ++w) W[w].write_static(); r.begin(); r.end(r.gen); break;
  case 11: // someone restored the game's setter under the lease; then the lease ends
    arm(r, 0); r.slot_holds(SLOT_NATIVE); r.frame(.9f); r.end(r.gen); r.begin(); r.end(r.gen); break;
  }
}

// ---- the FirdPerson policy: a captured lens held on the third-person camera ----
static void fird_frames(Run &r, int count, int field_row, float native) {
  for (int i = 0; i < count; ++i) { tick(.016); if (i % 3 != 2) r.lens(field_row, native); r.frame(native); } // every third frame the game returned early
}
static void script_fird(Run &r, int v) {
  now(40);
  switch (v) {
  case 0: // plain: held, then restored at the end
    r.normal(); r.validate_fird(); arm_fird(r); need(r.got.ledger == 1 && r.got.immediate == 1, "ledger after begin"); fird_frames(r, 30, 0, 1.3f);
    need(r.got.replaced == 30, "fird frames replaced"); r.lens(0, 1.3f); tick(.016); r.frame(1.3f); r.end(r.gen); need(r.result == 1, "end"); r.frame(1.3f); r.end(r.gen); break;
  case 1: // the end finds the game's value, an equal value, a vanished request, a changed header
    arm_fird(r); r.lens(0, 1.25f); r.end(r.gen);
    arm_fird(r); tick(.016); r.frame(1.3f); r.end(r.gen);                 // the game returned early: nothing to restore
    arm_fird(r); r.ring(0, 0); r.end(r.gen);
    arm_fird(r); r.ring(1, 3); r.end(r.gen);
    arm_fird(r); r.row(0, 4, AVATAR, 7, BODY); r.end(r.gen);
    arm_fird(r); r.row(0, 6, AVATAR, 0, BODY); r.end(r.gen);
    arm_fird(r); set_u32(A_DESCRIPTOR, 8, 0x999); r.end(r.gen); set_u32(A_DESCRIPTOR, 8, AVATAR);
    arm_fird(r, CAPTURED); r.end(r.gen);                                  // the field already held the captured lens
    arm_fird(r); r.controls(1); r.end(r.gen); r.controls(); break;        // an objection does not stop the restore
  case 2: // several requests: the input source is the first normal one from the head, not the bound top
    r.normal(); r.ring(0, 3); r.row(0, 5, AVATAR, 0, BODY); r.row(1, 4, AVATAR, 0, BODY); r.row(2, 4, AVATAR, 0, BODY); r.lens(1, 1.2f); r.lens(2, 1.4f); r.request_row = 2;
    r.validate_fird(); r.begin_fird(CAPTURED); need(r.result == 1, "begin"); fird_frames(r, 6, 1, 1.3f);
    r.row(0, 4, AVATAR, 0, BODY); r.lens(0, 1.1f); fird_frames(r, 6, 0, 1.3f);          // the source moves to an older row: the old ledger is retired, not replayed
    r.row(0, 4, 0x999, 0, BODY); tick(.016); r.frame(1.3f); need(NADSF_Error() == REQUEST_SOURCE_MISMATCH && !::armed.load(), "foreign first request"); r.end(r.gen); break;
  case 3: // the game's own aim camera and full sights are forwarded; back again
    arm_fird(r); fird_frames(r, 4, 0, 1.3f); r.ring(0, 2); r.row(1, 6, AVATAR, WEAPON, BODY); frames(r, 4, .016, .35f); need(r.got.ads == 4, "aim camera forwarded");
    r.ring(0, 1); fird_frames(r, 4, 0, 1.3f); r.aim(true); frames(r, 3, .016, .35f); r.aim(false); fird_frames(r, 4, 0, 1.3f);
    r.ring(0, 2); r.row(1, 6, 0x999, WEAPON, BODY); frames(r, 2, .016, .35f); r.ring(0, 1); fird_frames(r, 3, 0, 1.3f);
    r.ring(0, 2); r.row(1, 5, AVATAR, WEAPON, BODY); frames(r, 2, .016, .35f); r.ring(0, 1); r.aim(true); r.ring(0, 0); frames(r, 2, .016, .35f); r.aim(false); r.ring(0, 1); fird_frames(r, 3, 0, 1.3f);
    r.end(r.gen); break;
  case 4: // a field the policy cannot own: not finite, not positive, too wide, not writable
    for (uint32_t bits : {0x7fc00000u, 0u, 0x80000000u, 0x40490fdbu}) {
      arm_fird(r); fird_frames(r, 2, 0, 1.3f); r.lens_bits(0, bits); tick(.016); r.frame(1.3f); need(NADSF_Error() == REQUEST_FIELD_INVALID && !::armed.load(), "invalid field"); r.frame(1.3f); r.end(r.gen);
    }
    arm_fird(r); r.lens(0, 1.3f); protect_region(A_CONTEXT, PROT_READ); tick(.016); r.frame(1.3f); need(NADSF_Error() == REQUEST_FIELD_INVALID, "read-only field");
    r.end(r.gen); protect_region(A_CONTEXT, PROT_READ | PROT_WRITE);
    arm_fird(r); protect_region(A_CONTEXT, PROT_READ);
    r.end(r.gen); need(r.result == 0 && NADSF_Error() == CLEANUP_PENDING, "the restore waits for a writable field"); r.begin_fird(CAPTURED); r.frame(1.3f); protect_region(A_CONTEXT, PROT_READ | PROT_WRITE);
    r.end(r.gen); need(r.result == 1, "end"); break;
  case 5: // objections; a foreign camera; a foreign caller
    arm_fird(r); fird_frames(r, 2, 0, 1.3f); r.controls(1); frames(r, 2, .016, 1.3f); r.controls(); fird_frames(r, 2, 0, 1.3f);
    r.seated(true); frames(r, 2, .016, 1.3f); r.seated(false); r.controls(0, false, true); frames(r, 1, .016, 1.3f); r.controls(2); fird_frames(r, 3, 0, 1.3f);
    r.camera_argument_delta = 16; frames(r, 2, .016, 1.3f); r.camera_argument_delta = 0; r.caller_ours = false; frames(r, 2, .016, 1.3f); r.caller_ours = true;
    r.ring(0, 3); frames(r, 2, .016, 1.3f); r.ring(0, 1); fird_frames(r, 3, 0, 1.3f); r.end(r.gen); break;
  case 6: // bind to a new request; the old source is restored by the bind
    arm_fird(r); fird_frames(r, 2, 0, 1.3f); r.lens(0, 1.3f); tick(.016); r.frame(1.3f);
    r.ring(1, 2); r.row(1, 4, AVATAR, 0, BODY); r.lens(1, 1.5f);
    r.bind_fird(0, 1); r.bind_fird(r.gen + 1, 1); r.bind_fird(r.gen, 1, 0, true, 65535); r.bind_fird(r.gen, 0); r.bind_fird(r.gen, 1, 8); r.bind(r.gen, 1, WEAPON);
    r.transition(r.gen, 1, .5f, 0); need(NADSF_Error() == OWNER_MISMATCH, "no packet for the fird policy");
    r.aim(true); r.bind_fird(r.gen, 1); r.aim(false); r.controls(1); r.bind_fird(r.gen, 1); r.controls();
    set_u8(A_G_FIRD0 + 3, 2, 0); r.bind_fird(r.gen, 1); need(NADSF_Error() == API_MISMATCH, "bind with changed input code"); for (int w = 0; w < 2; ++w) W[w].write_static();
    r.bind_fird(r.gen, 1); need(r.result == 1 && r.got.binds == 1, "bind_fird"); fird_frames(r, 4, 1, 1.3f);
    // the old row comes back into the ring as the first normal request, and the bind after it must restore the newer one
    r.ring(0, 2); r.row(0, 4, AVATAR, 0, BODY); r.bind_fird(r.gen, 1); need(r.result == 1, "bind_fird again"); fird_frames(r, 3, 0, 1.3f);
    // a bind that fails after its own apply disarms
    r.ring(1, 2); r.lens(1, 1.3f); forward_pokes.push_back({1, {{A_CONTROLLER, 0}, raw_value(1)}}); r.bind_fird(r.gen, 1); need(r.result == 0 && !::armed.load(), "failed bind_fird disarms"); r.controls();
    r.bind_fird(r.gen, 1); r.end(r.gen); break;
  case 7: // the idle record as the request
    r.normal(); r.ring(4, 4); r.row(IDLE_ROW, 4, AVATAR, 0, BODY); r.lens(IDLE_ROW, 1.2f); r.request_row = IDLE_ROW; r.validate_fird(); r.begin_fird(CAPTURED); need(r.result == 1, "begin on the idle record");
    fird_frames(r, 5, IDLE_ROW, 1.3f); r.lens(IDLE_ROW, 1.3f); tick(.016); r.frame(1.3f); r.end(r.gen); need(r.result == 1, "end"); break;
  case 8: // begin fails after its own data write: the field is restored by the failed begin
    r.normal(); r.lens(0, 1.2f); forward_pokes.push_back({1, {{A_CONTROLLER, 0}, raw_value(1)}}); r.begin_fird(CAPTURED); need(r.result == 0, "failed begin"); r.controls();
    r.normal(); r.lens(0, 1.2f); forward_pokes.push_back({1, {{A_CONTROLLER, 4}, raw_value(1, 1)}}); r.begin_fird(CAPTURED); need(r.result == 0, "full sights during a fird begin"); r.aim(false);
    r.normal(); r.lens(0, 1.2f); forward_pokes.push_back({1, {{A_CONTROLLER, 0}, raw_value(1)}}); protect_region(A_CONTEXT, PROT_READ); r.begin_fird(CAPTURED); protect_region(A_CONTEXT, PROT_READ | PROT_WRITE); r.controls();
    r.normal(); r.lens(0, 1.2f); forward_pokes.push_back({1, {{A_CONTEXT, 0x1FC}, raw_value(0)}}); r.begin_fird(CAPTURED); r.ring(0, 1);
    if (::reserved.load()) r.end(r.gen);
    arm_fird(r); r.end(r.gen); break;
  }
}

// ---- the hook's own arguments ----
static void script_arguments(Run &r) {
  now(3); r.frame(.9f); r.fp(); r.read_stats(0);
  arm(r, 180); r.caller_ours = false; frames(r, 3, .016, .9f); r.caller_ours = true; r.camera_argument_delta = 8; frames(r, 3, .016, .9f); r.caller_ours = false; frames(r, 2, .016, .9f);
  r.caller_ours = true; r.camera_argument_delta = 0; frames(r, 3, .016, .9f);
  r.camera_argument_delta = 8; r.slot_holds(SLOT_FOREIGN); r.frame(.9f); need(::armed.load() != 0, "a foreign camera proves nothing"); r.slot_holds(SLOT_HELPER); r.camera_argument_delta = 0;
  r.end(r.gen); frames(r, 3, .016, .9f); r.caller_ours = false; frames(r, 2, .016, .9f);
}

// ---- the owner is lost under an armed helper ----
struct Loss { const char *name; Sym at; Value bad; };
static std::vector<Loss> losses() {
  return {
      {"context pointer", {A_G_CONTEXT, 0}, address_value(A_SPARE)}, {"camera pointer", {A_CONTEXT, 0}, address_value(A_SPARE)}, {"owner pointer", {A_G_OWNER, 0}, address_value(A_SPARE)},
      {"controller moved", {A_G_CONTROLLER, 0}, address_value(A_SPARE, 0x1000)}, {"controller null", {A_G_CONTROLLER, 0}, raw_value(0, 8)}, {"count", {A_OWNER, 0x6C}, raw_value(0)},
      {"descriptor moved", {A_OWNER, 0x110}, address_value(A_SPARE, 0x2000)}, {"descriptor avatar", {A_DESCRIPTOR, 8}, raw_value(0x999)}, {"descriptor body", {A_DESCRIPTOR, 12}, raw_value(0x400009)},
      {"descriptor other bytes", {A_DESCRIPTOR, 16}, raw_value(7)}, {"registry moved", {A_E_REGISTRY, 0}, address_value(A_SPARE, 0x2800)}, {"capacity", {A_REGISTRY, 0x98}, raw_value(3)},
      {"generations moved", {A_REGISTRY, 0xA0}, address_value(A_SPARE, 0x2400)}, {"generation byte", {A_GENERATIONS, 3}, raw_value(2, 1)}, {"units moved", {A_REGISTRY, 0x88}, address_value(A_SPARE, 0x2600)},
      {"unit object", {A_UNITS, 24}, address_value(A_BODY1)}, {"unit null", {A_UNITS, 24}, raw_value(0, 8)},
      {"table moved", {A_API_ROOT, 0x20}, address_value(A_API_OTHER)}, {"getter", {A_API, 0x78}, raw_value(0x10000, 8)}, {"slot foreign", {A_API, 0x80}, slot_value(SLOT_FOREIGN)},
      {"api root", {A_G_API_ROOT, 0}, raw_value(8, 8)}};
}
static void script_owner_lost(Run &r, int which, int state) {
  now(60);
  // The moved copies are valid where they are: only the pointer differs from the one captured.
  for (int w = 0; w < 2; ++w) {
    std::memcpy(reinterpret_cast<void *>(W[w].a[A_SPARE] + 0x2000), reinterpret_cast<void *>(W[w].a[A_DESCRIPTOR]), 20);
    std::memcpy(reinterpret_cast<void *>(W[w].a[A_SPARE] + 0x2400), reinterpret_cast<void *>(W[w].a[A_GENERATIONS]), 16);
    std::memcpy(reinterpret_cast<void *>(W[w].a[A_SPARE] + 0x2600), reinterpret_cast<void *>(W[w].a[A_UNITS]), 128);
    std::memcpy(reinterpret_cast<void *>(W[w].a[A_SPARE] + 0x2800), reinterpret_cast<void *>(W[w].a[A_REGISTRY]), 0x100);
  }
  const Loss loss = losses()[which];
  if (state == 2) { arm_fird(r); fird_frames(r, 3, 0, 1.3f); r.lens(0, 1.3f); }
  else { arm(r, state == 1 ? 180 : 0); frames(r, 3, .016, .9f); if (state == 1) { r.aim(true); frames(r, 3, .016, .35f); } }
  set(loss.at, loss.bad); tick(.016); r.frame(state == 2 ? 1.3f : .9f);
  need(!::armed.load() && (NADSF_Error() == OWNER_MISMATCH || NADSF_Error() == API_MISMATCH), "disarmed by the loss");
  r.frame(.9f); r.transition(r.gen, 1, .5f, 0); r.bind(r.gen, r.request_row, WEAPON); r.bind_fird(r.gen, r.request_row); r.validate(); r.read_stats(r.gen);
  r.end(r.gen);                                                    // while the owner is still lost
  for (int w = 0; w < 2; ++w) W[w].write_static();
  if (::reserved.load()) { r.slot_holds(SLOT_HELPER); r.end(r.gen); }
  r.slot_holds(SLOT_NATIVE); r.aim(false);
  if (state == 2) { arm_fird(r); fird_frames(r, 2, 0, 1.3f); } else { arm(r, 0); frames(r, 2, .016, .9f); }
  r.end(r.gen);
}

// ---- control calls in right and wrong orders ----
static void script_controls(Run &r) {
  now(9); r.fp();
  r.end(0); r.end(1); r.read_stats(0); r.read_stats(1); r.read_stats(0, 87); r.read_stats(0, 88, 2); r.read_stats(0, 96); r.stats_nowhere(0); r.stats_nowhere(1);
  r.bind(1, 1, WEAPON); r.bind_fird(1, 1); r.transition(1, 1, .5f, 0); r.validate(); r.validate();
  arm(r, 0); const uint64_t first = r.gen; need(first == 1, "first generation");
  r.read_stats(0); r.read_stats(first); r.read_stats(first + 1); r.read_stats(first, 87); r.stats_nowhere(first); r.stats_nowhere(0);
  r.transition(first, 1, .5f, 0); need(NADSF_Error() == OWNER_MISMATCH, "no packet without the opt-in ease");
  r.end(0); r.end(first + 1); r.end(first); r.end(first); r.end(first + 1); r.end(0); r.read_stats(first); r.read_stats(0);
  r.reserved_ms = 180; r.begin(); const uint64_t second = r.gen; need(second == 2, "second generation");
  r.end(first); need(NADSF_Error() == TOKEN_MISMATCH, "the retired generation while another is held"); r.transition(first, 1, .5f, 0); r.bind(first, 1, WEAPON);
  frames(r, 3, .016, .9f); r.transition(second, 1, .5f, 0); r.end(second); r.end(first); r.end(second);
  r.normal(); r.reserved_ms = 0; r.begin_fird(CAPTURED); const uint64_t third = r.gen; need(third == 3, "third generation"); r.bind(third, 0, WEAPON); r.transition(third, 1, 0, 0); r.read_stats(third);
  r.end(third); r.end(third); r.end(second);
  // a begin that fails before the reservation leaves the retired generation valid
  r.fp(); set_u32(A_CONTROLLER, 0, 1); r.begin(); r.controls(); r.end(third); need(r.result == 1, "retired generation still ends");
  r.reserved_ms = 180; r.begin_fird(CAPTURED); need(NADSF_Error() == INVALID_CONFIG, "no ease for the fird policy");
}

// ---- seeded random walks mixing all of it ----
static void script_walk(Run &r, uint32_t seed, int steps) {
  std::mt19937 rng(seed); auto pick = [&](uint32_t n) { return static_cast<uint32_t>(rng() % n); };
  now(1000); r.fp();
  uint64_t epoch = 1; float progress = 0; bool aiming = false, shouldered = false; int clock_bad = 0, damaged = 0; Sym damage_at{0, 0};
  const float lenses[] = {.9f, .9f, .9f, .35f, .35f, .30f, 1.2f, 1.3f, .6f, CAPTURED, .05f, 3.0f};
  const float odd[] = {NAN, 0.0f, 3.2f, -1.0f, INFINITY, 3.14159265f};
  const float settings[] = {60, 75.5f, 90, 90, 110, 179, 1, .5f, NAN};
  const uint64_t reserves[] = {0, 180, 180, 80, 500};
  auto refresh_rows = [&] {
    const uint32_t tail = 1 + pick(4); r.ring(pick(8) == 0 ? tail : 0, tail);
    for (uint32_t i = 0; i < tail; ++i) r.row(static_cast<int>(i), pick(3) == 0 ? 4 : pick(6) == 0 ? 5 : 6, pick(12) == 0 ? 0x999 : AVATAR, pick(4) == 0 ? WEAPON_B : WEAPON, pick(15) == 0 ? 0x400009 : BODY);
    r.row(IDLE_ROW, pick(2) ? 4 : 6, AVATAR, WEAPON, BODY);
  };
  for (int i = 0; i < steps; ++i) {
    switch (pick(24)) { case 0: tick(.3); break; case 1: tick(-.02); break; case 2: break; case 3: tick(.12); break; case 4: tick(1.0 / 30); break; case 5: tick(.25); break; default: tick(1.0 / 60 + pick(5) * .002); }
    if (clock_bad && !--clock_bad) fixture_clock_ok = true;
    if (damaged && !--damaged) { for (int w = 0; w < 2; ++w) W[w].write_static(); }
    const bool held = ::reserved.load() != 0, fird = held && ::policy.fird;
    const uint32_t action = pick(100);
    if (action < 52) {
      if (fird && pick(3)) { uint32_t head, tail; std::memcpy(&head, reinterpret_cast<void *>(W[0].a[A_CONTEXT] + 0x1F8), 4); std::memcpy(&tail, reinterpret_cast<void *>(W[0].a[A_CONTEXT] + 0x1FC), 4);
        r.lens(head == tail ? IDLE_ROW : static_cast<int>(pick(4)), lenses[pick(12)]); }
      r.frame(pick(25) == 0 ? odd[pick(6)] : lenses[pick(12)]);
    } else if (action < 58) { aiming = !aiming; r.aim(aiming); }
    else if (action < 64) { shouldered = !shouldered; if (pick(6) == 0) set_u8(r.flags_id(), 10, shouldered ? 4 : 0); else r.shoulder(shouldered); }
    else if (action < 69) {
      switch (pick(6)) {
      case 0: refresh_rows(); break;
      case 1: r.fp(); r.aim(aiming); r.shoulder(shouldered); break;
      case 2: r.normal(); r.lens(0, lenses[pick(12)]); r.aim(aiming); r.shoulder(shouldered); break;
      case 3: set_u32(A_CONTEXT, row_offset(r.request_row) + 8, pick(2) ? WEAPON_B : WEAPON); break;
      case 4: set_u32(A_CONTEXT, row_offset(r.request_row), pick(2) ? 4 : 6); break;
      default: r.ring(0, 1 + pick(3)); break;
      }
    } else if (action < 72) {
      switch (pick(5)) { case 0: set_u32(A_CONTROLLER, 0, 2); break; case 1: set_u32(A_CONTROLLER, 0, pick(2) ? 1 : 0x20); break; case 2: set_u8(A_CONTROLLER, 5, static_cast<uint8_t>(pick(2))); break;
                         case 3: r.seated(pick(2) != 0); break; default: r.controls(0, aiming, false); r.seated(false); break; }
    } else if (action < 75) r.degrees(settings[pick(9)]);
    else if (action < 82) {
      if (pick(4) == 0) { ++epoch; progress = 0; } else progress = std::min(1.0f, progress + pick(5) * .11f);
      r.transition(pick(30) == 0 ? r.gen + 1 : r.gen, pick(20) == 0 ? epoch - 1 : epoch, pick(30) == 0 ? progress - .2f : progress, pick(8) == 0 ? !aiming : aiming);
    } else if (action < 86) {
      uint32_t tail; std::memcpy(&tail, reinterpret_cast<void *>(W[0].a[A_CONTEXT] + 0x1FC), 4); uint32_t head; std::memcpy(&head, reinterpret_cast<void *>(W[0].a[A_CONTEXT] + 0x1F8), 4);
      const int top_row = head == tail ? IDLE_ROW : static_cast<int>((tail + 31) % 32);
      if (fird || pick(8) == 0) r.bind_fird(r.gen, pick(6) == 0 ? 0 : top_row); else r.bind(r.gen, pick(8) == 0 ? 0 : top_row, pick(3) ? WEAPON : WEAPON_B);
    } else if (action < 89) r.end(pick(10) == 0 ? r.gen + 1 : r.gen);
    else if (action < 94) {
      uint32_t tail; std::memcpy(&tail, reinterpret_cast<void *>(W[0].a[A_CONTEXT] + 0x1FC), 4); uint32_t head; std::memcpy(&head, reinterpret_cast<void *>(W[0].a[A_CONTEXT] + 0x1F8), 4);
      if (pick(3) == 0) { if (pick(4) == 0) r.fp(); else r.normal(); r.aim(aiming); r.shoulder(shouldered); }
      else r.request_row = head == tail ? IDLE_ROW : static_cast<int>((tail + 31) % 32);
      r.weapon = pick(5) == 0 ? WEAPON_B : WEAPON; r.reserved_ms = reserves[pick(5)];
      if (pick(4) == 0) { r.reserved_ms = 0; r.begin_fird(pick(10) == 0 ? .1f : CAPTURED); } else r.begin();
    } else if (action < 95) { if (pick(2)) r.validate(); else r.validate_fird(); }
    else if (action < 96) r.read_stats(pick(3) == 0 ? r.gen + 1 : held ? r.gen : 0, pick(8) == 0 ? 80 : 88);
    else if (action < 97) { fixture_clock_ok = false; clock_bad = 1 + static_cast<int>(pick(2)); }
    else if (action < 98) r.slot_holds(pick(3) == 0 ? SLOT_FOREIGN : pick(2) ? SLOT_NATIVE : held ? SLOT_HELPER : SLOT_NATIVE);
    else if (action < 99) {
      static const Loss damage[] = {{"", {A_DESCRIPTOR, 8}, raw_value(0x999)}, {"", {A_GENERATIONS, 3}, raw_value(2, 1)}, {"", {A_API, 0x78}, raw_value(0x10000, 8)},
                                    {"", {A_G_CONTROLLER, 0}, raw_value(0, 8)}, {"", {A_G_CALL, 0}, raw_value(0, 1)}, {"", {A_EXE, 136}, raw_value(5)}, {"", {A_G_FIRD0 + 5, 3}, raw_value(0, 1)}};
      const Loss &d = damage[pick(7)]; set(d.at, d.bad); damaged = 1 + static_cast<int>(pick(3)); damage_at = d.at;
    } else { if (pick(2)) fixture_cas_fail_at = fixture_cas_calls + 1 + pick(2); else { fixture_foreign_value = SLOT_FOREIGN; fixture_foreign_cas_at = fixture_cas_calls + 1; } }
  }
  (void)damage_at;
  fixture_clock_ok = true; fixture_cas_fail_at = 0; fixture_foreign_cas_at = 0; for (int w = 0; w < 2; ++w) W[w].write_static();
  if (::reserved.load()) { r.end(r.gen); if (!r.result) { r.slot_holds(SLOT_HELPER); r.end(r.gen); } }
}

// ---- what only the translation checks (the reference is the fixture build, which compiles these out) ----
static void script_translation_only(Run &r) {
  now(11); r.fp();
  auto alone = [&](const char *what, uint32_t expected_result, uint32_t expected_error, bool begin) {
    call_text = what; events[1].clear(); const uint32_t cas_before = translation_cas_calls;
    for (int w = 0; w < 2; ++w) { r.write_config(w); World::put<uint64_t>(W[w].a[A_IO] + IO_GENERATION, UINT64_MAX); }
    const uint32_t got = begin ? tfp_fov_begin(reinterpret_cast<TfpFovConfig *>(W[1].a[A_IO] + IO_CONFIG), reinterpret_cast<uint64_t *>(W[1].a[A_IO] + IO_GENERATION))
                               : tfp_fov_validate(reinterpret_cast<TfpFovConfig *>(W[1].a[A_IO] + IO_CONFIG));
    uint64_t generation; std::memcpy(&generation, reinterpret_cast<void *>(W[1].a[A_IO] + IO_GENERATION), 8);
    uint64_t in_slot; std::memcpy(&in_slot, reinterpret_cast<void *>(W[1].slot()), 8);
    if (got != expected_result || tfp_fov_error() != expected_error || (begin && generation != 0) || in_slot != W[1].native_value || translation_cas_calls != cas_before || lock_depth[0] || lock_depth[1])
      fail("translation only: expected %u with error %u, generation 0, slot untouched; got %u with error %u, generation %llu", expected_result, expected_error, got, tfp_fov_error(), (unsigned long long)generation);
    ++counted.translation_only;
  };
  // cpp:495: the getter must be executable (non-fixture path) -> API_MISMATCH (6).
  answer_getter_executable = false; alone("validate, getter not executable", 0, API_MISMATCH, false); alone("begin, getter not executable", 0, API_MISMATCH, true); answer_getter_executable = true;
  // cpp:647-649: the executable is pinned too -> PIN_FAILED (9), before anything is reserved.
  answer_pin_executable = false; alone("begin, executable not pinned", 0, PIN_FAILED, true); answer_pin_executable = true;
  // Nothing was consumed: the compared lease that follows gets generation 1 in both, with the error the original has now.
  ::last_error.store(PIN_FAILED);
  r.begin(); need(r.result == 1 && r.gen == 1, "generation 1 after the translation-only refusals"); frames(r, 3, .016, .9f); r.end(r.gen);
}
static void script_reset(Run &r) {
  now(12); arm(r, 180); frames(r, 3, .016, .9f); r.aim(true); frames(r, 3, .016, .35f); r.transition(r.gen, 4, .5f, 1); r.frame(.35f); r.end(r.gen);
  r.normal(); r.begin_fird(CAPTURED); tick(.016); r.frame(1.3f); r.end(r.gen);
  // The original has no reset. The translation alone: after the reset it must behave as at start-up.
  tfp_fov_reset_for_tests();
  call_text = "translation only: after tfp_fov_reset_for_tests"; expect_null_original = true; events[1].clear();
  tfp_fov_setter_from(W[1].a[A_GAME] + HERE.call + 6, reinterpret_cast<void *>(W[1].a[A_CAMERA]), .9f);
  expect_null_original = false;
  if (events[1].size() != 1 || events[1][0].kind != EV_FORWARD || tfp_fov_error() != 0) fail("expected one forwarded call to a null original and no error");
  TfpFovStats stats{}; stats.size = sizeof stats; stats.abi = 1;
  if (tfp_fov_read_stats(0, &stats) != 1 || stats.seen != 1 || stats.forwarded != 1 || stats.replaced || stats.immediate_applies || stats.binds || stats.ads_forwarded || stats.armed || stats.installed || stats.error
      || stats.last_native != 0 || stats.last_applied != 0 || stats.source_degrees != 0) fail("unexpected statistics after the reset");
  if (tfp_fov_end(2) != 0 || tfp_fov_error() != TOKEN_MISMATCH) fail("the retired generation survived the reset");
  r.fp(); r.reserved_ms = 180; for (int w = 0; w < 2; ++w) { r.write_config(w); World::put<uint64_t>(W[w].a[A_IO] + IO_GENERATION, UINT64_MAX); }
  events[1].clear();
  if (tfp_fov_begin(reinterpret_cast<TfpFovConfig *>(W[1].a[A_IO] + IO_CONFIG), reinterpret_cast<uint64_t *>(W[1].a[A_IO] + IO_GENERATION)) != 1) fail("begin after the reset failed, error %u", tfp_fov_error());
  uint64_t generation; std::memcpy(&generation, reinterpret_cast<void *>(W[1].a[A_IO] + IO_GENERATION), 8);
  if (generation != 1) fail("after the reset begin gave generation %llu, not 1", (unsigned long long)generation);
  // The first hip lens after the reset is the setting exactly: no leftover ease or phase.
  uint32_t lens_bits = 0; for (const Event &e : events[1]) if (e.kind == EV_FORWARD) lens_bits = static_cast<uint32_t>(e.v[1]);
  const float expected = 90.0f * RAD; uint32_t expected_bits; std::memcpy(&expected_bits, &expected, 4);
  if (lens_bits != expected_bits) fail("after the reset the first lens is %08x, not the setting %08x", lens_bits, expected_bits);
  if (tfp_fov_set_transition(1, 1, .5f, 0) != 1) fail("after the reset epoch 1 was refused (error %u): a phase survived", tfp_fov_error());
  if (tfp_fov_end(1) != 1) fail("end after the reset failed");
  counted.translation_only += 6;
}

// ---- probes: one call, every read of it failing in turn, and every listed value changing before each read ----
static std::vector<Poke> standard_values(const Run &r) {
  std::vector<Poke> v; const uint64_t top = row_offset(r.request_row);
  auto value = [&](int id, uint64_t offset, Value x) { v.push_back({{id, offset}, x}); };
  value(A_CONTROLLER, 0, raw_value(1)); value(A_CONTROLLER, 0, raw_value(2)); value(A_CONTROLLER, 4, raw_value(1, 1)); value(A_CONTROLLER, 4, raw_value(0, 1)); value(A_CONTROLLER, 5, raw_value(1, 1));
  value(A_O_FLAGS0, 10, raw_value(4, 1)); value(A_O_FLAGS0, 10, raw_value(0, 1)); value(A_O_FLAGS0, 15, raw_value(32, 1)); value(A_O_FLAGS0, 14, raw_value(4, 1));
  value(A_CONTEXT, 0x1FC, raw_value(3)); value(A_CONTEXT, 0x1FC, raw_value(0)); value(A_CONTEXT, 0x1FC, raw_value(32)); value(A_CONTEXT, 0x1F8, raw_value(1)); value(A_CONTEXT, 0x1F8, raw_value(40));
  value(A_CONTEXT, top, raw_value(4)); value(A_CONTEXT, top, raw_value(6)); value(A_CONTEXT, top, raw_value(5)); value(A_CONTEXT, top + 4, raw_value(0x999));
  value(A_CONTEXT, top + 8, raw_value(WEAPON_B)); value(A_CONTEXT, top + 12, raw_value(0x400009));
  value(A_CONTEXT, top + 0x3C, raw_value(0x7fc00000)); value(A_CONTEXT, top + 0x3C, float_value(.5f));
  value(A_CONTEXT, row_offset(0), raw_value(4)); value(A_CONTEXT, row_offset(0), raw_value(6)); value(A_CONTEXT, row_offset(0) + 4, raw_value(0x999)); value(A_CONTEXT, row_offset(0) + 8, raw_value(77));
  value(A_CONTEXT, row_offset(0) + 0x3C, float_value(.7f)); value(A_CONTEXT, row_offset(0) + 0x3C, raw_value(0)); value(A_CONTEXT, row_offset(IDLE_ROW), raw_value(6));
  value(A_S_FOV, 0, float_value(100)); value(A_S_FOV, 0, raw_value(0x7fc00000)); value(A_G_STATE, 0, raw_value(0, 8));
  value(A_GENERATIONS, 3, raw_value(2, 1)); value(A_UNITS, 24, address_value(A_BODY1)); value(A_DESCRIPTOR, 8, raw_value(0x999)); value(A_DESCRIPTOR, 0, raw_value(7));
  value(A_OWNER, 0x6C, raw_value(0)); value(A_REGISTRY, 0x98, raw_value(2));
  value(A_API, 0x80, slot_value(SLOT_FOREIGN)); value(A_API, 0x80, slot_value(SLOT_NATIVE)); value(A_API, 0x80, slot_value(SLOT_HELPER));
  value(A_API, 0x78, raw_value(0x12345678, 8)); value(A_API_ROOT, 0x20, address_value(A_API_OTHER));
  value(A_G_CONTEXT, 0, address_value(A_SPARE)); value(A_CONTEXT, 0, address_value(A_SPARE)); value(A_G_CONTROLLER, 0, address_value(A_SPARE, 0x1000));
  value(A_IO, IO_STATS_SCRIPT, raw_value(87)); value(A_IO, IO_CONFIG, raw_value(79)); value(A_G_CALL, 0, raw_value(0, 1)); value(A_G_FIRD0 + 2, 0, raw_value(0, 1));
  return v;
}
static const char *const PROBES[] = {
    "hip frame", "shoulder tween frame", "frame easing into the sight", "frame easing out of the sight", "full sights passed through", "frame under a shared phase",
    "begin", "begin with the ease, in full sights", "validate", "bind", "set_transition", "end", "read_stats",
    "validate_fird", "begin_fird", "fird frame", "fird frame, the input source moved", "bind_fird with a ledger to restore", "end of a fird lease", "fird frame, aim camera forwarded",
    "begin that fails after its own setter call", "frame after the request changed (reset)", "begin_fird that fails after its own data write"};
static void probe_script(Run &r, int which) {
  now(70);
  auto values = [&] { r.probe_values = standard_values(r); };
  auto after = [&](float native) { tick(.016); r.frame(native); r.read_stats(r.gen); if (::reserved.load()) { r.end(r.gen); if (!r.result) r.end(r.gen); } r.end(r.gen); };
  switch (which) {
  case 0: arm(r, 0); frames(r, 2, .016, .9f); values(); tick(.016); r.probe(); r.frame(.9f); after(.9f); break;
  case 1: arm(r, 180); frames(r, 2, .016, .9f); r.shoulder(true); frames(r, 2, .016, .9f); values(); tick(.016); r.probe(); r.frame(.9f); after(.9f); break;
  case 2: arm(r, 180); frames(r, 2, .016, .9f); r.aim(true); frames(r, 2, .016, .35f); values(); tick(.016); r.probe(); r.frame(.35f); after(.35f); break;
  case 3: arm(r, 180); frames(r, 2, .016, .9f); r.aim(true); frames(r, 14, .016, .35f); r.aim(false); frames(r, 2, .016, .35f); values(); tick(.016); r.probe(); r.frame(.35f); after(.35f); break;
  case 4: arm(r, 0); r.aim(true); frames(r, 2, .016, .35f); values(); tick(.016); r.probe(); r.frame(.35f); after(.35f); break;
  case 5: arm(r, 180); frames(r, 2, .016, .9f); r.aim(true); tick(.016); r.transition(r.gen, 1, .2f, 1); r.frame(.35f); tick(.016); r.transition(r.gen, 1, .5f, 1); values(); r.probe(); r.frame(.35f); after(.35f); break;
  case 6: r.fp(); values(); r.probe(); r.begin(); after(.9f); break;
  case 7: r.fp(); r.aim(true); r.reserved_ms = 180; values(); r.probe(); r.begin(); after(.35f); break;
  case 8: r.fp(); values(); r.probe(); r.validate(); r.begin(); after(.9f); break;
  case 9: arm(r, 180); frames(r, 2, .016, .9f); r.ring(0, 3); r.row(2, 6, AVATAR, WEAPON_B, BODY); r.request_row = 2; values(); r.probe(); r.bind(r.gen, 2, WEAPON_B); after(.9f); break;
  case 10: arm(r, 180); frames(r, 2, .016, .9f); values(); r.probe(); r.transition(r.gen, 1, .3f, 0); after(.9f); break;
  case 11: arm(r, 0); frames(r, 2, .016, .9f); values(); r.probe(); r.end(r.gen); after(.9f); break;
  case 12: arm(r, 0); frames(r, 2, .016, .9f); values(); r.probe(); r.read_stats(r.gen); after(.9f); break;
  case 13: r.normal(); r.lens(0, 1.2f); values(); r.probe(); r.validate_fird(); r.begin_fird(CAPTURED); after(1.3f); break;
  case 14: r.normal(); r.lens(0, 1.2f); values(); r.probe(); r.begin_fird(CAPTURED); after(1.3f); break;
  case 15: arm_fird(r); fird_frames(r, 2, 0, 1.3f); r.lens(0, 1.3f); values(); tick(.016); r.probe(); r.frame(1.3f); after(1.3f); break;
  case 16: r.normal(); r.ring(0, 3); r.row(0, 5, AVATAR, 0, BODY); r.row(1, 4, AVATAR, 0, BODY); r.row(2, 4, AVATAR, 0, BODY); r.lens(1, 1.2f); r.lens(2, 1.4f); r.request_row = 2;
    r.begin_fird(CAPTURED); fird_frames(r, 2, 1, 1.3f); r.row(0, 4, AVATAR, 0, BODY); r.lens(0, 1.1f); values(); tick(.016); r.probe(); r.frame(1.3f); after(1.3f); break;
  case 17: arm_fird(r); r.lens(0, 1.3f); tick(.016); r.frame(1.3f); r.ring(0, 2); r.row(1, 4, AVATAR, 0, BODY); r.lens(1, 1.5f); r.row(0, 5, AVATAR, 0, BODY); r.row(0, 4, AVATAR, 0, BODY); r.ring(1, 2); r.ring(0, 2);
    r.request_row = 1; values(); r.probe(); r.bind_fird(r.gen, 1); after(1.3f); break;
  case 18: arm_fird(r); r.lens(0, 1.3f); tick(.016); r.frame(1.3f); values(); r.probe(); r.end(r.gen); after(1.3f); break;
  case 19: arm_fird(r); fird_frames(r, 2, 0, 1.3f); r.ring(0, 2); r.row(1, 6, AVATAR, WEAPON, BODY); r.request_row = 1; values(); tick(.016); r.probe(); r.frame(.35f); r.request_row = 0; after(.35f); break;
  case 20: r.fp(); values(); forward_pokes.push_back({1, {{A_CONTROLLER, 0}, raw_value(1)}}); r.probe(); r.begin(); r.controls(); after(.9f); break;
  case 21: arm(r, 180); frames(r, 2, .016, .9f); r.aim(true); frames(r, 3, .016, .35f); r.row(1, 6, AVATAR, WEAPON_B, BODY); values(); tick(.016); r.probe(); r.frame(.35f); after(.35f); break;
  case 22: r.normal(); r.lens(0, 1.2f); values(); forward_pokes.push_back({1, {{A_CONTROLLER, 0}, raw_value(1)}}); r.probe(); r.begin_fird(CAPTURED); r.controls(); after(1.3f); break;
  }
}

static void register_scripts() {
  for (double fps : {30.0, 60.0, 144.0}) for (uint64_t ms : {0ull, 180ull}) {
    char name[96]; std::snprintf(name, sizeof name, "hip, %g fps, reserved %llu", fps, (unsigned long long)ms); add(name, [=](Run &r) { script_hip(r, fps, ms); });
    std::snprintf(name, sizeof name, "shoulder aim, %g fps, reserved %llu", fps, (unsigned long long)ms); add(name, [=](Run &r) { script_shoulder(r, fps, ms); });
  }
  add("shoulder aim, exact clock limits", script_shoulder_exact);
  const char *ads[] = {"out on the weapon camera", "out on the third-person request", "in again on the way out", "objections", "another weapon, bound", "refused lenses", "clock trouble", "switch at time 0", "exact end of the ease"};
  for (int v = 0; v < 9; ++v) for (double fps : {30.0, 60.0, 144.0}) for (uint64_t ms : {180ull, 80ull, 500ull}) {
    if (ms != 180 && fps != 60.0) continue;
    char name[128]; std::snprintf(name, sizeof name, "sights: %s, %g fps, %llu ms", ads[v], fps, (unsigned long long)ms); add(name, [=](Run &r) { script_ads(r, fps, ms, v); });
  }
  add("full sights passed through (reserved 0)", script_passthrough);
  const char *phases[] = {"plain", "token for the old mode", "producer stops", "clock trouble", "two frames per packet", "zoom and setting move", "refused packets", "mid-epoch changes", "objections"};
  for (int v = 0; v < 9; ++v) for (double fps : {30.0, 60.0, 144.0}) {
    if (v != 0 && v != 4 && v != 5 && fps != 60.0) continue;
    char name[128]; std::snprintf(name, sizeof name, "presentation phase: %s, %g fps", phases[v], fps); add(name, [=](Run &r) { script_phase(r, fps, v); });
  }
  add("bind: weapon and request changes, reserved 0", [](Run &r) { script_bind(r, 0); });
  add("bind: weapon and request changes, reserved 180", [](Run &r) { script_bind(r, 180); });
  for (int part = 0; part < 16; ++part) { char name[64]; std::snprintf(name, sizeof name, "refusals, part %d", part); add(name, [=](Run &r) { script_refusals(r, part, 16); }); }
  for (int v = 0; v < 12; ++v) { char name[64]; std::snprintf(name, sizeof name, "dispatch slot, case %d", v); add(name, [=](Run &r) { script_slot(r, v); }); }
  for (int v = 0; v < 9; ++v) { char name[64]; std::snprintf(name, sizeof name, "fird policy, case %d", v); add(name, [=](Run &r) { script_fird(r, v); }); }
  add("hook arguments", script_arguments);
  const std::vector<Loss> lost = losses();
  for (size_t which = 0; which < lost.size(); ++which) for (int state = 0; state < 3; ++state) {
    char name[96]; std::snprintf(name, sizeof name, "owner lost: %s, on a %s frame", lost[which].name, state == 0 ? "hip" : state == 1 ? "full-sights" : "fird");
    add(name, [=](Run &r) { script_owner_lost(r, static_cast<int>(which), state); });
  }
  add("control calls in right and wrong orders", script_controls);
  add("checks only the translation makes", script_translation_only);
  add("after tfp_fov_reset_for_tests", script_reset);
  for (int which = 0; which < 23; ++which) add(std::string("probe: ") + PROBES[which], [=](Run &r) { probe_script(r, which); }, true);
  // TFP_DIFF_EXTRA_WALKS=n: n more walks (a soak; not part of the normal run).
  const char *extra = std::getenv("TFP_DIFF_EXTRA_WALKS");
  const uint32_t walks = 16 + (extra ? static_cast<uint32_t>(std::atoi(extra)) : 0u);
  for (uint32_t seed = 1; seed <= walks; ++seed) { char name[64]; std::snprintf(name, sizeof name, "random walk, seed %u", seed); add(name, [=](Run &r) { script_walk(r, 25480438u + seed, 4000); }); }
}

// ---- the runner ----
struct Job { size_t script; int mode, index, value; };
static int running = 0, failed = 0, finished = 0;
static void reap() {
  int status = 0; const pid_t pid = wait(&status);
  if (pid < 0) return;
  --running; ++finished;
  if (WIFSIGNALED(status)) { ++failed; std::fprintf(stderr, "a field-of-view helper script's process ended on signal %d\n", WTERMSIG(status)); }
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
  const int limit = static_cast<int>(std::max(1L, sysconf(_SC_NPROCESSORS_ONLN)));
  size_t plain = 0, probe_runs = 0;
  for (size_t i = 0; i < scripts.size(); ++i) {
    if (only && scripts[i].name.find(only) == std::string::npos) continue;
    if (scripts[i].probed) continue;
    launch({i, INJECT_NONE, -1, 0}, limit); ++plain;
  }
  while (running) reap();
  // Probes: one run counts the probed call's reads; then one run per read and kind of trouble.
  for (size_t i = 0; i < scripts.size() && failed < 20 && !(stop_at_first && failed); ++i) {
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
  std::printf("FOV helper: %zu scripts and %zu probe runs; %llu calls compared (%llu of them frames), %llu reads, writes, swaps, lock steps and forwarded calls matched; "
              "%llu translation-only host answers; %llu frames skipped (hook before any begin)\n",
              plain, probe_runs, (unsigned long long)shared->calls, (unsigned long long)shared->frames, (unsigned long long)shared->events,
              (unsigned long long)shared->translation_only, (unsigned long long)shared->skipped_frames);
  if (failed) { std::printf("FOV helper: %d script run(s) FAILED\n", failed); return 1; }
  std::printf("FOV helper: the translation behaves as the retail helper in every compared call\n");
  return 0;
}
