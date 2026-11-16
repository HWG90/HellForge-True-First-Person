// The scripts of tests/diff_near_helper.cpp, and the runner (one child process per script).
#pragma once

namespace dt {

struct Script { std::string name; std::function<void(Run &)> run; bool probed = false; };
static std::vector<Script> scripts;
static void add(const std::string &name, std::function<void(Run &)> run, bool probed = false) { scripts.push_back({name, std::move(run), probed}); }

static const float NATIVE = .05f;
static float from_bits(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
static void print_reached(const Run &r) {
  const Run::Reached &g = r.got;
  std::printf("REACHED %s: replaced %llu, full ADS %llu, aim applied %llu, other request %llu, gliding %llu, owner lost %llu; errors", script_name.c_str(), (unsigned long long)g.replaced,
              (unsigned long long)g.ads, (unsigned long long)g.aim, (unsigned long long)g.other, (unsigned long long)g.gliding, (unsigned long long)g.lost);
  for (int code = 0; code < 18; ++code) if (g.errors[code]) std::printf(" %d:%llu", code, (unsigned long long)g.errors[code]);
  std::printf("\n"); std::fflush(stdout);
}
static Run *current_run;
// A script that did not get where it was written to go is a broken script, not a passing one.
static void need(bool reached, const char *what) { if (!reached && current_run) print_reached(*current_run); if (!reached) fail("the script did not reach what it is for: %s", what); }
static void expect(Run &r, uint32_t result, uint32_t code, const char *what) {
  if (r.result != result || NADSN_Error() != code) fail("the script expected %s: result %u error %u, got result %u error %u", what, result, code, r.result, NADSN_Error());
}
static void frames(Run &r, int count, int64_t step, float meters = NATIVE) { for (int i = 0; i < count; ++i) r.frame(step, meters); }

// Everything a script or a probe can change under the helper's feet (one at a time).
static std::vector<Poke> damage_list() {
  return {
      {{A_G_CONTEXT, 0}, address_value(A_CONTEXT2)}, {{A_G_CONTEXT, 0}, bytes_value(0x10, 8)}, {{A_G_CONTEXT, 0}, bytes_value(0x7770000, 8)},
      {{A_CONTEXT, 0}, address_value(A_CAMERA2)}, {{A_CONTEXT, 0}, bytes_value(0, 8)},
      {{A_G_OWNER, 0}, address_value(A_OWNER2)}, {{A_G_OWNER, 0}, bytes_value(0x800000000000ull, 8)},
      {{A_OWNER, 0x6C}, bytes_value(0)}, {{A_OWNER, 0x6C}, bytes_value(9)},
      {{A_CONTEXT, 0x1F8}, bytes_value(32)}, {{A_CONTEXT, 0x1FC}, bytes_value(32)}, {{A_CONTEXT, 0x1FC}, bytes_value(0)},
      {{A_CONTEXT, 0x200 + 0xE8}, bytes_value(4)}, {{A_CONTEXT, 0x1F00}, bytes_value(6)}, {{A_CONTEXT, 0x1F8}, bytes_value(2)},
      {{A_G_CONTROLLER, 0}, bytes_value(0, 8)}, {{A_G_CONTROLLER, 0}, address_value(A_CONTROLLER2)},
      {{A_CONTROLLER, 4}, bytes_value(1, 1)}, {{A_CONTROLLER, 4}, bytes_value(0, 1)},
      {{A_O_FLAGS, 10}, bytes_value(4, 1)}, {{A_O_FLAGS, 15}, bytes_value(32, 1)}, {{A_O_FLAGS, 10}, bytes_value(0, 1)},
      {{A_G_API_ROOT, 0}, address_value(A_API_ROOT2)}, {{A_G_API_ROOT, 0}, bytes_value(1, 8)},
      {{A_API_ROOT, 0x20}, address_value(A_API2)}, {{A_API_ROOT, 0x20}, bytes_value(0xffff, 8)},
      {{A_API, 0x58}, address_value(A_E_GETTER, 0x10)}, {{A_API, 0x58}, bytes_value(0, 8)},
      {{A_API, 0x68}, slot_value(SLOT_FOREIGN)}, {{A_API, 0x68}, slot_value(SLOT_NATIVE)}, {{A_API, 0x68}, slot_value(SLOT_HELPER)},
      {{A_G_SITE, 33}, bytes_value(0x70, 1)}, {{A_G_SITE, 0}, bytes_value(0x49, 1)}, {{A_G_NEAR, 0}, bytes_value(0x3f800000)},
      {{A_E_SETTER, 4}, bytes_value(0x34, 1)}, {{A_E_SETTER, 9}, bytes_value(1, 1)}, {{A_E_GETTER, 5}, bytes_value(0x90, 1)},
      {{A_GAME, 128 + 8}, bytes_value(0)}, {{A_GAME, 128 + 80}, bytes_value(0)}, {{A_EXE, 128 + 8}, bytes_value(0)}, {{A_EXE, 128 + 80}, bytes_value(0)},
      {{A_CAMERA, 0x28}, bytes_value(0x3e800000)},
  };
}

// ---- hip, at one frame rate: lands at once, then eases to every changed distance ----
static void script_hip(Run &r, int64_t step) {
  r.fp(); r.arm();
  r.frame(1000000); frames(r, 30, step);
  r.set(r.gen, .30f, 0); frames(r, 60, step);
  r.set(r.gen, .03f, 0); frames(r, 60, step);
  r.set(r.gen, .15f, 0); frames(r, 60, step);
  r.set(r.gen, .05f, 0); frames(r, 60, step);   // exactly the game's: nothing to replace once landed
  r.set(r.gen, 1.0f, 0); frames(r, 80, step);
  r.set(r.gen, .0501f, .0501f); frames(r, 80, step);
  need(r.got.replaced > 150 && r.got.gliding > 12, "hip frames replaced, with glides between the distances");
  r.end(r.gen);
}
// ---- hip, normal aim and full ADS ----
static void script_views(Run &r, int64_t step) {
  r.fp(); r.hip = .15f; r.aim = .08f; r.arm();
  r.frame(1000000); frames(r, 20, step);
  r.shoulder(true); frames(r, 40, step);
  r.shoulder(false); r.ads(true); frames(r, 40, step);
  r.shoulder(true); frames(r, 10, step);                 // full ADS wins over the normal-aim flags
  r.ads(false); frames(r, 40, step);                     // back to normal aim
  r.shoulder(false); frames(r, 40, step);
  r.set(r.gen, .15f, 0); r.shoulder(true); frames(r, 40, step);   // aim 0: the game's distance
  r.set(r.gen, .15f, .4f); frames(r, 40, step);                   // aim further than hip
  r.set(r.gen, 0, .2f); frames(r, 40, step); r.shoulder(false); frames(r, 40, step);
  r.set(r.gen, .15f, .1f);
  r.s.flags[10] = 4; r.s.flags[15] = 0; frames(r, 30, step);      // a lone flag is hip
  r.s.flags[10] = 0; r.s.flags[15] = 32; frames(r, 30, step);
  r.s.flags[10] = 0xfb; r.s.flags[15] = 0xff; frames(r, 20, step);
  r.s.flags[10] = 0xff; r.s.flags[15] = 0xdf; frames(r, 20, step);
  r.s.flags[10] = 0xff; r.s.flags[15] = 0xff; frames(r, 30, step);
  for (int i = 0; i < 24; ++i) r.s.flags[i] = 0xff; r.s.flags[10] = 0xfb; frames(r, 20, step);
  std::memset(r.s.flags, 0, 24);
  r.ads(true); r.s.controls[4] = 0x80; frames(r, 20, step); r.s.controls[4] = 0; r.s.controls[3] = 1; r.s.controls[5] = 1; frames(r, 20, step);
  need(r.got.replaced > 100 && r.got.ads > 40 && r.got.aim > 60 && r.got.gliding > 20, "hip, aim and full-ADS frames with glides");
  r.end(r.gen);
}
// ---- a sight's own distance ----
static void script_sight(Run &r, int64_t step) {
  r.fp(); r.hip = 0; r.aim = 0; r.arm();
  r.frame(1000000, .2214f); frames(r, 20, step, .2214f);            // pulled back to the game's outside full sights
  frames(r, 20, step, .3521f); r.shoulder(true); frames(r, 20, step, .3521f); r.shoulder(false);
  r.ads(true); frames(r, 50, step, .2214f);                         // kept in full sights; glides out
  frames(r, 50, step, .3521f);                                      // the scope's other zoom level
  r.ads(false); frames(r, 50, step, .3521f);                        // and glides back
  r.ads(true); frames(r, 3, step, .2214f); r.ads(false); frames(r, 3, step, .2214f); r.ads(true); frames(r, 50, step, .2214f); r.ads(false);
  r.set(r.gen, .15f, 0); frames(r, 50, step, .2214f);               // the configured hip distance still wins when it is further
  r.set(r.gen, .30f, .25f); frames(r, 50, step, .2214f); r.shoulder(true); frames(r, 50, step, .2214f); r.shoulder(false);
  r.s.kind = 4; frames(r, 5, step, .2214f); r.s.kind = 6; frames(r, 5, step, .2214f);
  frames(r, 30, step, .115f); frames(r, 10, step, NATIVE); frames(r, 10, step, .115f);
  r.set(r.gen, .115f, 0); frames(r, 40, step, .115f);               // target equals the sight's value: lands and stops replacing
  need(r.got.replaced > 150 && r.got.ads > 100 && r.got.gliding > 30 && r.got.other == 5, "sight distances pulled back, kept in full sights, gliding both ways");
  r.end(r.gen);
}
// ---- every kind of value the game could pass, on every view ----
static void script_values(Run &r, int view) {
  static const float values[] = {NATIVE, from_bits(0x3d4ccccc), from_bits(0x3d4cccce), .0501f, .1f, .115f, .15f, .2214f, .3521f, .999f, 1.0f, from_bits(0x3f800001), 1.5f, 40.0f,
                                 .03f, 0.0f, -0.0f, -1.0f, from_bits(0x7fc00000), from_bits(0xffc00000), from_bits(0x7f800001), HUGE_VALF, -HUGE_VALF, from_bits(1), 1e-30f};
  r.fp(); r.hip = .15f; r.aim = .1f; r.arm();
  if (view == 1) r.shoulder(true); if (view == 2) r.ads(true); if (view == 3) r.s.kind = 4; if (view == 4) { r.s.tail = 0; r.s.idle = 6; }
  if (view == 5) r.set(r.gen, 0, 0); if (view == 6) { r.set(r.gen, 1.0f, 1.0f); r.shoulder(true); }
  for (float v : values) { r.frame(1000000, v); r.frame(16667, v); r.frame(16667, NATIVE); r.frame(16667, v); r.frame(16667, v); }
  for (float v : values) for (float u : {.2214f, NATIVE, .999f}) { r.frame(7000, v); r.frame(7000, u); }
  r.end(r.gen);
  need(view == 3 ? r.got.other > 50 : view == 2 ? r.got.ads > 50 : r.got.replaced > 50, "values tried on this view");
}
// ---- the 45 ms ease against time: steps of every size, gaps, repeats, the clock going back or reading zero ----
static void script_time(Run &r, float hip, float aim, uint64_t glides) {
  static const int64_t steps[] = {1, 2, 500, 1000, 4167, 6944, 8333, 16667, 33333, 49999, 50000, 50001, 100000, 200000, 249999, 250000, 250001, 300000, 1000000, 0, -1, -16667, -1000000};
  r.fp(); r.hip = hip; r.aim = aim; r.arm();
  for (int64_t step : steps) {
    r.ads(false); r.shoulder(false); r.frame(1000000); r.frame(16667);
    r.ads(true); r.frame(step); r.frame(step); r.frame(step);
    r.ads(false); r.frame(step); r.frame(16667); r.frame(step); r.frame(step);
    r.shoulder(true); r.frame(step, .2214f); r.frame(step, .2214f); r.shoulder(false); r.frame(16667); r.frame(step);
    for (int i = 0; i < 12; ++i) r.frame(step);
  }
  // The clock reads zero (retail: the counter could not be read), then a small value, then jumps far ahead.
  r.frame(1000000); r.ads(true); r.frame(16667);
  fixture_now_us = 0; r.frame_now(); r.frame_now(); r.ads(false); r.frame_now();
  fixture_now_us = 1; r.frame_now(); r.frame(1); r.frame(16667); r.ads(true); r.frame(16667);
  fixture_now_us = 0; r.frame_now(); fixture_now_us = 5; r.frame_now(); r.frame(16667);
  // A frame at time zero leaves a distance on record with no time: the next frame must not ease from it.
  r.frame(1000000); r.ads(true); fixture_now_us = 0; r.frame_now(); r.ads(false); fixture_now_us = 5000; r.frame_now(); r.frame(5000);
  r.ads(true); r.frame(300000); fixture_now_us = 0; r.frame_now(.3521f); r.ads(false); fixture_now_us = 249999; r.frame_now(.3521f); r.frame(1, .3521f);
  fixture_now_us = UINT64_MAX - 20000; r.frame_now(); r.ads(false); r.frame(16667); r.frame(16667); r.frame(16667); // wraps past zero
  fixture_now_us = 1ull << 62; r.frame_now(); r.frame(16667); r.ads(true); r.frame(16667);
  // The same glide at 30, 60, 144 and 240 frames a second, both ways.
  for (int64_t step : {33333, 16667, 6944, 4167}) {
    r.ads(false); r.frame(1000000); r.ads(true); frames(r, static_cast<int>(400000 / step), step);
    r.ads(false); frames(r, static_cast<int>(400000 / step), step);
  }
  need(r.got.gliding >= glides && r.got.ads > 100, "glides at every step size");
  r.end(r.gen);
}
// ---- other callers and other cameras pass through; the caller table ----
static void script_callers(Run &r) {
  r.fp(); r.arm();
  r.frame(1000000);
  for (int caller : {World::CALL_TEXT_FIRST, World::CALL_BELOW_TEXT, World::CALL_TEXT_LAST, World::CALL_TEXT_END, World::CALL_FOREIGN, World::CALL_GAME_BASE, World::CALL_ZERO_PAGE, World::CALL_SITE})
    for (float v : {NATIVE, .2214f, 1.5f}) { r.frame(16667, v, caller); r.frame(16667, v, caller); }
  for (int camera : {SEL_SECOND, SEL_SMALL, SEL_UNMAPPED, SEL_NULL, SEL_OWN}) for (float v : {NATIVE, .2214f}) for (int caller : {World::CALL_SITE, World::CALL_FOREIGN}) r.frame(16667, v, caller, camera);
  r.read_sites(16); r.read_sites(0); r.read_sites(3); r.read_sites(20);
  for (int i = 0; i < World::CALL_MANY_COUNT; ++i) { r.frame(16667, NATIVE, World::CALL_MANY + i); if (i % 3 == 0) r.frame(16667, .3f, World::CALL_MANY + i, SEL_SECOND); }
  for (int i = World::CALL_MANY_COUNT - 1; i >= 0; i -= 2) r.frame(16667, .2214f, World::CALL_MANY + i);
  r.read_sites(16); r.read_sites(0); r.read_sites(1); r.read_sites(15); r.read_sites(17); r.read_sites(20); r.read_sites(0xffffffffu);
  r.read_sites(5, 1); expect(r, 0, INVALID_CONFIG, "the caller table refused into a null pointer");
  r.read_sites(0, 1); r.read_sites(5, 2); expect(r, 0, INVALID_CONFIG, "the caller table refused into a read-only page"); r.read_sites(0, 2);
  need(r.got.replaced > 30 && ::site_overflow.load() > 20, "callers inside and outside the game's code, and more of them than the table holds");
  r.end(r.gen);
  r.frame(16667); r.frame(16667, .2214f, World::CALL_FOREIGN, SEL_SECOND); r.read_sites(16);
}
// ---- requests that are not the first-person one ----
static void script_requests(Run &r) {
  r.fp(); r.arm(); r.frame(1000000);
  for (uint32_t tail = 0; tail < 34; ++tail) for (uint32_t head : {0u, 1u, tail, 31u, 32u, 0xffffffffu}) {
    r.s.head = head; r.s.tail = tail; r.s.kind = 6; r.s.idle = 4; r.frame(16667);
    r.s.idle = 6; r.s.kind = 4; r.frame(16667);
  }
  r.fp();
  for (uint32_t kind : {6u, 4u, 0u, 7u, 0x10006u, 0xffff0006u, 0x8006u, 0x60000u, 0x106u, 6u}) { r.s.kind = kind; r.frame(16667); r.frame(16667, .2214f); }
  r.poke(A_G_CONTROLLER, 0, bytes_value(0, 8)); r.frame(16667); r.heal();
  r.poke(A_G_CONTROLLER, 0, address_value(A_CONTROLLER2)); r.frame(16667); r.ads(true); r.frame(16667); r.heal(); r.frame(16667); r.ads(false);
  r.poke(A_G_CONTROLLER, 0, address_value(A_CONTROLLER, 0xFFB)); r.frame(16667); r.heal(); // six bytes that run off the page
  need(r.got.other > 150 && r.got.replaced > 50, "the first-person request among others");
  r.end(r.gen);
}
// ---- the owner goes away under the armed helper ----
static void script_lost(Run &r, int what, int view) {
  r.fp(); r.hip = .2f; r.aim = .1f; r.arm();
  if (view == 1) r.shoulder(true); if (view == 2) r.ads(true); if (view == 3) r.s.kind = 4;
  r.frame(1000000); r.frame(16667);
  switch (what) {
  case 0: r.poke(A_G_CONTEXT, 0, address_value(A_CONTEXT2)); break;
  case 1: r.poke(A_G_CONTEXT, 0, bytes_value(0x100, 8)); break;
  case 2: r.poke(A_G_CONTEXT, 0, bytes_value(0x7770000, 8)); break;
  case 3: r.poke(A_CONTEXT, 0, address_value(A_CAMERA2)); break;
  case 4: r.poke(A_CONTEXT, 0, bytes_value(0, 8)); break;
  case 5: r.poke(A_G_OWNER, 0, address_value(A_OWNER2)); break;
  case 6: r.poke(A_G_OWNER, 0, bytes_value(0x800000000000ull, 8)); break;
  case 7: r.s.count = 0; break;
  case 8: r.s.count = 9; break;
  case 9: r.poke(A_G_CONTEXT, 0, bytes_value(0xffff, 8)); break;
  case 10: r.poke(A_G_OWNER, 0, bytes_value(0x7fffffffffffull, 8)); break;
  default: r.poke(A_CONTEXT, 0, address_value(A_CAMERA, 8)); break;
  }
  r.frame(16667, .2214f, World::CALL_FOREIGN);          // not a call the helper looks at: still armed
  r.frame(16667, NATIVE, World::CALL_SITE, SEL_SECOND);
  r.frame(16667);                                       // disarms
  need(r.got.lost == 1 && r.got.errors[OWNER_MISMATCH] >= 1, "the owner lost on a frame");
  r.frame(16667); r.frame(16667, .2214f);
  r.heal(); r.s.count = 1; r.frame(16667); r.frame(16667);
  r.set(r.gen, .3f, .3f); r.read_stats(r.gen);
  r.begin(); expect(r, 0, BUSY, "busy while the disarmed lease is held");
  r.end(r.gen); r.frame(16667);
  r.arm(); r.frame(16667); r.frame(16667); r.end(r.gen);
}
// ---- control calls in right and wrong orders ----
static void script_controls(Run &r) {
  r.fp();
  r.end(0); r.end(1); r.set(0, .1f, .1f); r.set(1, .1f, .1f); r.read_stats(0); r.read_stats(1); r.read_sites(16);
  r.read_stats(0, 87); r.read_stats(0, 88, 2); r.read_stats(0, 88, 1, 1); r.read_stats(0, 88, 1, 2); r.read_stats(5, 88, 1, 1); r.read_stats(0, 88, 1, 3); r.read_sites(4, 3);
  r.arm(); const uint64_t first = r.gen;
  r.begin(); expect(r, 0, BUSY, "a second begin");
  r.set(first + 1, .2f, 0); expect(r, 0, TOKEN_MISMATCH, "set with another generation"); r.set(0, .2f, 0); r.set(UINT64_MAX, .2f, 0);
  for (float v : {1.5f, -.1f, from_bits(0x7fc00000), HUGE_VALF, -HUGE_VALF, from_bits(0x3f800001)}) {
    r.set(first, v, 0); expect(r, 0, INVALID_CONFIG, "set with a hip distance out of range"); r.set(first, .1f, v); expect(r, 0, INVALID_CONFIG, "set with an aim distance out of range");
    r.set(first + 1, v, v);
  }
  for (float v : {0.0f, -0.0f, 1.0f, .05f, from_bits(1)}) { r.set(first, v, .1f); expect(r, 1, OK, "set at a bound"); r.frame(300000); r.set(first, .1f, v); r.shoulder(true); r.frame(300000); r.shoulder(false); }
  r.read_stats(0); expect(r, 0, TOKEN_MISMATCH, "stats with generation 0 while a lease is held"); r.read_stats(first + 1); r.read_stats(first);
  r.read_stats(first, 87); expect(r, 0, INVALID_CONFIG, "stats with a wrong size"); r.read_stats(first, 89); r.read_stats(first, 88, 0); r.read_stats(first, 88, 2);
  r.read_stats(first, 88, 1, 1); expect(r, 0, INVALID_CONFIG, "stats into a null pointer");
  r.read_stats(first, 88, 1, 2); r.read_stats(first + 1, 88, 1, 1);
  r.frame(16667);
  r.end(0); expect(r, 0, TOKEN_MISMATCH, "end with generation 0"); r.end(first + 1); expect(r, 0, TOKEN_MISMATCH, "end with another generation");
  r.end(first); expect(r, 1, OK, "end"); r.end(first); expect(r, 1, OK, "end again with the retired generation");
  r.end(first + 1); expect(r, 0, TOKEN_MISMATCH, "end with a generation never given"); r.end(0);
  r.set(first, .2f, 0); expect(r, 0, TOKEN_MISMATCH, "set after end"); r.read_stats(first); r.read_stats(0);
  r.frame(16667); r.frame(16667, .2214f);
  r.arm(); const uint64_t second = r.gen; if (second != first + 1) fail("the second lease has generation %llu", (unsigned long long)second);
  r.end(first); expect(r, 0, TOKEN_MISMATCH, "end with the generation before"); r.set(first, .2f, 0); r.read_stats(first);
  r.frame(16667); r.set(second, .25f, .2f); r.frame(16667); r.end(second); r.end(first); r.end(second);
  r.arm(); r.end(r.gen); r.arm(); r.frame(16667); r.begin(); r.end(r.gen);
}
// ---- refused configurations and worlds: nothing may be written to the slot ----
static void script_refusals(Run &r) {
  r.fp();
  auto refused = [&](uint32_t code, const char *what) {
    const uint32_t swaps = fixture_cas_calls;
    r.begin(); expect(r, 0, code, what);
    if (fixture_cas_calls != swaps) fail("a refused begin reached the slot: %s", what);
    r.heal();
  };
  { Run saved = r;
    r.size = 71; refused(INVALID_CONFIG, "size 71"); r = saved; r.size = 73; refused(INVALID_CONFIG, "size 73"); r = saved; r.size = 0; refused(INVALID_CONFIG, "size 0"); r = saved;
    r.abi = 2; refused(INVALID_CONFIG, "abi 2"); r = saved; r.abi = 0; refused(INVALID_CONFIG, "abi 0"); r = saved;
    r.reserved_field = 1; refused(INVALID_CONFIG, "reserved"); r = saved; r.reserved2 = 1ull << 40; refused(INVALID_CONFIG, "reserved2"); r = saved;
    r.index = 8; refused(INVALID_CONFIG, "row 8"); r = saved; r.index = 0xffffffffu; refused(INVALID_CONFIG, "row -1"); r = saved;
    r.context_sel = SEL_SMALL; refused(INVALID_CONFIG, "context below 64K"); r = saved; r.camera_sel = SEL_SMALL; refused(INVALID_CONFIG, "camera below 64K"); r = saved;
    r.owner_sel = SEL_SMALL; refused(INVALID_CONFIG, "owner below 64K"); r = saved; r.context_sel = SEL_NULL; refused(INVALID_CONFIG, "context null"); r = saved;
    for (float v : {2.0f, -1.0f, from_bits(0x7fc00000), HUGE_VALF, from_bits(0x3f800001), -1e-30f}) { r.hip = v; refused(INVALID_CONFIG, "hip out of range"); r = saved; r.aim = v; refused(INVALID_CONFIG, "aim out of range"); r = saved; }
    r.config_at = 1; refused(INVALID_CONFIG, "null configuration"); r = saved; r.generation_at = 1; refused(INVALID_CONFIG, "null generation"); r = saved;
    r.generation_at = 2; refused(INVALID_CONFIG, "unwritable generation"); r = saved;
    r.config_at = 1; r.generation_at = 1; refused(INVALID_CONFIG, "both null"); r = saved;
    r.config_at = 3; refused(INVALID_CONFIG, "configuration below 64K"); r = saved; r.generation_at = 3; refused(INVALID_CONFIG, "generation below 64K"); r = saved;
    r.config_at = 2; refused(INVALID_CONFIG, "configuration with a wrong header"); r = saved;
    // the owner proof
    r.context_sel = SEL_SECOND; refused(OWNER_MISMATCH, "another context"); r = saved; r.camera_sel = SEL_SECOND; refused(OWNER_MISMATCH, "another camera"); r = saved;
    r.owner_sel = SEL_SECOND; refused(OWNER_MISMATCH, "another owner"); r = saved; r.context_sel = SEL_UNMAPPED; refused(OWNER_MISMATCH, "unmapped context"); r = saved;
    r.camera_sel = SEL_UNMAPPED; refused(OWNER_MISMATCH, "unmapped camera"); r = saved; r.owner_sel = SEL_UNMAPPED; refused(OWNER_MISMATCH, "unmapped owner"); r = saved;
    // Pointers at the very top of user space are not pointers, even when the configuration names the same value.
    r.camera_sel = SEL_HIGH; r.poke(A_CONTEXT, 0, bytes_value(0x800000000000ull, 8)); refused(OWNER_MISMATCH, "camera at the top of user space"); r = saved;
    r.context_sel = SEL_HIGH; r.poke(A_G_CONTEXT, 0, bytes_value(0x800000000000ull, 8)); refused(OWNER_MISMATCH, "context at the top of user space"); r = saved;
    r.owner_sel = SEL_HIGH; r.poke(A_G_OWNER, 0, bytes_value(0x800000000000ull, 8)); refused(OWNER_MISMATCH, "owner at the top of user space"); r = saved;
    r.s.count = 0; refused(OWNER_MISMATCH, "no rows"); r = saved; r.s.count = 9; refused(OWNER_MISMATCH, "nine rows"); r = saved;
    r.index = 1; refused(OWNER_MISMATCH, "row 1 of 1"); r = saved; r.index = 7; r.s.count = 7; refused(OWNER_MISMATCH, "row 7 of 7"); r = saved;
    for (int what : {0, 1, 2, 3, 4, 5, 6}) { r.poke(damage_list()[what].at.id, damage_list()[what].at.offset, damage_list()[what].value); refused(OWNER_MISMATCH, "a pointer of the owner proof"); r = saved; }
    // the images
    r.exe_delta = 8; refused(IMAGE_MISMATCH, "exe base off"); r = saved; r.exe_delta = 0x1000; refused(IMAGE_MISMATCH, "exe base off by a page"); r = saved;
    // (the game base is used by the owner proof first)
    r.game_delta = 0x1000; refused(OWNER_MISMATCH, "game base off by a page"); r = saved;
  }
  struct Bad { int id; uint64_t offset; uint64_t raw; int length; uint32_t code; };
  const Bad bad[] = {
      {A_GAME, 0, 0x5A4C, 2, IMAGE_MISMATCH}, {A_GAME, 0x3c, 63, 4, IMAGE_MISMATCH}, {A_GAME, 0x3c, 4097, 4, IMAGE_MISMATCH}, {A_GAME, 0x3c, 0xffffff80u, 4, IMAGE_MISMATCH},
      {A_GAME, 128, 0x4551, 4, IMAGE_MISMATCH}, {A_GAME, 128 + 4, 0x14c, 2, IMAGE_MISMATCH}, {A_GAME, 128 + 24, 0x10b, 2, IMAGE_MISMATCH},
      {A_GAME, 128 + 8, 0x6ab3b440, 4, IMAGE_MISMATCH}, {A_GAME, 128 + 80, 0x4745000, 4, IMAGE_MISMATCH},
      {A_EXE, 0, 0, 2, IMAGE_MISMATCH}, {A_EXE, 128, 0, 4, IMAGE_MISMATCH}, {A_EXE, 128 + 4, 0, 2, IMAGE_MISMATCH}, {A_EXE, 128 + 24, 0, 2, IMAGE_MISMATCH},
      {A_EXE, 128 + 8, 1, 4, IMAGE_MISMATCH}, {A_EXE, 128 + 80, 1, 4, IMAGE_MISMATCH}, {A_EXE, 0x3c, 0x7fffffff, 4, IMAGE_MISMATCH},
      {A_G_SITE, 0, 0x49, 1, CODE_MISMATCH}, {A_G_SITE, 3, 0, 1, CODE_MISMATCH}, {A_G_SITE, 16, 0, 1, CODE_MISMATCH}, {A_G_SITE, 33, 0x70, 1, CODE_MISMATCH}, {A_G_SITE, 32, 0x50, 1, CODE_MISMATCH},
      {A_G_NEAR, 0, 0x3f800000, 4, CODE_MISMATCH}, {A_G_NEAR, 0, 0xcc, 1, CODE_MISMATCH}, {A_G_NEAR, 3, 0x3e, 1, CODE_MISMATCH},
      {A_E_SETTER, 0, 0xf2, 1, CODE_MISMATCH}, {A_E_SETTER, 4, 0x34, 1, CODE_MISMATCH}, {A_E_SETTER, 9, 0x01, 1, CODE_MISMATCH}, {A_E_SETTER, 6, 0, 1, CODE_MISMATCH},
      {A_E_GETTER, 0, 0xf2, 1, CODE_MISMATCH}, {A_E_GETTER, 5, 0xcc, 1, CODE_MISMATCH}, {A_E_GETTER, 4, 0x2c, 1, CODE_MISMATCH},
      {A_G_API_ROOT, 0, 0, 8, API_MISMATCH}, {A_G_API_ROOT, 0, 0xffff, 8, API_MISMATCH}, {A_G_API_ROOT, 0, 0x7770000, 8, API_MISMATCH},
      {A_API_ROOT, 0x20, 0, 8, API_MISMATCH}, {A_API_ROOT, 0x20, 0x800000000000ull, 8, API_MISMATCH}, {A_API_ROOT, 0x20, 0x7770000, 8, API_MISMATCH},
      {A_API, 0x58, 0, 8, API_MISMATCH}, {A_API, 0x58, 0x7770000, 8, API_MISMATCH}};
  for (const Bad &b : bad) { r.poke(b.id, b.offset, bytes_value(b.raw, b.length)); refused(b.code, "one damaged place"); }
  // A header that is right but sits elsewhere (e_lfanew 64 and 4096 are the limits).
  for (int32_t at : {64, 4096}) {
    for (int w = 0; w < 2; ++w) { uint8_t header[264]; std::memcpy(header, reinterpret_cast<void *>(W[w].a[A_GAME] + 128), 264); std::memcpy(reinterpret_cast<void *>(W[w].a[A_GAME] + at), header, 264); World::put<int32_t>(W[w].a[A_GAME] + 0x3c, at); }
    r.arm(); r.end(r.gen);
    for (int w = 0; w < 2; ++w) { std::memset(reinterpret_cast<void *>(W[w].a[A_GAME] + 64), 0, 4096 + 264 - 64); } r.heal();
  }
  r.poke(A_API, 0x58, address_value(A_E_GETTER, 0x10)); refused(API_MISMATCH, "another getter");
  r.poke(A_API, 0x58, address_value(A_E2_GETTER)); refused(API_MISMATCH, "the getter of another image");
  r.poke(A_API, 0x58, address_value(A_E_SETTER)); refused(API_MISMATCH, "the setter where the getter belongs");
  Run::set_slot(SLOT_FOREIGN); refused(API_MISMATCH, "a foreign setter in the slot");
  Run::set_slot(SLOT_HELPER); refused(API_MISMATCH, "the helper already in the slot");
  Run::set_slot(SLOT_NATIVE2); refused(API_MISMATCH, "another image's setter in the slot");
  Run::set_slot(0); refused(API_MISMATCH, "an empty slot");
  Run::set_slot(SLOT_NATIVE);
  // The slot's page: the fixture's answer, a really read-only page, and an unaligned slot.
  fixture_slot_readonly = true; refused(SLOT_NOT_WRITABLE, "slot not writable (answered)"); fixture_slot_readonly = false;
  for (int w = 0; w < 2; ++w) protect(W[w].slot(), 8, PROT_READ);
  { const uint32_t swaps = fixture_cas_calls; r.begin(); expect(r, 0, SLOT_NOT_WRITABLE, "slot on a read-only page"); if (fixture_cas_calls != swaps) fail("a refused begin reached the slot"); }
  for (int w = 0; w < 2; ++w) protect(W[w].slot(), 8, PROT_READ | PROT_WRITE);
  for (int w = 0; w < 2; ++w) { World::put<uint64_t>(W[w].a[A_API2] + 0x804 + 0x58, W[w].a[A_EXE] + W[w].L->getter); World::put<uint64_t>(W[w].a[A_API2] + 0x804 + 0x68, W[w].native_value); }
  r.poke(A_API_ROOT, 0x20, address_value(A_API2, 0x804)); refused(SLOT_NOT_WRITABLE, "an unaligned slot");
  // The slot holds the right setter, but that address is not executable code.
  { static uint64_t not_code[4]; const uint64_t real = W[0].native_value; W[0].native_value = reinterpret_cast<uint64_t>(&not_code); Run::set_slot(SLOT_NATIVE);
    World::put<uint64_t>(W[0].a[A_API2] + 0x68, W[0].native_value);
    refused(API_MISMATCH, "a setter that is not executable"); W[0].native_value = real; Run::set_slot(SLOT_NATIVE); World::put<uint64_t>(W[0].a[A_API2] + 0x68, real); }
  shim_pin_fails = true; refused(PIN_FAILED, "the helper could not be pinned"); shim_pin_fails = false;
  // The request does not have to be the first-person one at begin (kind 4 is accepted).
  r.s.kind = 4; r.arm(); r.frame(16667); r.s.kind = 6; r.frame(16667); r.end(r.gen);
  r.s.head = 40; r.arm(); r.end(r.gen); r.fp();
  // Another API table reached through the root: a lease on it works, and the first table is left alone.
  r.poke(A_G_API_ROOT, 0, address_value(A_API_ROOT2)); r.arm(); r.frame(16667); r.frame(16667, .2214f);
  r.heal(); r.frame(16667); r.end(r.gen); r.poke(A_G_API_ROOT, 0, address_value(A_API_ROOT2)); r.end(r.gen); r.heal();
  r.arm(); r.frame(16667); r.end(r.gen);
  // The recorded setter belongs to the first executable: a begin against another one is refused.
  r.second_exe = true; Run::set_slot(SLOT_NATIVE2); r.poke(A_API, 0x58, address_value(A_E2_GETTER));
  r.begin(); expect(r, 0, API_MISMATCH, "a second executable after the first was recorded");
  r.second_exe = false; Run::set_slot(SLOT_NATIVE); r.heal(); r.arm(); r.frame(16667); r.end(r.gen);
  need(r.got.replaced >= 3, "leases still work after every refusal");
}
// The first lease of a process against the second executable image.
static void script_second_exe(Run &r) {
  r.fp(); r.second_exe = true; Run::set_slot(SLOT_NATIVE2); r.poke(A_API, 0x58, address_value(A_E2_GETTER));
  r.arm(); r.frame(1000000); r.frame(16667, .2214f); r.end(r.gen);
  r.second_exe = false; r.pokes.clear(); r.heal(); Run::set_slot(SLOT_NATIVE);
  r.begin(); expect(r, 0, API_MISMATCH, "the first executable after the second was recorded");
  need(r.got.replaced == 2, "frames on the second executable's setter");
}
// ---- the dispatch slot: conflicts, faults and half-done begins ----
static void script_slot(Run &r, int what) {
  r.fp(); r.hip = .2f;
  auto after = [&] { r.heal(); fixture_slot_readonly = false; fixture_cas_fail_at = fixture_foreign_cas_at = 0; r.frame(16667); r.end(r.gen); r.end(r.gen); Run::set_slot(SLOT_NATIVE); r.arm(); r.frame(16667); r.end(r.gen); };
  switch (what) {
  case 0: // a foreign value arrives while the lease is held: End leaves it alone
    r.arm(); r.frame(1000000); Run::set_slot(SLOT_FOREIGN); r.read_stats(r.gen); r.end(r.gen); expect(r, 1, OK, "end over a foreign slot"); r.end(r.gen);
    Run::set_slot(SLOT_NATIVE); r.arm(); r.frame(16667); r.end(r.gen); break;
  case 1: // the game's setter is back in the slot behind the helper's back
    r.arm(); r.frame(1000000); Run::set_slot(SLOT_NATIVE); r.frame(16667); r.end(r.gen); expect(r, 1, OK, "end over a restored slot"); r.arm(); r.end(r.gen); break;
  case 2: // the swap faults at begin
    fixture_cas_fail_at = fixture_cas_calls + 1; r.begin(); expect(r, 0, CAS_FAULT, "a faulting swap at begin"); fixture_cas_fail_at = 0;
    r.arm(); if (r.gen != 2) fail("generation %llu after a failed begin", (unsigned long long)r.gen); r.frame(16667); r.end(r.gen); break;
  case 3: // a foreign racer between proof and swap: never overwritten
    fixture_foreign_value = SLOT_FOREIGN; fixture_foreign_cas_at = fixture_cas_calls + 1; r.begin(); expect(r, 0, CAS_MISMATCH, "a racer at begin"); fixture_foreign_cas_at = 0;
    r.end(1); r.read_stats(0); Run::set_slot(SLOT_NATIVE); r.arm(); r.frame(16667); r.end(r.gen); break;
  case 4: // the swap faults at end: cleanup stays pending, then succeeds
    r.arm(); r.frame(1000000); fixture_cas_fail_at = fixture_cas_calls + 1; r.end(r.gen); expect(r, 0, CLEANUP_PENDING, "a faulting swap at end"); fixture_cas_fail_at = 0;
    r.frame(16667); r.set(r.gen, .3f, 0); r.read_stats(r.gen); r.begin(); expect(r, 0, BUSY, "busy while cleanup is pending");
    r.end(r.gen); expect(r, 1, OK, "the second end"); r.frame(16667); r.arm(); r.end(r.gen); break;
  case 5: // the slot cannot be read at end
    r.arm(); r.frame(1000000); for (int w = 0; w < 2; ++w) protect(W[w].slot(), 8, PROT_NONE);
    call_text = "end with the slot unreadable"; { const uint32_t a = NADSN_End(r.gen), b = tfp_near_end(r.gen); for (int w = 0; w < 2; ++w) protect(W[w].slot(), 8, PROT_READ | PROT_WRITE);
      ++counted.calls; if (a != 0 || b != 0 || NADSN_Error() != CLEANUP_PENDING || tfp_near_error() != CLEANUP_PENDING) fail("expected both to answer 0 with error 13: retail %u/%u, translation %u/%u", a, NADSN_Error(), b, tfp_near_error()); }
    r.inspect(); r.frame(16667); r.end(r.gen); expect(r, 1, OK, "end once the slot is readable"); break;
  case 6: // the slot's page is no longer writable at end
    r.arm(); r.frame(1000000); fixture_slot_readonly = true; r.end(r.gen); expect(r, 0, CLEANUP_PENDING, "end with the slot read-only"); r.frame(16667);
    Run::set_slot(SLOT_FOREIGN); r.end(r.gen); expect(r, 1, OK, "end with a foreign slot needs no write"); fixture_slot_readonly = false; Run::set_slot(SLOT_NATIVE); r.arm(); r.end(r.gen); break;
  case 7: // the API table changes right after the swap, and can be restored: begin answers 0
    after_cas = {{{A_API, 0x58}, bytes_value(0, 8)}}; r.begin(); expect(r, 0, CAS_MISMATCH, "the getter changed after the swap"); after(); break;
  case 8: // the same, and the slot cannot be restored: begin answers 2 and the lease is owned
    after_cas = {{{A_API, 0x58}, bytes_value(0, 8)}}; readonly_after_cas = true; r.begin(); expect(r, 2, CLEANUP_PENDING, "begin answering 2");
    r.frame(16667); r.frame(16667, .2214f); r.set(r.gen, .3f, 0); r.read_stats(r.gen); r.read_stats(0); r.begin(); expect(r, 0, BUSY, "busy after begin answered 2");
    r.end(r.gen); expect(r, 0, CLEANUP_PENDING, "end while the slot is still read-only"); after(); break;
  case 9: // the owner changes right after the swap
    after_cas = {{{A_CONTEXT, 0}, address_value(A_CAMERA2)}}; r.begin(); expect(r, 0, OWNER_MISMATCH, "the camera changed after the swap"); after(); break;
  case 10: // the root points at another table right after the swap
    after_cas = {{{A_G_API_ROOT, 0}, address_value(A_API_ROOT2)}}; r.begin(); expect(r, 0, CAS_MISMATCH, "another table after the swap"); after(); break;
  case 11: // someone replaces the helper in the slot right after the swap: restore leaves the foreign value
    after_cas = {{{A_API, 0x68}, slot_value(SLOT_FOREIGN)}}; r.begin(); expect(r, 0, CAS_MISMATCH, "a foreign value right after the swap"); after(); break;
  case 12: // the generation cannot be written the second time
    generation_locks_after_write = true; r.begin(); expect(r, 0, INVALID_CONFIG, "the generation page turned read-only");
    r.read_stats(0); r.frame(16667); r.arm(); if (r.gen != 2) fail("generation %llu after the refused begin", (unsigned long long)r.gen); r.frame(16667); r.end(r.gen); break;
  case 13: // the swap at begin faults and so does the one that would restore: nothing to restore
    fixture_cas_fail_at = fixture_cas_calls + 1; r.begin(); expect(r, 0, CAS_FAULT, "a faulting swap"); fixture_cas_fail_at = fixture_cas_calls + 2; r.arm(); r.frame(16667);
    r.end(r.gen); expect(r, 0, CLEANUP_PENDING, "the restoring swap faults"); r.end(r.gen); expect(r, 1, OK, "the next one does not"); break;
  case 14: // the owner is gone after the swap and the slot cannot be restored either
    after_cas = {{{A_OWNER, 0x6C}, bytes_value(0)}}; r.s.count = 1; readonly_after_cas = true; r.begin();
    // (the scene's count is rewritten before the next call; the begin saw 0 after its swap)
    expect(r, 2, CLEANUP_PENDING, "begin answering 2 on a lost owner"); fixture_slot_readonly = false; r.frame(16667); r.end(r.gen); expect(r, 1, OK, "end"); r.arm(); r.end(r.gen); break;
  case 15: // the slot's page stops being writable between the proof and the swap
    readonly_after_policy_lock = true; r.begin(); expect(r, 0, SLOT_NOT_WRITABLE, "slot read-only after the proof"); fixture_slot_readonly = false;
    r.read_stats(0); r.arm(); if (r.gen != 2) fail("generation %llu", (unsigned long long)r.gen); r.frame(16667); r.end(r.gen); break;
  default: // ... and the same at end
    r.arm(); r.frame(1000000); readonly_after_policy_lock = true; r.end(r.gen); expect(r, 0, CLEANUP_PENDING, "slot read-only at end"); fixture_slot_readonly = false; r.end(r.gen); expect(r, 1, OK, "end"); break;
  }
}
// ---- End puts the game's distance back only while the owned camera still holds ours ----
static void script_restore(Run &r, int what) {
  r.fp(); r.hip = .15f; r.aim = .1f; r.arm(); r.frame(1000000);
  switch (what) {
  case 0: break;                                                             // ours is on the camera: one call with the game's value
  case 1: r.frame(16667, 1.5f); break;                                       // the game wrote something else since
  case 2: r.poke(A_G_CONTEXT, 0, address_value(A_CONTEXT2)); break;          // the context moved
  case 3: r.poke(A_CONTEXT, 0, address_value(A_CAMERA2)); break;             // another camera
  case 4: for (int w = 0; w < 2; ++w) World::put<float>(W[w].a[A_CAMERA] + 0x28, .25f); break; // someone else set the distance
  case 5: r.ads(true); r.frame(16667); break;                                // mid-ease toward the game's value
  case 6: r.set(r.gen, 0, 0); r.frame(300000, .2214f); break;                // a sight pulled back: applied below native, nothing to put back
  case 7: r.frame(300000, .2214f); break;                                    // applied .15 below the sight's .2214
  case 8: r.set(r.gen, .3f, 0); r.frame(300000, .2214f); break;              // applied .3 above the sight's: the sight's value goes back
  case 9: r.poke(A_G_CONTEXT, 0, bytes_value(0, 8)); break;
  case 10: r.s.count = 0; r.frame(16667); r.s.count = 1; break;              // disarmed by a lost owner before the frame could apply
  case 11: r.s.kind = 4; r.frame(16667); break;                              // the last frame was another request: applied = native
  case 12: r.ads(true); r.frame(300000); break;                              // landed on the game's own value
  case 13: r.poke(A_CONTEXT, 0, bytes_value(0x7770000, 8)); break;
  case 14: for (int w = 0; w < 2; ++w) World::put<float>(W[w].a[A_CAMERA] + 0x28, from_bits(0x7fc00000)); break;
  default: r.frame(16667, NATIVE, World::CALL_SITE, SEL_SECOND); r.frame(16667, 1.5f, World::CALL_FOREIGN); break; // other cameras and callers do not disturb the record
  }
  r.end(r.gen); expect(r, 1, OK, "end");
  r.heal(); r.frame(16667); r.arm(); r.frame(16667); r.end(r.gen);
}
// The camera's distance cannot be read at end (its page is gone): nothing is put back, in either.
static void script_restore_unreadable(Run &r) {
  r.fp(); r.arm(); r.frame(1000000);
  for (int w = 0; w < 2; ++w) protect(W[w].a[A_CAMERA], 8, PROT_NONE);
  call_text = "end with the camera unreadable"; for (int w = 0; w < 2; ++w) events[w].clear();
  const uint32_t a = NADSN_End(r.gen), b = tfp_near_end(r.gen);
  for (int w = 0; w < 2; ++w) protect(W[w].a[A_CAMERA], 8, PROT_READ | PROT_WRITE);
  if (a != 1 || b != 1) fail("end returned %u in retail, %u in the translation", a, b);
  compare_events("end with the camera unreadable"); r.inspect(); ++counted.calls;
  r.arm(); r.frame(16667); r.end(r.gen);
}
// ---- the other avatar rows ----
static void script_rows(Run &r, uint32_t row) {
  r.fp(); r.index = row; r.s.count = 8; r.hip = .2f; r.aim = .1f; r.arm();
  r.frame(1000000); r.shoulder(true); frames(r, 12, 16667); r.shoulder(false); frames(r, 12, 16667);
  // The flags of the row before are not this row's.
  for (int w = 0; w < 2; ++w) { World::put<uint8_t>(W[w].a[A_O_FLAGS + ((row + 7) & 7)] + 10, 4); World::put<uint8_t>(W[w].a[A_O_FLAGS + ((row + 7) & 7)] + 15, 32); }
  frames(r, 6, 16667);
  r.s.count = row + 1; r.frame(16667); need(r.got.lost == 0, "the last row in use is still owned");
  if (row) { r.s.count = row; r.frame(16667); need(r.got.lost == 1, "a row past the last one is lost"); r.s.count = 8; r.frame(16667); }
  need(r.got.aim >= 12 && r.got.replaced >= 30, "aim and hip on this row");
  r.end(r.gen);
}
// ---- checks only the translation makes (the reference is the fixture build, which compiles them out) ----
static void script_translation_only(Run &r) {
  r.fp(); r.sync();
  for (int w = 0; w < 2; ++w) { r.write_config(w); World::put<uint64_t>(W[w].a[A_GEN], UINT64_MAX); }
  auto alone = [&](const char *what, uint32_t code) {
    call_text = what; World::put<uint64_t>(W[1].a[A_GEN], UINT64_MAX);
    const uint32_t swaps = translation_cas_calls;
    const uint32_t result = tfp_near_begin(reinterpret_cast<TfpNearConfig *>(W[1].a[A_IO] + IO_CONFIG), reinterpret_cast<uint64_t *>(W[1].a[A_GEN]));
    uint64_t g, slot_now; std::memcpy(&g, reinterpret_cast<void *>(W[1].a[A_GEN]), 8); std::memcpy(&slot_now, reinterpret_cast<void *>(W[1].slot()), 8);
    if (result != 0 || tfp_near_error() != code || g != 0 || slot_now != W[1].native_value || translation_cas_calls != swaps)
      fail("expected 0, error %u, generation 0 and the slot untouched; got %u, error %u, generation %llu, slot class %#llx", code, result, tfp_near_error(), (unsigned long long)g, (unsigned long long)slot_class(1, slot_now));
    ++counted.translation_only;
  };
  answer_getter_executable = false; alone("translation only: the getter is not executable (cpp:185)", API_MISMATCH); answer_getter_executable = true;
  answer_pin_executable = false; alone("translation only: the executable cannot be pinned (cpp:281-283)", PIN_FAILED); answer_pin_executable = true;
  // Neither refusal recorded a setter or consumed a generation: the first compared begin gives generation 1 in both.
  ::last_error.store(PIN_FAILED); // the original is given the error code the translation was left with
  r.arm(); if (r.gen != 1) fail("generation %llu after the translation-only refusals", (unsigned long long)r.gen);
  r.frame(1000000); r.end(r.gen);
}
// ---- tfp_near_reset_for_tests: after it the translation must be where a fresh process is ----
// The original has no reset; its start-up values are put back here by hand (every static of the retail file).
static void script_reset(Run &r) {
  r.fp(); r.hip = .3f; r.aim = .2f; r.arm(); r.end(r.gen); r.arm(); r.end(r.gen); // generations 1 and 2 retired
  r.arm(); r.frame(1000000); r.shoulder(true); frames(r, 3, 16667);
  for (int i = 0; i < 20; ++i) r.frame(16667, .2214f, World::CALL_MANY + i);
  r.s.count = 0; r.frame(16667); r.s.count = 1; // left reserved, installed, disarmed, mid-ease, with a full caller table and an error code
  tfp_near_reset_for_tests();
  ::original.store(nullptr); ::installed.store(0); ::armed.store(0); ::reserved.store(0); ::last_error.store(0);
  ::seen.store(0); ::forwarded.store(0); ::replaced.store(0); ::ads_forwarded.store(0); ::aim_applied.store(0); ::other_kind.store(0); ::text_low.store(0); ::text_high.store(0);
  ::hip_meters.store(0); ::aim_meters.store(0); ::last_native.store(0); ::last_applied.store(0);
  ::slot = 0; ::slot_image = 0; ::generation_counter = 0; ::owned_generation = 0; ::retired_generation = 0; ::possible = false;
  ::policy = ::Policy{};
  for (auto &row : ::site_rows) { row.address.store(0); row.calls.store(0); row.owned.store(0); row.value.store(0); }
  ::site_overflow.store(0); ::shown_meters.store(0); ::shown_at.store(0);
  fixture_cas_calls = 0; translation_cas_calls = 0;
  Run::set_slot(SLOT_NATIVE);
  for (int w = 0; w < 2; ++w) World::put<float>(W[w].a[A_CAMERA] + 0x28, 0.0f);
  r = Run(); r.fp(); r.inspect();
  r.read_stats(0); r.end(1); r.end(2); r.end(3); r.set(1, .1f, .1f); r.set(3, .1f, .1f); r.read_stats(3); r.frame(16667);
  // The hook before any begin: the original would call a null pointer. The translation alone: it must hand the
  // host a null original (nothing left over from the lease before the reset), unarmed.
  call_text = "translation only: the hook right after the reset"; expect_null_original = true; events[1].clear();
  tfp_near_setter_from(W[1].caller(World::CALL_SITE), reinterpret_cast<void *>(W[1].a[A_CAMERA]), NATIVE);
  expect_null_original = false;
  if (events[1].empty() || events[1].back().kind != EV_FORWARD || events[1].back().v[2] != 1 || last_forwarded[1] != NATIVE || tfp_near_error() != NADSN_Error()) fail("expected only the game's own value forwarded to a null original and no error");
  // The original is given the same call's counts and the camera its value.
  ::seen.store(1); ::forwarded.store(1); ::site_rows[0].address.store(W[0].caller(World::CALL_SITE)); ::site_rows[0].calls.store(1); ::site_rows[0].value.store(NATIVE);
  World::put<float>(W[0].a[A_CAMERA] + 0x28, NATIVE);
  ++counted.translation_only;
  r.inspect();
  r.arm(); if (r.gen != 1) fail("after the reset begin gave generation %llu, not 1", (unsigned long long)r.gen);
  r.frame(16667); r.frame(16667); r.ads(true); frames(r, 5, 16667); r.end(r.gen); r.end(r.gen);
}
// ---- seeded random walks mixing everything ----
static void script_walk(Run &r, uint32_t seed, int steps, bool check) {
  std::mt19937 rng(seed); auto n = [&](uint32_t limit) { return static_cast<uint32_t>(rng() % limit); };
  static const float values[] = {.2214f, .3521f, .115f, 1.0f, 1.5f, .03f, 0.0f, from_bits(0x7fc00000), from_bits(0x3d4cccce), .15f, .999f, -1.0f, HUGE_VALF};
  static const float settings[] = {0, .03f, .05f, .08f, .15f, .15f, .3f, .4f, 1.0f, .0501f, 1.5f, -.1f, from_bits(0x7fc00000)};
  static const int64_t steps_us[] = {6944, 6944, 16667, 16667, 16667, 16667, 33333, 4167, 1, 0, 49999, 50000, 50001, 120000, 250000, 250001, 400000, -1, -20000};
  const std::vector<Poke> damages = damage_list();
  r.fp(); r.arm();
  for (int i = 0; i < steps; ++i) {
    const uint32_t op = n(1000);
    if (op < 700) {
      const float v = n(10) < 7 ? NATIVE : values[n(13)];
      const int caller = n(20) < 17 ? World::CALL_SITE : n(3) ? static_cast<int>(n(8)) : World::CALL_MANY + static_cast<int>(n(World::CALL_MANY_COUNT));
      const int camera = n(30) < 28 ? SEL_OWN : 1 + static_cast<int>(n(4));
      r.frame(steps_us[n(19)], v, caller, camera);
    } else if (op < 790) {
      switch (n(9)) {
      case 0: case 1: r.ads(!r.s.controls[4]); break;
      case 2: case 3: { static const uint8_t a[] = {0, 4, 4, 0xff, 0xfb}, b[] = {0, 32, 32, 0xff, 0xdf}; r.s.flags[10] = a[n(5)]; r.s.flags[15] = b[n(5)]; break; }
      case 4: { static const uint32_t kinds[] = {6, 6, 6, 6, 4, 0x10006, 0x8006, 0}; r.s.kind = kinds[n(8)]; break; }
      case 5: r.s.head = n(4) ? 0 : n(40); r.s.tail = n(4) ? 2 : n(40); break;
      case 6: { static const uint32_t counts[] = {1, 1, 1, 1, 2, 8, 0, 9}; r.s.count = counts[n(8)]; break; }
      case 7: r.s.idle = n(2) ? 4 : 6; break;
      default: r.fp(); r.s.count = 1; break;
      }
    } else if (op < 825) r.set(n(12) ? r.gen : r.gen + n(3) - 1, settings[n(13)], settings[n(13)]);
    else if (op < 835) r.end(n(8) ? r.gen : r.gen - 1 + n(3));
    else if (op < 880) {
      if (n(5)) { r.heal(); fixture_slot_readonly = false; }
      r.index = n(12) ? 0 : n(3); r.hip = settings[n(11)]; r.aim = settings[n(11)];
      r.begin();
    } else if (op < 895) { const Poke &p = damages[n(static_cast<uint32_t>(damages.size()))]; r.pokes.push_back(p); }
    else if (op < 935) { r.heal(); fixture_slot_readonly = false; fixture_cas_fail_at = fixture_foreign_cas_at = 0; r.fp(); r.s.count = 1; }
    else if (op < 950) r.read_stats(n(3) ? r.gen : n(2), n(8) ? 88 : 87, n(8) ? 1 : 2, n(8) ? 0 : 1 + static_cast<int>(n(2)));
    else if (op < 960) r.read_sites(n(4) ? 16 : n(20), n(8) ? 0 : 1 + static_cast<int>(n(2)));
    else if (op < 968) { if (n(2)) fixture_cas_fail_at = fixture_cas_calls + 1 + n(2); else { fixture_foreign_value = SLOT_FOREIGN; fixture_foreign_cas_at = fixture_cas_calls + 1 + n(2); } }
    else if (op < 975) fixture_slot_readonly = !fixture_slot_readonly;
    else if (op < 985) { static const uint64_t times[] = {0, 1, 5, 1ull << 40}; if (n(3)) fixture_now_us += 1000000 + n(5000000); else fixture_now_us = times[n(4)]; }
    else if (op < 993) { static const uint64_t which[] = {SLOT_NATIVE, SLOT_NATIVE, SLOT_FOREIGN, SLOT_HELPER}; Run::set_slot(which[n(4)]); }
    else if (n(4) == 0) generation_locks_after_write = true; // armed for the next call, whatever it is
    else if (n(3) == 0) readonly_after_cas = true;
    else if (n(2)) readonly_after_policy_lock = true;
    else after_cas = {damages[n(static_cast<uint32_t>(damages.size()))]};
  }
  if (check) need(r.got.replaced > 100 && r.got.ads > 20 && r.got.aim > 5 && r.got.other > 20 && r.got.gliding > 20 && r.got.lost > 2, "a walk through every kind of frame");
}
// ---- probes: one call, and one run for every read it makes (failing; failing for the rest of the call; and
// each value of the damage list changing just before that read) ----
static void probe_script(Run &r, int which) {
  r.probe_values = damage_list();
  r.fp(); r.hip = .2f; r.aim = .1f;
  auto tail = [&] {
    r.frame(16667); r.frame(16667, .2214f); r.heal(); r.fp(); r.s.count = r.index + 1; r.frame(16667); r.read_stats(r.gen);
    r.end(r.gen); r.end(r.gen); Run::set_slot(SLOT_NATIVE);
    r.arm(); r.frame(16667); r.frame(16667); r.end(r.gen);
  };
  switch (which) {
  case 0: r.probe(); r.begin(); tail(); break;
  case 1: r.arm(); r.frame(1000000); r.end(r.gen); r.probe(); r.begin(); tail(); break;
  case 2: r.arm(); r.frame(1000000); r.probe(); r.end(r.gen); tail(); break;
  case 3: r.arm(); r.probe(); r.end(r.gen); tail(); break;
  case 4: r.arm(); r.frame(1000000); r.ads(true); r.frame(16667); r.ads(false); r.probe(); r.frame(16667); tail(); break;
  case 5: r.arm(); r.frame(1000000); r.shoulder(true); r.probe(); r.frame(16667); tail(); break;
  case 6: r.arm(); r.frame(1000000); r.ads(true); r.probe(); r.frame(16667, .2214f); tail(); break;
  case 7: r.arm(); r.frame(1000000); r.s.kind = 4; r.probe(); r.frame(16667); tail(); break;
  case 8: r.arm(); r.frame(1000000); r.s.tail = 0; r.s.idle = 6; r.probe(); r.frame(16667, .3521f); tail(); break;
  case 9: r.arm(); r.frame(1000000); r.probe(); r.read_stats(r.gen); tail(); break;
  case 10: r.index = 1; r.s.count = 2; r.arm(); r.frame(1000000); r.shoulder(true); r.probe(); r.frame(16667); tail(); break;
  default: r.arm(); r.frame(1000000); r.set(r.gen, .3f, 0); r.frame(300000, .2214f); r.probe(); r.end(r.gen); tail(); break; // end that puts a sight's value back
  }
}

static void register_scripts() {
  for (double fps : {30.0, 60.0, 144.0, 240.0}) {
    const int64_t step = static_cast<int64_t>(1000000.0 / fps);
    char name[96];
    std::snprintf(name, sizeof name, "hip distance, %g fps", fps); add(name, [=](Run &r) { script_hip(r, step); });
    std::snprintf(name, sizeof name, "hip, normal aim and full sights, %g fps", fps); add(name, [=](Run &r) { script_views(r, step); });
    std::snprintf(name, sizeof name, "a sight's own distance, %g fps", fps); add(name, [=](Run &r) { script_sight(r, step); });
  }
  const char *views[] = {"hip", "normal aim", "full sights", "another request", "idle first-person request", "hip with distance 0", "normal aim with distance 1"};
  for (int v = 0; v < 7; ++v) add(std::string("every value on ") + views[v], [=](Run &r) { script_values(r, v); });
  add("ease against time, hip 0.15", [](Run &r) { script_time(r, .15f, .08f, 60); });
  add("ease against time, hip 1.0", [](Run &r) { script_time(r, 1.0f, 0, 60); });
  add("ease against time, hip 0.052", [](Run &r) { script_time(r, .052f, .0515f, 0); });
  add("other callers and cameras; the caller table", script_callers);
  add("requests other than the first-person one", script_requests);
  for (int what = 0; what < 12; ++what) for (int view = 0; view < 4; ++view) {
    char name[64]; std::snprintf(name, sizeof name, "owner lost, case %d on view %d", what, view); add(name, [=](Run &r) { script_lost(r, what, view); });
  }
  add("control calls in right and wrong orders", script_controls);
  add("refused configurations and worlds", script_refusals);
  add("second executable image first", script_second_exe);
  for (int v = 0; v < 17; ++v) { char name[64]; std::snprintf(name, sizeof name, "dispatch slot, case %d", v); add(name, [=](Run &r) { script_slot(r, v); }); }
  for (int v = 0; v < 16; ++v) { char name[64]; std::snprintf(name, sizeof name, "distance put back at end, case %d", v); add(name, [=](Run &r) { script_restore(r, v); }); }
  add("distance put back at end, camera unreadable", script_restore_unreadable);
  for (uint32_t row : {0u, 1u, 3u, 7u}) { char name[64]; std::snprintf(name, sizeof name, "avatar row %u", row); add(name, [=](Run &r) { script_rows(r, row); }); }
  add("checks only the translation makes", script_translation_only);
  add("after tfp_near_reset_for_tests", script_reset);
  const char *probes[] = {"first begin", "begin after a lease", "end with our distance on the camera", "end right after begin", "hip frame in mid-glide", "normal-aim frame",
                          "full-sights frame", "frame of another request", "frame of the idle request", "read_stats", "normal-aim frame on row 1", "end that puts a sight's value back"};
  for (int which = 0; which < 12; ++which) add(std::string("probe: ") + probes[which], [=](Run &r) { probe_script(r, which); }, true);
  // TFP_DIFF_EXTRA_WALKS=n: n more walks (a soak; not part of the normal run).
  const char *extra = std::getenv("TFP_DIFF_EXTRA_WALKS");
  const uint32_t walks = 24 + (extra ? static_cast<uint32_t>(std::atoi(extra)) : 0u);
  for (uint32_t seed = 1; seed <= walks; ++seed) { char name[64]; std::snprintf(name, sizeof name, "random walk, seed %u", seed); add(name, [=](Run &r) { script_walk(r, 25480438u + seed, 8000, seed <= 16); }); }
}

// ---- the runner ----
struct Job { size_t script; int mode, index, value; };
static int running = 0, failed = 0, finished = 0;
static void reap() {
  int status = 0; const pid_t pid = wait(&status);
  if (pid < 0) return;
  --running; ++finished;
  if (WIFSIGNALED(status)) { ++failed; std::fprintf(stderr, "a near helper script's process ended on signal %d\n", WTERMSIG(status)); }
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
  if (only && !std::strcmp(only, "--list")) { for (const Script &s : scripts) std::printf("%s\n", s.name.c_str()); return 0; }
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
  std::printf("Near helper: %zu scripts and %zu probe runs; %llu calls compared (%llu of them hook calls), %llu reads, writes, swaps, lock steps and forwarded calls matched; "
              "%llu translation-only host answers; %llu hook calls skipped (before any begin): %s\n",
              plain, probe_runs, (unsigned long long)shared->calls, (unsigned long long)shared->frames, (unsigned long long)shared->events,
              (unsigned long long)shared->translation_only, (unsigned long long)shared->skipped_frames,
              failed ? "DIFFERENCES FOUND" : "the translation behaves as the retail helper in every compared call");
  return failed ? 1 : 0;
}
