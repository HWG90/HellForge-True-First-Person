/* Round 45 (src/cockpit_frame.h): the cockpit frame drawn over the see-through exosuit seat. Shown only while seated,
 * with See through the exosuit cockpit and Cockpit frame on and the suit seen through; hidden on leaving, death, an
 * option off or the mod off (no seat then); one log line a seat; the plain view and one line without the host. The
 * layout keeps the game's HUD places clear at 1080p, 1440p, 4K and 21:9; every colour is a theme colour; the panel id
 * is this load's own; no colour literal in the source. */
#include "cockpit_frame.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

/* ---- a recording host ---- */
#define ROLES (OV_THEME_HUD_SHADOW + 1)
static uint32_t theme_of[ROLES];
static uint32_t t_theme(int role) { return role >= 0 && role < ROLES ? theme_of[role] : UINT32_MAX; }
typedef struct Call { TfpCockpitBox box; uint32_t colour; char what[16]; } Call;
static Call calls[4096];
static int ncalls;
static bool bad_colour;
static void rec(const char *what, float x0, float y0, float x1, float y1, uint32_t c) {
  if (ncalls < 4096) {
    Call *k = &calls[ncalls++];
    k->box = (TfpCockpitBox){fminf(x0, x1), fminf(y0, y1), fmaxf(x0, x1), fmaxf(y0, y1)};
    k->colour = c; snprintf(k->what, sizeof k->what, "%s", what);
  }
  bool known = false; /* the colour's RGB is a theme role's, its alpha no more than the role's */
  for (int r = 0; r < ROLES; r++) if ((theme_of[r] << 8) == (c << 8) && (c >> 24) <= (theme_of[r] >> 24)) known = true;
  if (!known) bad_colour = true;
}
static void t_line(float x0, float y0, float x1, float y1, uint32_t c, float t) { rec("line", x0 - t / 2, y0 - t / 2, x1 + t / 2, y1 + t / 2, c); }
static void t_rect(float x0, float y0, float x1, float y1, uint32_t c, float r, float t) { (void)r; rec("rect", x0 - t / 2, y0 - t / 2, x1 + t / 2, y1 + t / 2, c); }
static void t_rect_filled(float x0, float y0, float x1, float y1, uint32_t c, float r) { (void)r; rec("fill", x0, y0, x1, y1, c); }
static void t_circle(float x, float y, float r, uint32_t c, float t) { rec("circle", x - r - t / 2, y - r - t / 2, x + r + t / 2, y + r + t / 2, c); }
static void t_circle_filled(float x, float y, float r, uint32_t c) { rec("disc", x - r, y - r, x + r, y + r, c); }
static void t_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
  rec("tri", fminf(x0, fminf(x1, x2)), fminf(y0, fminf(y1, y2)), fmaxf(x0, fmaxf(x1, x2)), fmaxf(y0, fmaxf(y1, y2)), c);
}
static float text_px(float size) { return size > 0 ? size : 18; }
static void t_text_size(int font, float size, const char *text, float *w, float *h) { (void)font; *w = 0.62f * text_px(size) * (float)strlen(text); *h = text_px(size) * 1.2f; }
static void t_text(float x, float y, uint32_t c, int font, float size, const char *text) {
  float w, h; t_text_size(font, size, text, &w, &h); rec("text", x, y, x + w, y + h, c);
}
static struct { int registered, opens, closes, open; OvPanelDef def; char id[160]; bool refuse; } host;
static OvPanel *const HANDLE = (OvPanel *)(uintptr_t)0x40;
static OvPanel *t_register(const OvPanelDef *def) {
  host.registered++;
  if (host.refuse) return NULL;
  host.def = *def; snprintf(host.id, sizeof host.id, "%s", def->id); host.open = 1;
  return HANDLE;
}
static void t_set_open(OvPanel *p, int open) { (void)p; open ? host.opens++ : host.closes++; host.open = open; }
static OvApi api;
static void new_host(void) {
  memset(&api, 0, sizeof api); memset(&host, 0, sizeof host);
  api.version = 8, api.size = sizeof api;
  api.register_panel = t_register, api.set_open = t_set_open, api.theme = t_theme;
  api.draw_line = t_line, api.draw_rect = t_rect, api.draw_rect_filled = t_rect_filled, api.draw_circle = t_circle;
  api.draw_circle_filled = t_circle_filled, api.draw_triangle_filled = t_tri, api.draw_text = t_text, api.text_size = t_text_size;
  for (int r = 0; r < ROLES; r++) theme_of[r] = ((uint32_t)(200 + r % 50) << 24) | (uint32_t)(r * 0x0A0B07 + 0x101010); /* distinct per role */
}

/* ---- the HUD places ---- */
static void measured_boxes_inside_the_reserve(void) {
  /* what the game drew on the user's 2000 x 1125 screenshot (round 37), in its pixels */
  static const TfpCockpitBox seen[TFP_COCKPIT_HUD_COUNT] = {
    {8, 30, 210, 68}, {715, 28, 1285, 115}, {1530, 40, 1995, 150}, {8, 965, 480, 1110}, {840, 475, 1160, 670}, {1895, 1060, 1950, 1092}};
  TfpCockpitBox r[TFP_COCKPIT_HUD_COUNT];
  tfp_cockpit_hud_reserved(2000, 1125, r);
  for (int i = 0; i < TFP_COCKPIT_HUD_COUNT; i++)
    CHECK(r[i].x0 <= seen[i].x0 && r[i].y0 <= seen[i].y0 && r[i].x1 >= seen[i].x1 && r[i].y1 >= seen[i].y1);
}
static void draw_at(float w, float h, float opacity) {
  TfpCockpitLayout l;
  tfp_cockpit_layout(w, h, &l);
  ncalls = 0, bad_colour = false;
  tfp_cockpit_draw(&api, &l, opacity);
}
static void hud_kept_clear(void) {
  static const float sizes[][2] = {{1920, 1080}, {2560, 1440}, {3840, 2160}, {2560, 1080}, {3440, 1440}, {5120, 2160}, {2000, 1125}, {1280, 720}};
  for (unsigned s = 0; s < sizeof sizes / sizeof *sizes; s++) {
    const float w = sizes[s][0], h = sizes[s][1];
    new_host();
    draw_at(w, h, 1);
    TfpCockpitBox r[TFP_COCKPIT_HUD_COUNT];
    tfp_cockpit_hud_reserved(w, h, r);
    int over = 0, outside = 0;
    for (int i = 0; i < ncalls; i++) {
      for (int k = 0; k < TFP_COCKPIT_HUD_COUNT; k++)
        if (tfp_cockpit_box_overlap(calls[i].box, r[k])) {
          over++;
          printf("  %gx%g: %s %.0f,%.0f..%.0f,%.0f covers the %s\n", w, h, calls[i].what, calls[i].box.x0, calls[i].box.y0,
                 calls[i].box.x1, calls[i].box.y1, tfp_cockpit_hud_names[k]);
        }
      if (calls[i].box.x0 < -1 || calls[i].box.y0 < -1 || calls[i].box.x1 > w + 1 || calls[i].box.y1 > h + 1) outside++;
    }
    CHECK(over == 0);
    CHECK(outside == 0);
    CHECK(!bad_colour);
    TfpCockpitLayout l;
    tfp_cockpit_layout(w, h, &l);
    /* a frame: two pillars, the dash and its hood, both rails (16:9 and wider), the gauges on the dash */
    CHECK(l.plates >= 9);
    CHECK(l.heading.x0 >= l.dash.x0 && l.heading.x1 <= l.dash.x1 && l.heading.y0 >= l.dash.y0 && l.heading.y1 <= l.dash.y1);
    CHECK(l.lamp[0] + l.lamp_r * 1.7f < l.dash.x1 && l.lamp[1] - l.lamp_r * 1.7f > l.dash.y0);
    /* the open window is most of the screen */
    CHECK((l.window.x1 - l.window.x0) * (l.window.y1 - l.window.y0) > 0.75f * w * h);
    CHECK(ncalls < 120); /* cheap: one panel, about 95 calls */
  }
}
static void colours_are_the_theme_faded(void) {
  new_host();
  draw_at(2560, 1440, 1);
  CHECK(!bad_colour && ncalls > 0);
  uint32_t first = calls[0].colour;
  CHECK(first == theme_of[OV_THEME_PANEL]); /* the first plate: the window background, as it is */
  draw_at(2560, 1440, 0.5f);
  CHECK(!bad_colour);
  CHECK((calls[0].colour << 8) == (theme_of[OV_THEME_PANEL] << 8) && (calls[0].colour >> 24) == ((theme_of[OV_THEME_PANEL] >> 24) + 1) / 2);
  /* a host before the HUD roles answers white for them: the fallback role */
  new_host();
  theme_of[OV_THEME_HUD_SHADOW] = UINT32_MAX;
  draw_at(2560, 1440, 1);
  int whites = 0;
  for (int i = 0; i < ncalls; i++) if (calls[i].colour == UINT32_MAX) whites++;
  CHECK(whites == 0);
  CHECK(tfp_cockpit_fade(theme_of[OV_THEME_LINE], 0) >> 24 == 0 && (tfp_cockpit_fade(theme_of[OV_THEME_LINE], 2) == theme_of[OV_THEME_LINE]));
}

/* ---- no colour literal in the new source ---- */
static int literals_in(const char *path, const char *from, const char *to) {
  FILE *f = fopen(path, "rb");
  if (!f) { printf("  cannot read %s\n", path); return -1; }
  static char text[1 << 22];
  const size_t n = fread(text, 1, sizeof text - 1, f);
  fclose(f);
  text[n] = 0;
  char *start = from ? strstr(text, from) : text, *end = start && to ? strstr(start, to) : NULL;
  if (!start || (to && !end)) { printf("  %s: the marked part is missing\n", path); return -1; }
  if (end) *end = 0;
  int found = 0;
  for (char *p = start; *p; p++) {
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
      int digits = 0;
      while (isxdigit((unsigned char)p[2 + digits])) digits++;
      if (digits >= 6) { found++; printf("  %s: a colour-like literal %.*s\n", path, digits + 2, p); }
    }
    if (p[0] == '#' && isxdigit((unsigned char)p[1])) {
      int digits = 0;
      while (isxdigit((unsigned char)p[1 + digits])) digits++;
      if (digits == 6 || digits == 8) { found++; printf("  %s: a colour-like literal %.*s\n", path, digits + 1, p); }
    }
    if (!strncmp(p, "OV_RGBA", 7) || !strncmp(p, "IM_COL32", 8)) { found++; printf("  %s: a colour macro\n", path); }
  }
  return found;
}
static void no_colour_literals(void) {
  CHECK(literals_in(TFP_SRC "/cockpit_frame.h", NULL, NULL) == 0);
  CHECK(literals_in(TFP_SRC "/mod_view.c", "/* Round 45: the cockpit frame", "/* end of round 45's cockpit frame */") == 0);
  /* the scanner finds what it looks for */
  FILE *f = fopen(TFP_WORK "/cockpit_literal_probe.h", "wb");
  if (f) { fputs("uint32_t c = 0xFF1B1A18; /* #1B1A18 */ OV_RGBA(1,2,3,4)\n", f); fclose(f); }
  CHECK(literals_in(TFP_WORK "/cockpit_literal_probe.h", NULL, NULL) == 3);
}

/* ---- when ---- */
typedef struct Io { unsigned says, shows, hides, opacities, drawn_asks; char said[8][400]; bool show_ok, drawn_ok; float opacity; char why[80]; } Io;
static void io_say(void *u, const char *line) { Io *io = u; if (io->says < 8) snprintf(io->said[io->says], 400, "%s", line); io->says++; }
static bool io_show(void *u, float opacity, char *why, size_t size) {
  Io *io = u; io->shows++; io->opacity = opacity;
  if (!io->show_ok) snprintf(why, size, "%s", io->why);
  return io->show_ok;
}
static void io_hide(void *u) { ((Io *)u)->hides++; }
static void io_opacity(void *u, float opacity) { Io *io = u; io->opacities++; io->opacity = opacity; }
static bool io_drawn(void *u, char *d, size_t size) { Io *io = u; io->drawn_asks++; if (io->drawn_ok) snprintf(d, size, "2560 x 1440, margins"); return io->drawn_ok; }
static TfpCockpitFrameIo io_of(Io *io) { return (TfpCockpitFrameIo){io, io_say, io_show, io_hide, io_opacity, io_drawn}; }
static Io fresh(void) { Io io; memset(&io, 0, sizeof io); io.show_ok = true; snprintf(io.why, sizeof io.why, "the Goose Overlay Host is not loaded"); return io; }
static const TfpCockpitFrameIn SEATED = {true, true, true, true, 0.9f};
static int ticks(TfpCockpitFrame *f, Io *io, uint64_t *now, TfpCockpitFrameIn in, int n) {
  const TfpCockpitFrameIo x = io_of(io);
  int shown = 0;
  for (int i = 0; i < n; i++, *now += 16) shown += tfp_cockpit_frame_tick(f, &x, *now, &in);
  return shown;
}
static void shown_while_seated_and_hidden_on_leave(void) {
  TfpCockpitFrame f = {0}; Io io = fresh(); uint64_t now = 1000;
  TfpCockpitFrameIn off = SEATED; off.seated = false;
  CHECK(ticks(&f, &io, &now, off, 50) == 0 && io.shows == 0 && io.hides == 0 && io.says == 0); /* on foot: nothing asked */
  CHECK(ticks(&f, &io, &now, SEATED, 10) == 10 && io.shows == 1 && io.opacity == 0.9f);  /* asked once, on the edge */
  io.drawn_ok = true;
  CHECK(ticks(&f, &io, &now, SEATED, 10) == 10 && io.shows == 1 && io.says == 1);
  CHECK(!strcmp(io.said[0], "Seat: cockpit frame drawn (2560 x 1440, margins)"));
  const unsigned asks = io.drawn_asks;
  CHECK(ticks(&f, &io, &now, SEATED, 100) == 100 && io.drawn_asks == asks && io.says == 1); /* told: not asked again */
  CHECK(ticks(&f, &io, &now, off, 1) == 0 && io.hides == 1);                               /* leaving: hidden */
  CHECK(ticks(&f, &io, &now, off, 100) == 0 && io.hides == 1 && io.shows == 1);            /* and nothing more */
  CHECK(ticks(&f, &io, &now, SEATED, 5) == 5 && io.shows == 2 && io.says == 2);            /* a new seat: shown, one line */
  CHECK(f.entries == 2);
}
static void hidden_on_death_option_off_and_see_through_off(void) {
  TfpCockpitFrame f = {0}; Io io = fresh(); uint64_t now = 1000; io.drawn_ok = true;
  ticks(&f, &io, &now, SEATED, 5);
  TfpCockpitFrameIn in = SEATED; in.option = false;                       /* Cockpit frame switched off in the seat */
  CHECK(ticks(&f, &io, &now, in, 5) == 0 && io.hides == 1 && strstr(io.said[io.says - 1], "the option is off"));
  CHECK(ticks(&f, &io, &now, SEATED, 5) == 5 && io.shows == 2);           /* and on again */
  in = SEATED; in.see_through = false;                                    /* the cockpit drawn again: no frame over it */
  CHECK(ticks(&f, &io, &now, in, 5) == 0 && io.hides == 2);
  CHECK(ticks(&f, &io, &now, SEATED, 5) == 5 && io.shows == 3);
  in = SEATED; in.seated = false;                                         /* death, or the mod switched off: no seat shown */
  CHECK(ticks(&f, &io, &now, in, 5) == 0 && io.hides == 3);
  in = SEATED; in.suit = false;                                           /* the suit lost: the cockpit drawn, no frame */
  ticks(&f, &io, &now, SEATED, 5);
  CHECK(ticks(&f, &io, &now, in, 5) == 0 && io.hides == 4);
}
static void why_not_said_once_a_seat(void) {
  TfpCockpitFrame f = {0}; Io io = fresh(); uint64_t now = 1000;
  TfpCockpitFrameIn in = SEATED; in.see_through = false;
  ticks(&f, &io, &now, in, 300);
  CHECK(io.says == 1 && !strcmp(io.said[0], "Seat: no cockpit frame: See through the exosuit cockpit is off (the cockpit is drawn)"));
  CHECK(io.shows == 0);
  in = SEATED; in.option = false;
  Io io2 = fresh(); TfpCockpitFrame f2 = {0};
  ticks(&f2, &io2, &now, in, 300);
  CHECK(io2.says == 1 && !strcmp(io2.said[0], "Seat: no cockpit frame: the option is off") && io2.shows == 0);
  /* the suit not found yet: quiet for 3 s (the search takes up to 60 frames), then one line */
  Io io3 = fresh(); TfpCockpitFrame f3 = {0};
  in = SEATED; in.suit = false;
  ticks(&f3, &io3, &now, in, 100); /* 1.6 s */
  CHECK(io3.says == 0);
  ticks(&f3, &io3, &now, in, 200);
  CHECK(io3.says == 1 && strstr(io3.said[0], "no suit is seen through"));
  io3.drawn_ok = true;
  CHECK(ticks(&f3, &io3, &now, SEATED, 3) == 3 && io3.shows == 1 && io3.says == 2); /* found later in the same seat: shown */
}
static void no_host_plain_view_one_line(void) {
  TfpCockpitFrame f = {0}; Io io = fresh(); uint64_t now = 1000; io.show_ok = false;
  CHECK(ticks(&f, &io, &now, SEATED, 500) == 0);
  CHECK(io.shows == 1 && io.says == 1); /* not asked again every tick */
  CHECK(!strcmp(io.said[0], "Seat: no cockpit frame: the Goose Overlay Host is not loaded; the plain clear view"));
  TfpCockpitFrameIn off = SEATED; off.seated = false;
  ticks(&f, &io, &now, off, 5);
  CHECK(io.hides == 0);
  ticks(&f, &io, &now, SEATED, 5);
  CHECK(io.shows == 2 && io.says == 2); /* a new seat tries once more */
}
static void not_drawn_said_after_three_seconds(void) {
  TfpCockpitFrame f = {0}; Io io = fresh(); uint64_t now = 1000;
  ticks(&f, &io, &now, SEATED, 200);
  CHECK(io.says == 1 && strstr(io.said[0], "Seat: cockpit frame not drawn: the Goose Overlay Host drew no frame of it in 3 s"));
  const unsigned asks = io.drawn_asks;
  ticks(&f, &io, &now, SEATED, 100);
  CHECK(io.drawn_asks == asks && io.says == 1);
}
static void opacity_follows_the_slider(void) {
  TfpCockpitFrame f = {0}; Io io = fresh(); uint64_t now = 1000; io.drawn_ok = true;
  ticks(&f, &io, &now, SEATED, 5);
  CHECK(io.opacities == 0);
  TfpCockpitFrameIn in = SEATED; in.opacity = 0.5f;
  ticks(&f, &io, &now, in, 5);
  CHECK(io.opacities == 1 && io.opacity == 0.5f && io.shows == 1);
}

/* ---- the host's panel ---- */
static void panel_id_is_this_loads(void) {
  char a[120], b[120];
  tfp_cockpit_panel_id(a, sizeof a, 0x1234, 0x10000);
  tfp_cockpit_panel_id(b, sizeof b, 0x1234, 0x20000); /* a replaced build: another load, another tick */
  CHECK(!strncmp(a, "goose.true-first-person/cockpit-frame@", 38) && strcmp(a, b));
  CHECK(!strcmp(a, "goose.true-first-person/cockpit-frame@1234-10000"));
}
static void hud_show_draw_hide(void) {
  new_host();
  static TfpCockpitHud hud;
  memset(&hud, 0, sizeof hud);
  char why[160] = "", d[300] = "";
  CHECK(!tfp_cockpit_hud_show(&hud, NULL, "id@1", 1, why, sizeof why) && !strcmp(why, "the Goose Overlay Host is not loaded"));
  OvApi old = api; old.version = 1; old.theme = NULL;
  CHECK(!tfp_cockpit_hud_show(&hud, &old, "id@1", 1, why, sizeof why) && strstr(why, "lacks a HUD drawing call") && host.registered == 0);
  host.refuse = true;
  CHECK(!tfp_cockpit_hud_show(&hud, &api, "id@1", 1, why, sizeof why) && strstr(why, "refused"));
  host.refuse = false;
  CHECK(tfp_cockpit_hud_show(&hud, &api, "goose.true-first-person/cockpit-frame@1-2", 0.9f, why, sizeof why));
  CHECK(host.registered == 2 && !strcmp(host.id, "goose.true-first-person/cockpit-frame@1-2") && host.def.kind == OV_PANEL_HUD && host.def.hotkey == 0);
  CHECK(host.open == 1 && hud.opacity_pct == 90);
  CHECK(!tfp_cockpit_hud_drawn(&hud, d, sizeof d)); /* no frame yet */
  OvFrame frame = {2560, 1440, 0.016f, 1.0, 1.333f, 0};
  ncalls = 0;
  host.def.draw(&frame, host.def.user);
  CHECK(ncalls > 20 && ncalls < 120 && !bad_colour);
  CHECK(tfp_cockpit_hud_drawn(&hud, d, sizeof d) && strstr(d, "2560 x 1440, margins left 104 px"));
  printf("  drawn: %s\n", d);
  tfp_cockpit_hud_hide(&hud);
  CHECK(host.open == 0);
  CHECK(tfp_cockpit_hud_show(&hud, &api, "goose.true-first-person/cockpit-frame@1-2", 0.9f, why, sizeof why));
  CHECK(host.registered == 2 && host.open == 1);       /* registered once, opened again */
  CHECK(!tfp_cockpit_hud_drawn(&hud, d, sizeof d));     /* a new show waits for its own frame */
  host.def.draw(&frame, host.def.user);
  CHECK(tfp_cockpit_hud_drawn(&hud, d, sizeof d));
  tfp_cockpit_hud_opacity(&hud, 0.4f);
  CHECK(hud.opacity_pct == 40);
  tfp_cockpit_hud_retire(&hud);                         /* live replace: closed, a late frame closes it again and draws nothing */
  CHECK(host.open == 0);
  host.open = 1; ncalls = 0;
  host.def.draw(&frame, host.def.user);
  CHECK(host.open == 0 && ncalls == 0);
  CHECK(!tfp_cockpit_hud_show(&hud, &api, "x", 1, why, sizeof why) && strstr(why, "taken out"));
}

int main(void) {
  measured_boxes_inside_the_reserve();
  hud_kept_clear();
  colours_are_the_theme_faded();
  no_colour_literals();
  shown_while_seated_and_hidden_on_leave();
  hidden_on_death_option_off_and_see_through_off();
  why_not_said_once_a_seat();
  no_host_plain_view_one_line();
  not_drawn_said_after_three_seconds();
  opacity_follows_the_slider();
  panel_id_is_this_loads();
  hud_show_draw_hide();
  printf("cockpit frame: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
