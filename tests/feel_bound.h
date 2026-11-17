/* Which runtime host pointers the feel build (0.6.0-feel.1) binds to real code instead of the
 * answers of src/wire_off.c. tests/wire_feel_test.c checks this list against the pointers the
 * binders really replace; tests/wire_off_test.c (built with -DTFP_OFF_FEEL) uses it to tell a
 * module that is now bound from one whose off answer must still be what retail was given. */
#ifndef TFP_FEEL_BOUND_H
#define TFP_FEEL_BOUND_H
#include <stdbool.h>
#include <string.h>
static bool feel_bound(const char *name) {
  static const char *const groups[] = {"motion_", "head_", "model_shadows_", "arm_pieces_", "grip_hand_", "dof_", "movement_", "native_aim_",
                                       "aim_markers_", "actions_", "config_", "profile_", "helper_"};
  static const char *const singles[] = {"menu_fird_person_arms", "menu_show_torso", "menu_near_options", "menu_motion_options", "menu_native_ads",
                                        "menu_ads_mode", "mod_set", "backend_prepare_pose", "transition_action", "transition_resume"};
  if (!strncmp(name, "head_positioner_", 16)) return false; /* the editor's parked camera mode: still off */
  if (!strcmp(name, "native_aim_source") || !strcmp(name, "aim_markers_source")) return false; /* the module files exist: true, as before */
  for (size_t i = 0; i < sizeof groups / sizeof *groups; i++) if (!strncmp(name, groups[i], strlen(groups[i]))) return true;
  for (size_t i = 0; i < sizeof singles / sizeof *singles; i++) if (!strcmp(name, singles[i])) return true;
  return false;
}
#endif
