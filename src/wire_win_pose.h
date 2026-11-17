/* The Windows / HellForge half of the pose (editor) helper for the feel build: its TfpPoseHost. See
 * wire_win_pose.c. Call tfp_win_setup (wire_win.h) first. */
#ifndef TFP_WIRE_WIN_POSE_H
#define TFP_WIRE_WIN_POSE_H
#include "wire_win.h"
#include "wire_pose.h"

/* One record per value the helper can put in a slot, by TFP_POSE_HOOK_* (pose_helper.h). `calls` is
 * 0 or 1 here (fired at least once): these slots carry every unit's calls, so the hooks count nothing.
 * The two input thunks are assembly and record nothing at all. */
enum { TFP_WIN_POSE_HOOKS = TFP_POSE_HOOK_COUNT };

/* Sets the helper's host (tfp_pose_set_host): the six hook values, memory, the dispatch-slot swap with
 * the pin check, two SRW locks. Before the first adapter call. */
void tfp_win_pose_hosts(void);
/* The adapter's statistics tick: GetTickCount64, no clock function (pose.lua:29-36, backend.lua:807). */
const TfpPoseAdapterIo *tfp_win_pose_io(void);
/* The five functions the pose slots must hold (UnitAPI +0x38, +0x48, +0x40, +0x00, +0x90), read as they
 * were before any HellForge hook, and what the five slots hold now. 1: all pinned and in place; 0: not
 * (`text` says which); -1: something is not readable yet. `text` always gets one line. */
int tfp_win_pose_check(char *text, size_t size);
const TfpWinHook *tfp_win_pose_hook(int which);
const char *tfp_win_pose_hook_name(int which);
void tfp_win_pose_hook_refusal_seen(int which);
/* Session end, as tfp_win_restore_slots: one compare-and-swap for every slot that still holds one of
 * the helper's six values, nothing else. Returns how many were put back. */
unsigned tfp_win_pose_restore_slots(void);
#endif
