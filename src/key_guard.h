/* Round 36b: the toggle key (a letter, U, since round 36) is not taken while text is being typed.
 *
 * F11 never reached a text field; U does: a "u" typed in the game's chat, in a Goose Overlay Host window (DBF-MCM, the
 * mod's own panel, the placement editor, any ImGui text field) would switch the view. A press of the toggle key is
 * refused, in this order, when:
 *   chat     the game's chat has the keyboard. The game's own hotkeys ask the same byte: the ship's Toggle HUD handler
 *            (game+0x12f3a08: mov rdx,[rip+X]; cmp byte [rdx+0x27e50],0; jne skip) and the handler at game+0xa96e42
 *            (mov rax,[rip+X]; cmp byte [rax+0x27e50],0; jne skip) both load the UI root (game+0x346d538) and skip their
 *            action while the chat flag (UI root + 0x27e50) is set. The War Journalist port (photo-mode
 *            src/input_context.h, from LLtM's H.dbf_game_chat) reads the same byte for the same purpose. Here the root's
 *            address and the flag's offset are read from both instructions (as the game runs them, checked first) and
 *            must agree, so a HellForge build that moved either is followed or refused, never guessed.
 *   overlay  a Goose Overlay Host window is open or a panel holds look capture: the host has the keyboard (its ImGui
 *            text fields are only in windows). The host's input_captured (API 8) or look_capture (API 3).
 *   panel    the mod's own settings panel or placement editor is open (also covered by the host's capture; kept for a
 *            host too old to say).
 *   typing   only when the chat flag cannot be read: another text key (a letter, a digit, space, punctuation) went
 *            down in the last TFP_KEY_GUARD_TYPING_MS before the press. A key that was already held (W while running)
 *            makes no edge and does not count; a game key (W, A, S, D, Space, a digit) that went down counts only
 *            inside a word (round 36c, tfp_key_guard_is_game).
 * One log line per burst of refusals: the first refusal says why; refusals follow without a line until a press is
 * taken or TFP_KEY_GUARD_BURST_MS pass without one.
 *
 * Header only, no Windows: the readers and the clock are the caller's (src/mod_view.c, through tfp_key_guard_toggle),
 * tests/key_guard_test.c. */
#ifndef TFP_KEY_GUARD_H
#define TFP_KEY_GUARD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TFP_KEY_GUARD_TYPING_MS 300u
#define TFP_KEY_GUARD_BURST_MS 2000u

/* ---- the game's chat flag ------------------------------------------------------------------------------------------ */
#define TFP_CHAT_SITE_HUD   UINT64_C(0x12f3a08)   /* game+: mov rdx,[rip+d]; cmp byte [rdx+o],0; jne +0x10; cmp byte [rdx+..] */
#define TFP_CHAT_SITE_GATE  UINT64_C(0xa96e42)    /* game+: mov rax,[rip+d]; cmp byte [rax+o],0; jne +0x41; mov rcx,rdi; call */
#define TFP_CHAT_GAME_ROOT  UINT64_C(0x346d538)   /* game+: the UI root's global, as build 25480438 has it */
#define TFP_CHAT_GAME_FLAG  UINT32_C(0x27e50)     /* UI root + : the chat flag (0 closed, 1 typing) */

typedef bool (*TfpGuardRead)(void *user, uint64_t at, void *out, size_t size);
typedef struct TfpChatLayout {
  bool resolved;
  uint64_t root;      /* the global holding the UI root (an absolute address) */
  uint32_t flag;      /* UI root + flag: the chat byte */
} TfpChatLayout;

static inline bool tfp_chat_bytes(const uint8_t *have, const char *want_hex) { /* "??" matches any byte */
  for (size_t i = 0; want_hex[2 * i]; i++) {
    if (want_hex[2 * i] == '?') continue;
    unsigned v = 0;
    if (sscanf(want_hex + 2 * i, "%2x", &v) != 1 || have[i] != (uint8_t)v) return false;
  }
  return true;
}
/* 1 matched (root and flag set), 0 other bytes, -1 unreadable (a cold page: ask again later). */
static inline int tfp_chat_site(TfpGuardRead code, void *user, uint64_t at, const char *pattern, uint64_t *root, uint32_t *flag) {
  uint8_t b[32];
  int32_t d;
  const size_t n = strlen(pattern) / 2;
  if (n < 13 || n > sizeof b) return 0;
  if (!code(user, at, b, n)) return -1;
  if (!tfp_chat_bytes(b, pattern)) return 0;
  memcpy(&d, b + 3, 4);
  memcpy(flag, b + 9, 4);
  *root = at + 7 + (uint64_t)(int64_t)d;
  return 1;
}
/* `code` reads code as the game runs it now (HellForge's rewrites included, its hooks not); game: game.dll's base and
 * image size. false: why says which check failed; the layout stays unresolved and tfp_chat_open answers -1. */
static inline bool tfp_chat_resolve(TfpGuardRead code, void *user, uint64_t game, uint64_t image_size, TfpChatLayout *out,
                                    char *why, size_t why_size) {
  uint64_t root_a = 0, root_b = 0;
  uint32_t flag_a = 0, flag_b = 0;
  memset(out, 0, sizeof *out);
  if (!game) { snprintf(why, why_size, "game.dll is not loaded"); return false; }
  const int a = tfp_chat_site(code, user, game + TFP_CHAT_SITE_HUD, "488b15????????80ba????????00751080ba", &root_a, &flag_a);
  if (a != 1) {
    snprintf(why, why_size, "the Toggle HUD handler at game+0x%llx %s", (unsigned long long)TFP_CHAT_SITE_HUD, a < 0 ? "is unreadable" : "differs");
    return false;
  }
  const int b = tfp_chat_site(code, user, game + TFP_CHAT_SITE_GATE, "488b05????????80b8????????007541488bcfe8", &root_b, &flag_b);
  if (b != 1) {
    snprintf(why, why_size, "the chat-gated handler at game+0x%llx %s", (unsigned long long)TFP_CHAT_SITE_GATE, b < 0 ? "is unreadable" : "differs");
    return false;
  }
  if (root_a != root_b || flag_a != flag_b) {
    snprintf(why, why_size, "the two handlers disagree (root game+0x%llx / game+0x%llx, flag +0x%x / +0x%x)",
             (unsigned long long)(root_a - game), (unsigned long long)(root_b - game), (unsigned)flag_a, (unsigned)flag_b);
    return false;
  }
  if (root_a < game || root_a + 8 > game + image_size || root_a % 8 || flag_a >= 0x1000000u) {
    snprintf(why, why_size, "root game+0x%llx or flag +0x%x is out of bounds", (unsigned long long)(root_a - game), (unsigned)flag_a);
    return false;
  }
  out->resolved = true, out->root = root_a, out->flag = flag_a;
  return true;
}
/* 1 typing in chat, 0 closed, -1 unknown (unresolved, no UI root yet, unreadable, a value that is not 0 or 1, or the
 * root replaced during the read). `live` reads data as it is now. */
static inline int tfp_chat_open(TfpGuardRead live, void *user, const TfpChatLayout *l) {
  uint64_t root = 0, again = 0;
  uint8_t flag = 0xff;
  if (!l || !l->resolved || !live(user, l->root, &root, 8)) return -1;
  if (root < 65536 || root >= UINT64_C(0x800000000000) || root % 8) return -1;
  if (!live(user, root + l->flag, &flag, 1) || flag > 1) return -1;
  if (!live(user, l->root, &again, 8) || again != root) return -1;
  return flag;
}

/* ---- the press ----------------------------------------------------------------------------------------------------- */
typedef enum TfpKeyVerdict { TFP_KEY_TAKEN, TFP_KEY_CHAT, TFP_KEY_OVERLAY, TFP_KEY_PANEL, TFP_KEY_TYPING } TfpKeyVerdict;
typedef struct TfpKeyContext {
  int chat;       /* tfp_chat_open: 1, 0, or -1 unknown */
  bool overlay;   /* the overlay host has the keyboard */
  bool panel;     /* the mod's own panel or editor is open */
} TfpKeyContext;
typedef struct TfpKeyGuard {
  uint8_t held[32];        /* the text keys' last levels (bit per virtual key) */
  bool seen;               /* held[] has a first sample: before it, keys already down are not edges */
  bool any_text;           /* a text key that is not a game key went down at last_text_ms */
  uint64_t last_text_ms;
  bool any_game;           /* round 36c: a game key (W, A, S, D, Space, a digit) went down at last_game_ms */
  uint64_t last_game_ms;
  bool in_burst;           /* a refusal was logged; quiet until a taken press or BURST_MS without a refusal */
  uint64_t last_refusal_ms;
  unsigned refused;        /* presses refused, in all */
} TfpKeyGuard;

/* The keys whose edges mean typing: letters, digits, space, the OEM punctuation keys, backspace. Not Enter (it sends
 * the line), not the modifiers or the function keys. */
static inline bool tfp_key_guard_is_text(int vk) {
  return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || vk == 0x20 || vk == 0x08 || (vk >= 0xBA && vk <= 0xC0) ||
         (vk >= 0xDB && vk <= 0xDF) || vk == 0xE2;
}
/* Round 36c: the text keys the game plays with: W, A, S, D (moving), Space (jump), the digits (weapon and stratagem
 * slots). Their edges alone are play, not typing: one counts only inside a word, when another text key went down
 * within TFP_KEY_GUARD_WORD_MS of the press ("what u": H and T; "was u" alone reads as moving, and is taken). */
#define TFP_KEY_GUARD_WORD_MS 1500u
static inline bool tfp_key_guard_is_game(int vk) {
  return vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D' || vk == 0x20 || (vk >= '0' && vk <= '9');
}
/* One sample of every text key, from one snapshot of the keyboard (keys[vk] & 0x80: down, as GetKeyboardState fills
 * it), with the toggle key's own press left out: its edge is the press being judged. */
static inline void tfp_key_guard_sample(TfpKeyGuard *g, uint64_t now_ms, int toggle_key, const uint8_t keys[256]) {
  for (int vk = 1; vk < 256; vk++) {
    if (!tfp_key_guard_is_text(vk)) continue;
    const bool d = (keys[vk] & 0x80) != 0, was = (g->held[vk >> 3] >> (vk & 7)) & 1;
    if (d) g->held[vk >> 3] |= (uint8_t)(1u << (vk & 7)); else g->held[vk >> 3] &= (uint8_t)~(1u << (vk & 7));
    if (!d || was || !g->seen || vk == toggle_key) continue;
    if (tfp_key_guard_is_game(vk)) g->any_game = true, g->last_game_ms = now_ms;
    else g->any_text = true, g->last_text_ms = now_ms;
  }
  g->seen = true;
}
/* Typing just before the press: another text key in the last TYPING_MS, or a game key in the last TYPING_MS inside a
 * word (another text key in the last WORD_MS). */
static inline bool tfp_key_guard_typing(const TfpKeyGuard *g, uint64_t now_ms) {
  const bool text = g->any_text && now_ms - g->last_text_ms <= TFP_KEY_GUARD_TYPING_MS;
  const bool game = g->any_game && now_ms - g->last_game_ms <= TFP_KEY_GUARD_TYPING_MS;
  return text || (game && g->any_text && now_ms - g->last_text_ms <= TFP_KEY_GUARD_WORD_MS);
}
/* A press of the toggle key: taken, or refused and why. `line` gets the one log line of a new burst ("" otherwise). */
static inline TfpKeyVerdict tfp_key_guard_press(TfpKeyGuard *g, uint64_t now_ms, const TfpKeyContext *c, char *line, size_t line_size) {
  TfpKeyVerdict v = TFP_KEY_TAKEN;
  if (c->chat == 1) v = TFP_KEY_CHAT;
  else if (c->overlay) v = TFP_KEY_OVERLAY;
  else if (c->panel) v = TFP_KEY_PANEL;
  else if (c->chat < 0 && tfp_key_guard_typing(g, now_ms)) v = TFP_KEY_TYPING;
  if (line_size) line[0] = 0;
  if (v == TFP_KEY_TAKEN) { g->in_burst = false; return v; }
  g->refused++;
  if (g->in_burst && now_ms - g->last_refusal_ms <= TFP_KEY_GUARD_BURST_MS) { g->last_refusal_ms = now_ms; return v; }
  g->in_burst = true, g->last_refusal_ms = now_ms;
  static const char *const why[] = {"", "typing in chat", "a Goose Overlay Host window has the keyboard",
                                    "the mod's settings panel or placement editor is open",
                                    "typing (another text key just before it; the game's chat state is not readable)"};
  snprintf(line, line_size, "toggle key ignored: %s", why[v]);
  return v;
}

/* ---- one tick of the toggle key (round 36c) ------------------------------------------------------------------------ */
/* The whole toggle-key path of src/mod_view.c's tick, so that the press it acts on can only come out of the guard:
 * the key's level and edge, the typing sample, and the verdict. The Windows side is the caller's:
 *   key_down   the toggle key's level (GetAsyncKeyState: one call a tick, as before)
 *   edge       the press edge from that level (the menu's TfpKeyEdge in the feel build); called every tick, so a key
 *              let go while the game was not in front is seen let go
 *   keyboard   one snapshot of all 256 keys (GetKeyboardState): one call a tick, and only while `sample` (the chat flag
 *              cannot be read); false leaves the sample out. It replaced a GetAsyncKeyState per text key (56 a tick):
 *              under Wine each of those can be a wineserver request, which a mod's reads had saturated before
 *   chat, overlay, panel, say   asked only on a press
 *   key_tapped (round 50, may be NULL)   the toggle key is up now but was pressed and let go since the previous tick
 *              (src/key_tap.h: once per press, never for a key that was held, only with the game in front at both
 *              ticks). The edge is told down, then up, in this one tick, so a tap is a press exactly when a level press
 *              would have been (a key just bound stays refused until it was seen up) and the next press has its edge;
 *              the press then goes through the same chat, overlay, panel and typing answers as any other
 * Returns true for a press to act on. */
typedef struct TfpKeyGuardIo {
  void *user;
  bool (*key_down)(void *user, int vk);
  bool (*edge)(void *user, int key, bool down);
  bool (*keyboard)(void *user, uint8_t keys[256]);
  int (*chat)(void *user);
  bool (*overlay)(void *user);
  bool (*panel)(void *user);
  void (*say)(void *user, const char *line);
  bool (*key_tapped)(void *user, int vk);
} TfpKeyGuardIo;
static inline bool tfp_key_guard_toggle(TfpKeyGuard *g, const TfpKeyGuardIo *io, uint64_t now_ms, int toggle_key, bool focused, bool sample) {
  const bool down = toggle_key && focused && io->key_down(io->user, toggle_key);
  const bool tapped = toggle_key && focused && !down && io->key_tapped && io->key_tapped(io->user, toggle_key);
  const bool pressed = io->edge(io->user, toggle_key, down || tapped);
  if (tapped) io->edge(io->user, toggle_key, false);
  if (toggle_key && focused && sample) {
    uint8_t keys[256];
    if (io->keyboard(io->user, keys)) tfp_key_guard_sample(g, now_ms, toggle_key, keys);
  }
  if (!pressed) return false;
  const TfpKeyContext c = {io->chat(io->user), io->overlay(io->user), io->panel(io->user)};
  char line[200];
  const bool taken = tfp_key_guard_press(g, now_ms, &c, line, sizeof line) == TFP_KEY_TAKEN;
  if (line[0]) io->say(io->user, line);
  return taken;
}
#endif
