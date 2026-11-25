/* Round 28 (docs/design/specs/2026-11-25-performance.md): host_write without WriteProcessMemory.
 *
 * Built for Windows with zig (tests/run-host-write.sh) and run under Wine when it is there. The five helpers'
 * output paths (every host.write in camera_helper.c, fov_helper.c, near_helper.c, blur_helper.c, pose_helper.c) are
 * driven twice with the same helper state:
 *   A. through a test host whose write is WriteProcessMemory(GetCurrentProcess()), what host_write was up to
 *      0.6.0-feel.53;
 *   B. through the mod's own hosts: tfp_win_hosts (src/wire_win.c) for camera, FOV, near and blur, and for the pose
 *      helper a host whose write is tfp_win_write, as src/wire_win_pose.c gives it.
 * The answers, the helpers' error codes and every byte of the output structs must be the same; the destination is
 * pre-filled with a pattern so a write that did not happen shows. Then the counters of the Perf line
 * (tfp_win_counts): host_write counts every write of B and WriteProcessMemory none of them; VirtualQuery and the log
 * count theirs. Last, the guarded copy itself on a page it may not write and on a null address: false, nothing
 * written, no crash. */
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hellforge_mod.h"
#include "guarded_copy.h"
/* controller.h (through wire_win.h) names a type TfpPoseStats too: the helper's is renamed here, as src/wire_pose.h does */
#define TfpPoseStats TfpPoseHelperStats
#include "pose_helper.h"
#undef TfpPoseStats
#include "wire_win.h"
#include "camera_helper.h"
#include "fov_helper.h"
#include "near_helper.h"
#include "blur_helper.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("host write: FAILED %s (line %d)\n", #c, __LINE__); } } while (0)

/* ---- the HellForge API the mod's hosts need: an in-process read, a log ---- */
static unsigned logged;
static int fake_read(uintptr_t at, void *out, size_t size) { return tfp_guarded_copy(out, (const void *)at, size) ? 0 : 1; }
static int fake_write(uintptr_t at, const void *data, size_t size) { (void)at; (void)data; (void)size; return 1; }
static uintptr_t fake_module(const char *name, size_t *size) { (void)name; *size = 0; return 0x10000; } /* not an image: no code ranges */
static void fake_log(HfMod *mod, const char *format, ...) { (void)mod; (void)format; logged++; }

/* ---- host A: what host_write was ---- */
static unsigned process_writes;
static bool a_write(void *u, void *out, const void *data, size_t length) {
  SIZE_T written = 0;
  (void)u;
  process_writes++;
  return WriteProcessMemory(GetCurrentProcess(), out, data, length, &written) && written == length;
}
static bool a_read(void *u, uint64_t at, void *out, size_t length) { (void)u; return tfp_guarded_copy(out, (const void *)(uintptr_t)at, length); }
static SRWLOCK a_locks[4];
static void a_lock(void *u, int lock) { (void)u; AcquireSRWLockExclusive(&a_locks[lock & 3]); }
static void a_unlock(void *u, int lock) { (void)u; ReleaseSRWLockExclusive(&a_locks[lock & 3]); }
static void a_lock_shared(void *u, int lock) { (void)u; AcquireSRWLockShared(&a_locks[lock & 3]); }
static void a_unlock_shared(void *u, int lock) { (void)u; ReleaseSRWLockShared(&a_locks[lock & 3]); }
static uint64_t a_tick(void *u) { (void)u; return 1000; }
static bool b_write(void *u, void *out, const void *data, size_t length) { (void)u; return tfp_win_write(out, data, length); }

static void hosts_a(void) {
  TfpCameraHost c; memset(&c, 0, sizeof c);
  c.read = a_read, c.write = a_write, c.lock_exclusive = a_lock, c.unlock_exclusive = a_unlock, c.lock_shared = a_lock_shared, c.unlock_shared = a_unlock_shared;
  tfp_camera_set_host(&c);
  TfpFovHost f; memset(&f, 0, sizeof f);
  f.read = a_read, f.write = a_write, f.lock_exclusive = a_lock, f.unlock_exclusive = a_unlock;
  tfp_fov_set_host(&f);
  TfpNearHost n; memset(&n, 0, sizeof n);
  n.read = a_read, n.write = a_write, n.lock_exclusive = a_lock, n.unlock_exclusive = a_unlock, n.lock_shared = a_lock_shared, n.unlock_shared = a_unlock_shared;
  tfp_near_set_host(&n);
  TfpBlurHost b; memset(&b, 0, sizeof b);
  b.read = a_read, b.write = a_write, b.lock_exclusive = a_lock, b.unlock_exclusive = a_unlock;
  tfp_blur_set_host(&b);
}
static void pose_host(bool real) {
  TfpPoseHost p; memset(&p, 0, sizeof p);
  p.read = a_read, p.write = real ? b_write : a_write, p.tick = a_tick;
  p.lock_exclusive = a_lock, p.unlock_exclusive = a_unlock, p.lock_shared = a_lock_shared, p.unlock_shared = a_unlock_shared;
  tfp_pose_set_host(&p);
}

/* ---- one pass over every output path; `out` gets each answer, error and struct, back to back ---- */
#define PATTERN 0xA5
typedef struct Pass {
  uint32_t answers[24], errors[24];
  TfpCameraStats cs; TfpCameraHandoffData ch; TfpCameraEntryTraceData ce; TfpCameraAnchorData ca; uint64_t cg;
  TfpFovStats fs; uint64_t fg;
  TfpNearStats ns; TfpNearSite sites[4]; uint64_t ng;
  TfpBlurStats bs; uint64_t bg;
  TfpPoseHelperStats ps; TfpPoseDepthStats pd; TfpPoseInputStats pi; TfpPoseRotationTraceData pr; TfpPoseEntryPositionData pe; uint64_t pg;
} Pass;
static void pass(Pass *o) {
  unsigned k = 0;
  memset(o, PATTERN, sizeof *o);
  memset(o->answers, 0, sizeof o->answers), memset(o->errors, 0, sizeof o->errors);
#define SIZED(s, abi_value) (memset(&(s), PATTERN, sizeof (s)), (s).size = sizeof (s), (s).abi = (abi_value))
#define ANSWER(call, error) (o->answers[k] = (call), o->errors[k++] = (error)())
  /* camera: stats (cpp:1130-1138), handoff, entry trace, anchor, and a begin refused on its config (generation 0 written first) */
  SIZED(o->cs, 1); ANSWER(tfp_camera_read_stats(0, &o->cs), tfp_camera_error);
  ANSWER(tfp_camera_handoff_stats(0, &o->ch), tfp_camera_error);
  ANSWER(tfp_camera_entry_trace(&o->ce), tfp_camera_error);
  ANSWER(tfp_camera_anchor_stats(&o->ca), tfp_camera_error);
  TfpCameraConfig cc; memset(&cc, 0, sizeof cc);
  ANSWER(tfp_camera_begin(&cc, &o->cg), tfp_camera_error);
  /* FOV */
  SIZED(o->fs, 1); ANSWER(tfp_fov_read_stats(0, &o->fs), tfp_fov_error);
  TfpFovConfig fc; memset(&fc, 0, sizeof fc);
  ANSWER(tfp_fov_begin(&fc, &o->fg), tfp_fov_error);
  /* near clip: stats, the call sites (none: no hook ran), a refused begin */
  SIZED(o->ns, 1); ANSWER(tfp_near_read_stats(0, &o->ns), tfp_near_error);
  ANSWER(tfp_near_read_sites(o->sites, 4), tfp_near_error);
  TfpNearConfig nc; memset(&nc, 0, sizeof nc);
  ANSWER(tfp_near_begin(&nc, &o->ng), tfp_near_error);
  /* blur */
  SIZED(o->bs, 1); ANSWER(tfp_blur_read_stats(0, &o->bs), tfp_blur_error);
  TfpBlurConfig bc; memset(&bc, 0, sizeof bc);
  ANSWER(tfp_blur_begin(&bc, &o->bg), tfp_blur_error);
  /* pose: stats (abi 2), depth, input, rotation trace, entry position, a refused begin */
  SIZED(o->ps, 2); ANSWER(tfp_pose_read_stats(0, &o->ps), tfp_pose_error);
  SIZED(o->pd, 1); o->pd.reserved = 0; ANSWER(tfp_pose_read_depth_stats(0, &o->pd), tfp_pose_error);
  SIZED(o->pi, 1); o->pi.reserved = 0; ANSWER(tfp_pose_read_input_stats(0, &o->pi), tfp_pose_error);
  ANSWER(tfp_pose_rotation_trace(&o->pr), tfp_pose_error);
  ANSWER(tfp_pose_entry_position_stats(&o->pe), tfp_pose_error);
  TfpPoseConfig pc; memset(&pc, 0, sizeof pc);
  ANSWER(tfp_pose_begin(&pc, &o->pg), tfp_pose_error);
#undef SIZED
#undef ANSWER
}

int main(void) {
  static HfApi api;
  api.version = 16, api.size = sizeof api;
  api.log = fake_log, api.read = fake_read, api.write = fake_write, api.module_base = fake_module, api.read_code = fake_read;
  tfp_win_setup(&api, NULL); /* false (no image at the fake base): only `hf` is wanted */

  /* A: WriteProcessMemory, after a first pass that leaves each helper in the state every later pass leaves it in
   * (a refused begin sets its error, which the next stats read reports) */
  static Pass a, b;
  hosts_a();
  pose_host(false);
  pass(&a);
  process_writes = 0;
  pass(&a);
  const unsigned writes_a = process_writes;
  CHECK(writes_a == 16); /* every path but the near sites (no hook ran) and the camera handoff (no owner) wrote once */

  /* B: the mod's own hosts */
  TfpOpsHost memory;
  TfpWirePlatform platform;
  TfpWireHelpers helpers;
  tfp_win_hosts(&memory, &platform, &helpers);
  pose_host(true);
  uint64_t before[TFP_WIN_COUNTS], after[TFP_WIN_COUNTS];
  tfp_win_counts(before);
  pass(&b);
  tfp_win_counts(after);
  CHECK(process_writes == writes_a); /* host A was not used */
  CHECK(!memcmp(a.answers, b.answers, sizeof a.answers) && !memcmp(a.errors, b.errors, sizeof a.errors));
  CHECK(!memcmp(&a, &b, sizeof a)); /* byte for byte, the pattern where nothing was written included */
  /* what was written was written: the stats carry their size and the generation of a refused begin is 0 */
  CHECK(b.cs.size == sizeof b.cs && b.ce.size == sizeof b.ce && b.ca.size == sizeof b.ca && b.cg == 0);
  /* the camera's handoff answers only its owner (cpp:1258-1267): refused here, before any write, on both paths */
  CHECK(b.ch.size == 0xA5A5A5A5u && b.answers[1] == 0);
  CHECK(b.fs.size == sizeof b.fs && b.fg == 0 && b.ns.size == sizeof b.ns && b.ng == 0 && b.bs.size == sizeof b.bs && b.bg == 0);
  CHECK(b.ps.size == sizeof b.ps && b.pd.size == sizeof b.pd && b.pi.size == sizeof b.pi && b.pr.size == sizeof b.pr && b.pe.size == sizeof b.pe && b.pg == 0);
  { uint8_t untouched[sizeof b.sites]; memset(untouched, PATTERN, sizeof untouched); CHECK(!memcmp(b.sites, untouched, sizeof untouched)); }

  /* the Perf line's counters */
  CHECK(after[TFP_WIN_COUNT_HOST_WRITE] - before[TFP_WIN_COUNT_HOST_WRITE] == writes_a); /* every write of B, one each */
  CHECK(after[TFP_WIN_COUNT_PROCESS_WRITE] == before[TFP_WIN_COUNT_PROCESS_WRITE]);      /* none on WriteProcessMemory */
  tfp_win_counts(before);
  CHECK(!tfp_win_writable32(0x10)); /* one VirtualQuery: the first page is not committed */
  memory.log(memory.user, "a line");
  tfp_win_counts(after);
  CHECK(after[TFP_WIN_COUNT_QUERY] == before[TFP_WIN_COUNT_QUERY] + 1 && after[TFP_WIN_COUNT_LOG] == before[TFP_WIN_COUNT_LOG] + 1 && logged == 1);

  /* the guarded copy where it may not write: false, nothing written, the process goes on */
  uint8_t *page = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  DWORD old = 0;
  CHECK(page != NULL);
  memset(page, 0x5A, 64);
  CHECK(VirtualProtect(page, 4096, PAGE_READONLY, &old));
  const uint64_t value = 0x1122334455667788ull;
  CHECK(!tfp_win_write(page, &value, sizeof value) && page[0] == 0x5A && page[7] == 0x5A);
  CHECK(!tfp_win_write(NULL, &value, sizeof value));
  CHECK(!tfp_win_write(&b.cg, (const void *)(uintptr_t)0x10, 8)); /* an unreadable source */
  uint64_t into = 0;
  CHECK(tfp_win_write(&into, &value, sizeof value) && into == value && tfp_win_write(&into, &value, 0));
  VirtualFree(page, 0, MEM_RELEASE);

  printf("Host write: %d checks, %d failed (%u helper writes on WriteProcessMemory and on the guarded copy: same answers, errors and bytes)\n",
         checks, failures, writes_a);
  return failures != 0;
}
