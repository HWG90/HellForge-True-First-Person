/* Round 41: F11, the toggle key until round 36, now opens HellForge 0.5's Armor LUT editor, and players press the key
 * they know (a tester: feel.56 "still doesn't work", pressing F11). When the toggle key is any other key and the player
 * presses F11 in the game, one short notice names the configured key:
 *   True First Person: press U to switch first person (F11 is HellForge's Armor LUT editor now)
 * drawn by the Goose Overlay Host as a HUD plate for TFP_F11_HINT_SHOW_S seconds (the host has no toast call; a HUD
 * panel is the host's way to draw outside a window, and DESIGN.md's "HUD elements" the look: `text` over a 1 px
 * `hud_shadow`, on a `hud_plate` with 6 px corners, every colour from ov->theme()). Once shown, F11 is not even read for
 * TFP_F11_HINT_REPEAT_MS; a press after that shows it again. One log line in the session:
 *   Keys: F11 pressed; True First Person is on U (F11 opens HellForge's Armor LUT editor)
 * which also says when no notice could be drawn (no host, a refused panel): then that line is all there is.
 *
 * Nothing while the game's chat has the keyboard (key_guard's chat flag), a Goose Overlay Host window or look capture
 * has it, or the mod's own panel or placement editor is open; a refused press uses nothing up. Nothing when the toggle
 * key is F11 (the menu refuses it: tfp_wire_menu_toggle_key never returns it) or unbound (no key to name), outside the
 * game, or while the game is not in front. F11 is only read (GetAsyncKeyState), never taken or blocked: HellForge's
 * editor opens as before.
 *
 * Cost: one level read of F11 a tick, beside the toggle key's own, only while the watch is on (in the game, in front,
 * before the first notice or after the quiet time); acted on only on its press edge. The SDK has no key event.
 *
 * Header only, no Windows: the readers, the log and the HUD's host are the caller's (src/mod_view.c), tests/f11_hint_test.c. */
#ifndef TFP_F11_HINT_H
#define TFP_F11_HINT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "goose_overlay.h"

#define TFP_F11_VK 0x7A
#define TFP_F11_HINT_REPEAT_MS 600000u   /* ten minutes */
#define TFP_F11_HINT_SHOW_S 7.0

typedef struct TfpF11Hint {
  bool seen;            /* F11 has a first sample since the watch began: before it, a key already down is no edge */
  bool held;            /* F11's last level */
  bool shown;           /* a notice was shown at shown_ms */
  uint64_t shown_ms;
  bool logged;          /* the session's one log line is out */
  unsigned presses;     /* notices shown, in all */
} TfpF11Hint;

typedef struct TfpF11HintIo {
  void *user;
  bool (*key_down)(void *user, int vk);
  int (*chat)(void *user);       /* tfp_chat_open: 1 typing, 0 closed, -1 unknown */
  bool (*overlay)(void *user);   /* a host window or look capture has the keyboard */
  bool (*panel)(void *user);     /* the mod's own panel or placement editor is open */
  void (*say)(void *user, const char *line);
  bool (*show)(void *user, const char *notice, char *why, size_t why_size); /* false: not drawn, and why */
} TfpF11HintIo;

/* One tick. key_name: the toggle key's name as the menu shows it (tfp_wire_menu_key_name). Returns true when a notice
 * was asked for this tick. */
static inline bool tfp_f11_hint_tick(TfpF11Hint *h, const TfpF11HintIo *io, uint64_t now_ms, int toggle_key, const char *key_name,
                                     bool in_game, bool focused) {
  const bool quiet = h->shown && now_ms - h->shown_ms < TFP_F11_HINT_REPEAT_MS;
  if (!in_game || !focused || !toggle_key || toggle_key == TFP_F11_VK || quiet || !key_name || !key_name[0]) {
    h->seen = false; /* resuming, a key held now is not a press */
    return false;
  }
  const bool down = io->key_down(io->user, TFP_F11_VK);
  const bool pressed = h->seen && down && !h->held;
  h->held = down, h->seen = true;
  if (!pressed) return false;
  if (io->chat(io->user) == 1 || io->overlay(io->user) || io->panel(io->user)) return false; /* F11 is no text key: -1 shows */
  char notice[200], why[120] = "", line[360];
  snprintf(notice, sizeof notice, "True First Person: press %s to switch first person (F11 is HellForge's Armor LUT editor now)", key_name);
  const bool drawn = io->show(io->user, notice, why, sizeof why);
  h->shown = true, h->shown_ms = now_ms, h->presses++;
  if (!h->logged) {
    h->logged = true;
    snprintf(line, sizeof line, "Keys: F11 pressed; True First Person is on %s (F11 opens HellForge's Armor LUT editor)%s%s", key_name,
             drawn ? "" : "; no on-screen notice: ", drawn ? "" : (why[0] ? why : "the overlay host did not take it"));
    io->say(io->user, line);
  }
  return true;
}

/* ---- the notice on screen: a HUD panel of the Goose Overlay Host ---------------------------------------------------- */
/* show (the game thread) writes the text, then the generation; the draw (the host's frame) copies the text when the
 * generation moved and starts its clock at that frame. Shows are minutes apart, so the copy never meets a write. */
typedef struct TfpF11HintHud {
  const OvApi *ov;
  OvPanel *panel;
  char text[200];
  unsigned gen;          /* bumped by each show (atomic) */
  unsigned drawn_gen;    /* the draw's: the generation it copied */
  char drawing[200];
  double start;          /* frame time of the notice's first frame */
} TfpF11HintHud;

static inline uint32_t tfp_f11_hint_colour(const OvApi *ov, int role, int fallback) {
  const uint32_t c = ov->theme(role);
  return c == 0xFFFFFFFFu ? ov->theme(fallback) : c; /* a host before the HUD roles answers white for them */
}
static inline void tfp_f11_hint_hud_draw(const OvFrame *frame, void *user) {
  TfpF11HintHud *hud = user;
  const OvApi *ov = hud->ov;
  const unsigned gen = __atomic_load_n(&hud->gen, __ATOMIC_ACQUIRE);
  if (gen != hud->drawn_gen) {
    hud->drawn_gen = gen, hud->start = frame->time;
    snprintf(hud->drawing, sizeof hud->drawing, "%s", hud->text);
  }
  if (frame->time - hud->start > TFP_F11_HINT_SHOW_S) { ov->set_open(hud->panel, 0); return; }
  const float s = frame->scale > 0 ? frame->scale : 1.0f, pad_x = 14 * s, pad_y = 8 * s;
  float w = 0, h = 0;
  ov->text_size(0, 0, hud->drawing, &w, &h);
  const float x = (frame->width - w) * 0.5f, y = frame->height * 0.16f;
  ov->draw_rect_filled(x - pad_x, y - pad_y, x + w + pad_x, y + h + pad_y, tfp_f11_hint_colour(ov, OV_THEME_HUD_PLATE, OV_THEME_PANEL), 6 * s);
  ov->draw_text(x + 1, y + 1, tfp_f11_hint_colour(ov, OV_THEME_HUD_SHADOW, OV_THEME_PANEL), 0, 0, hud->drawing);
  ov->draw_text(x, y, ov->theme(OV_THEME_TEXT), 0, 0, hud->drawing);
}
/* Shows `notice` (registering the HUD panel `id` on the first show). false: not shown, and why. */
static inline bool tfp_f11_hint_hud_show(TfpF11HintHud *hud, const OvApi *ov, const char *id, const char *notice, char *why, size_t why_size) {
  if (!ov) { snprintf(why, why_size, "the Goose Overlay Host is not loaded"); return false; }
  if (!ov->register_panel || !ov->set_open || !ov->draw_text || !ov->draw_rect_filled || !ov->text_size || !ov->theme) {
    snprintf(why, why_size, "the Goose Overlay Host (API %u) lacks a HUD drawing call", (unsigned)ov->version);
    return false;
  }
  if (hud->panel && hud->ov != ov) hud->panel = NULL; /* a reloaded host: its panels are gone */
  hud->ov = ov;
  snprintf(hud->text, sizeof hud->text, "%s", notice);
  __atomic_add_fetch(&hud->gen, 1u, __ATOMIC_RELEASE);
  if (!hud->panel) {
    const OvPanelDef def = {sizeof def, id, "True First Person: key notice", OV_PANEL_HUD, 0, 0, 0, 0, tfp_f11_hint_hud_draw, hud};
    hud->panel = ov->register_panel(&def); /* a HUD panel is open from its registration */
    if (!hud->panel) { snprintf(why, why_size, "the Goose Overlay Host refused the notice's HUD panel"); return false; }
  }
  ov->set_open(hud->panel, 1);
  return true;
}
#endif
