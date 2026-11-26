/* Round 45: the "Cockpit frame" (option C of the exosuit cockpit diagnosis, 2026-11-25). With "See through the exosuit
 * cockpit" on, the suit's hull is hidden from the diver's own camera, so the seat looks out on open air. This draws a
 * STATIC cockpit frame over the screen while seated, around an open window in the middle: pillars, a dash and two roof
 * rails as warm graphite plates (DESIGN.md's tokens through ov->theme(), never a literal colour), bevels, bolts and seams
 * as lines and circles, a heading strip and a status lamp on the dash. Drawn procedurally with the Goose Overlay Host's
 * draw_* calls, no bitmap: the theme's colours go through the host's own HDR path like every other HUD element, so there
 * is no screenshot art whose brightness could disagree with the game.
 *
 * The game's HUD keeps its places (tfp_cockpit_hud_reserved): the stratagem list (top left, room for the opened list),
 * the compass and the mission banners (top centre), the objectives (top right), the weapon and ammo panel (bottom
 * left), the reticle and the suit's own gauges (the centre) and the key prompt (bottom right). Measured on the user's
 * 2000 x 1125 screenshot of an exosuit seat (round 37) with the game's default HUD scale; the game anchors those to the
 * screen's corners and edges and scales them with the screen's height, so every distance here is a fraction of the
 * height from the edge it hangs on, and a wide screen (21:9) only lengthens the dash and the rails.
 *
 * The heading strip is a static scale: the mod has no camera yaw it can read without the camera helper, which this
 * round does not touch. The lamp is decorative (the suit's state is not read).
 *
 * Cost: while seated, one HUD panel draw a frame (about 60 draw calls; the layout is computed again only when the back
 * buffer's size changes). Otherwise none: the panel is closed (the host does not call a closed panel) and the tick asks
 * for it on the seat's edges only (tfp_cockpit_frame_tick).
 *
 * Header only, no Windows: the seat state, the log and the host are the caller's (src/mod_view.c),
 * tests/cockpit_frame_test.c, tools/cockpit_frame_preview.c. */
#ifndef TFP_COCKPIT_FRAME_H
#define TFP_COCKPIT_FRAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "goose_overlay.h"

/* ---- the game's HUD, kept clear ---------------------------------------------------------------------------------- */
typedef struct TfpCockpitBox { float x0, y0, x1, y1; } TfpCockpitBox;
enum {
  TFP_COCKPIT_HUD_STRATAGEMS, /* top left: "STRATAGEMS" and the list it opens */
  TFP_COCKPIT_HUD_COMPASS,    /* top centre: the compass strip, its bearing, the mission banners under it */
  TFP_COCKPIT_HUD_OBJECTIVES, /* top right: the objective and its progress */
  TFP_COCKPIT_HUD_WEAPON,     /* bottom left: grenades, stims, magazines, the weapon, the ammo bar, the stratagem row */
  TFP_COCKPIT_HUD_RETICLE,    /* the centre: the reticle and the suit's own ammo gauges beside it */
  TFP_COCKPIT_HUD_PROMPT,     /* bottom right: the TAB key prompt */
  TFP_COCKPIT_HUD_COUNT
};
static const char *const tfp_cockpit_hud_names[TFP_COCKPIT_HUD_COUNT] = {"stratagem list", "compass", "objectives",
                                                                          "weapon and ammo", "reticle", "key prompt"};
/* Where the game draws its HUD on a w x h back buffer, with room to spare. On the 2000 x 1125 screenshot (h = 1125):
 * STRATAGEMS 40..210 x 30..68 (the opened list grows down: room to 0.52 h); compass and banner 715..1285 x 28..115;
 * objective 1530..1995 x 40..150 (more objectives grow down: room to 0.32 h); weapon panel 45..480 x 965..1105;
 * the reticle and the suit's gauges 840..1160 x 475..670; TAB 1895..1950 x 1060..1092. */
static inline void tfp_cockpit_hud_reserved(float w, float h, TfpCockpitBox out[TFP_COCKPIT_HUD_COUNT]) {
  const float cx = w * 0.5f, cy = h * 0.5f;
  out[TFP_COCKPIT_HUD_STRATAGEMS] = (TfpCockpitBox){0, 0, 0.40f * h, 0.52f * h};
  out[TFP_COCKPIT_HUD_COMPASS] = (TfpCockpitBox){cx - 0.30f * h, 0, cx + 0.30f * h, 0.13f * h};
  out[TFP_COCKPIT_HUD_OBJECTIVES] = (TfpCockpitBox){w - 0.48f * h, 0, w, 0.32f * h};
  out[TFP_COCKPIT_HUD_WEAPON] = (TfpCockpitBox){0, h - 0.20f * h, 0.48f * h, h};
  out[TFP_COCKPIT_HUD_RETICLE] = (TfpCockpitBox){cx - 0.20f * h, cy - 0.15f * h, cx + 0.20f * h, cy + 0.15f * h};
  out[TFP_COCKPIT_HUD_PROMPT] = (TfpCockpitBox){w - 0.12f * h, h - 0.09f * h, w, h};
}
static inline bool tfp_cockpit_box_overlap(TfpCockpitBox a, TfpCockpitBox b) {
  return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

/* ---- the layout ---------------------------------------------------------------------------------------------------- */
enum { TFP_COCKPIT_RECT, TFP_COCKPIT_TRI };
/* The plates' edges: which side faces the open window (gets the lip and the bright bevel). */
enum { TFP_COCKPIT_FACE_NONE, TFP_COCKPIT_FACE_RIGHT, TFP_COCKPIT_FACE_LEFT, TFP_COCKPIT_FACE_UP, TFP_COCKPIT_FACE_DOWN };
typedef struct TfpCockpitPlate {
  int kind, face;
  float p[6];         /* RECT: x0 y0 x1 y1; TRI: three points */
  TfpCockpitBox box;  /* its bounds */
} TfpCockpitPlate;
#define TFP_COCKPIT_PLATES 16
#define TFP_COCKPIT_BOLTS 20
typedef struct TfpCockpitLayout {
  float w, h, unit;                        /* the back buffer; unit: h / 1080 (DESIGN.md's sizes are 1080p pixels) */
  int plates; TfpCockpitPlate plate[TFP_COCKPIT_PLATES];
  int bolts; float bolt[TFP_COCKPIT_BOLTS][2]; float bolt_r;
  int seams; float seam_x[8];              /* vertical seams across the dash */
  TfpCockpitBox dash, heading;             /* the dash plate; the heading strip on it */
  float lamp[2], lamp_r;                   /* the status lamp */
  float left, right, bottom, top;          /* the margins: the pillars' widths, the dash's height, the rails' height (px) */
  TfpCockpitBox window;                    /* the open window: inside the pillars, under the rails, above the dash */
} TfpCockpitLayout;

static inline TfpCockpitBox tfp_cockpit_tri_box(const float *p) {
  TfpCockpitBox b = {p[0], p[1], p[0], p[1]};
  for (int i = 2; i < 6; i += 2) {
    if (p[i] < b.x0) b.x0 = p[i];
    if (p[i] > b.x1) b.x1 = p[i];
    if (p[i + 1] < b.y0) b.y0 = p[i + 1];
    if (p[i + 1] > b.y1) b.y1 = p[i + 1];
  }
  return b;
}
static inline void tfp_cockpit_rect(TfpCockpitLayout *l, float x0, float y0, float x1, float y1, int face) {
  if (l->plates >= TFP_COCKPIT_PLATES || x1 - x0 < 1 || y1 - y0 < 1) return;
  TfpCockpitPlate *p = &l->plate[l->plates++];
  *p = (TfpCockpitPlate){TFP_COCKPIT_RECT, face, {x0, y0, x1, y1, 0, 0}, {x0, y0, x1, y1}};
}
static inline void tfp_cockpit_tri(TfpCockpitLayout *l, float ax, float ay, float bx, float by, float cx, float cy) {
  if (l->plates >= TFP_COCKPIT_PLATES) return;
  TfpCockpitPlate *p = &l->plate[l->plates++];
  *p = (TfpCockpitPlate){TFP_COCKPIT_TRI, TFP_COCKPIT_FACE_NONE, {ax, ay, bx, by, cx, cy}, {0, 0, 0, 0}};
  p->box = tfp_cockpit_tri_box(p->p);
}
static inline void tfp_cockpit_bolt(TfpCockpitLayout *l, float x, float y) {
  if (l->bolts < TFP_COCKPIT_BOLTS) l->bolt[l->bolts][0] = x, l->bolt[l->bolts][1] = y, l->bolts++;
}
/* Every distance is a fraction of the height, from the edge it hangs on (see tfp_cockpit_hud_reserved). A plate
 * that would be too narrow to read on a narrow screen is left out; nothing is ever moved into the HUD's places. */
static inline void tfp_cockpit_layout(float w, float h, TfpCockpitLayout *l) {
  memset(l, 0, sizeof *l);
  l->w = w, l->h = h, l->unit = h / 1080.0f;
  const float cx = w * 0.5f, pillar = 0.072f * h, dash_top = h - 0.11f * h, rail = 0.05f * h, in = 0.018f * h;
  l->bolt_r = 0.0055f * h;
  /* the left pillar: below the stratagem list, above the weapon panel; a brace into the dash's corner */
  const float left_top = 0.55f * h, left_bottom = 0.78f * h;
  tfp_cockpit_rect(l, 0, left_top, pillar, left_bottom, TFP_COCKPIT_FACE_RIGHT);
  tfp_cockpit_tri(l, pillar, 0.63f * h, pillar, left_bottom, 0.20f * h, left_bottom);
  tfp_cockpit_bolt(l, pillar * 0.5f, left_top + in), tfp_cockpit_bolt(l, pillar * 0.5f, left_bottom - in);
  /* the right pillar: below the objectives, down to the dash's level; a gusset into the dash over the key prompt */
  const float right_top = 0.35f * h;
  tfp_cockpit_rect(l, w - pillar, right_top, w, dash_top, TFP_COCKPIT_FACE_LEFT);
  tfp_cockpit_tri(l, w - 0.14f * h, dash_top, w - pillar, dash_top, w - pillar, h - 0.20f * h);
  tfp_cockpit_bolt(l, w - pillar * 0.5f, right_top + in), tfp_cockpit_bolt(l, w - pillar * 0.5f, dash_top - in);
  tfp_cockpit_bolt(l, w - pillar * 0.5f, (right_top + dash_top) * 0.5f);
  /* the dash: right of the weapon panel, left of the key prompt, with a raised hood in the middle */
  const float dash_x0 = 0.50f * h, dash_x1 = w - 0.14f * h;
  tfp_cockpit_rect(l, dash_x0, dash_top, dash_x1, h, TFP_COCKPIT_FACE_UP);
  l->dash = (TfpCockpitBox){dash_x0, dash_top, dash_x1, h};
  tfp_cockpit_bolt(l, dash_x0 + in, dash_top + in), tfp_cockpit_bolt(l, dash_x1 - in, dash_top + in);
  tfp_cockpit_bolt(l, dash_x0 + in, h - in), tfp_cockpit_bolt(l, dash_x1 - in, h - in);
  const float ramp = 0.08f * h, hood_top = h - 0.15f * h;
  float hood_x0 = cx - 0.30f * h, hood_x1 = cx + 0.30f * h;
  if (hood_x0 - ramp < dash_x0) hood_x0 = dash_x0 + ramp;
  if (hood_x1 + ramp > dash_x1) hood_x1 = dash_x1 - ramp;
  if (hood_x1 - hood_x0 >= 0.20f * h) {
    tfp_cockpit_rect(l, hood_x0, hood_top, hood_x1, dash_top, TFP_COCKPIT_FACE_UP);
    tfp_cockpit_tri(l, hood_x0 - ramp, dash_top, hood_x0, dash_top, hood_x0, hood_top);
    tfp_cockpit_tri(l, hood_x1, dash_top, hood_x1 + ramp, dash_top, hood_x1, hood_top);
    tfp_cockpit_bolt(l, hood_x0 + in, hood_top + in * 0.8f), tfp_cockpit_bolt(l, hood_x1 - in, hood_top + in * 0.8f);
  }
  /* the dash's seams: every quarter of the height, clear of the gauges */
  for (float x = dash_x0 + 0.25f * h; x < dash_x1 - 0.05f * h && l->seams < 8; x += 0.25f * h)
    if (x < cx - 0.40f * h || x > cx + 0.40f * h) l->seam_x[l->seams++] = x;
  /* the roof rails: in the gaps between the stratagem list, the compass and the objectives */
  const float rail_l0 = 0.42f * h, rail_l1 = cx - 0.32f * h, rail_r0 = cx + 0.32f * h, rail_r1 = w - 0.50f * h;
  if (rail_l1 - rail_l0 >= 0.05f * h) {
    tfp_cockpit_rect(l, rail_l0, 0, rail_l1, rail, TFP_COCKPIT_FACE_DOWN);
    tfp_cockpit_bolt(l, rail_l0 + in, rail * 0.5f), tfp_cockpit_bolt(l, rail_l1 - in, rail * 0.5f);
  }
  if (rail_r1 - rail_r0 >= 0.05f * h) {
    tfp_cockpit_rect(l, rail_r0, 0, rail_r1, rail, TFP_COCKPIT_FACE_DOWN);
    tfp_cockpit_bolt(l, rail_r0 + in, rail * 0.5f), tfp_cockpit_bolt(l, rail_r1 - in, rail * 0.5f);
  }
  /* the gauges on the dash */
  const float strip = 0.26f * h;
  l->heading = (TfpCockpitBox){cx - strip, h - 0.092f * h, cx + strip, h - 0.052f * h};
  l->lamp[0] = cx + strip + 0.05f * h, l->lamp[1] = h - 0.072f * h, l->lamp_r = 0.011f * h;
  l->left = pillar, l->right = pillar, l->bottom = h - dash_top, l->top = rail;
  l->window = (TfpCockpitBox){pillar, rail, w - pillar, dash_top};
}
/* "2560 x 1440, margins left 104 px (y 792..1123), ..." for the log. */
static inline int tfp_cockpit_describe(const TfpCockpitLayout *l, char *out, size_t size) {
  return snprintf(out, size, "%.0f x %.0f, margins left %.0f px, right %.0f px, bottom %.0f px, top rails %.0f px; window %.0f x %.0f; "
                  "%d plates, the game's HUD places kept clear", l->w, l->h, l->left, l->right, l->bottom, l->top,
                  l->window.x1 - l->window.x0, l->window.y1 - l->window.y0, l->plates);
}

/* ---- the drawing ------------------------------------------------------------------------------------------------- */
/* A theme colour with its opacity scaled by k (0 .. 1). The host's colours are ABGR: alpha in the top byte. */
static inline uint32_t tfp_cockpit_fade(uint32_t c, float k) {
  if (k < 0) k = 0;
  if (k > 1) k = 1;
  const uint32_t a = (uint32_t)((float)(c >> 24) * k + 0.5f);
  return ((c << 8) >> 8) | (a << 24);
}
static inline uint32_t tfp_cockpit_colour(const OvApi *ov, int role, int fallback, float k) {
  uint32_t c = ov->theme(role);
  if (c == UINT32_MAX) c = ov->theme(fallback); /* a host before the HUD roles answers white for them */
  return tfp_cockpit_fade(c, k);
}
static inline void tfp_cockpit_text(const OvApi *ov, float x, float y, float size, float k, const char *text) {
  ov->draw_text(x + 1, y + 1, tfp_cockpit_colour(ov, OV_THEME_HUD_SHADOW, OV_THEME_PANEL_ALT, k), 0, size, text);
  ov->draw_text(x, y, tfp_cockpit_colour(ov, OV_THEME_TEXT_DIM, OV_THEME_TEXT, k), 0, size, text);
}
/* One frame of the cockpit. opacity 0 .. 1 scales every colour's own opacity. */
static inline void tfp_cockpit_draw(const OvApi *ov, const TfpCockpitLayout *l, float opacity) {
  const float u = l->unit > 0 ? l->unit : 1, line = u < 1 ? 1 : u, lip = 0.010f * l->h;
  const uint32_t plate = tfp_cockpit_colour(ov, OV_THEME_PANEL, OV_THEME_PANEL, opacity);
  const uint32_t inner = tfp_cockpit_colour(ov, OV_THEME_PANEL_ALT, OV_THEME_PANEL, opacity);
  const uint32_t edge = tfp_cockpit_colour(ov, OV_THEME_LINE, OV_THEME_LINE, opacity);
  const uint32_t bevel = tfp_cockpit_colour(ov, OV_THEME_LINE_STRONG, OV_THEME_LINE, opacity);
  const uint32_t field = tfp_cockpit_colour(ov, OV_THEME_ROW, OV_THEME_PANEL_ALT, opacity);
  for (int i = 0; i < l->plates; i++) {
    const TfpCockpitPlate *p = &l->plate[i];
    if (p->kind == TFP_COCKPIT_TRI) { ov->draw_triangle_filled(p->p[0], p->p[1], p->p[2], p->p[3], p->p[4], p->p[5], plate); continue; }
    const float x0 = p->p[0], y0 = p->p[1], x1 = p->p[2], y1 = p->p[3];
    ov->draw_rect_filled(x0, y0, x1, y1, plate, 0);
    ov->draw_rect(x0, y0, x1, y1, edge, 0, line);
    /* the lip on the window's side, darker, and the bevel's bright line on its edge */
    switch (p->face) {
    case TFP_COCKPIT_FACE_RIGHT: ov->draw_rect_filled(x1 - lip, y0, x1, y1, inner, 0); ov->draw_line(x1 - line, y0, x1 - line, y1, bevel, line); break;
    case TFP_COCKPIT_FACE_LEFT: ov->draw_rect_filled(x0, y0, x0 + lip, y1, inner, 0); ov->draw_line(x0 + line, y0, x0 + line, y1, bevel, line); break;
    case TFP_COCKPIT_FACE_UP: ov->draw_rect_filled(x0, y0, x1, y0 + lip, inner, 0); ov->draw_line(x0, y0 + line, x1, y0 + line, bevel, line); break;
    case TFP_COCKPIT_FACE_DOWN: ov->draw_rect_filled(x0, y1 - lip, x1, y1, inner, 0); ov->draw_line(x0, y1 - line, x1, y1 - line, bevel, line); break;
    default: break;
    }
  }
  for (int i = 0; i < l->seams; i++) ov->draw_line(l->seam_x[i], l->dash.y0 + lip, l->seam_x[i], l->dash.y1, edge, line);
  for (int i = 0; i < l->bolts; i++) {
    ov->draw_circle_filled(l->bolt[i][0], l->bolt[i][1], l->bolt_r, field);
    ov->draw_circle(l->bolt[i][0], l->bolt[i][1], l->bolt_r, bevel, line);
  }
  /* the heading strip: a field plate, ticks, the four points (a static scale), a yellow mark in the middle (yours) */
  const TfpCockpitBox s = l->heading;
  const float sw = s.x1 - s.x0, sh = s.y1 - s.y0, step = sw / 24, mid = (s.x0 + s.x1) * 0.5f;
  ov->draw_rect_filled(s.x0, s.y0, s.x1, s.y1, field, 6 * u);
  ov->draw_rect(s.x0, s.y0, s.x1, s.y1, edge, 6 * u, line);
  const uint32_t tick = tfp_cockpit_colour(ov, OV_THEME_TEXT_DIM, OV_THEME_TEXT, opacity * 0.8f);
  static const char *const points[4] = {"W", "N", "E", "S"};
  const float text = 14 * u;
  for (int i = 1; i < 24; i++) {
    const float x = s.x0 + step * (float)i;
    if (i % 6 == 3) {
      float tw = 0, th = 0;
      ov->text_size(0, text, points[i / 6], &tw, &th);
      tfp_cockpit_text(ov, x - tw * 0.5f, s.y0 + (sh - th) * 0.5f, text, opacity, points[i / 6]);
    } else {
      const float len = i % 2 ? sh * 0.22f : sh * 0.38f;
      ov->draw_line(x, s.y1 - 3 * u - len, x, s.y1 - 3 * u, tick, line);
    }
  }
  const uint32_t mark = tfp_cockpit_colour(ov, OV_THEME_ACCENT, OV_THEME_TEXT, opacity);
  ov->draw_triangle_filled(mid - 5 * u, s.y0 + line, mid + 5 * u, s.y0 + line, mid, s.y0 + 7 * u, mark);
  /* the status lamp: a halo, the lamp, a rim; "SYS" beside it */
  const uint32_t lamp = tfp_cockpit_colour(ov, OV_THEME_SUCCESS, OV_THEME_TEXT, opacity);
  ov->draw_circle_filled(l->lamp[0], l->lamp[1], l->lamp_r * 1.7f, tfp_cockpit_fade(lamp, 0.25f * opacity));
  ov->draw_circle_filled(l->lamp[0], l->lamp[1], l->lamp_r, lamp);
  ov->draw_circle(l->lamp[0], l->lamp[1], l->lamp_r, bevel, line);
  float tw = 0, th = 0;
  ov->text_size(0, text, "SYS", &tw, &th);
  tfp_cockpit_text(ov, l->lamp[0] + l->lamp_r * 2.4f, l->lamp[1] - th * 0.5f, text, opacity, "SYS");
}

/* ---- when: the seat's edges (the game thread) -------------------------------------------------------------------- */
#define TFP_COCKPIT_FRAME_WAIT_MS 3000u  /* a suit not found by then, or no frame drawn by then, is said once */
typedef struct TfpCockpitFrameIn {
  bool seated;       /* first person is shown from an exosuit's seat (not gliding in or out) */
  bool see_through;  /* the option "See through the exosuit cockpit" */
  bool option;       /* the option "Cockpit frame" */
  bool suit;         /* the suit the diver sits in is found and seen through */
  float opacity;     /* the option "Cockpit frame opacity", 0 .. 1 */
} TfpCockpitFrameIn;
typedef struct TfpCockpitFrameIo {
  void *user;
  void (*say)(void *user, const char *line);
  bool (*show)(void *user, float opacity, char *why, size_t why_size);   /* false: not shown, and why */
  void (*hide)(void *user);
  void (*opacity)(void *user, float opacity);
  bool (*drawn)(void *user, char *description, size_t size);           /* the host drew a frame of it since the last show */
} TfpCockpitFrameIo;
typedef struct TfpCockpitFrame {
  bool seat;            /* in a seat since seat_ms */
  uint64_t seat_ms;
  bool shown;           /* asked for since shown_ms */
  uint64_t shown_ms;
  bool said_drawn, said_why, refused;  /* this seat's: the drawn line, the why-not line, the host refused it */
  float opacity;
  unsigned entries, shows;
} TfpCockpitFrame;

static inline void tfp_cockpit_frame_say(const TfpCockpitFrameIo *io, const char *format, const char *what) {
  char line[400];
  snprintf(line, sizeof line, format, what);
  io->say(io->user, line);
}
/* One tick. Returns true while the frame is asked for. Nothing is asked of the host but on a change: entering or
 * leaving the seat, an option switched, the opacity moved; and the drawn check until the first frame is told. */
static inline bool tfp_cockpit_frame_tick(TfpCockpitFrame *f, const TfpCockpitFrameIo *io, uint64_t now_ms, const TfpCockpitFrameIn *in) {
  if (!in->seated) {
    if (f->shown) io->hide(io->user), f->shown = false;
    f->seat = false;
    return false;
  }
  if (!f->seat) {
    f->seat = true, f->seat_ms = now_ms, f->entries++;
    f->said_drawn = f->said_why = f->refused = false;
  }
  if (!in->see_through || !in->option || !in->suit) {
    if (f->shown) io->hide(io->user), f->shown = false;
    const char *why = !in->see_through ? "See through the exosuit cockpit is off (the cockpit is drawn)"
                    : !in->option      ? "the option is off"
                    : now_ms - f->seat_ms >= TFP_COCKPIT_FRAME_WAIT_MS ? "no suit is seen through (the suit the diver sits in was not found)" : NULL;
    if (why && !f->said_why) f->said_why = true, tfp_cockpit_frame_say(io, "Seat: no cockpit frame: %s", why);
    return false;
  }
  if (!f->shown) {
    if (f->refused) return false;
    char why[160] = "";
    if (!io->show(io->user, in->opacity, why, sizeof why)) {
      f->refused = true;
      if (!f->said_why) f->said_why = true, tfp_cockpit_frame_say(io, "Seat: no cockpit frame: %s; the plain clear view", why[0] ? why : "the overlay host did not take it");
      return false;
    }
    f->shown = true, f->shown_ms = now_ms, f->opacity = in->opacity, f->shows++; /* the drawn line: once a seat */
  } else if (in->opacity != f->opacity) {
    io->opacity(io->user, in->opacity), f->opacity = in->opacity;
  }
  if (!f->said_drawn) {
    char description[300] = "";
    if (io->drawn(io->user, description, sizeof description)) {
      f->said_drawn = true;
      tfp_cockpit_frame_say(io, "Seat: cockpit frame drawn (%s)", description);
    } else if (now_ms - f->shown_ms >= TFP_COCKPIT_FRAME_WAIT_MS) {
      f->said_drawn = true;
      if (!f->said_why)
        f->said_why = true, tfp_cockpit_frame_say(io, "Seat: cockpit frame not drawn: %s", "the Goose Overlay Host drew no frame of it in 3 s (its drawing is off, or the game is not presenting)");
    }
  }
  return true;
}

/* ---- on screen: a HUD panel of the Goose Overlay Host -------------------------------------------------------------- */
/* show (the game thread) bumps the generation; the draw (the host's frame) describes the layout of the first frame
 * after it and publishes that generation as told. The opacity is a whole percentage, written by the game thread and
 * read by each frame. retired: this build was taken out (live replace); the panel stays registered with the host (it
 * cannot take a panel out), closed, and its draw does nothing. */
typedef struct TfpCockpitHud {
  const OvApi *ov;
  OvPanel *panel;
  unsigned gen, told, opacity_pct;  /* atomic */
  int retired;                      /* atomic */
  char description[300];            /* written by the draw before it publishes told */
  TfpCockpitLayout layout;          /* the draw's own */
  unsigned frames;                  /* frames drawn, in all (the draw's) */
} TfpCockpitHud;
/* The panel's id, this load's own: the host refuses a second panel with an id it holds, and a replaced build of this
 * mod leaves its panel behind (round 41's check: a fixed id breaks after a live replace). */
static inline void tfp_cockpit_panel_id(char *out, size_t size, unsigned long process, unsigned long long tick) {
  snprintf(out, size, "goose.true-first-person/cockpit-frame@%lx-%llx", process, tick);
}
static inline bool tfp_cockpit_host_usable(const OvApi *ov) {
  return ov && ov->version >= 2 && ov->size >= offsetof(OvApi, theme) + sizeof ov->theme && ov->register_panel && ov->set_open &&
         ov->draw_line && ov->draw_rect && ov->draw_rect_filled && ov->draw_circle && ov->draw_circle_filled &&
         ov->draw_triangle_filled && ov->draw_text && ov->text_size && ov->theme;
}
static inline void tfp_cockpit_hud_draw(const OvFrame *frame, void *user) {
  TfpCockpitHud *hud = user;
  const OvApi *ov = hud->ov;
  if (__atomic_load_n(&hud->retired, __ATOMIC_ACQUIRE)) { ov->set_open(hud->panel, 0); return; }
  if (frame->width != hud->layout.w || frame->height != hud->layout.h) tfp_cockpit_layout(frame->width, frame->height, &hud->layout);
  const unsigned gen = __atomic_load_n(&hud->gen, __ATOMIC_ACQUIRE);
  if (gen != __atomic_load_n(&hud->told, __ATOMIC_RELAXED)) {
    tfp_cockpit_describe(&hud->layout, hud->description, sizeof hud->description);
    __atomic_store_n(&hud->told, gen, __ATOMIC_RELEASE);
  }
  hud->frames++;
  tfp_cockpit_draw(ov, &hud->layout, (float)__atomic_load_n(&hud->opacity_pct, __ATOMIC_RELAXED) / 100.0f);
}
static inline unsigned tfp_cockpit_pct(float opacity) {
  return opacity <= 0 ? 0u : opacity >= 1 ? 100u : (unsigned)(opacity * 100.0f + 0.5f);
}
/* Opens the frame (registering the HUD panel `id` on the first show). false: not shown, and why. */
static inline bool tfp_cockpit_hud_show(TfpCockpitHud *hud, const OvApi *ov, const char *id, float opacity, char *why, size_t why_size) {
  if (!ov) { snprintf(why, why_size, "the Goose Overlay Host is not loaded"); return false; }
  if (!tfp_cockpit_host_usable(ov)) {
    snprintf(why, why_size, "the Goose Overlay Host (API %u) lacks a HUD drawing call", (unsigned)ov->version);
    return false;
  }
  if (__atomic_load_n(&hud->retired, __ATOMIC_ACQUIRE)) { snprintf(why, why_size, "this build was taken out (live replace)"); return false; }
  if (hud->panel && hud->ov != ov) hud->panel = NULL; /* a reloaded host: its panels are gone */
  hud->ov = ov;
  __atomic_store_n(&hud->opacity_pct, tfp_cockpit_pct(opacity), __ATOMIC_RELAXED);
  __atomic_add_fetch(&hud->gen, 1u, __ATOMIC_RELEASE);
  if (!hud->panel) {
    const OvPanelDef def = {sizeof def, id, "True First Person: cockpit frame", OV_PANEL_HUD, 0, 0, 0, 0, tfp_cockpit_hud_draw, hud};
    hud->panel = ov->register_panel(&def); /* a HUD panel is open from its registration */
    if (!hud->panel) { snprintf(why, why_size, "the Goose Overlay Host refused the frame's HUD panel"); return false; }
  }
  ov->set_open(hud->panel, 1);
  return true;
}
static inline void tfp_cockpit_hud_hide(TfpCockpitHud *hud) { if (hud->panel) hud->ov->set_open(hud->panel, 0); }
static inline void tfp_cockpit_hud_opacity(TfpCockpitHud *hud, float opacity) {
  __atomic_store_n(&hud->opacity_pct, tfp_cockpit_pct(opacity), __ATOMIC_RELAXED);
}
static inline bool tfp_cockpit_hud_drawn(TfpCockpitHud *hud, char *description, size_t size) {
  const unsigned gen = __atomic_load_n(&hud->gen, __ATOMIC_ACQUIRE);
  if (!gen || __atomic_load_n(&hud->told, __ATOMIC_ACQUIRE) != gen) return false;
  snprintf(description, size, "%s", hud->description);
  return true;
}
/* Live replace: closed for good; a frame already inside the draw finishes, the next one closes it again. */
static inline void tfp_cockpit_hud_retire(TfpCockpitHud *hud) {
  __atomic_store_n(&hud->retired, 1, __ATOMIC_RELEASE);
  if (hud->panel) hud->ov->set_open(hud->panel, 0);
}
#endif
