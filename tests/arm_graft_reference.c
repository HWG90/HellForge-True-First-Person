/* Writes tests/reference/arm-graft/paired-poses.bin: recorded body and first-person rig matrices with what
 * the RETIRED First Person Controller's own functions make of them. Compiled by tests/arm_graft_fixture.py
 * against the retired source tree (read-only); never part of the mod. ARM_GRAFT_RETIRED_STATICS is the text
 * of graft_underlying, graft_local_matrix, graft_align_action, graft_set_transform and graft_mix_local, cut
 * out of the retired src/native_arm_graft.h by that script (they are static there, inside a file that needs
 * the whole controller).
 *
 * stdin:  bn[107] bl[107] fn[63] fl[63] (uint32), then per frame body[107] fp[63] (4x4 float32)
 * stdout: the fixture, see tests/arm_graft_test.c for the layout. */
#include "arm_graft.h"
#include "hud_projection.h"
#include <assert.h>
#include <stdio.h>

static struct {
  FpcArmGraft map;
  uint64_t owned;
  FpcNativeTransform original[FPC_GRAFT_JOINTS], applied[FPC_GRAFT_JOINTS];
} graft;
ARM_GRAFT_RETIRED_STATICS

static void take(void *out, size_t n) { assert(fread(out, 1, n, stdin) == n); }
static void give(const void *in, size_t n) { assert(fwrite(in, 1, n, stdout) == n); }
static void word(uint32_t v) { give(&v, 4); }

int main(int argc, char **argv) {
  uint32_t bn[FPC_BODY_BONES], bl[FPC_BODY_BONES], fn[FPC_FP_BONES], fl[FPC_FP_BONES], frames = 0;
  static FpcNativeMatrix body[FPC_BODY_BONES], fp[FPC_FP_BONES];
  static const float weights[2] = {.25f, .5f};
  assert(argc == 2);
  const uint32_t declared = (uint32_t)atoi(argv[1]);
  take(bn, sizeof bn), take(bl, sizeof bl), take(fn, sizeof fn), take(fl, sizeof fl);
  give("TFP-ARM-GRAFT-1\n", 16);
  word(declared), word(FPC_BODY_BONES), word(FPC_FP_BONES), word(FPC_GRAFT_JOINTS);
  give(bn, sizeof bn), give(bl, sizeof bl), give(fn, sizeof fn), give(fl, sizeof fl);
  while (fread(body, sizeof body, 1, stdin) == 1) {
    take(fp, sizeof fp);
    if (!frames) { /* bound once, on the first frame, as the controller binds once per rig */
      assert(fpc_graft_bind(&graft.map, bn, bl, fn, fl, body, fp));
      for (unsigned i = 0; i < FPC_GRAFT_JOINTS; i++) word(graft.map.body[i]);
      for (unsigned i = 0; i < FPC_GRAFT_JOINTS; i++) word(graft.map.fp[i]);
      for (unsigned i = 0; i < FPC_GRAFT_JOINTS; i++) word(graft.map.parent[i]);
      word(graft.map.head), word(0), word(0), word(0);
      give(&graft.map.alignment, 64), give(&graft.map.grip, 64);
    }
    give(body, sizeof body), give(fp, sizeof fp);
    /* 1. the pose with the alignment of the bind */
    FpcNativeMatrix pose[FPC_GRAFT_JOINTS];
    memset(pose, 0, sizeof pose);
    uint32_t ok = fpc_graft_pose(&graft.map, body, pose);
    word(ok), word(0), word(0), word(0);
    give(pose, sizeof pose);
    /* 2. the rig's own locals of this frame (what the game holds), from its world matrices */
    FpcNativeTransform native[FPC_GRAFT_JOINTS];
    for (unsigned i = 0; i < FPC_GRAFT_JOINTS; i++) {
      FpcNativeMatrix local = fpc_relative_pose(&fp[graft.map.fp[i]], &fp[graft.map.parent[i]]);
      native[i] = (FpcNativeTransform){.preserved = {1, 1, 1, (float)i}};
      graft_set_transform(&native[i], &local);
    }
    give(native, sizeof native);
    /* 3. graft_mix_local at .25, .5 and 1, with the record's tail kept as graft_present keeps it */
    for (unsigned w = 0; w < 3; w++) {
      FpcNativeTransform mixed[FPC_GRAFT_JOINTS];
      uint32_t mixed_ok = ok;
      memset(mixed, 0, sizeof mixed);
      for (unsigned i = 0; ok && i < FPC_GRAFT_JOINTS; i++) {
        if (!graft_mix_local(&native[i], &pose[i], w < 2 ? weights[w] : 1.f, &mixed[i])) mixed_ok = 0;
        memcpy(mixed[i].preserved, native[i].preserved, sizeof mixed[i].preserved);
      }
      word(mixed_ok), word(0), word(0), word(0);
      give(mixed, sizeof mixed);
    }
    /* 4. graft_align_action on the rig's own socket local, and the pose it gives */
    FpcArmGraft kept = graft.map;
    uint32_t aligned = graft_align_action(body, &native[FPC_GRAFT_JOINTS - 1]);
    memset(pose, 0, sizeof pose);
    if (aligned) aligned = fpc_graft_pose(&graft.map, body, pose);
    word(aligned), word(0), word(0), word(0);
    give(&graft.map.alignment, 64);
    give(pose, sizeof pose);
    /* 5. graft_underlying: joint i was written with the .5 mix; the game has since rewritten every third joint */
    FpcNativeTransform under[FPC_GRAFT_JOINTS];
    graft.map = kept;
    graft.owned = 0;
    for (unsigned i = 0; i < FPC_GRAFT_JOINTS; i++) {
      FpcNativeTransform now;
      graft_mix_local(&native[i], &pose[i], .5f, &now);
      graft.original[i] = native[i], graft.applied[i] = now;
      if (i % 5 != 4) graft.owned |= UINT64_C(1) << i;
      if (i % 3 == 0) now.basis[0] += .125f, now.position[1] = native[i].position[1] + .5f;
      under[i] = graft_underlying(i, now);
    }
    give(under, sizeof under);
    frames++;
  }
  assert(feof(stdin) && frames == declared);
  return 0;
}
