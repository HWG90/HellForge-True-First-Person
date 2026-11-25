/* Round 36b (src/key_guard.h): the toggle key is refused while text is typed. The chat flag is resolved from the two
 * game handlers that test it, with the bytes of build 25480438's game.dll (game-pre1020.dll, a memory-layout dump:
 * game+0x12f3a08 and game+0xa96e42); a typing burst is refused, a clean press is taken, an open panel refuses. */
#include "key_guard.h"
#include <stdlib.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

#define GAME UINT64_C(0x7ff700000000)
#define IMAGE UINT64_C(0x4744000)
typedef struct Mem { uint64_t at; uint8_t bytes[64]; size_t n; } Mem;
static Mem mem[8];
static int mems;
static void put(uint64_t at, const char *hex) {
  Mem *m = &mem[mems++];
  m->at = at, m->n = strlen(hex) / 2;
  for (size_t i = 0; i < m->n; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); m->bytes[i] = (uint8_t)v; }
}
static void put_raw(uint64_t at, const void *p, size_t n) { Mem *m = &mem[mems++]; m->at = at, m->n = n; memcpy(m->bytes, p, n); }
static bool rd(void *u, uint64_t at, void *out, size_t size) {
  (void)u;
  for (size_t k = 0; k < size; k++) { /* byte by byte, the latest put wins: a later put patches an earlier one */
    int i = mems - 1;
    while (i >= 0 && !(at + k >= mem[i].at && at + k < mem[i].at + mem[i].n)) i--;
    if (i < 0) return false;
    ((uint8_t *)out)[k] = mem[i].bytes[at + k - mem[i].at];
  }
  return true;
}
static void game_code(void) { /* the two sites as build 25480438 runs them */
  put(GAME + 0x12f3a08, "488b15299b170280ba507e020000751080ba38e32400000f94c0888238e32400");
  put(GAME + 0xa96e42, "488b05ef669d0280b8507e0200007541488bcfe8c6fbffff84c0");
}
/* Round 36c: a site's rip-relative load pointed at `root` (the displacement at +3 counts from the next instruction, +7). */
static void disp(uint64_t site, uint64_t root) {
  const int32_t d = (int32_t)(int64_t)(root - (site + 7));
  put_raw(site + 3, &d, 4);
}
static bool keys[256];
static bool key_down(void *u, int vk) { (void)u; return keys[vk]; }

int main(void) {
  char why[200], line[200];
  TfpChatLayout l;

  /* The layout from the game's code: root game+0x346d538, flag +0x27e50, from both sites. */
  mems = 0; game_code();
  CHECK(tfp_chat_resolve(rd, NULL, GAME, IMAGE, &l, why, sizeof why));
  CHECK(l.resolved && l.root == GAME + TFP_CHAT_GAME_ROOT && l.flag == TFP_CHAT_GAME_FLAG);
  /* A moved flag in both handlers is followed. */
  mems = 0; game_code();
  put(GAME + 0x12f3a0f + 2, "607e0200"); put(GAME + 0xa96e49 + 2, "607e0200");
  CHECK(tfp_chat_resolve(rd, NULL, GAME, IMAGE, &l, why, sizeof why) && l.flag == 0x27e60);
  /* Disagreeing handlers, other code, and a root outside the image are refused. */
  mems = 0; game_code(); put(GAME + 0xa96e49 + 2, "607e0200");
  CHECK(!tfp_chat_resolve(rd, NULL, GAME, IMAGE, &l, why, sizeof why) && !l.resolved && strstr(why, "disagree"));
  mems = 0; game_code(); put(GAME + 0x12f3a16, "7410");
  CHECK(!tfp_chat_resolve(rd, NULL, GAME, IMAGE, &l, why, sizeof why) && strstr(why, "Toggle HUD"));
  mems = 0; put(GAME + 0x12f3a08, "488b15299b170280ba507e020000751080ba");   /* the second page still cold */
  CHECK(!tfp_chat_resolve(rd, NULL, GAME, IMAGE, &l, why, sizeof why) && strstr(why, "chat-gated") && strstr(why, "unreadable"));
  mems = 0; game_code(); put(GAME + 0xa96e4f, "74");
  CHECK(!tfp_chat_resolve(rd, NULL, GAME, IMAGE, &l, why, sizeof why) && strstr(why, "chat-gated handler at game+0xa96e42 differs"));
  mems = 0;
  CHECK(!tfp_chat_resolve(rd, NULL, GAME, IMAGE, &l, why, sizeof why) && strstr(why, "Toggle HUD handler at game+0x12f3a08 is unreadable"));
  mems = 0; game_code();
  CHECK(!tfp_chat_resolve(rd, NULL, GAME, 0x3000000, &l, why, sizeof why) && strstr(why, "out of bounds"));
  CHECK(!tfp_chat_resolve(rd, NULL, 0, IMAGE, &l, why, sizeof why));
  /* Round 36c: the handlers agree on the flag but load different roots (the HUD site's global moved by 8, still aligned
   * and inside the image): refused for the roots alone. */
  mems = 0; game_code(); disp(GAME + 0x12f3a08, GAME + TFP_CHAT_GAME_ROOT + 8);
  CHECK(!tfp_chat_resolve(rd, NULL, GAME, IMAGE, &l, why, sizeof why) && !l.resolved && strstr(why, "disagree") &&
        strstr(why, "root game+0x346d540 / game+0x346d538, flag +0x27e50 / +0x27e50"));
  /* Round 36c: both handlers agree, and the root or the flag is not where a UI root's global can be. */
  const struct { uint64_t root; uint32_t flag; bool ok; const char *what; } at[] = {
    {GAME + TFP_CHAT_GAME_ROOT + 4, TFP_CHAT_GAME_FLAG, false, "root game+0x346d53c or flag +0x27e50 is out of bounds"},  /* misaligned */
    {GAME - 8, TFP_CHAT_GAME_FLAG, false, "out of bounds"},                                     /* below the image */
    {GAME + IMAGE - 8, TFP_CHAT_GAME_FLAG, true, NULL},                                         /* the image's last 8 bytes */
    {GAME + IMAGE, TFP_CHAT_GAME_FLAG, false, "out of bounds"},                                 /* just past it */
    {GAME + TFP_CHAT_GAME_ROOT, 0xffffffu, true, NULL},                                         /* the largest flag offset */
    {GAME + TFP_CHAT_GAME_ROOT, 0x1000000u, false, "flag +0x1000000 is out of bounds"},
  };
  for (size_t i = 0; i < sizeof at / sizeof at[0]; i++) {
    char flag_hex[9];
    for (int k = 0; k < 4; k++) snprintf(flag_hex + 2 * k, 3, "%02x", (unsigned)(at[i].flag >> (8 * k)) & 0xff);
    mems = 0; game_code();
    disp(GAME + 0x12f3a08, at[i].root); disp(GAME + 0xa96e42, at[i].root);
    put(GAME + 0x12f3a08 + 9, flag_hex); put(GAME + 0xa96e42 + 9, flag_hex);
    const bool ok = tfp_chat_resolve(rd, NULL, GAME, IMAGE, &l, why, sizeof why);
    CHECK(ok == at[i].ok && l.resolved == at[i].ok);
    if (ok) CHECK(l.root == at[i].root && l.flag == at[i].flag);
    else CHECK(strstr(why, at[i].what) != NULL);
  }

  /* Reading the flag: closed, typing, unknown. */
  mems = 0; game_code();
  CHECK(tfp_chat_resolve(rd, NULL, GAME, IMAGE, &l, why, sizeof why));
  const uint64_t root = UINT64_C(0x2a0001000);
  uint8_t flag = 0;
  CHECK(tfp_chat_open(rd, NULL, &l) == -1);                          /* no root yet */
  put_raw(GAME + TFP_CHAT_GAME_ROOT, &root, 8);
  CHECK(tfp_chat_open(rd, NULL, &l) == -1);                          /* the root's page unreadable */
  put_raw(root + TFP_CHAT_GAME_FLAG, &flag, 1);
  CHECK(tfp_chat_open(rd, NULL, &l) == 0);
  flag = 1; put_raw(root + TFP_CHAT_GAME_FLAG, &flag, 1);
  CHECK(tfp_chat_open(rd, NULL, &l) == 1);
  flag = 7; put_raw(root + TFP_CHAT_GAME_FLAG, &flag, 1);
  CHECK(tfp_chat_open(rd, NULL, &l) == -1);                          /* not a bool: not the field */
  const uint64_t odd = root + 4;
  put_raw(GAME + TFP_CHAT_GAME_ROOT, &odd, 8);
  CHECK(tfp_chat_open(rd, NULL, &l) == -1);
  TfpChatLayout none = {0};
  CHECK(tfp_chat_open(rd, NULL, &none) == -1);

  /* The press. Chat open refuses; one line for the burst; a taken press ends it. */
  TfpKeyGuard g; memset(&g, 0, sizeof g);
  TfpKeyContext c = {1, false, false};
  CHECK(tfp_key_guard_press(&g, 1000, &c, line, sizeof line) == TFP_KEY_CHAT && !strcmp(line, "toggle key ignored: typing in chat"));
  CHECK(tfp_key_guard_press(&g, 1200, &c, line, sizeof line) == TFP_KEY_CHAT && !line[0]);   /* same burst: quiet */
  CHECK(tfp_key_guard_press(&g, 3100, &c, line, sizeof line) == TFP_KEY_CHAT && !line[0]);   /* 1.9 s after the last */
  CHECK(tfp_key_guard_press(&g, 5200, &c, line, sizeof line) == TFP_KEY_CHAT && line[0]);    /* 2.1 s: a new burst */
  c.chat = 0;
  CHECK(tfp_key_guard_press(&g, 5300, &c, line, sizeof line) == TFP_KEY_TAKEN && !line[0]);
  c.chat = 1;
  CHECK(tfp_key_guard_press(&g, 5400, &c, line, sizeof line) == TFP_KEY_CHAT && line[0]);    /* after a taken press */
  CHECK(g.refused == 5);

  /* With the chat flag known closed, keys typed just before do not matter (the flag says where they went). */
  memset(&g, 0, sizeof g);
  memset(keys, 0, sizeof keys);
  tfp_key_guard_sample(&g, 0, 'U', key_down, NULL);
  keys['Y'] = true; tfp_key_guard_sample(&g, 100, 'U', key_down, NULL);
  c = (TfpKeyContext){0, false, false};
  CHECK(tfp_key_guard_press(&g, 150, &c, line, sizeof line) == TFP_KEY_TAKEN);

  /* Chat unknown: "you" typed fast. Y and O go down within 300 ms of the U: refused, one line. */
  memset(&g, 0, sizeof g);
  memset(keys, 0, sizeof keys);
  c = (TfpKeyContext){-1, false, false};
  tfp_key_guard_sample(&g, 0, 'U', key_down, NULL);
  keys['Y'] = true; tfp_key_guard_sample(&g, 1000, 'U', key_down, NULL);
  keys['Y'] = false; keys['O'] = true; tfp_key_guard_sample(&g, 1150, 'U', key_down, NULL);
  keys['O'] = false; keys['U'] = true; tfp_key_guard_sample(&g, 1300, 'U', key_down, NULL);
  CHECK(tfp_key_guard_press(&g, 1300, &c, line, sizeof line) == TFP_KEY_TYPING && strstr(line, "toggle key ignored: typing"));
  /* " uh" later in the same line: space then U, refused without a second line. */
  keys['U'] = false; tfp_key_guard_sample(&g, 1500, 'U', key_down, NULL);
  keys[' '] = true; tfp_key_guard_sample(&g, 1700, 'U', key_down, NULL);
  keys[' '] = false; keys['U'] = true; tfp_key_guard_sample(&g, 1850, 'U', key_down, NULL);
  CHECK(tfp_key_guard_press(&g, 1850, &c, line, sizeof line) == TFP_KEY_TYPING && !line[0]);
  /* A clean press: nothing else for 400 ms. Taken. */
  keys['U'] = false; tfp_key_guard_sample(&g, 2000, 'U', key_down, NULL);
  tfp_key_guard_sample(&g, 2300, 'U', key_down, NULL);
  keys['U'] = true; tfp_key_guard_sample(&g, 2400, 'U', key_down, NULL);
  CHECK(tfp_key_guard_press(&g, 2400, &c, line, sizeof line) == TFP_KEY_TAKEN && !line[0]);
  /* Running (W held since before) and pressing U: W makes no edge; taken. The U's own earlier edges never count. */
  memset(&g, 0, sizeof g);
  memset(keys, 0, sizeof keys);
  keys['W'] = true; tfp_key_guard_sample(&g, 0, 'U', key_down, NULL);           /* held from before the first sample */
  keys['U'] = true; tfp_key_guard_sample(&g, 50, 'U', key_down, NULL);
  CHECK(tfp_key_guard_press(&g, 50, &c, line, sizeof line) == TFP_KEY_TAKEN);
  keys['U'] = false; tfp_key_guard_sample(&g, 100, 'U', key_down, NULL);
  keys['U'] = true; tfp_key_guard_sample(&g, 150, 'U', key_down, NULL);
  CHECK(tfp_key_guard_press(&g, 150, &c, line, sizeof line) == TFP_KEY_TAKEN);
  /* Modifiers, Enter and F-keys are not text: Shift and Enter just before U do not refuse it. */
  memset(&g, 0, sizeof g);
  memset(keys, 0, sizeof keys);
  tfp_key_guard_sample(&g, 0, 'U', key_down, NULL);
  keys[0x10] = keys[0x0D] = keys[0x7A] = true; tfp_key_guard_sample(&g, 100, 'U', key_down, NULL);
  CHECK(tfp_key_guard_press(&g, 150, &c, line, sizeof line) == TFP_KEY_TAKEN);
  CHECK(!tfp_key_guard_is_text(0x0D) && !tfp_key_guard_is_text(0x10) && tfp_key_guard_is_text('Q') && tfp_key_guard_is_text(0xBE));

  /* The mod's panel open, or an overlay window: refused with their own reasons, chat known closed. */
  memset(&g, 0, sizeof g);
  c = (TfpKeyContext){0, false, true};
  CHECK(tfp_key_guard_press(&g, 100, &c, line, sizeof line) == TFP_KEY_PANEL && strstr(line, "settings panel or placement editor"));
  memset(&g, 0, sizeof g);
  c = (TfpKeyContext){0, true, true};
  CHECK(tfp_key_guard_press(&g, 100, &c, line, sizeof line) == TFP_KEY_OVERLAY && strstr(line, "Overlay Host window"));
  c = (TfpKeyContext){1, true, true};
  memset(&g, 0, sizeof g);
  CHECK(tfp_key_guard_press(&g, 100, &c, line, sizeof line) == TFP_KEY_CHAT);   /* chat names itself first */

  printf("key_guard: %d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
