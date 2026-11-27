/* Round 53: the placement editor's list of weapons with a saved placement, sorted into categories.
 *
 * A tester asked for the editor's weapon list "sorted into categories (primaries, secondaries, supports)". Up to
 * round 53 the editor had no list at all: it named the weapon in your hands by its 16-character key and nothing else.
 * This is that list, built grouped from the start: a second panel under the editor's own, naming every weapon the
 * settings hold a placement for (settings.json first_person action_placements and the older weapon_placements).
 *
 *   SAVED PLACEMENTS
 *   IN HANDS: AR-23 Liberator          the weapon being edited: always first, in the accent colour
 *   - PRIMARY (3)                      a header per category that has a weapon; a click folds or unfolds it
 *       AR-23P Liberator Penetrator    by name, capitals and small letters alike
 *       SG-225 Breaker
 *   + SUPPORT (2)                      folded
 *
 * The category is the loadout slot the kit's catalogue gives the weapon (src/action_placement_data.h `kind`, the field
 * the family is derived from): 1 Primary, 3 Secondary, 2 Support; anything else, a key the catalogue does not know,
 * and the shared "__all" row are Other. It is not read from the game: the catalogue row is found by the same placement
 * key the Weapon: line names.
 *
 * The list is read only: the editor still edits the weapon in your hands. A press that begins on the list is the
 * list's (TfpEwView.press_in_list: the caller keeps it from the editor, which would take it for a move drag), and so is
 * the wheel while the pointer is over it (it scrolls the list instead of moving the depth). No key is read here.
 * What is folded is kept in a TfpEwView the caller owns: for the session, until the mod is loaded again.
 *
 * Header only, no Windows, no game: src/wire_editor.c builds the list, src/mod_editor.c draws the items with the
 * overlay host's theme tokens (the colour roles of TfpEdCommand); tests/editor_weapons_test.c. */
#ifndef TFP_EDITOR_WEAPONS_H
#define TFP_EDITOR_WEAPONS_H
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "placement_editor.h"

enum { TFP_EW_MAX = 192, TFP_EW_KEY = 32, TFP_EW_NAME = 64, TFP_EW_ROWS = TFP_EW_MAX + 5, TFP_EW_COMMANDS = 128 };
typedef enum TfpEwGroup { TFP_EW_PRIMARY, TFP_EW_SECONDARY, TFP_EW_SUPPORT, TFP_EW_OTHER, TFP_EW_GROUPS } TfpEwGroup;
typedef struct TfpEwWeapon {
  char key[TFP_EW_KEY], name[TFP_EW_NAME];
  TfpEwGroup group;
  bool held;    /* the weapon in your hands (set by tfp_ew_sort) */
  bool saved;   /* the settings hold a placement for it; false only for a held weapon that has none */
} TfpEwWeapon;
typedef enum TfpEwRowType { TFP_EW_ROW_HELD, TFP_EW_ROW_HEADER, TFP_EW_ROW_WEAPON } TfpEwRowType;
typedef struct TfpEwRow {
  TfpEwRowType type;
  TfpEwGroup group;
  int count;                   /* HEADER: the weapons of the category, folded or not */
  bool collapsed;              /* HEADER */
  const TfpEwWeapon *weapon;   /* HELD, WEAPON */
} TfpEwRow;
/* What the caller keeps between frames. Zero: everything unfolded, at the top. */
typedef struct TfpEwView {
  unsigned collapsed;          /* bit (1 << group) */
  int scroll;                  /* rows scrolled away at the top */
  bool was_down;               /* the left button at the last frame */
  bool press_in_list;          /* the press in progress began on the list: not the editor's */
} TfpEwView;

static inline const char *tfp_ew_group_label(TfpEwGroup group) {
  static const char *const labels[TFP_EW_GROUPS] = {"PRIMARY", "SECONDARY", "SUPPORT", "OTHER"};
  return (unsigned)group < TFP_EW_GROUPS ? labels[group] : "OTHER";
}
/* The catalogue's loadout slot of a weapon. kind 0, or any other number: no row, or not a weapon slot. */
static inline TfpEwGroup tfp_ew_group_of_kind(int kind) {
  return kind == 1 ? TFP_EW_PRIMARY : kind == 3 ? TFP_EW_SECONDARY : kind == 2 ? TFP_EW_SUPPORT : TFP_EW_OTHER;
}
/* One more weapon. name NULL or "": the key is shown. false: no room, no key, or the key is already there (a key
 * already there that was not `saved` becomes so when this one is). */
static inline bool tfp_ew_add(TfpEwWeapon *list, int *count, int capacity, const char *key, const char *name, TfpEwGroup group, bool saved) {
  if (!key || !*key) return false;
  for (int i = 0; i < *count; i++)
    if (!strncmp(list[i].key, key, TFP_EW_KEY - 1)) { list[i].saved = list[i].saved || saved; return false; }
  if (*count >= capacity) return false;
  TfpEwWeapon *w = &list[(*count)++];
  memset(w, 0, sizeof *w);
  snprintf(w->key, sizeof w->key, "%.*s", TFP_EW_KEY - 1, key);
  snprintf(w->name, sizeof w->name, "%.*s", TFP_EW_NAME - 1, name && *name ? name : key);
  w->group = (unsigned)group < TFP_EW_GROUPS ? group : TFP_EW_OTHER;
  w->saved = saved;
  return true;
}
static inline int tfp_ew_fold(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static inline int tfp_ew_compare(const void *left, const void *right) {
  const TfpEwWeapon *a = left, *b = right;
  if (a->held != b->held) return a->held ? -1 : 1;
  if (a->group != b->group) return a->group < b->group ? -1 : 1;
  for (size_t i = 0; a->name[i] || b->name[i]; i++) {
    const int x = tfp_ew_fold((unsigned char)a->name[i]), y = tfp_ew_fold((unsigned char)b->name[i]);
    if (x != y) return x < y ? -1 : 1;
  }
  return strcmp(a->key, b->key);
}
/* The order of the list: the weapon in your hands (held_key; NULL: none), then Primary, Secondary, Support, Other, by
 * name inside each without regard to capitals, by key between two of one name. */
static inline void tfp_ew_sort(TfpEwWeapon *list, int count, const char *held_key) {
  for (int i = 0; i < count; i++) list[i].held = held_key && !strncmp(list[i].key, held_key, TFP_EW_KEY - 1);
  if (count > 1) qsort(list, (size_t)count, sizeof list[0], tfp_ew_compare);
}
/* The rows of a sorted list: the held weapon, then a header for each category that has a weapon other than the held
 * one, with its weapons unless bit (1 << group) of `collapsed` is set. Returns how many (at most `capacity`). */
static inline int tfp_ew_rows(const TfpEwWeapon *list, int count, unsigned collapsed, TfpEwRow *rows, int capacity) {
  int n = 0, i = 0;
  if (count > 0 && list[0].held && n < capacity) rows[n++] = (TfpEwRow){TFP_EW_ROW_HELD, list[0].group, 0, false, &list[0]}, i = 1;
  while (i < count) {
    const TfpEwGroup group = list[i].group;
    int end = i;
    while (end < count && list[end].group == group) end++;
    const bool folded = (collapsed >> group & 1u) != 0;
    if (n < capacity) rows[n++] = (TfpEwRow){TFP_EW_ROW_HEADER, group, end - i, folded, NULL};
    for (; i < end; i++) if (!folded && n < capacity) rows[n++] = (TfpEwRow){TFP_EW_ROW_WEAPON, group, 0, false, &list[i]};
  }
  return n;
}

/* The list's panel in the editor's units (pixels from the bottom left, y up), under the editor's own panel
 * (src/placement_editor.c draw: 24, h - 280, 530 x 256 at 1080, and its footer line under that). has false: the frame
 * is too small for a title and three rows. */
typedef struct TfpEwBox { bool has; double scale, x, y, w, h, pitch; int visible; } TfpEwBox;
static inline TfpEwBox tfp_ew_box(double w, double h) {
  TfpEwBox b; memset(&b, 0, sizeof b);
  if (!(w > 0) || !(h > 0)) return b;
  b.scale = fmax(.6, fmin(1.5, h / 1080));
  b.x = 24 * b.scale; b.y = 24 * b.scale;
  b.w = fmin(w - 48 * b.scale, 320 * b.scale);
  b.h = h - 310 * b.scale - b.y;
  b.pitch = 24 * b.scale;
  b.visible = (int)floor((b.h - 48 * b.scale) / b.pitch);
  b.has = b.w > 120 * b.scale && b.visible >= 3;
  return b;
}
/* The bottom edge of visible row `index` (0: the first under the title). */
static inline double tfp_ew_row_y(const TfpEwBox *b, int index) { return b->y + b->h - 40 * b->scale - (index + 1) * b->pitch; }

/* One drawn frame while the editor is open. list: sorted (tfp_ew_sort). w, h: the frame; x, y: the pointer in the
 * editor's units (NAN: not known); down: the left button; *wheel: notches since the last frame, + away from the user,
 * set to 0 when the list used them. Writes the items to draw, in order, and returns how many (0: no room). After it,
 * view->press_in_list says whether the left button is the list's. */
static inline int tfp_ew_frame(TfpEwView *view, const TfpEwWeapon *list, int count, double w, double h, double x, double y, bool down, double *wheel,
                               TfpEdCommand *out, int capacity) {
  TfpEwRow rows[TFP_EW_ROWS];
  const TfpEwBox b = tfp_ew_box(w, h);
  if (!b.has) { view->was_down = down; view->press_in_list = false; return 0; }
  int total = tfp_ew_rows(list, count, view->collapsed, rows, TFP_EW_ROWS);
  const bool over = x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
  if (!down) view->press_in_list = false;
  else if (!view->was_down) {
    view->press_in_list = over;
    if (over && x >= b.x + 8 * b.scale && x <= b.x + b.w - 8 * b.scale)
      for (int i = 0; i < b.visible && view->scroll + i < total; i++) {
        const TfpEwRow *row = &rows[view->scroll + i];
        const double bottom = tfp_ew_row_y(&b, i);
        if (row->type == TFP_EW_ROW_HEADER && y >= bottom && y <= bottom + 20 * b.scale) {
          view->collapsed ^= 1u << row->group;
          total = tfp_ew_rows(list, count, view->collapsed, rows, TFP_EW_ROWS);
          break;
        }
      }
  }
  view->was_down = down;
  if (over && wheel && *wheel == *wheel && *wheel != 0) {
    view->scroll -= (int)(*wheel > 0 ? ceil(*wheel) : floor(*wheel)) * 3;
    *wheel = 0;
  }
  if (view->scroll > total - b.visible) view->scroll = total - b.visible;
  if (view->scroll < 0) view->scroll = 0;

  int n = 0;
  const double left = b.x + 16 * b.scale;
#define TFP_EW_TEXT(tx, ty, ts, role, ...) do { if (n < capacity) { out[n] = (TfpEdCommand){true, false, tx, ty, 0, 0, (ts) * b.scale, role, 301, ""}; \
    snprintf(out[n].text, sizeof out[n].text, __VA_ARGS__); n++; } } while (0)
  if (n < capacity) out[n++] = (TfpEdCommand){false, true, b.x, b.y, b.w, b.h, 0, TFP_ED_COLOR_PANEL, 300, ""};
  TFP_EW_TEXT(left, b.y + b.h - 28 * b.scale, 15, TFP_ED_COLOR_ACCENT, "SAVED PLACEMENTS");
  if (total > b.visible)
    TFP_EW_TEXT(b.x + b.w - 116 * b.scale, b.y + b.h - 27 * b.scale, 13, TFP_ED_COLOR_TEXT_DIM, "%d-%d of %d (wheel)", view->scroll + 1,
                view->scroll + b.visible, total);
  if (!total) TFP_EW_TEXT(left, tfp_ew_row_y(&b, 0) + 4 * b.scale, 14, TFP_ED_COLOR_TEXT_DIM, "None yet: save one to list it");
  for (int i = 0; i < b.visible && view->scroll + i < total; i++) {
    const TfpEwRow *row = &rows[view->scroll + i];
    const double bottom = tfp_ew_row_y(&b, i);
    if (row->type == TFP_EW_ROW_HEADER) {
      if (n + 2 > capacity) break;
      out[n++] = (TfpEdCommand){false, false, b.x + 8 * b.scale, bottom, b.w - 16 * b.scale, 20 * b.scale, 0, TFP_ED_COLOR_FIELD, 300, ""};
      TFP_EW_TEXT(left, bottom + 4 * b.scale, 14, TFP_ED_COLOR_ACCENT, "%s %s (%d)", row->collapsed ? "+" : "-", tfp_ew_group_label(row->group), row->count);
    } else if (row->type == TFP_EW_ROW_HELD) {
      TFP_EW_TEXT(left, bottom + 4 * b.scale, 14, TFP_ED_COLOR_ACCENT, "IN HANDS: %.40s%s", row->weapon->name, row->weapon->saved ? "" : " (none saved)");
    } else {
      TFP_EW_TEXT(left + 18 * b.scale, bottom + 4 * b.scale, 14, TFP_ED_COLOR_TEXT, "%.48s", row->weapon->name);
    }
  }
#undef TFP_EW_TEXT
  return n;
}
#endif
