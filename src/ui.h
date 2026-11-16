#ifndef TFP_UI_H
#define TFP_UI_H
#include "goose_overlay.h"
#include "runtime.h"
enum FpCommand {
  FP_CMD_TOGGLE = 1,
  FP_CMD_EDIT = 2,
  FP_CMD_CLOSE_EDITOR = 4,
  FP_CMD_FREE_AIM = 8,
  FP_CMD_COMPARE = 16
};
typedef struct FpUi {
  SRWLOCK lock;
  const OvApi *ov;
  OvPanel *panel, *editor_panel;
  FpSettings settings;
  unsigned revision, commands;
  bool enabled, editing, can_edit, config_dirty;
  uint64_t weapon;
  int action, binding, drag;
  FpPlacement draft;
  unsigned draft_revision;
  float mouse_x, mouse_y;
  char status[256], effects[160], models[160], issue[160];
} FpUi;
bool fp_ui_init(FpUi *, const OvApi *, const FpSettings *);
void fp_ui_editor_open(FpUi *, bool);
#endif
