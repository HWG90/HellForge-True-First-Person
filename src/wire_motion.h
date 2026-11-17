/* Weapon motion behind the runtime host (the feel build): retail motion_runtime.lua with motion.lua,
 * action_placement.lua's select, locomotion.lua and transition.lua's action / resume.
 *
 *   motion:update / reset / result / capture_neutral  -> src/motion_runtime.c
 *   its sampler                                         -> src/locomotion.c, reading through backend_ops
 *                                                          (tfp_ops_player_snapshot, _action_state, _on_foot,
 *                                                          _alignment_executable): one snapshot code per frame
 *   presentation:action / :resume                       -> src/transition.c
 *
 * tfp_wire_motion_bind is called after tfp_wire_init and tfp_wire_settings_bind, before the runtime's
 * on_enable; its state is w->ext[TFP_WIRE_EXT_MOTION]. No Windows. docs/WIRING-FEEL.md lists every
 * binding. */
#ifndef TFP_WIRE_MOTION_H
#define TFP_WIRE_MOTION_H
#include "wire.h"
#include "motion_runtime.h"

/* Logs `Ready: motion ...` or `Refused: motion: <why>`. false: refused; the view build's "off"
 * answers stay (no motion, the saved placement is still applied at entry by the runtime). */
bool tfp_wire_motion_bind(TfpWire *w);
void tfp_wire_motion_free(TfpWire *w);
/* Once per tick, after on_update: the `Weapon:` line of the settings binder on a weapon change, one
 * `Motion: action=... placement=... softened=...` line on each action change, and this second's
 * numbers. */
void tfp_wire_motion_frame(TfpWire *w);
/* Once a second while the mode is on: a fragment such as
 *   motion=on action=sprint phase=active move=0.0123m turn=1.20deg bob_weight=0.85 speed=4.10 scale=1.00 applied=58 softened=0 refused=0
 * (move / turn: the largest displacement and rotation handed to the pose in that second). Resets
 * the second's numbers. Returns the characters written (0: not bound or no room). */
size_t tfp_wire_motion_second(TfpWire *w, char *out, size_t size);
/* The motion runtime, for tests and the log; NULL: not bound or not created yet. */
const TfpMotionRuntime *tfp_wire_motion_runtime(TfpWire *w);

/* The sampler as the motion runtime calls it (locomotion.lua's sample(lease)) on one of the binder's
 * leases; NULL: no lease. false with the refusal's text. For tests and diagnostics. */
bool tfp_wire_motion_sample(TfpWire *w, const TfpWireLease *lease, TfpLocomotionSample *out, const char **why);

/* The adapters with logic of their own, exported for tests/wire_motion_test.c. */
void tfp_wire_motion_snapshot(const TfpOpsSnapshot *from, TfpSnapshot *out);       /* field for field */
void tfp_wire_motion_action(const TfpOpsAction *from, const TfpSnapshot *owner, TfpActionState *out);
#endif
