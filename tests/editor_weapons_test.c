/* Round 53: the placement editor's list of saved placements, grouped (src/editor_weapons.h), with no game.
 *
 * Checked: the category of a weapon is the catalogue's loadout slot (every row of the kit's catalogue, by its kind);
 * the order is the held weapon, then Primary, Secondary, Support, Other, by name inside each; a category without a
 * weapon has no header; a folded category keeps its header and its count; the drawn frame names them in that order
 * with the editor's colour roles only; a click on a header folds and unfolds it and stays folded over later frames;
 * a press that begins on the list is the list's until the button is let go, one that begins elsewhere is not; the
 * wheel scrolls the list only while the pointer is over it and is left alone elsewhere.
 * What it cannot show: how the overlay host draws the items (src/mod_editor.c), and the list in the game. */
#include "editor_weapons.h"
#include "action_placement_data.h"

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { printf("editor weapons: FAILED %s (line %d)\n", #x, __LINE__); return 1; } } while (0)

static TfpEdCommand items[TFP_EW_COMMANDS];
/* The index of the first text item holding `part`, or -1. */
static int find(int n, const char *part) {
  for (int i = 0; i < n; i++) if (items[i].is_text && strstr(items[i].text, part)) return i;
  return -1;
}

int main(void) {
  /* ---- the category: the catalogue's own slot ---- */
  CHECK(tfp_ew_group_of_kind(1) == TFP_EW_PRIMARY && tfp_ew_group_of_kind(3) == TFP_EW_SECONDARY && tfp_ew_group_of_kind(2) == TFP_EW_SUPPORT);
  CHECK(tfp_ew_group_of_kind(0) == TFP_EW_OTHER && tfp_ew_group_of_kind(6) == TFP_EW_OTHER && tfp_ew_group_of_kind(-1) == TFP_EW_OTHER);
  static TfpActionWeapon weapons[TFP_ACTION_GEAR_COUNT];
  TfpActionCatalogue catalogue = {weapons, TFP_ACTION_GEAR_COUNT, 0};
  CHECK(tfp_action_configure_catalogue(&catalogue, tfp_action_gear, TFP_ACTION_GEAR_COUNT) && catalogue.count == 105);
  int by_group[TFP_EW_GROUPS] = {0};
  for (size_t i = 0; i < catalogue.count; i++) {
    const TfpActionWeapon *info = &catalogue.weapons[i];
    const TfpEwGroup group = tfp_ew_group_of_kind(info->kind);
    by_group[group]++;
    /* the family the Weapon: line names never disagrees with the slot */
    if (info->family == TFP_FAMILY_SIDEARM) CHECK(group == TFP_EW_SECONDARY);
    else if (info->family == TFP_FAMILY_SUPPORT) CHECK(group == TFP_EW_SUPPORT);
    else if (info->family != TFP_FAMILY_NONE) CHECK(group == TFP_EW_PRIMARY);
  }
  CHECK(by_group[TFP_EW_PRIMARY] + by_group[TFP_EW_SECONDARY] + by_group[TFP_EW_SUPPORT] + by_group[TFP_EW_OTHER] == 105);
  CHECK(by_group[TFP_EW_PRIMARY] >= 53 && by_group[TFP_EW_SECONDARY] >= 20 && by_group[TFP_EW_SUPPORT] >= 32 && by_group[TFP_EW_OTHER] == 0);
  const TfpActionWeapon *liberator = tfp_action_weapon_info(&catalogue, "64ce3d03c0118296");   /* AR-23 Liberator, the type's bytes reversed */
  CHECK(liberator && !strcmp(liberator->name, "AR-23 Liberator") && tfp_ew_group_of_kind(liberator->kind) == TFP_EW_PRIMARY);

  /* ---- the order ---- */
  TfpEwWeapon list[12];
  int count = 0;
  CHECK(tfp_ew_add(list, &count, 12, "k-stalwart", "M-105 Stalwart", TFP_EW_SUPPORT, true));
  CHECK(tfp_ew_add(list, &count, 12, "k-breaker", "SG-225 Breaker", TFP_EW_PRIMARY, true));
  CHECK(tfp_ew_add(list, &count, 12, "k-peace", "P-2 Peacemaker", TFP_EW_SECONDARY, true));
  CHECK(tfp_ew_add(list, &count, 12, "k-lib", "ar-23 liberator", TFP_EW_PRIMARY, true));          /* small letters sort with capitals */
  CHECK(tfp_ew_add(list, &count, 12, "k-pen", "AR-23P Liberator Penetrator", TFP_EW_PRIMARY, true));
  CHECK(tfp_ew_add(list, &count, 12, "0123456789abcdef", NULL, TFP_EW_OTHER, true));              /* not in the catalogue: its key */
  CHECK(tfp_ew_add(list, &count, 12, "k-senator", "P-4 Senator", TFP_EW_SECONDARY, true));
  CHECK(!tfp_ew_add(list, &count, 12, "k-senator", "P-4 Senator", TFP_EW_SECONDARY, true) && count == 7);   /* once */
  CHECK(!tfp_ew_add(list, &count, 12, NULL, "x", TFP_EW_OTHER, true) && !tfp_ew_add(list, &count, 12, "", "x", TFP_EW_OTHER, true));
  CHECK(!tfp_ew_add(list, &count, 7, "k-more", "More", TFP_EW_OTHER, true) && count == 7);         /* full */
  tfp_ew_sort(list, count, NULL);
  static const char *const plain[7] = {"ar-23 liberator", "AR-23P Liberator Penetrator", "SG-225 Breaker", "P-2 Peacemaker", "P-4 Senator",
                                       "M-105 Stalwart", "0123456789abcdef"};
  for (int i = 0; i < 7; i++) CHECK(!strcmp(list[i].name, plain[i]) && !list[i].held);
  TfpEwRow rows[TFP_EW_ROWS];
  int n = tfp_ew_rows(list, count, 0, rows, TFP_EW_ROWS);
  CHECK(n == 11);                                                                                  /* 4 headers, 7 weapons */
  CHECK(rows[0].type == TFP_EW_ROW_HEADER && rows[0].group == TFP_EW_PRIMARY && rows[0].count == 3 && !rows[0].collapsed);
  CHECK(rows[1].type == TFP_EW_ROW_WEAPON && rows[1].weapon == &list[0] && rows[3].weapon == &list[2]);
  CHECK(rows[4].type == TFP_EW_ROW_HEADER && rows[4].group == TFP_EW_SECONDARY && rows[4].count == 2);
  CHECK(rows[7].type == TFP_EW_ROW_HEADER && rows[7].group == TFP_EW_SUPPORT && rows[7].count == 1);
  CHECK(rows[9].type == TFP_EW_ROW_HEADER && rows[9].group == TFP_EW_OTHER && rows[10].weapon == &list[6]);

  /* the weapon in your hands: first, marked, and no longer under its category */
  tfp_ew_sort(list, count, "k-senator");
  CHECK(list[0].held && !strcmp(list[0].name, "P-4 Senator") && !list[1].held && !strcmp(list[1].name, "ar-23 liberator"));
  n = tfp_ew_rows(list, count, 0, rows, TFP_EW_ROWS);
  CHECK(n == 11 && rows[0].type == TFP_EW_ROW_HELD && rows[0].weapon == &list[0] && rows[1].type == TFP_EW_ROW_HEADER);
  CHECK(rows[5].type == TFP_EW_ROW_HEADER && rows[5].group == TFP_EW_SECONDARY && rows[5].count == 1 && !strcmp(rows[6].weapon->name, "P-2 Peacemaker"));
  /* held and the only one of its category: that category has no header */
  tfp_ew_sort(list, count, "k-stalwart");
  n = tfp_ew_rows(list, count, 0, rows, TFP_EW_ROWS);
  CHECK(n == 10 && rows[0].type == TFP_EW_ROW_HELD);
  for (int i = 1; i < n; i++) CHECK(rows[i].group != TFP_EW_SUPPORT);
  /* a key that is not in the list marks nothing; sorting again takes the mark off */
  tfp_ew_sort(list, count, "k-nothing");
  for (int i = 0; i < count; i++) CHECK(!list[i].held && !strcmp(list[i].name, plain[i]));
  /* folded: the header stays with its count, its weapons go */
  n = tfp_ew_rows(list, count, 1u << TFP_EW_PRIMARY | 1u << TFP_EW_OTHER, rows, TFP_EW_ROWS);
  CHECK(n == 7 && rows[0].collapsed && rows[0].count == 3 && rows[1].type == TFP_EW_ROW_HEADER && rows[1].group == TFP_EW_SECONDARY && !rows[1].collapsed);
  CHECK(rows[6].type == TFP_EW_ROW_HEADER && rows[6].group == TFP_EW_OTHER && rows[6].collapsed && rows[6].count == 1);
  CHECK(tfp_ew_rows(list, count, 0, rows, 5) == 5 && tfp_ew_rows(list, 0, 0, rows, TFP_EW_ROWS) == 0);

  /* ---- the drawn frame, 1920 x 1080 ---- */
  TfpEwView view; memset(&view, 0, sizeof view);
  double wheel = 0;
  tfp_ew_sort(list, count, "k-senator");
  const TfpEwBox box = tfp_ew_box(1920, 1080);
  CHECK(box.has && box.x == 24 && box.y == 24 && box.w == 320 && box.y + box.h == 1080 - 310 && box.visible >= 20);
  CHECK(box.y + box.h < 1080 - 280 - 20);                                                          /* under the editor's panel and its footer line */
  n = tfp_ew_frame(&view, list, count, 1920, 1080, 1000, 500, false, &wheel, items, TFP_EW_COMMANDS);
  CHECK(n == 1 + 1 + 11 + 4);                                                                      /* panel, title, a text a row, a field a header */
  CHECK(!items[0].is_text && items[0].color == TFP_ED_COLOR_PANEL && items[0].x == box.x && items[0].h == box.h);
  CHECK(!strcmp(items[1].text, "SAVED PLACEMENTS") && items[1].color == TFP_ED_COLOR_ACCENT);
  const int held = find(n, "IN HANDS: P-4 Senator"), primary = find(n, "- PRIMARY (3)"), lib = find(n, "ar-23 liberator"), pen = find(n, "AR-23P"),
            breaker = find(n, "SG-225"), secondary = find(n, "- SECONDARY (1)"), support = find(n, "- SUPPORT (1)"), other = find(n, "- OTHER (1)"),
            key = find(n, "0123456789abcdef");
  CHECK(held == 2 && held < primary && primary < lib && lib < pen && pen < breaker && breaker < secondary && secondary < support && support < other && other < key);
  CHECK(items[held].color == TFP_ED_COLOR_ACCENT && items[primary].color == TFP_ED_COLOR_ACCENT && items[lib].color == TFP_ED_COLOR_TEXT);
  CHECK(!items[primary - 1].is_text && items[primary - 1].color == TFP_ED_COLOR_FIELD);            /* the header's field, under its text */
  CHECK(items[held].y > items[primary].y && items[primary].y > items[lib].y && items[lib].y > items[pen].y);   /* top to bottom */
  CHECK(items[lib].x > items[primary].x);                                                          /* a weapon is set in under its header */
  for (int i = 0; i < n; i++) {
    CHECK(items[i].color >= TFP_ED_COLOR_PANEL && items[i].color <= TFP_ED_COLOR_TEXT_DIM);        /* roles only: the host's tokens */
    CHECK(items[i].x >= box.x && items[i].y >= box.y && items[i].y <= box.y + box.h);
  }
  CHECK(!view.press_in_list && view.collapsed == 0 && wheel == 0);

  /* ---- a click on a header folds it; it stays folded; another unfolds it ---- */
  const double hx = items[primary - 1].x + 30, hy = items[primary - 1].y + 5;
  n = tfp_ew_frame(&view, list, count, 1920, 1080, hx, hy, true, &wheel, items, TFP_EW_COMMANDS);
  CHECK(view.collapsed == 1u << TFP_EW_PRIMARY && view.press_in_list && n == 1 + 1 + 8 + 4);
  CHECK(find(n, "+ PRIMARY (3)") >= 0 && find(n, "ar-23 liberator") < 0 && find(n, "P-2 Peacemaker") >= 0);
  n = tfp_ew_frame(&view, list, count, 1920, 1080, hx, hy, true, &wheel, items, TFP_EW_COMMANDS);     /* held down: once, not every frame */
  CHECK(view.collapsed == 1u << TFP_EW_PRIMARY && view.press_in_list);
  n = tfp_ew_frame(&view, list, count, 1920, 1080, 1000, 500, true, &wheel, items, TFP_EW_COMMANDS);  /* dragged off the list, still held: still the list's */
  CHECK(view.press_in_list);
  n = tfp_ew_frame(&view, list, count, 1920, 1080, 1000, 500, false, &wheel, items, TFP_EW_COMMANDS);
  CHECK(!view.press_in_list && view.collapsed == 1u << TFP_EW_PRIMARY && find(n, "+ PRIMARY (3)") >= 0);   /* remembered */
  /* a press that begins off the list is the editor's, also when it is dragged over the list */
  n = tfp_ew_frame(&view, list, count, 1920, 1080, 1000, 500, true, &wheel, items, TFP_EW_COMMANDS);
  CHECK(!view.press_in_list);
  n = tfp_ew_frame(&view, list, count, 1920, 1080, hx, hy, true, &wheel, items, TFP_EW_COMMANDS);
  CHECK(!view.press_in_list && view.collapsed == 1u << TFP_EW_PRIMARY);
  n = tfp_ew_frame(&view, list, count, 1920, 1080, hx, hy, false, &wheel, items, TFP_EW_COMMANDS);
  /* a click on a weapon's row, or on the title, is the list's and changes nothing */
  const int peace = find(n, "P-2 Peacemaker");
  n = tfp_ew_frame(&view, list, count, 1920, 1080, items[peace].x + 5, items[peace].y + 3, true, &wheel, items, TFP_EW_COMMANDS);
  CHECK(view.press_in_list && view.collapsed == 1u << TFP_EW_PRIMARY);
  n = tfp_ew_frame(&view, list, count, 1920, 1080, hx, hy, false, &wheel, items, TFP_EW_COMMANDS);
  n = tfp_ew_frame(&view, list, count, 1920, 1080, hx, hy, true, &wheel, items, TFP_EW_COMMANDS);     /* the header again */
  CHECK(view.collapsed == 0 && find(n, "- PRIMARY (3)") >= 0 && find(n, "ar-23 liberator") >= 0);
  n = tfp_ew_frame(&view, list, count, 1920, 1080, hx, hy, false, &wheel, items, TFP_EW_COMMANDS);
  /* the pointer not known yet: nothing is hit */
  n = tfp_ew_frame(&view, list, count, 1920, 1080, NAN, NAN, true, &wheel, items, TFP_EW_COMMANDS);
  CHECK(!view.press_in_list && view.collapsed == 0);
  n = tfp_ew_frame(&view, list, count, 1920, 1080, NAN, NAN, false, &wheel, items, TFP_EW_COMMANDS);

  /* ---- the wheel ---- */
  wheel = 1;
  n = tfp_ew_frame(&view, list, count, 1920, 1080, 1000, 500, false, &wheel, items, TFP_EW_COMMANDS);
  CHECK(wheel == 1 && view.scroll == 0);                                                           /* off the list: the editor's depth */
  n = tfp_ew_frame(&view, list, count, 1920, 1080, hx, hy, false, &wheel, items, TFP_EW_COMMANDS);
  CHECK(wheel == 0 && view.scroll == 0 && find(n, "(wheel)") < 0);                                 /* over it: used; everything fits, nothing moves */
  static TfpEwWeapon many[TFP_EW_MAX];
  int many_count = 0;
  for (int i = 0; i < 60; i++) {
    char k[16], name[32];
    snprintf(k, sizeof k, "key-%02d", i); snprintf(name, sizeof name, "Weapon %02d", 59 - i);
    CHECK(tfp_ew_add(many, &many_count, TFP_EW_MAX, k, name, (TfpEwGroup)(i % 3), true));
  }
  tfp_ew_sort(many, many_count, "key-00");
  memset(&view, 0, sizeof view);
  n = tfp_ew_frame(&view, many, many_count, 1920, 1080, hx, hy, false, &wheel, items, TFP_EW_COMMANDS);
  CHECK(n <= TFP_EW_COMMANDS && find(n, "IN HANDS: Weapon 59") == 3 && find(n, "of 63 (wheel)") >= 0);     /* 60 weapons, 3 headers */
  wheel = -2;
  n = tfp_ew_frame(&view, many, many_count, 1920, 1080, hx, hy, false, &wheel, items, TFP_EW_COMMANDS);
  CHECK(wheel == 0 && view.scroll == 6 && find(n, "IN HANDS") < 0 && find(n, "7-") >= 0);
  wheel = -100;
  n = tfp_ew_frame(&view, many, many_count, 1920, 1080, hx, hy, false, &wheel, items, TFP_EW_COMMANDS);
  CHECK(view.scroll == 63 - box.visible && find(n, "Weapon 57") >= 0);                             /* the end: the last of Support */
  wheel = 100;
  n = tfp_ew_frame(&view, many, many_count, 1920, 1080, hx, hy, false, &wheel, items, TFP_EW_COMMANDS);
  CHECK(view.scroll == 0 && find(n, "IN HANDS: Weapon 59") == 3);
  /* folding everything away brings a scrolled list back into its panel */
  view.scroll = 30; view.collapsed = 7;
  n = tfp_ew_frame(&view, many, many_count, 1920, 1080, 1000, 500, false, &wheel, items, TFP_EW_COMMANDS);
  CHECK(view.scroll == 0 && n == 1 + 1 + 1 + 3 * 2 && find(n, "(wheel)") < 0);
  /* a small item buffer is never overrun */
  view.collapsed = 0;
  CHECK(tfp_ew_frame(&view, many, many_count, 1920, 1080, 1000, 500, false, &wheel, items, 7) <= 7);

  /* ---- nothing saved, nothing held; a held weapon without a saved placement; a frame too small ---- */
  memset(&view, 0, sizeof view);
  n = tfp_ew_frame(&view, many, 0, 1920, 1080, 1000, 500, false, &wheel, items, TFP_EW_COMMANDS);
  CHECK(n == 3 && find(n, "None yet") == 2 && items[2].color == TFP_ED_COLOR_TEXT_DIM);
  count = 0;
  CHECK(tfp_ew_add(list, &count, 12, "k-lib", "AR-23 Liberator", TFP_EW_PRIMARY, false));
  tfp_ew_sort(list, count, "k-lib");
  n = tfp_ew_frame(&view, list, count, 1920, 1080, 1000, 500, false, &wheel, items, TFP_EW_COMMANDS);
  CHECK(n == 3 && find(n, "IN HANDS: AR-23 Liberator (none saved)") == 2);
  CHECK(!tfp_ew_add(list, &count, 12, "k-lib", "AR-23 Liberator", TFP_EW_PRIMARY, true) && count == 1 && list[0].saved);   /* found saved after all */
  view.press_in_list = true;
  CHECK(tfp_ew_frame(&view, list, count, 640, 240, 30, 30, true, &wheel, items, TFP_EW_COMMANDS) == 0 && !view.press_in_list);   /* no room: no list, no press taken */
  CHECK(!tfp_ew_box(0, 0).has && !tfp_ew_box(NAN, NAN).has);
  /* 4K: the same list, scaled as the editor's panel is */
  const TfpEwBox big = tfp_ew_box(3840, 2160);
  CHECK(big.has && big.scale == 1.5 && big.x == 36 && big.w == 480 && big.visible > box.visible);

  printf("editor weapons: %u checks passed (the catalogue's slot is the category; held first, then Primary / Secondary / Support / Other by name; "
         "headers fold on a click and stay folded; a press or the wheel on the list is kept from the editor)\n", checks);
  return 0;
}
