/* Three features of the locked view that are switched by options of the "Locked view" tab (TFP_SETTINGS_LOCKED:
 * animated_arms, put_away, smooth_eye), live, and by test files beside the DLL that override the option:
 *
 *   feature            option         forced on by     forced off by    what the setter reaches (src/mod_view.c)
 *   TFP_FEATURE_ARMS   animated_arms  arms_live.txt    arms_off.txt     the arm drive and its render hook
 *   TFP_FEATURE_HOLD   put_away       hold_lease.txt   hold_off.txt     the action hold (controller, backend, pose helper)
 *   TFP_FEATURE_SMOOTH smooth_eye     -                smooth_off.txt   the eye's continuity
 *
 * A file beats the option both ways; with both files of a feature there, off wins. This unit only decides and
 * calls the setter: once at the start (tfp_wire_options_start, which also logs one line per feature saying
 * whether it is on and whether the option or a file decided), then whenever the answer changes
 * (tfp_wire_options_tick, every game update after tfp_wire_panel_frame).
 *
 * A change in the middle of a session is made on the game thread (the tick) and only while no lease is being
 * held through an action and none is waiting for its native exit: while the controller holds, or its phase is
 * "cleanup pending", every change waits, so a hold in progress ends through its normal exit first.
 * No Windows, no game call here. */
#ifndef TFP_WIRE_OPTIONS_H
#define TFP_WIRE_OPTIONS_H
#include "wire.h"

typedef enum TfpFeature { TFP_FEATURE_ARMS, TFP_FEATURE_HOLD, TFP_FEATURE_SMOOTH, TFP_FEATURE_COUNT } TfpFeature;
typedef struct TfpWireOptions {
  /* the caller's, before tfp_wire_options_start */
  void *user;
  void (*set[TFP_FEATURE_COUNT])(void *user, bool on); /* may be NULL */
  const char *force_on[TFP_FEATURE_COUNT];             /* the name of the file that is there; NULL: no such file */
  const char *force_off[TFP_FEATURE_COUNT];
  /* this unit's */
  bool started, on[TFP_FEATURE_COUNT], waiting[TFP_FEATURE_COUNT];
} TfpWireOptions;

/* What the feature should be now. *file (may be NULL): the file that decided, NULL when the option did. */
bool tfp_wire_options_wanted(TfpWire *w, const TfpWireOptions *o, TfpFeature feature, const char **file);
/* Once, after the settings store is bound and before the mode starts: every setter is called with its answer,
 * and one "Option: ..." line per feature is logged. */
void tfp_wire_options_start(TfpWire *w, TfpWireOptions *o);
/* Every game update. A setter is called only when its answer changed, and not while tfp_wire_options_busy. */
void tfp_wire_options_tick(TfpWire *w, TfpWireOptions *o);
/* A lease is held through an action, or is waiting for its native exit. */
bool tfp_wire_options_busy(const TfpWire *w);
#endif
