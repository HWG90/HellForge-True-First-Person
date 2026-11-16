/* Goose Overlay Host supplies the shared renderer, input, FS Sinclair and
 * theme. This module never calls the engine or mutates a game transform. */
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static bool checkbox(const OvApi *ov, const char *name, bool *value) {
  int v = *value;
  bool change = ov->checkbox(name, &v) != 0;
  if (change)
    *value = v != 0;
  return change;
}
static bool slider(const OvApi *ov, const char *name, double *value, float min,
                   float max) {
  float v = (float)*value;
  bool changed = ov->slider_float(name, &v, min, max, "%.2f") != 0;
  if (changed)
    *value = v;
  return changed;
}
static void key_name(int vk, char *out, size_t count) {
  if (vk >= VK_F1 && vk <= VK_F24)
    snprintf(out, count, "F%d", vk - VK_F1 + 1);
  else if (vk >= 32 && vk <= 90)
    snprintf(out, count, "%c", vk);
  else
    snprintf(out, count, "VK %d", vk);
}
static void binding(FpUi *ui, const char *label, int *key, int id) {
  const OvApi *ov = ui->ov;
  char name[32];
  key_name(*key, name, sizeof name);
  ov->text(label);
  ov->same_line();
  ov->key_cap(name);
  ov->same_line();
  ov->push_id_int(id);
  if (ov->small_button(ui->binding == id ? "PRESS A KEY" : "CHANGE"))
    ui->binding = id;
  ov->pop_id();
  if (ui->binding == id)
    for (int vk = 1; vk < 256; vk++)
      if (ov->key_pressed(vk)) {
        if (vk != VK_ESCAPE) {
          *key = vk;
          ui->revision++;
          ui->config_dirty = true;
        }
        ui->binding = 0;
        break;
      }
}
static void draw(const OvFrame *frame, void *user) {
  (void)frame;
  FpUi *ui = user;
  const OvApi *ov = ui->ov;
  AcquireSRWLockExclusive(&ui->lock);
  bool changed = false;
  ov->heading("TRUE FIRST PERSON");
  ov->text_wrapped(ui->status);
  if (ov->button_primary(ui->enabled ? "TURN FIRST PERSON OFF"
                                     : "ENABLE TRUE FIRST PERSON"))
    ui->commands |= FP_CMD_TOGGLE;
  changed |= checkbox(ov, "Native ADS while aiming", &ui->settings.native_ads);
  bool markers = ui->settings.show_markers;
  if (ui->settings.marker_legacy && ui->weapon) {
    markers = false;
    for (unsigned i = 0; i < ui->settings.marker_count; i++)
      if (ui->settings.marker_allow[i] == ui->weapon)
        markers = true;
  }
  if (checkbox(ov, "Show aim markers while aiming", &markers)) {
    ui->settings.show_markers = markers;
    ui->settings.marker_legacy = false;
    changed = true;
  }
  ov->text_disabled(
      "Crosshair and aiming circle remain visible when not aiming.");
  ov->begin_disabled(!ui->can_edit);
  if (ov->button("EDIT ARMS AND WEAPON PLACEMENT"))
    ui->commands |= FP_CMD_EDIT;
  ov->end_disabled();
  ov->heading("CONTROLS");
  binding(ui, "Toggle True First Person", &ui->settings.toggle_key, 1);
  binding(ui, "Placement editor", &ui->settings.editor_key, 2);
  ov->heading("NEAR CLIP");
  changed |= ov->slider_int("Distance (cm)", &ui->settings.near_cm, 5, 40) != 0;
  changed |=
      checkbox(ov, "Apply distance to shoulder aim", &ui->settings.near_aim);
  ov->text_disabled(
      "5 cm keeps close hands and weapons visible. Native ADS uses its own distance.");
  if (ov->collapsing_header("BOB AND SWAY", 0)) {
    FpMotionOptions *m = &ui->settings.eye.motion;
    changed |= checkbox(ov, "Weapon movement", &m->enabled);
    changed |= slider(ov, "Movement strength", &m->scale, 0, 2);
    changed |= slider(ov, "Weapon bob strength", &m->bob_strength, 0, 3);
    changed |= slider(ov, "Weapon bob speed", &m->bob_speed, .25, 2);
    changed |= slider(ov, "Weapon sway strength", &m->sway_strength, 0, 3);
    changed |= slider(ov, "Weapon sway response", &m->sway_speed, .25, 2);
    changed |= slider(ov, "Sprint movement", &m->sprint_strength, 0, 2);
    ov->text_disabled("Native camera movement and recoil are preserved.");
  }
  if (ov->collapsing_header("STATUS", 0)) {
    ov->text_wrapped(ui->effects);
    ov->text_wrapped(ui->models);
    if (*ui->issue)
      ov->text_wrapped(ui->issue);
    ov->text_disabled(
        "DOF and edge vignette are suppressed while this view is active.");
    ov->begin_disabled(ui->enabled);
    if (ov->button("COMPARE NATIVE ADS — 30 SECONDS"))
      ui->commands |= FP_CMD_COMPARE;
    ov->end_disabled();
  }
  if (changed) {
    ui->revision++;
    ui->config_dirty = true;
  }
  ReleaseSRWLockExclusive(&ui->lock);
}
static double clamp(double v, double min, double max) {
  return fmin(max, fmax(min, v));
}
static void drag_pad(FpUi *ui, const OvFrame *frame) {
  const OvApi *ov = ui->ov;
  float x = 0, y = 0, w = 0, h = 0;
  ov->cursor_screen_pos(&x, &y);
  ov->content_region_avail(&w, &h);
  (void)h;
  if (w < 180)
    w = 180;
  float scale = frame->scale > 0 ? frame->scale : 1;
  float height = 96 * scale;
  ov->invisible_button("placement-drag-pad", w, height);
  bool hovered = ov->is_item_hovered() != 0;
  float mx = 0, my = 0;
  ov->mouse_pos(&mx, &my);
  if (hovered)
    for (int b = 0; b < 3; b++)
      if (ov->mouse_clicked(b)) {
        ui->drag = b + 1;
        ui->mouse_x = mx;
        ui->mouse_y = my;
      }
  if (ui->drag && !ov->mouse_down(ui->drag - 1))
    ui->drag = 0;
  if (ui->drag) {
    double dx = mx - ui->mouse_x, dy = my - ui->mouse_y;
    ui->mouse_x = mx;
    ui->mouse_y = my;
    if (ui->drag == 1) {
      ui->draft.x = clamp(ui->draft.x + dx * .001, -.5, .5);
      ui->draft.z = clamp(ui->draft.z - dy * .001, -.5, .5);
    } else if (ui->drag == 2) {
      ui->draft.yaw = clamp(ui->draft.yaw + dx * .1, -90, 90);
      ui->draft.pitch = clamp(ui->draft.pitch + dy * .1, -90, 90);
    } else
      ui->draft.y = clamp(ui->draft.y + dy * .001, -.5, .5);
    if (dx || dy)
      ui->draft_revision++;
  }
  if (hovered) {
    float wheel = ov->mouse_wheel();
    if (wheel) {
      ui->draft.y = clamp(ui->draft.y + wheel * .005, -.5, .5);
      ui->draft_revision++;
    }
  }
  ov->draw_rect_filled(x, y, x + w, y + height, ov->theme(OV_THEME_ROW),
                       6 * scale);
  ov->draw_rect(x, y, x + w, y + height, ov->theme(OV_THEME_LINE), 6 * scale,
                scale);
  ov->draw_text(x + 10 * scale, y + 14 * scale, ov->theme(OV_THEME_TEXT), 0, 0,
                "Drag to position the arms and weapon");
  ov->draw_text(x + 10 * scale, y + 42 * scale, ov->theme(OV_THEME_TEXT_DIM), 0,
                0, "Left: right/up    Middle or wheel: depth");
  ov->draw_text(x + 10 * scale, y + 66 * scale, ov->theme(OV_THEME_TEXT_DIM), 0,
                0, "Right: yaw/pitch");
}
static void editor_draw(const OvFrame *frame, void *user) {
  (void)frame;
  FpUi *ui = user;
  const OvApi *ov = ui->ov;
  AcquireSRWLockExclusive(&ui->lock);
  if (!ui->editing || !ui->weapon) {
    ov->text_wrapped(
        "Enable True First Person with a firearm equipped to edit placement.");
    ReleaseSRWLockExclusive(&ui->lock);
    return;
  }
  ov->heading("ARMS AND WEAPON PLACEMENT");
  ov->textf("Weapon %016llx", (unsigned long long)ui->weapon);
  static const char *const actions[] = {"Idle",   "Normal aim", "ADS",
                                        "Sprint", "Reload",     "Crouch",
                                        "Prone",  "Slide",      "Dive"};
  if (ov->combo("Action profile", &ui->action, actions, FP_ACTION_COUNT)) {
    ui->draft = fp_placement_resolve(&ui->settings.placements, ui->weapon,
                                     (FpAction)ui->action, NULL);
    ui->draft_revision++;
  }
  drag_pad(ui, frame);
  bool changed = false;
  changed |= slider(ov, "Right / left (m)", &ui->draft.x, -.5, .5);
  changed |= slider(ov, "Forward / back (m)", &ui->draft.y, -.5, .5);
  changed |= slider(ov, "Up / down (m)", &ui->draft.z, -.5, .5);
  changed |= slider(ov, "Yaw (degrees)", &ui->draft.yaw, -90, 90);
  changed |= slider(ov, "Pitch (degrees)", &ui->draft.pitch, -90, 90);
  changed |= slider(ov, "Roll (degrees)", &ui->draft.roll, -90, 90);
  if (changed)
    ui->draft_revision++;
  if (ov->button_primary("SAVE FOR THIS WEAPON")) {
    if (fp_placement_save(&ui->settings.placements, ui->weapon,
                          (FpAction)ui->action, &ui->draft, false)) {
      ui->revision++;
      ui->config_dirty = true;
    }
  }
  ov->same_line();
  if (ov->button("SAVE FOR ALL WEAPONS")) {
    if (fp_placement_save(&ui->settings.placements, ui->weapon,
                          (FpAction)ui->action, &ui->draft, true)) {
      ui->revision++;
      ui->config_dirty = true;
    }
  }
  if (ov->button("RESET DRAFT")) {
    ui->draft = ui->action == FP_ADS ? (FpPlacement){0}
                                     : (FpPlacement){.15, 0, -.12, 0, 0, 0};
    ui->draft_revision++;
  }
  ov->same_line();
  if (ov->button("CLOSE EDITOR"))
    ui->commands |= FP_CMD_CLOSE_EDITOR;
  ov->text_disabled(
      "Closing discards unsaved placement. First person stays on.");
  if (ov->key_pressed(VK_ESCAPE))
    ui->commands |= FP_CMD_CLOSE_EDITOR;
  ReleaseSRWLockExclusive(&ui->lock);
}
bool fp_ui_init(FpUi *ui, const OvApi *ov, const FpSettings *settings) {
  memset(ui, 0, sizeof *ui);
  InitializeSRWLock(&ui->lock);
  ui->ov = ov;
  ui->settings = *settings;
  ui->revision = 1;
  if (!ov)
    return false;
  OvPanelDef panel = {0};
  panel.size = sizeof panel;
  panel.id = "goose.true-first-person";
  panel.title = "True First Person";
  panel.kind = OV_PANEL_WINDOW;
  panel.width = 560;
  panel.draw = draw;
  panel.user = ui;
  ui->panel = ov->register_panel(&panel);
  panel.id = "goose.true-first-person/editor";
  panel.title = "First Person placement";
  panel.width = 620;
  panel.draw = editor_draw;
  ui->editor_panel = ov->register_panel(&panel);
  return ui->panel && ui->editor_panel;
}
void fp_ui_editor_open(FpUi *ui, bool open) {
  if (ui->ov && ui->editor_panel)
    ui->ov->set_open(ui->editor_panel, open ? 1 : 0);
}
