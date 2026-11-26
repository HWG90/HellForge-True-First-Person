/* Round 41 (src/f11_hint.h): a player who presses F11, the old toggle key, while the toggle key is U (or any other key)
 * gets one short on-screen notice naming the configured key, at most once in TFP_F11_HINT_REPEAT_MS, and one log line
 * in the session. Nothing while chat or an overlay window has the keyboard, nothing when the toggle key is F11 (the menu
 * refuses it) or unbound, nothing outside the game. F11 itself is only read (GetAsyncKeyState), never taken. */
#include "f11_hint.h"
#include <stdlib.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

typedef struct Io {
  bool f11;          /* F11's level */
  unsigned reads;    /* F11 level reads (GetAsyncKeyState calls) */
  int chat;
  bool overlay, panel;
  unsigned says, shows;
  char said[300], shown[300];
  bool show_ok;
} Io;
static bool io_key(void *u, int vk) { Io *io = u; io->reads++; return vk == TFP_F11_VK && io->f11; }
static int io_chat(void *u) { return ((Io *)u)->chat; }
static bool io_overlay(void *u) { return ((Io *)u)->overlay; }
static bool io_panel(void *u) { return ((Io *)u)->panel; }
static void io_say(void *u, const char *line) { Io *io = u; io->says++; snprintf(io->said, sizeof io->said, "%s", line); }
static bool io_show(void *u, const char *notice, char *why, size_t why_size) {
  Io *io = u;
  io->shows++;
  snprintf(io->shown, sizeof io->shown, "%s", notice);
  if (!io->show_ok) snprintf(why, why_size, "the Goose Overlay Host is not loaded");
  return io->show_ok;
}
static TfpF11HintIo io_of(Io *io) { return (TfpF11HintIo){io, io_key, io_chat, io_overlay, io_panel, io_say, io_show}; }

/* One press: up for a tick, down for a tick, up again. Returns how many ticks reported a notice. */
static int press(TfpF11Hint *h, Io *io, uint64_t *now, int key, const char *name, bool in_game) {
  const TfpF11HintIo f = io_of(io);
  int n = 0;
  io->f11 = false; n += tfp_f11_hint_tick(h, &f, *now, key, name, in_game, true); *now += 16;
  io->f11 = true;  n += tfp_f11_hint_tick(h, &f, *now, key, name, in_game, true); *now += 16;
  io->f11 = true;  n += tfp_f11_hint_tick(h, &f, *now, key, name, in_game, true); *now += 16; /* held: no second edge */
  io->f11 = false; n += tfp_f11_hint_tick(h, &f, *now, key, name, in_game, true); *now += 16;
  return n;
}
static Io fresh(void) { Io io; memset(&io, 0, sizeof io); io.show_ok = true; return io; }

static void default_u_one_notice_one_line(void) {
  TfpF11Hint h = {0};
  Io io = fresh();
  uint64_t now = 100000;
  CHECK(press(&h, &io, &now, 0x55, "U", true) == 1);
  CHECK(io.shows == 1);
  CHECK(!strcmp(io.shown, "True First Person: press U to switch first person (F11 is HellForge's Armor LUT editor now)"));
  CHECK(io.says == 1);
  CHECK(!strcmp(io.said, "Keys: F11 pressed; True First Person is on U (F11 opens HellForge's Armor LUT editor)"));
}
static void second_press_within_ten_minutes_shows_nothing(void) {
  TfpF11Hint h = {0};
  Io io = fresh();
  uint64_t now = 100000;
  CHECK(press(&h, &io, &now, 0x55, "U", true) == 1);
  const unsigned reads = io.reads;
  for (int i = 0; i < 20; i++) CHECK(press(&h, &io, &now, 0x55, "U", true) == 0);
  now += 30000;
  CHECK(press(&h, &io, &now, 0x55, "U", true) == 0);
  CHECK(io.shows == 1 && io.says == 1);
  CHECK(io.reads == reads); /* during the quiet time F11 is not even read */
  /* just before the ten minutes are up: still nothing; after them, the next press shows it again, without a log line */
  now = 100000 + 16 + TFP_F11_HINT_REPEAT_MS - 200;
  CHECK(press(&h, &io, &now, 0x55, "U", true) == 0);
  now = 100000 + 16 + TFP_F11_HINT_REPEAT_MS + 1;
  CHECK(press(&h, &io, &now, 0x55, "U", true) == 1);
  CHECK(io.shows == 2 && io.says == 1);
}
static void held_at_resume_is_not_a_press(void) {
  /* F11 already held when the watch starts (the game came to the front, the quiet time ended, the mod reached the game):
   * the first sample is not an edge */
  TfpF11Hint h = {0};
  Io io = fresh();
  const TfpF11HintIo f = io_of(&io);
  io.f11 = true;
  CHECK(!tfp_f11_hint_tick(&h, &f, 1000, 0x55, "U", true, true));
  CHECK(!tfp_f11_hint_tick(&h, &f, 1016, 0x55, "U", true, true));
  io.f11 = false;
  CHECK(!tfp_f11_hint_tick(&h, &f, 1032, 0x55, "U", true, true));
  io.f11 = true;
  CHECK(tfp_f11_hint_tick(&h, &f, 1048, 0x55, "U", true, true));
  /* lost focus while held, back in front still held: no edge */
  h = (TfpF11Hint){0};
  io = fresh();
  io.f11 = false;
  CHECK(!tfp_f11_hint_tick(&h, &f, 2000, 0x55, "U", true, true));
  io.f11 = true;
  CHECK(!tfp_f11_hint_tick(&h, &f, 2016, 0x55, "U", true, false));
  CHECK(!tfp_f11_hint_tick(&h, &f, 2032, 0x55, "U", true, true));
  CHECK(io.shows == 0);
}
static void toggle_key_f11_or_unbound_nothing(void) {
  /* the menu refuses F11 (tfp_wire_menu_toggle_key never returns it); if it ever came through, nothing extra */
  TfpF11Hint h = {0};
  Io io = fresh();
  uint64_t now = 100000;
  CHECK(press(&h, &io, &now, TFP_F11_VK, "F11", true) == 0);
  CHECK(io.shows == 0 && io.says == 0 && io.reads == 0);
  CHECK(press(&h, &io, &now, 0, "", true) == 0);  /* unbound: no key to name */
  CHECK(io.shows == 0 && io.says == 0 && io.reads == 0);
}
static void chat_overlay_panel_nothing(void) {
  TfpF11Hint h = {0};
  Io io = fresh();
  uint64_t now = 100000;
  io.chat = 1;
  CHECK(press(&h, &io, &now, 0x55, "U", true) == 0);
  io.chat = 0, io.overlay = true;
  CHECK(press(&h, &io, &now, 0x55, "U", true) == 0);
  io.overlay = false, io.panel = true;
  CHECK(press(&h, &io, &now, 0x55, "U", true) == 0);
  CHECK(io.shows == 0 && io.says == 0);
  /* a refused press uses nothing up: the next clean press shows it */
  io.panel = false;
  CHECK(press(&h, &io, &now, 0x55, "U", true) == 1);
  CHECK(io.shows == 1 && io.says == 1);
  /* the chat state unreadable (-1): F11 is no text key, so a press still shows it */
  TfpF11Hint h2 = {0};
  Io io2 = fresh();
  io2.chat = -1;
  CHECK(press(&h2, &io2, &now, 0x55, "U", true) == 1);
}
static void outside_the_game_nothing(void) {
  TfpF11Hint h = {0};
  Io io = fresh();
  uint64_t now = 100000;
  CHECK(press(&h, &io, &now, 0x55, "U", false) == 0);
  CHECK(io.shows == 0 && io.reads == 0);
}
static void notice_names_the_configured_key(void) {
  const struct { int key; const char *name; } keys[] = {{'J', "J"}, {0x70, "F1"}, {0x21, "PAGE UP"}, {0x05, "MOUSE 4"}};
  for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
    TfpF11Hint h = {0};
    Io io = fresh();
    uint64_t now = 100000;
    char want[200], line[200];
    snprintf(want, sizeof want, "True First Person: press %s to switch first person (F11 is HellForge's Armor LUT editor now)", keys[i].name);
    snprintf(line, sizeof line, "Keys: F11 pressed; True First Person is on %s (F11 opens HellForge's Armor LUT editor)", keys[i].name);
    CHECK(press(&h, &io, &now, keys[i].key, keys[i].name, true) == 1);
    CHECK(!strcmp(io.shown, want));
    CHECK(!strcmp(io.said, line));
    CHECK(strstr(io.shown, "press U") == NULL);
  }
}
static void no_overlay_says_so_in_the_one_line(void) {
  TfpF11Hint h = {0};
  Io io = fresh();
  io.show_ok = false;
  uint64_t now = 100000;
  CHECK(press(&h, &io, &now, 0x55, "U", true) == 1);
  CHECK(io.says == 1);
  CHECK(!strcmp(io.said, "Keys: F11 pressed; True First Person is on U (F11 opens HellForge's Armor LUT editor); "
                         "no on-screen notice: the Goose Overlay Host is not loaded"));
  /* the quiet time holds all the same: no line each press */
  CHECK(press(&h, &io, &now, 0x55, "U", true) == 0);
  CHECK(io.says == 1);
}

/* ---- the HUD notice: drawn through the host with theme tokens, for TFP_F11_HINT_SHOW_S, then closed ---- */
static OvPanelDef registered;
static int registrations, open_state = -1, draws_text, rects;
static uint32_t text_colours[8], rect_colour;
static char drawn[300];
static int fake_panel;
static OvPanel *f_register(const OvPanelDef *d) { registered = *d; registrations++; return (OvPanel *)&fake_panel; }
static void f_set_open(OvPanel *p, int open) { (void)p; open_state = open; }
static int f_is_open(OvPanel *p) { (void)p; return open_state; }
static void f_draw_text(float x, float y, uint32_t c, int font, float size, const char *t) {
  (void)x, (void)y, (void)font, (void)size;
  if (draws_text < 8) text_colours[draws_text] = c;
  draws_text++;
  snprintf(drawn, sizeof drawn, "%s", t);
}
static void f_text_size(int font, float size, const char *t, float *w, float *h) { (void)font, (void)size; *w = 9.0f * (float)strlen(t); *h = 20; }
static void f_rect_filled(float x0, float y0, float x1, float y1, uint32_t c, float r) { (void)x0, (void)y0, (void)x1, (void)y1, (void)r; rects++; rect_colour = c; }
static uint32_t theme_plate = 0x11000000u + OV_THEME_HUD_PLATE;
static uint32_t f_theme(int role) { return role == OV_THEME_HUD_PLATE ? theme_plate : 0x11000000u + (uint32_t)role; }
static OvApi fake_api(void) {
  OvApi a;
  memset(&a, 0, sizeof a);
  a.version = 8, a.size = sizeof a;
  a.register_panel = f_register, a.set_open = f_set_open, a.is_open = f_is_open;
  a.draw_text = f_draw_text, a.text_size = f_text_size, a.draw_rect_filled = f_rect_filled, a.theme = f_theme;
  return a;
}
static void hud_draws_with_theme_tokens_and_closes(void) {
  OvApi api = fake_api();
  static TfpF11HintHud hud;
  memset(&hud, 0, sizeof hud);
  char why[120] = "";
  CHECK(!tfp_f11_hint_hud_show(&hud, NULL, "goose.true-first-person/f11", "x", why, sizeof why));
  CHECK(strstr(why, "Goose Overlay Host") != NULL);
  CHECK(tfp_f11_hint_hud_show(&hud, &api, "goose.true-first-person/f11", "True First Person: press U to switch", why, sizeof why));
  CHECK(registrations == 1 && registered.kind == OV_PANEL_HUD && registered.hotkey == 0 && open_state == 1);
  CHECK(!strcmp(registered.id, "goose.true-first-person/f11"));
  OvFrame frame = {1920, 1080, 0.016f, 50.0, 1.0f, 0};
  registered.draw(&frame, registered.user);
  CHECK(!strcmp(drawn, "True First Person: press U to switch"));
  CHECK(rects == 1 && rect_colour == f_theme(OV_THEME_HUD_PLATE));
  CHECK(draws_text == 2 && text_colours[0] == f_theme(OV_THEME_HUD_SHADOW) && text_colours[1] == f_theme(OV_THEME_TEXT));
  frame.time = 50.0 + TFP_F11_HINT_SHOW_S - 0.5;
  registered.draw(&frame, registered.user);
  CHECK(open_state == 1 && draws_text == 4);
  frame.time = 50.0 + TFP_F11_HINT_SHOW_S + 0.1;
  registered.draw(&frame, registered.user);
  CHECK(open_state == 0 && draws_text == 4); /* closed: the host stops calling it */
  /* a second show reuses the panel and starts a new clock from its first frame */
  CHECK(tfp_f11_hint_hud_show(&hud, &api, "goose.true-first-person/f11", "again", why, sizeof why));
  CHECK(registrations == 1 && open_state == 1);
  frame.time = 900.0;
  registered.draw(&frame, registered.user);
  CHECK(!strcmp(drawn, "again") && open_state == 1);
  /* a host without the HUD roles answers 0xFFFFFFFF for them: the panel token instead, never a literal */
  theme_plate = 0xFFFFFFFFu;
  registered.draw(&frame, registered.user);
  CHECK(rect_colour == f_theme(OV_THEME_PANEL));
  /* a host that refuses the panel: not shown, and why */
  static TfpF11HintHud other;
  memset(&other, 0, sizeof other);
  api.register_panel = NULL;
  CHECK(!tfp_f11_hint_hud_show(&other, &api, "goose.true-first-person/f11b", "x", why, sizeof why));
}

int main(void) {
  default_u_one_notice_one_line();
  second_press_within_ten_minutes_shows_nothing();
  held_at_resume_is_not_a_press();
  toggle_key_f11_or_unbound_nothing();
  chat_overlay_panel_nothing();
  outside_the_game_nothing();
  notice_names_the_configured_key();
  no_overlay_says_so_in_the_one_line();
  hud_draws_with_theme_tokens_and_closes();
  printf("f11_hint: %d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
