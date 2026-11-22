/* The arm graft's maths (src/arm_graft.h) against the retired First Person Controller.
 *
 *   arm_graft_test FIXTURE   every frame of the fixture: the bind, the pose, the blends, the entry alignment
 *                            and the recovery of the game's own values must be the retired functions' results
 *                            bit for bit. The fixture is tests/reference/arm-graft/paired-poses.bin, made by
 *                            tests/arm_graft_fixture.py from poses recorded in the game on build 25480438.
 * Then the retired unit test of the map (tests/native_view_test.c test_fp_graft), refusals of the bind on
 * wrong names and links, and the two ends of the blend. */
#include "arm_graft.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static const TfpGraftMatrix ID = {.values = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};

static FILE *in;
static void take(void *out, size_t n) { if (fread(out, 1, n, in) != n) { fprintf(stderr, "fixture is short\n"); exit(1); } }
static uint32_t word(void) { uint32_t v; take(&v, 4); return v; }
static uint32_t flag(void) { uint32_t v = word(); word(), word(), word(); return v; }

static void fixture(const char *path) {
  char magic[16];
  uint32_t bn[TFP_GRAFT_BODY_NODES], bl[TFP_GRAFT_BODY_NODES], fn[TFP_GRAFT_RIG_NODES], fl[TFP_GRAFT_RIG_NODES];
  static TfpGraftMatrix body[TFP_GRAFT_BODY_NODES], fp[TFP_GRAFT_RIG_NODES], want[TFP_GRAFT_JOINTS], got[TFP_GRAFT_JOINTS];
  static TfpGraftLocal native[TFP_GRAFT_JOINTS], wanted[TFP_GRAFT_JOINTS];
  static const float weights[3] = {.25f, .5f, 1.f};
  TfpGraft map, expected;
  in = fopen(path, "rb");
  if (!in) { fprintf(stderr, "%s: cannot be opened\n", path); exit(1); }
  take(magic, 16);
  CHECK(!memcmp(magic, "TFP-ARM-GRAFT-1\n", 16));
  const uint32_t frames = word();
  CHECK(word() == TFP_GRAFT_BODY_NODES && word() == TFP_GRAFT_RIG_NODES && word() == TFP_GRAFT_JOINTS && frames > 0);
  take(bn, sizeof bn), take(bl, sizeof bl), take(fn, sizeof fn), take(fl, sizeof fl);
  for (uint32_t frame = 0; frame < frames; frame++) {
    if (!frame) {
      for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) expected.body[i] = word();
      for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) expected.fp[i] = word();
      for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) expected.parent[i] = word();
      expected.head = flag();
      take(&expected.alignment, 64), take(&expected.grip, 64);
    }
    take(body, sizeof body), take(fp, sizeof fp);
    if (!frame) {
      memset(&map, 0xee, sizeof map);
      CHECK(tfp_graft_bind(&map, bn, bl, fn, fl, body, fp));
      CHECK(!memcmp(map.body, expected.body, sizeof map.body) && !memcmp(map.fp, expected.fp, sizeof map.fp) &&
            !memcmp(map.parent, expected.parent, sizeof map.parent) && map.head == expected.head);
      CHECK(!memcmp(&map.alignment, &expected.alignment, 64) && !memcmp(&map.grip, &expected.grip, 64));
      CHECK(map.fp[0] == 10 && fn[10] == TFP_GRAFT_NAME_L_SHOULDER && map.parent[0] == 7); /* the test bend's joint */
      CHECK(fn[TFP_GRAFT_RIG_EYE] == TFP_GRAFT_NAME_RIG_EYE);
    }
    /* 1. the pose */
    const uint32_t ok = flag();
    take(want, sizeof want);
    memset(got, 0, sizeof got);
    CHECK(tfp_graft_pose(&map, body, got) == (ok != 0));
    CHECK(!ok || !memcmp(got, want, sizeof got));
    /* 2, 3. the game's locals and the blends; weight 1 is the pose itself in the game's record */
    take(native, sizeof native);
    for (unsigned w = 0; w < 3; w++) {
      const uint32_t mixed_ok = flag();
      take(wanted, sizeof wanted);
      for (unsigned i = 0; ok && i < TFP_GRAFT_JOINTS; i++) {
        TfpGraftLocal mixed;
        const bool mixed_now = tfp_graft_mix(&native[i], &got[i], weights[w], &mixed);
        CHECK(mixed_now || !mixed_ok);
        if (mixed_ok) CHECK(!memcmp(&mixed, &wanted[i], 64));
        if (w == 2) { /* weight 1: bit-identical to the retired pose, tail of the record the game's */
          CHECK(mixed_now && !memcmp(mixed.basis, got[i].values, 12) && !memcmp(mixed.basis + 3, got[i].values + 4, 12) &&
                !memcmp(mixed.basis + 6, got[i].values + 8, 12) && !memcmp(mixed.position, got[i].values + 12, 12) &&
                !memcmp(mixed.preserved, native[i].preserved, 16));
        }
        TfpGraftLocal zero; /* weight 0: the game's local, all 64 bytes */
        CHECK(tfp_graft_mix(&native[i], &got[i], 0, &zero) && !memcmp(&zero, &native[i], 64));
      }
    }
    /* 4. the entry alignment */
    TfpGraft aligned = map;
    TfpGraftMatrix alignment;
    const uint32_t aligned_ok = flag();
    take(&alignment, 64), take(want, sizeof want);
    bool aligned_now = tfp_graft_align(&aligned, body, &native[TFP_GRAFT_JOINTS - 1]);
    if (aligned_now) {
      CHECK(!memcmp(&aligned.alignment, &alignment, 64));
      aligned_now = tfp_graft_pose(&aligned, body, got);
    }
    CHECK(aligned_now == (aligned_ok != 0));
    if (aligned_ok) {
      CHECK(!memcmp(got, want, sizeof got));
      /* what the alignment is for: the socket lands where the rig's own socket is (not exact: two float products) */
      for (unsigned k = 0; k < 3; k++) CHECK(fabsf(got[TFP_GRAFT_JOINTS - 1].values[12 + k] - native[TFP_GRAFT_JOINTS - 1].position[k]) < 1e-4f);
    }
    /* 5. the game's own values under this mod's writes */
    take(wanted, sizeof wanted);
    for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) {
      TfpGraftLocal now, under;
      tfp_graft_mix(&native[i], &got[i], .5f, &now);
      const TfpGraftLocal applied = now;
      if (i % 3 == 0) now.basis[0] += .125f, now.position[1] = native[i].position[1] + .5f;
      under = tfp_graft_underlying(i % 5 != 4, &native[i], &applied, now);
      CHECK(!memcmp(&under, &wanted[i], 64));
    }
  }
  CHECK(fgetc(in) == EOF);
  fclose(in);
  printf("arm graft: %u recorded frames equal the retired functions bit for bit (%s)\n", frames, path);
}

/* The retired repository's own test of the map (tests/native_view_test.c test_fp_graft), unchanged but for names. */
static void synthetic(uint32_t *bn, uint32_t *bl, uint32_t *fn, uint32_t *fl, TfpGraftMatrix *body, TfpGraftMatrix *fp) {
  memset(bn, 0, TFP_GRAFT_BODY_NODES * 4), memset(bl, 0, TFP_GRAFT_BODY_NODES * 4);
  for (unsigned n = 0; n < TFP_GRAFT_RIG_NODES; ++n) fn[n] = n + 10, fl[n] = ((n > 0 ? n - 1 : 0) << 16) | 1, fp[n] = ID;
  fn[7] = 0x6f3bd021, fn[8] = 0x4fdd3337, fn[13] = 0x5c0c68d3, fn[34] = 0x248004f5;
  fl[10] = fl[30] = fl[8] = (7u << 16) | 1, fl[58] = (12u << 16) | 1;
  for (unsigned i = 0; i < TFP_GRAFT_BODY_NODES; ++i) body[i] = ID;
  for (unsigned n = 0; n < TFP_GRAFT_RIG_NODES; ++n) bn[n + 3] = fn[n], bl[n + 3] = (((fl[n] >> 16) + 3) << 16) | 1;
  bn[99] = 0x8c5570c9, bl[8 + 3] = ((34u + 3) << 16) | 1;
  fp[8].values[12] = .07f;
}
static void retired_unit_test(void) {
  uint32_t bn[TFP_GRAFT_BODY_NODES], bl[TFP_GRAFT_BODY_NODES], fn[TFP_GRAFT_RIG_NODES], fl[TFP_GRAFT_RIG_NODES];
  TfpGraftMatrix body[TFP_GRAFT_BODY_NODES], fp[TFP_GRAFT_RIG_NODES], local[TFP_GRAFT_JOINTS];
  TfpGraft g;
  synthetic(bn, bl, fn, fl, body, fp);
  CHECK(tfp_graft_bind(&g, bn, bl, fn, fl, body, fp));
  body[13 + 3].values[14] = .45f;
  body[8 + 3].values[13] = .2f;
  CHECK(tfp_graft_pose(&g, body, local));
  CHECK(fabsf(local[13 - 10].values[14] - .45f) < 1e-6f);
  CHECK(fabsf(local[41].values[12] - .07f) < 1e-6f && fabsf(local[41].values[13] - .2f) < 1e-6f);
  for (unsigned i = 0; i < TFP_GRAFT_JOINTS; ++i) CHECK(g.fp[i] != 50 && g.fp[i] != 7);
  body[13 + 3].values[0] = NAN;
  CHECK(!tfp_graft_pose(&g, body, local));
  bl[8 + 3] = 0;
  CHECK(!tfp_graft_bind(&g, bn, bl, fn, fl, body, fp));
}
/* The bind refuses anything but the two rigs of this build, and leaves the caller's map alone when it does. */
static void refusals(void) {
  uint32_t bn[TFP_GRAFT_BODY_NODES], bl[TFP_GRAFT_BODY_NODES], fn[TFP_GRAFT_RIG_NODES], fl[TFP_GRAFT_RIG_NODES];
  TfpGraftMatrix body[TFP_GRAFT_BODY_NODES], fp[TFP_GRAFT_RIG_NODES];
  TfpGraft g, untouched;
  memset(&g, 0x5a, sizeof g), memset(&untouched, 0x5a, sizeof untouched);
#define REFUSED(change) do { synthetic(bn, bl, fn, fl, body, fp); change; CHECK(!tfp_graft_bind(&g, bn, bl, fn, fl, body, fp)); \
                             CHECK(!memcmp(&g, &untouched, sizeof g)); } while (0)
  REFUSED(fn[7] ^= 1);                       /* the four names that identify the rig */
  REFUSED(fn[8] ^= 1);
  REFUSED(fn[13] ^= 1);
  REFUSED(fn[34] ^= 1);
  REFUSED(bn[99] = 0);                       /* no head on the body */
  REFUSED(bn[20 + 3] = 0x77777777);          /* a rig joint the body does not have */
  REFUSED(bn[100] = fn[20]);                 /* ... or has twice */
  REFUSED(fl[20] = (19u << 16) | 2);         /* a rig joint linked to another unit */
  REFUSED(fl[20] = (20u << 16) | 1);         /* ... or to itself or a later node */
  REFUSED(fl[20] = (7u << 16) | 1);          /* only the two shoulders and the socket hang from the root */
  REFUSED(fl[20] = (9u << 16) | 1);          /* a parent outside the mapped arm nodes */
  REFUSED(fl[49] = (50u << 16) | 1);
  REFUSED(bl[20 + 3] = (5u << 16) | 1);      /* the body's joint hangs from another parent than the rig's */
  REFUSED(bl[8 + 3] = (36u << 16) | 1);      /* the body's socket is not under the right palm */
  REFUSED(body[0].values[0] = 2);            /* a scaled body root */
  REFUSED(fp[7].values[5] = -1);             /* a mirrored rig root */
  REFUSED(body[20 + 3].values[12] = INFINITY);
  REFUSED(fp[20].values[1] = .5f);
#undef REFUSED
  synthetic(bn, bl, fn, fl, body, fp);
  CHECK(tfp_graft_bind(&g, bn, bl, fn, fl, body, fp));
}
static void blend_and_bend(void) {
  TfpGraftLocal native = {.basis = {0, 1, 0, -1, 0, 0, 0, 0, 1}, .position = {.1f, .2f, .3f}, .preserved = {1, 1, 1, 7}}, out;
  TfpGraftMatrix animated = ID;
  animated.values[12] = .5f;
  CHECK(tfp_graft_mix(&native, &animated, .5f, &out));
  CHECK(fabsf(out.basis[0] - .70710678f) < 1e-6f && fabsf(out.basis[1] - .70710678f) < 1e-6f && fabsf(out.position[0] - .3f) < 1e-6f);
  CHECK(!memcmp(out.preserved, native.preserved, 16));
  { /* the shorter way round: from 80 degrees about Z to -110 degrees is 170 degrees onwards, through 165, not back through -15 */
    const float c = cosf(80 * .01745329252f), s = sinf(80 * .01745329252f), d = cosf(110 * .01745329252f), t = sinf(110 * .01745329252f);
    TfpGraftLocal left = {.basis = {c, s, 0, -s, c, 0, 0, 0, 1}, .preserved = {1, 1, 1, 0}};
    TfpGraftMatrix right = {.values = {d, -t, 0, 0, t, d, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    CHECK(tfp_graft_mix(&left, &right, .5f, &out) && fabsf(out.basis[0] - cosf(165 * .01745329252f)) < 1e-5f &&
          fabsf(out.basis[1] - sinf(165 * .01745329252f)) < 1e-5f);
  }
  native.basis[0] = 3; /* not a rotation: only the two ends are possible */
  CHECK(!tfp_graft_mix(&native, &animated, .5f, &out) && tfp_graft_mix(&native, &animated, 1, &out) && tfp_graft_mix(&native, &animated, 0, &out));
  /* 70 degrees about the joint's own Z: Z stays, X turns towards Y, the position and the tail stay */
  TfpGraftLocal rest = {.basis = {1, 0, 0, 0, 1, 0, 0, 0, 1}, .position = {1, 2, 3}, .preserved = {1, 1, 1, 9}};
  TfpGraftLocal bent = tfp_graft_bend_z(rest, 70);
  CHECK(fabsf(bent.basis[0] - .34202014f) < 1e-6f && fabsf(bent.basis[1] - .93969262f) < 1e-6f && bent.basis[2] == 0);
  CHECK(fabsf(bent.basis[3] + .93969262f) < 1e-6f && fabsf(bent.basis[4] - .34202014f) < 1e-6f && bent.basis[8] == 1);
  CHECK(!memcmp(bent.position, rest.position, 12) && !memcmp(bent.preserved, rest.preserved, 16));
  TfpGraftMatrix as_matrix = tfp_graft_local_matrix(&bent);
  CHECK(tfp_graft_rigid(&as_matrix));
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: arm_graft_test FIXTURE...\n"); return 2; }
  for (int i = 1; i < argc; i++) fixture(argv[i]);
  retired_unit_test();
  refusals();
  blend_and_bend();
  printf("arm graft: %u checks passed\n", checks);
  return 0;
}
