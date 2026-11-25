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

/* The resting weapon pose (see tfp_arm_drive_set_rest). How the alignment in use was come by: */
enum {
  TFP_ARM_ALIGN_NONE = 0,
  TFP_ARM_ALIGN_PROVISIONAL, /* no rest alignment is kept for this weapon yet: the first driven frame's (a new binding), or the one
                              * already in use (another gun on a kept binding); replaced at the weapon's first rest. One
                              * taken while the copy was hidden or fading (weight under 1: the player aiming), in a hold or
                              * with the gun away is taken once more, eased, on the first driven frame at full weight with no
                              * hold and the gun on the rig, unless a better one replaced it */
  TFP_ARM_ALIGN_KEPT,        /* the rest alignment kept for this weapon and stance: on a new binding at once, otherwise eased */
  TFP_ARM_ALIGN_REST,        /* measured with the body at rest and confirmed by a second measurement; kept for this weapon and
                              * stance; eased */
  TFP_ARM_ALIGN_TENTATIVE    /* measured with the body at rest, once: shown (eased) while nothing is kept for the weapon, and not
                              * kept: a new binding does not start from it */
};
enum { TFP_ARM_REST_NONE = 0, TFP_ARM_REST_STAND, TFP_ARM_REST_CROUCH, TFP_ARM_REST_PRONE, TFP_ARM_REST_KINDS };
enum {
  TFP_ARM_RETAKE_NONE = 0,
  TFP_ARM_RETAKE_AIM,   /* taken while the copy was hidden or fading: the player aiming */
  TFP_ARM_RETAKE_ACTION /* taken in a hold or with the gun away: the middle of an action */
};
typedef struct TfpArmDriveAlign {
  uint64_t count;
  uint32_t how, weapon, stance; /* TFP_ARM_ALIGN_*, the gun's unit, TFP_ARM_REST_* */
  uint32_t rest_ms;             /* TFP_ARM_ALIGN_REST, _TENTATIVE: how long the body's weapon socket had been still */
  uint32_t key;                 /* TFP_ARM_ALIGN_REST, _TENTATIVE: the body's animation layer states then (tfp_arm_drive_rest_of; 0: not known) */
  float moved_m, moved_degrees; /* the weapon socket as the new alignment shows it now, from where the one in use shows it */
  bool eased;
  uint32_t retake;              /* TFP_ARM_ALIGN_PROVISIONAL: taken once more, and why the first one was not good (TFP_ARM_RETAKE_*; 0: not a retake) */
} TfpArmDriveAlign;

/* What the last primary draw did (TfpArmDriveStats.draw). The game's kind-6 evaluator seats the first-person eye by the
 * rig's weapon socket (node 8; game+0x1287423 to 0x1287785: the rig is put so that the socket is at depth D before the
 * eye). sock_m says how far the socket this mod drew is from the game's own along the view: under put-back the gun's
 * depth error on screen; with the written values left, how far the eye is thrown the other way. */
enum {
  TFP_ARM_DRAW_NONE = 0, /* not driven: switched off, another camera, refused */
  TFP_ARM_DRAW_PUT_BACK, /* driven; the game's joint values were put back after the draw (the rule) */
  TFP_ARM_DRAW_KEPT,     /* driven; the written values were left in the rig (the test switch arms_live_keep.txt) */
  TFP_ARM_DRAW_HOLD      /* driven in a hold or with the gun away; the game's values were put back, as always there */
};
typedef struct TfpArmDriveDraw {
  uint64_t count;  /* primary draws seen on the game thread while the drive was switched on or held something */
  uint32_t mode;   /* TFP_ARM_DRAW_* of the last one */
  float sock_m;    /* of the last one, when driven: the socket as written less the game's own, along the view (the
                    * forward axis of the rig's eye, the Trace line's view=), in metres; positive: drawn further out */
} TfpArmDriveDraw;
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
  TfpArmDriveAlign align; /* the last time an alignment was taken or replaced (count: how many times so far) */
  TfpArmDriveDraw draw;   /* the last primary draw */
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
 * state in `on`). restore_after_draw: put the game's values back right after each primary render. That is
 * the rule since round 13 (2026-11-24): the rig holds this mod's joint values only from the draw's write to
 * the end of that draw, because the game seats the first-person eye by the rig's weapon socket (see
 * TfpArmDriveDraw) and a written socket left in the rig moved the camera against every move of the drawn
 * arms along the view. false: the written values are left and the game's own recovered on the next frame
 * (the retired mod's accepted 0.3.36 / 0.3.37 action path, this mod's default until then): kept for the
 * comparison in the game, behind a test file. weight: 1 the body's pose, below 1 a blend from the game's own. */
void tfp_arm_drive_set(bool on, bool restore_after_draw, float weight);
/* What the binder passes as restore_after_draw, from its two test files beside the DLL: arms_live_keep.txt asks for
 * the written values to be left; arms_live_restore.txt, the switch of the proof run, asks for what is now the rule
 * and changes nothing. */
static inline bool tfp_arm_drive_put_back(bool keep_file, bool restore_file) { (void)restore_file; return !keep_file; }
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
 *     whatever restore_after_draw says (with restore_after_draw, the rule, a hold changes nothing here). */
void tfp_arm_drive_set_hold(bool on);
/* "Ready": a lease runs and the hold's switch is on, whether or not the controller holds yet. The binder sets it
 * every tick beside tfp_arm_drive_set_hold. Why it exists: the tick comes before the game's update, so the first
 * frame of an action is drawn before the tick that sees the action; with only the controller's state the drive
 * would still be strict in that frame, refuse a gun that already left the rig, drop the binding, and bind anew a
 * frame later with an entry alignment taken from a pose in the middle of the action. While ready, everything in
 * the list above applies except the last point: without restore_after_draw the game's values go back after a draw
 * only in a frame whose gun is away (and, as before, in every frame of a hold). With the gun on the rig a ready
 * frame is the frame it always was. Off at the start and for good unless this is called with true. */
void tfp_arm_drive_set_hold_ready(bool on);
/* The resting weapon pose. The tick says each frame what time it is, whether the player is at rest and in which
 * stance (TFP_ARM_REST_*; 0: not at rest), and the body's animation layer states (pose_key; 0: not known); both are
 * tfp_arm_drive_rest_of's answers. Never called (the clock stays 0), the drive is what it was: the first driven
 * frame's alignment for the whole binding.
 *
 * Called, the weapon's resting place is the rig's own socket, the one the game's first-person placement shows:
 *   - a measurement is taken only at rest: the tick says so, the gun is on the rig, no hold, and the body's weapon
 *     socket and its left hand (in the head's frame) have stayed within 15 mm and 2 degrees for 350 ms with the same
 *     layer states. It makes the body's socket as it is then land on the rig's own socket local, and takes the
 *     hand-to-gun relation from the rig's own joint values of that moment. One stretch of stillness gives one;
 *   - one measurement is not kept. The tick's "at rest" is also a pause inside a reload, an ammo check or a melee
 *     (they are "idle" to it), and stillness alone does not tell a held pose from the rest. It is kept, per weapon unit
 *     and stance (8 entries, for this body in this world), when the next measurement, from a later stretch of
 *     stillness, has the body's socket within 15 mm and 2 degrees of it and the same layer states: the pose the body
 *     came back to. With nothing kept for the weapon, one stretch that lasts 2000 ms is kept as well (a body that does
 *     not sway never gives a second stretch);
 *   - while nothing is kept for the weapon, its first measurement is shown at once (tentative; a later one replaces it
 *     only from more than 30 mm or 4 degrees away), so the first driven frame's alignment does not stay;
 *   - a new binding (after a swap, a frame in another camera, a refusal) uses the kept alignment of its weapon from its
 *     first frame, so a draw plays around the resting place and ends on it. A weapon with none kept starts on the
 *     first driven frame's alignment;
 *   - a kept alignment is replaced only by two measurements in a row that agree as above, are both more than 30 mm
 *     or 4 degrees (of the body's socket) from the one it was measured at (outside the sway of the rest it was taken
 *     in), and have the body more than that far from them at some frame in between: a pose the body left and came
 *     back to. One stretch of stillness never replaces it, however long (a held pose, a paused game), and neither do
 *     the stretches a held pose's own sway cuts it into;
 *   - another gun on a kept binding, or another stance, changes to what is kept for it;
 *   - every change inside a binding is eased over 150 ms (hands, arms and gun as one piece) and starts from what is
 *     shown, so no frame steps. Each is counted in the stats' `align`.
 * The numbers are from the one recording there is of a standing player (the retired mod's 120 pose pairs, 9 s: the
 * body's socket sways by up to 30 mm and 8 degrees and is never still for longer than 1.1 s); tfp_arm_drive_rest_take
 * is there to measure them in the game. */
void tfp_arm_drive_set_rest(uint64_t now_ms, unsigned rest, uint32_t pose_key);
/* What the tick knows of a frame, and whether that is a rest. stance: the action (TFP_ARM_REST_*; 0: an action, a
 * hold, no lease, the motion not active). Not a rest either: the fire button down, a speed over the ground that is
 * not known or above 0.05 m/s (a walk is "idle" to the action), a look pitch that is not known or more than 8
 * degrees from level (the body aims with its spine: its arms are elsewhere against the head). *why: TFP_ARM_WHY_*.
 * *pose_key: the three layer states as one number, 0 when not all are known. Both may be NULL. */
typedef struct TfpArmRestSample {
  unsigned stance;
  bool fire, has_speed, has_pitch, has_layers;
  double speed, pitch; /* metres per second; radians, up positive */
  int l1, l3, l14;     /* the body's animation layers 1, 3 and 14 (src/action.c); -1: none */
} TfpArmRestSample;
enum { TFP_ARM_WHY_REST = 0, TFP_ARM_WHY_ACTION, TFP_ARM_WHY_FIRE, TFP_ARM_WHY_MOVING, TFP_ARM_WHY_PITCH, TFP_ARM_WHYS };
unsigned tfp_arm_drive_rest_of(const TfpArmRestSample *sample, uint32_t *pose_key, unsigned *why);
/* What the driven frames since the last call saw of the resting pose, for the log (the numbers above are to be
 * checked against it): the call empties it. off: how far the weapon socket as the alignment in use shows it is from
 * the rig's own; still: how long the body's socket and left hand had been within the stillness bounds. */
typedef struct TfpArmDriveRest {
  uint32_t frames, rest_frames; /* driven with a gun on the rig; of those, frames the tick called a rest outside a hold */
  uint32_t measured;            /* measurements taken (kept or not) */
  uint32_t weapon, key, rest, why; /* of the last frame; why as the tick gave it (tfp_arm_drive_set_rest_why) */
  uint32_t still_ms, longest_still_ms;
  float off_m, off_degrees, least_off_m, most_off_m, most_off_degrees;
  uint32_t how;                 /* TFP_ARM_ALIGN_*: what the alignment in use is */
} TfpArmDriveRest;
void tfp_arm_drive_set_rest_why(unsigned why); /* for the line only */
void tfp_arm_drive_rest_take(TfpArmDriveRest *out);
/* "30 frames, 30 at rest (standing), layers 75:68:36, socket 0.004 to 0.017 m / up to 6.1 degrees off the rig's own
 * (now 0.006 m / 1.2), still for 302 ms (longest 1132), 2 measured, alignment kept". A static buffer. */
const char *tfp_arm_drive_rest_line(const TfpArmDriveRest *rest);
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

/* The support hand on the weapon's grip (round 23, 2026-11-24; src/offhand_grip.h, docs/design/specs/2026-11-24-offhand-grip.md).
 * Off at the start and until this is called with true. on: the left hand of the drawn arms reaches the weapon's left-hand
 * grip node (name 67C94E4B; a foregrip's or vertical grip's own node when one is fitted) as the gun is drawn, whenever the
 * body's left hand is on that node in the world (the game pins it there, also while the rig is the view), by a two-bone
 * reach of the upper arm, forearm and hand (rig nodes 10, 12, 13) on the poses the drive writes, with its step limits; the
 * shoulder (node 10) comes forward where the grip is beyond 97 % of the arm (round 26). Round 36: the body's hand on any
 * grip node of the gun says the game pins it; the hand holds a fitted grip's node even when the body's is on the gun's own
 * (the R-4 Hyena's case), and units hanging on the gun's nodes from elsewhere (the rig's or the body's) are searched too.
 * one_handed: a handgun, held in one hand: the hand stays as the body (or, aiming, the game's aim pose) holds it. While aiming
 * the copy fades out (the drive's weight) and the game's own aim pose holds the gun; since round 36b the reach runs on that
 * blend, so the hand stays on the grip, and only the left arm (nodes 10, 12, 13) differs from the game's own there. */
void tfp_arm_drive_set_grip(bool on, bool one_handed);
/* What the grip did in the driven frames since the last call, for the "Grip:" line (the call empties it). */
enum {
  TFP_ARM_GRIP_OFF = 0,   /* switched off */
  TFP_ARM_GRIP_ONE_HANDED,/* a handgun at the hip */
  TFP_ARM_GRIP_AIMING,    /* not used since round 36b (the hand holds the grip while aiming too); kept for the numbering */
  TFP_ARM_GRIP_NO_GUN,    /* no gun on the rig */
  TFP_ARM_GRIP_NO_NODE,   /* none of the gun's units carries a grip node */
  TFP_ARM_GRIP_STALE,     /* the gun's units were not where the rig puts them, and no earlier reading of the grip is kept */
  TFP_ARM_GRIP_FREE,      /* the body's left hand is not on any grip node (a reload, a weapon the game does not pin) */
  TFP_ARM_GRIP_HELD,      /* the hand is on the grip */
  TFP_ARM_GRIP_EASING,    /* on its way to the grip or back */
  TFP_ARM_GRIP_FAR,       /* the grip is too far from the drawn hand or out of the arm's reach: as the body holds it */
  TFP_ARM_GRIP_POSE,      /* the arm's pose was not usable this frame */
  TFP_ARM_GRIPS
};
/* One of the gun's units as the grip search saw it (round 36). */
enum { TFP_ARM_GRIP_LIST = 6 };
typedef struct TfpArmDriveGripUnit {
  uint64_t resource;              /* the unit's resource (its 64-bit name hash) */
  uint32_t nodes;
  uint32_t on, on_node, on_name;  /* the unit[] index it hangs on, that unit's node and the node's name (unit 0: the gun, on the rig) */
  uint32_t grips, grip_node;      /* its grip nodes (name 67C94E4B), and the first */
  bool elsewhere;                 /* not a child of the gun: a unit of the rig or the body mounted on one of the gun's nodes */
} TfpArmDriveGripUnit;
typedef struct TfpArmDriveGrip {
  uint32_t frames, held;          /* driven frames with the grip switched on; of those, held on the grip */
  uint32_t state;                 /* TFP_ARM_GRIP_* of the last frame */
  uint32_t nodes, units;          /* grip nodes found on the gun's units, and those units, in the last frame */
  bool attachment;                /* the node in use is on an attachment, not on the gun itself */
  bool found;                     /* a node is in use */
  float body_m;                   /* the body's left hand from the node in use, last frame */
  float to_m, most_to_m;          /* the drawn hand from the grip as drawn: last frame, and the most while held */
  float step_degrees, limit_degrees; /* the largest turn of a written joint in one frame, and that frame's allowance */
  float weight;                   /* last frame */
  /* round 26 (what the next game run settles) */
  uint32_t cut;                   /* frames a joint or the shoulder was held back by its limit */
  uint32_t out_of_reach, too_far; /* frames the grip was not taken: beyond the arm's reach, or too far from the drawn hand */
  float reach, most_reach;        /* the grip from the drive's shoulder in arm lengths: last frame, and the most while held */
  float shoulder_m, most_shoulder_m; /* the shoulder brought forward to reach it: last frame, and the most while held */
  float held_step_degrees;        /* the largest turn of a written joint in a frame held on the grip (steady holding) */
  float body_degrees;             /* the largest turn of the drive's own arm joints in one frame while the reach was in force */
  bool drawn;                     /* the drawn hand was measured against the gun as evaluated for the draw (held, full weight) */
  float drawn_m, most_drawn_m;    /* the rig's evaluated hand from the gun's evaluated grip node: last frame, the most while held */
  /* round 36: the gun's units as the last grip search saw them, and which node the hand holds and the body's is on */
  uint32_t tree_units;            /* the gun, its attachments, and units from elsewhere mounted on its nodes */
  uint32_t listed;                /* of them in unit[] (the first TFP_ARM_GRIP_LIST) */
  TfpArmDriveGripUnit unit[TFP_ARM_GRIP_LIST];
  int32_t target_unit, gate_unit; /* unit[] indices: the node the hand holds, the node the body's hand is on (-1: none) */
  uint32_t target_node, gate_node;
} TfpArmDriveGrip;
void tfp_arm_drive_grip_take(TfpArmDriveGrip *out);
/* "the gun's units: 2, 2 with a grip node: [0] the gun e5796355a8fd67e0, 39 nodes, grip node 13; [1] on [0] node 17
 * (7950e36e) f9df21c8e52cac95, 13 nodes, grip node 8". Resources are the units' 64-bit name hashes (filediver's), the
 * name in brackets is that of the node a unit hangs on, grip nodes are those named 67C94E4B; "[2] from elsewhere, on
 * [0] ..." is a unit that is not the gun's child but hangs on one of its nodes. only_changed: NULL when the list is the one
 * last returned with only_changed (which node the hand holds is in the Grip: line). A static buffer. */
const char *tfp_arm_drive_grip_units_line(const TfpArmDriveGrip *grip, bool only_changed);
/* "60 frames, 60 held: on the attachment's grip (2 grip nodes on 3 units), the body's hand 0.002 m from it; hand 0.000 m from
 * the grip (most 0.004), largest joint step 3.1 degrees (allowed 12.0), weight 1.00; reach 1.05 of the arm (most 1.08), shoulder
 * forward 0.047 m (most 0.065), held step 0.4 degrees, the body's arm turned 2.0 degrees, 0 frames cut, not taken: 0 out of reach,
 * 0 too far; as drawn the hand 0.001 m from the gun's grip (most 0.002)". A static buffer. */
const char *tfp_arm_drive_grip_line(const TfpArmDriveGrip *grip);
const char *tfp_arm_drive_grip_state_name(uint32_t state);

void tfp_arm_drive_stats(TfpArmDriveStats *out);
/* The log's words for an alignment taken or replaced: "aligned (weapon 472, at rest for 363 ms, layers 75:68:36,
 * confirmed, moved 0.213 m / 12.4 degrees from the one in use, eased); standing". A static buffer: game thread only. */
const char *tfp_arm_drive_align_line(const TfpArmDriveAlign *align);
const char *tfp_arm_drive_reason_name(uint32_t reason);
void tfp_arm_drive_reset_for_tests(void);
#endif
