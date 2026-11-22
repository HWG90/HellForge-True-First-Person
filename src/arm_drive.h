/* The arm drive: the third-person body's arm animation written onto the game's first-person arm rig at
 * the primary world render, every frame the game's own first-person view (camera request kind 6) is
 * shown. Build 25480438. Lifted from the retired First Person Controller's writer
 * (true-first-person-controller-body14-20261118 src/native_arm_graft.h, native_weapon_tree.h,
 * native_attachment_pose.h, native_view.c); the maths is src/arm_graft.h. See arm_drive.c.
 *
 * No Windows calls: memory, the two engine functions and the page test come through the host, so the
 * whole drive runs offline (tests/arm_drive_test.c). The game half is src/render_slot.c. */
#ifndef TFP_ARM_DRIVE_H
#define TFP_ARM_DRIVE_H
#include "arm_graft.h"
#include "backend.h"

/* Why a primary render was not driven (the stats' `reason`, and tfp_arm_drive_reason_name). */
enum {
  TFP_ARM_NONE = 0,
  TFP_ARM_THREAD,   /* the render call was not on the game thread, or no tick preceded it: nothing is read or written */
  TFP_ARM_SNAPSHOT, /* the local player, its camera request or its weapon could not be proven (src/snapshot.c) */
  TFP_ARM_RAGDOLL,  /* the avatar is a ragdoll */
  TFP_ARM_UNITS,    /* the body or the rig is not a live unit, or not the first-person rig resource */
  TFP_ARM_WORLD,    /* a unit is not in the world being rendered */
  TFP_ARM_WEAPON,   /* the gun is not on the rig's socket (node 8) */
  TFP_ARM_CAMERA,   /* the game's camera is not on the rig's eye (node 50) */
  TFP_ARM_API,      /* the engine's setter, its update function or their tables are not the pinned ones */
  TFP_ARM_BIND,     /* the two units are not the two rigs of this build (names, links, counts, a matrix) */
  TFP_ARM_READ,     /* a pose could not be read */
  TFP_ARM_POSE,     /* the body's pose is not rigid, or a blend was not possible */
  TFP_ARM_TREE,     /* the weapon's units could not be collected or refreshed */
  TFP_ARM_FAULT,    /* an engine call faulted */
  TFP_ARM_RESTORE,  /* the game's own values could not be put back yet; tried again on the next frame */
  TFP_ARM_REASONS
};

typedef struct TfpArmDriveStats {
  uint64_t frames;   /* primary renders driven: the proofs held and the joints hold the wanted poses */
  uint64_t joints;   /* joint records written through the engine's setter (a record already as wanted is not written) */
  uint64_t pieces;   /* sleeve, glove and weapon units refreshed after the rig */
  uint64_t binds;    /* times the joint map was bound to a body and a rig */
  uint64_t refused;  /* primary renders while switched on, in the native view, that were not driven */
  uint64_t idle;     /* primary renders while switched on with another camera kind than 6 */
  uint64_t restores; /* times the game's own values were put back */
  uint32_t reason;   /* of the last refusal, TFP_ARM_* */
  uint32_t owned;    /* joints holding a value of this mod now */
} TfpArmDriveStats;

typedef struct TfpArmDriveHost {
  void *user;
  FpMemory memory; /* reads; game and exe bases */
  /* `at` is committed, executable code (the retired query_executable). */
  bool (*executable)(void *user, uint64_t at);
  /* The engine's own functions, called at the proven address. false: the call faulted.
   *   set_local:   UnitAPI+0x60, exe+0x1fcad0: void(uint32 unit, uint32 node, const local record *)
   *   update_unit: WorldAPI+0x100, exe+0x336530: void(world, uint32 unit) */
  bool (*set_local)(void *user, uint64_t function, uint32_t unit, uint32_t node, const TfpGraftLocal *local);
  bool (*update_unit)(void *user, uint64_t function, uint64_t world, uint32_t unit);
} TfpArmDriveHost;

/* Once, before anything else. The struct is copied. */
void tfp_arm_drive_set_host(const TfpArmDriveHost *host);
/* The switch. on: drive while the native first-person view is shown (the caller passes its own view
 * state in `on`). restore_after_draw: put the game's values back right after each primary render (the
 * retired 0.3.46 draw path) instead of leaving them written and recovering them on the next frame (the
 * accepted 0.3.36 / 0.3.37 action path). weight: 1 the body's pose, below 1 a blend from the game's own. */
void tfp_arm_drive_set(bool on, bool restore_after_draw, float weight);
/* The test of the first game run: ignore the body and turn the rig's l_shoulder by 70 degrees about its
 * own Z, through the same writer and refresh. */
void tfp_arm_drive_set_bend(bool on);

/* The action hold (a probe; docs/design/ACTION-CAMERA.md "Action hold"). Off at the start and for good
 * unless this is called with true; off, nothing below differs from before. While on (the controller is
 * holding the native view through an action):
 *   - a gun that is absent, or no longer on the rig's socket, is not a refusal;
 *   - when the full snapshot fails (empty hands, a request that names another weapon than the hands hold)
 *     the player's snapshot gives the body, the rig and the camera;
 *   - the binding and its entry alignment are kept while only the gun differs;
 *   - the gun's units are collected and refreshed only while the gun is on the rig;
 *   - the game's joint values are put back after every primary render (tfp_arm_drive_render answers true),
 *     whatever restore_after_draw says. */
void tfp_arm_drive_set_hold(bool on);
/* "Ready": a lease runs and the hold's switch is on, whether or not the controller holds yet. The binder sets it
 * every tick beside tfp_arm_drive_set_hold. Why it exists: the tick comes before the game's update, so the first
 * frame of an action is drawn before the tick that sees the action; with only the controller's state the drive
 * would still be strict in that frame, refuse a gun that already left the rig, drop the binding, and bind anew a
 * frame later with an entry alignment taken from a pose in the middle of the action. While ready, everything in
 * the list above applies except the last point: the game's values go back after a draw only in a frame whose gun
 * is away (and, as before, in every frame of a hold). With the gun on the rig a ready frame is the frame it
 * always was. Off at the start and for good unless this is called with true. */
void tfp_arm_drive_set_hold_ready(bool on);
/* For the hold's log line; reads only, game thread. gun: the unit the lease was entered with (0: none).
 * proven false: the local player's snapshot failed and nothing else is filled. */
typedef struct TfpArmDriveProbe {
  bool proven, has_camera, camera_parent_is_rig_eye, has_eye, has_rig_eye;
  const char *gun_parent;      /* "rig", "body", "other" (another unit), "none" (no live gun unit, or no parent) */
  float eye[3], rig_eye[3];    /* world positions: the game's camera unit, the rig's eye node (50) */
} TfpArmDriveProbe;
void tfp_arm_drive_probe(uint32_t gun, TfpArmDriveProbe *out);

/* The primary world render is about to be forwarded. `world` is its first argument. game_thread: the
 * call is on the thread of the mod's tick and a tick ran shortly before (src/render_slot.c).
 * Returns true when tfp_arm_drive_after_render must be called once the render call has returned. */
bool tfp_arm_drive_render(uint64_t world, bool game_thread);
void tfp_arm_drive_after_render(void);
/* Game thread only (the tick). Puts the game's own values back if any joint still holds this mod's.
 * true: nothing is held any more. */
bool tfp_arm_drive_release(void);
/* NULL, or why the mod still holds something in the game. */
const char *tfp_arm_drive_held(void);

void tfp_arm_drive_stats(TfpArmDriveStats *out);
const char *tfp_arm_drive_reason_name(uint32_t reason);
void tfp_arm_drive_reset_for_tests(void);
#endif
