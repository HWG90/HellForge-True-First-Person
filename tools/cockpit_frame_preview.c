/* Round 45: the cockpit frame's draw calls, as src/cockpit_frame.h makes them, printed one a line for
 * tools/cockpit_frame_preview.py to paint offline. The theme's colours come on the command line (21 ABGR values, in
 * OvThemeColor order), so the source itself names no colour.
 *   cockpit_frame_preview WIDTH HEIGHT OPACITY THEME0 .. THEME20
 * Lines: fill|rect|line|circle|disc|tri|text|zone, numbers, the colour as ABGR hex, then the text for `text`. `zone`:
 * the game's HUD places the frame keeps clear (tfp_cockpit_hud_reserved), for the zones picture. */
#include "cockpit_frame.h"
#include <stdlib.h>
#define ROLES (OV_THEME_HUD_SHADOW + 1)
static uint32_t theme_of[ROLES];
static uint32_t p_theme(int role) { return role >= 0 && role < ROLES ? theme_of[role] : UINT32_MAX; }
static void p_line(float x0, float y0, float x1, float y1, uint32_t c, float t) { printf("line %.2f %.2f %.2f %.2f %.2f %08x\n", x0, y0, x1, y1, t, c); }
static void p_rect(float x0, float y0, float x1, float y1, uint32_t c, float r, float t) { printf("rect %.2f %.2f %.2f %.2f %.2f %.2f %08x\n", x0, y0, x1, y1, r, t, c); }
static void p_fill(float x0, float y0, float x1, float y1, uint32_t c, float r) { printf("fill %.2f %.2f %.2f %.2f %.2f %08x\n", x0, y0, x1, y1, r, c); }
static void p_circle(float x, float y, float r, uint32_t c, float t) { printf("circle %.2f %.2f %.2f %.2f %08x\n", x, y, r, t, c); }
static void p_disc(float x, float y, float r, uint32_t c) { printf("disc %.2f %.2f %.2f %08x\n", x, y, r, c); }
static void p_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) { printf("tri %.2f %.2f %.2f %.2f %.2f %.2f %08x\n", x0, y0, x1, y1, x2, y2, c); }
static void p_text_size(int font, float size, const char *text, float *w, float *h) { (void)font; *w = 0.62f * size * (float)strlen(text); *h = size * 1.2f; }
static void p_text(float x, float y, uint32_t c, int font, float size, const char *text) { (void)font; printf("text %.2f %.2f %.2f %08x %s\n", x, y, size, c, text); }
int main(int argc, char **argv) {
  if (argc != 4 + ROLES) { fprintf(stderr, "usage: %s WIDTH HEIGHT OPACITY THEME0..THEME%d\n", argv[0], ROLES - 1); return 2; }
  const float w = strtof(argv[1], NULL), h = strtof(argv[2], NULL), opacity = strtof(argv[3], NULL);
  for (int r = 0; r < ROLES; r++) theme_of[r] = (uint32_t)strtoul(argv[4 + r], NULL, 16);
  OvApi api;
  memset(&api, 0, sizeof api);
  api.version = 8, api.size = sizeof api, api.theme = p_theme;
  api.draw_line = p_line, api.draw_rect = p_rect, api.draw_rect_filled = p_fill, api.draw_circle = p_circle;
  api.draw_circle_filled = p_disc, api.draw_triangle_filled = p_tri, api.draw_text = p_text, api.text_size = p_text_size;
  TfpCockpitLayout l;
  tfp_cockpit_layout(w, h, &l);
  tfp_cockpit_draw(&api, &l, opacity);
  TfpCockpitBox zone[TFP_COCKPIT_HUD_COUNT];
  tfp_cockpit_hud_reserved(w, h, zone);
  for (int i = 0; i < TFP_COCKPIT_HUD_COUNT; i++) printf("zone %.2f %.2f %.2f %.2f %s\n", zone[i].x0, zone[i].y0, zone[i].x1, zone[i].y1, tfp_cockpit_hud_names[i]);
  const float inset = tfp_cockpit_hud_inset(w, h); /* wider than 16:9: the HUD kept in a centred 16:9 area as well */
  if (inset > 0) {
    tfp_cockpit_hud_reserved_at(w, h, inset, zone);
    for (int i = 0; i < TFP_COCKPIT_HUD_COUNT; i++) printf("zone %.2f %.2f %.2f %.2f %s (16:9)\n", zone[i].x0, zone[i].y0, zone[i].x1, zone[i].y1, tfp_cockpit_hud_names[i]);
  }
  char d[300];
  tfp_cockpit_describe(&l, d, sizeof d);
  printf("describe %s\n", d);
  return 0;
}
