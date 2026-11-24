/* The placement editor's binder (TfpWire.ext[TFP_WIRE_EXT_EDITOR]): src/placement_editor.c behind the
 * runtime's seven editor_* host pointers.
 *
 *   the editor's controller  ->  the runtime's own pc_* entries (runtime.lua:739-752)
 *   the editor's store       ->  the runtime's store_* entries (runtime.lua:753-814): saved through
 *                                Actions.save, so the settings store counts a change and the file is
 *                                written once (docs/guides/POSER-AND-MENU.md)
 *   retail's MCM provider    ->  TfpEditorPlatform: the cursor, the mouse, the keys and the panel, which
 *                                src/mod_editor.c supplies from Goose Overlay Host and DBF-MCM
 *   Epic LUT                 ->  not on HellForge: never active
 *
 * No Windows here. Only the weapon mode: the eye-camera mode (retail's Shift+F8) needs the head
 * positioner, which this build leaves off. */
#ifndef TFP_WIRE_EDITOR_H
#define TFP_WIRE_EDITOR_H
#include "wire.h"
#include "placement_editor.h"

typedef struct TfpEditorSample {
  bool click;              /* the left button is down */
  double x, y;             /* retail's units: pixels, origin bottom left, y up; NAN: not known yet */
  double wheel;            /* 120 a notch, + away from the user, since the last sample */
  double dt;               /* seconds since the last sample */
} TfpEditorSample;
typedef struct TfpEditorPlatform {
  void *user;
  bool (*available)(void *user);             /* the overlay host is there and took the panel */
  bool (*menu_close)(void *user);            /* DBF-MCM closed (or absent) */
  bool (*menu_is_open)(void *user);
  bool (*capture_busy)(void *user);          /* another frontend holds the game's input */
  bool (*acquire)(void *user);               /* take the cursor: the panel opens */
  bool (*release)(void *user);
  bool (*focused)(void *user);               /* the game window is in the foreground */
  bool (*sample)(void *user, TfpEditorSample *out);
  bool (*down)(void *user, int virtual_key); /* 2 and 4 are the right and middle mouse buttons, as in retail */
  bool (*resolution)(void *user, double *width, double *height);
  void (*draw)(void *user, const TfpEdCommand *commands, int count);
  /* stingray.Window.mouse_focus() and show_cursor(); false: the functions could not be asked. */
  bool (*window)(void *user, bool *mouse_focus, bool *show_cursor);
} TfpEditorPlatform;

/* After the settings binder, before the runtime's on_enable. false: out of memory. */
bool tfp_wire_editor_bind(TfpWire *w, const TfpEditorPlatform *platform);
void tfp_wire_editor_unbind(TfpWire *w);
/* The menu's "Edit arms and weapon placement" button and the editor key (menu.lua:112-115): cancel when
 * open, else open for the weapon. NULL: done; otherwise why not (valid until the next call). */
const char *tfp_wire_editor_toggle(TfpWire *w);
bool tfp_wire_editor_active(const TfpWire *w);       /* open, opening or closing */
bool tfp_wire_editor_holds_draft(const TfpWire *w);  /* an edit that is neither saved nor put back */
const char *tfp_wire_editor_status(const TfpWire *w);
/* The bound editor, for the motion runtime (src/editor_motion.h): it draws the editor's yaw, pitch and roll.
 * NULL: none is bound yet. */
const TfpEditor *tfp_wire_editor_of(const TfpWire *w);
#endif
