/* A settings.json read by the settings loader of another commit of this repository: tests/run-wire-settings.sh
 * compiles this file against that commit's src/tfp_settings.c (0.6.0-feel.19, e9b3c10: the published build whose
 * loader refuses the whole document on a top-level member it does not know).
 *   old_loader_check <settings.json>    exit 0: loaded; 1: refused, with the loader's reason on stdout */
#include <stdio.h>
#include <string.h>
#include "tfp_settings.h"

int main(int argc, char **argv) {
  static char text[TFP_SETTINGS_MAX + 1];
  static TfpSettings s;
  char why[200] = "";
  FILE *file = argc == 2 ? fopen(argv[1], "rb") : NULL;
  if (!file) { printf("old loader: no file\n"); return 2; }
  size_t n = fread(text, 1, sizeof text, file);
  fclose(file);
  bool loaded = tfp_settings_load(&s, text, n, why, sizeof why);
  const TfpSettingsValue *profiles = s.field[TFP_SET_action_placements];
  if (loaded) printf("old loader: loaded, %zu placement profiles\n", profiles ? profiles->count : 0);
  else printf("old loader: refused: %s\n", why);
  tfp_settings_free(&s);
  return loaded ? 0 : 1;
}
