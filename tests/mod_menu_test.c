/* The Windows glue to DBF-MCM (src/mod_menu.c) on Linux, with a stand-in goose_mcm.dll (tests/fake_windows/windows.h):
 * what True First Person calls in MCM while MCM holds the menu, tick after tick.
 *
 * feel.51: "Menu options rapidly scrolling down". MCM moves its list only by a page change (its ui.c Select: focus -1),
 * focus_page (F10 / the API), a Down/Up key (focus and RevealFocus) or the wheel; a re-registration is an unregister
 * and a register, which takes the mod's page away (ui.c Draw: Select "" when the mod is gone). So while MCM holds the
 * menu and nothing changes, the once-a-second re-ask (src/wire_panel.c) and the per-tick menu frame (src/wire_menu.c
 * tfp_wire_menu_frame) must call none of register_mod, unregister, focus_page, open, close, set, set_label and
 * set_disabled; F10 alone opens or closes the window, once per press. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wire.h"
#include "wire_off.h"
#include "wire_settings.h"
#include "wire_menu.h"
#include "wire_panel.h"
#include "mod_menu.h"
#include "goose_mcm.h"
#include "windows.h"

static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { printf("mod menu: FAILED %s (line %d)\n", #condition, __LINE__); return 1; } } while (0)

/* ---- a host with no game behind it (as tests/wire_panel_test.c) ---- */
static double now_seconds = 100;
static uint64_t no_module(void *u, TfpOpsModule which) { (void)u; (void)which; return 0; }
static bool no_read(void *u, uint64_t at, void *out, size_t size) { (void)u; (void)at; (void)out; (void)size; return false; }
static bool no_write(void *u, uint64_t at, const void *bytes, size_t size) { (void)u; (void)at; (void)bytes; (void)size; return false; }
static void no_query(void *u, uint64_t at, TfpOpsRegion *out) { (void)u; (void)at; memset(out, 0, sizeof *out); }
static bool no_call(void *u, TfpOpsNative which, uint64_t address, const uint64_t args[4], uint64_t *result, TfpOpsText *raised) {
  (void)u; (void)which; (void)address; (void)args; (void)result;
  raised->present = true;
  snprintf(raised->text, sizeof raised->text, "no game");
  return false;
}
static double test_clock(void *u) { (void)u; return now_seconds; }
static int test_mouse(void *u, int button) { (void)u; (void)button; return 0; }
static void capture(void *u, const char *text) { (void)u; (void)text; }
static uint32_t h_validate_c(const TfpCameraConfig *c) { (void)c; return 0; }
static uint32_t h_begin_c(const TfpCameraConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_end(uint64_t g) { (void)g; return 1; }
static uint32_t h_stats_c(uint64_t g, TfpCameraStats *s) { (void)g; (void)s; return 0; }
static uint32_t h_error(void) { return 0; }
static uint32_t h_generation(uint64_t g) { (void)g; return 0; }
static uint32_t h_handoff(uint64_t g, float a, float b) { (void)g; (void)a; (void)b; return 0; }
static uint32_t h_u32(uint64_t g, uint32_t v) { (void)g; (void)v; return 0; }
static uint32_t h_validate_f(const TfpFovConfig *c) { (void)c; return 0; }
static uint32_t h_begin_f(const TfpFovConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_bind_f(uint64_t g, uint64_t r, uint32_t w) { (void)g; (void)r; (void)w; return 0; }
static uint32_t h_transition_f(uint64_t g, uint64_t e, float p, uint32_t a) { (void)g; (void)e; (void)p; (void)a; return 0; }
static uint32_t h_stats_f(uint64_t g, TfpFovStats *s) { (void)g; (void)s; return 0; }
static uint32_t h_begin_n(const TfpNearConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_set_n(uint64_t g, float a, float b) { (void)g; (void)a; (void)b; return 0; }
static uint32_t h_stats_n(uint64_t g, TfpNearStats *s) { (void)g; (void)s; return 0; }
static uint32_t h_validate_b(const TfpBlurConfig *c) { (void)c; return 0; }
static uint32_t h_begin_b(const TfpBlurConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_stats_b(uint64_t g, TfpBlurStats *s) { (void)g; (void)s; return 0; }
static TfpWire wire;
static void new_wire(void) {
  static const TfpCameraHelperApi camera = {h_validate_c, h_begin_c, h_end, h_stats_c, h_error, h_generation, h_handoff, h_u32, h_u32};
  static const TfpFovHelperApi fov = {h_validate_f, h_begin_f, h_bind_f, h_transition_f, h_end, h_stats_f, h_error};
  static const TfpNearHelperApi near_clip = {h_begin_n, h_set_n, h_end, h_stats_n, h_error};
  static const TfpBlurHelperApi blur = {h_validate_b, h_begin_b, h_end, h_stats_b, h_error};
  TfpOpsHost memory;
  memset(&memory, 0, sizeof memory);
  memory.module = no_module, memory.read = no_read, memory.write = no_write, memory.query = no_query, memory.call = no_call;
  TfpWirePlatform platform = {NULL, test_clock, test_mouse, capture};
  TfpWireHelpers helpers = {&camera, &fov, &near_clip, &blur};
  tfp_wire_init(&wire, &memory, &platform, &helpers);
}

/* ---- the stand-in goose_mcm.dll: every call counted ---- */
static struct {
  unsigned connects, registers, unregisters, sets, labels, disables, focus_pages, opens, closes;
  int registered, open;
  char page[81];
} mcm;
static int token;
static McmMod *f_register(const char *json, size_t length, const McmCallbacks *callbacks) {
  (void)callbacks;
  mcm.registers++;
  if (mcm.registered || !json || !length) return NULL;   /* as MCM's registry.c: "already registered" */
  mcm.registered = 1;
  return (McmMod *)&token;
}
static int f_unregister(McmMod *m) { (void)m; mcm.unregisters++, mcm.registered = 0; return 1; }
static int mcm_refuses;
static const char *f_error(void) { return mcm_refuses ? "not now" : ""; }
static int f_get(McmMod *m, const char *id, McmValue *out) { (void)m; (void)id; (void)out; return 0; }   /* storage: true: no values */
static int f_set(McmMod *m, const char *id, const McmValue *v) { (void)m; (void)id; (void)v; mcm.sets++; return 1; }
static int f_set_disabled(McmMod *m, const char *id, int d) { (void)m; (void)id; (void)d; mcm.disables++; return 1; }
static int f_set_label(McmMod *m, const char *id, const char *l) { (void)m; (void)id; (void)l; mcm.labels++; return 1; }
static int f_open(void) { mcm.opens++, mcm.open = 1; return 1; }
static int f_close(void) { if (mcm_refuses) return 0; mcm.closes++, mcm.open = 0; return 1; }
static int f_is_open(void) { return mcm.open; }
static int f_focus_page(const char *mod, const char *page) { (void)mod; if (mcm_refuses) return 0; mcm.focus_pages++, mcm.open = 1; snprintf(mcm.page, sizeof mcm.page, "%s", page); return 1; }
static int mcm_editing;
static int f_status(McmStatus *s) { s->open = mcm.open, s->menu_key = 46, s->editing = mcm_editing; return 1; }
static McmApi api;
static const McmApi *fake_mcm_api(void) { return &api; }
static int mcm_loaded;
HMODULE GetModuleHandleA(const char *name) {
  if (strcmp(name, "goose_mcm.dll")) return NULL;   /* no overlay host */
  mcm.connects++;
  return mcm_loaded ? (HMODULE)&api : NULL;
}
FARPROC GetProcAddress(HMODULE module, const char *name) {
  return module == (HMODULE)&api && !strcmp(name, "mcm_api") ? (FARPROC)fake_mcm_api : NULL;
}

/* ---- what src/mod_panel.c and HellForge supply ---- */
bool tfp_mod_panel_is_open(void) { return false; }
void tfp_mod_panel_close(void) {}
static void hf_log(HfMod *mod, const char *format, ...) { (void)mod; (void)format; }
static HfApi hf;
static TfpMcmState p_mcm(void *u, TfpWire *w, unsigned unavailable, TfpMenuEditFn edit) { (void)u; return tfp_mod_menu_try(&hf, NULL, w, unavailable, edit); }
static bool f10_down;
static bool p_key(void *u, int vk) { (void)u; return vk == TFP_PANEL_KEY && f10_down; }
static const char *p_show(void *u, bool *opened) { (void)u; return tfp_mod_menu_show(opened); }
static void p_lock(void *u) { (void)u; }
static const OvApi *p_connect(void *u, uint32_t version) { (void)u; (void)version; return NULL; }

static const char settings_json[] = "{\"format\":1,\"first_person\":{},\"menu\":{\"motion_strength\":120,\"toggle_key\":122}}";

int main(void) {
  api = (McmApi){.version = 1, .size = sizeof api, .register_mod = f_register, .unregister = f_unregister, .error = f_error, .get = f_get,
                 .set = f_set, .set_disabled = f_set_disabled, .set_label = f_set_label, .open = f_open, .close = f_close,
                 .is_open = f_is_open, .focus_page = f_focus_page, .status = f_status};
  hf.log = hf_log;
  const TfpPanelPlatform platform = {NULL, p_lock, p_lock, p_connect, p_mcm, false, "goose.true-first-person/settings", NULL, TFP_EDITOR_KEY, p_key, p_show, NULL};
  new_wire();
  CHECK(tfp_wire_settings_bind(&wire, settings_json, sizeof settings_json - 1, "test"));
  mcm_loaded = 1;
  uint64_t ms = 1000;
  /* the first tick registers, and the bind puts its labels and greyed rows into MCM once */
  tfp_wire_panel_tick(&wire, &platform, ms, 0, NULL), tfp_wire_menu_frame(&wire);
  CHECK(tfp_wire_panel_choice(&wire) == TFP_CHOICE_MCM && tfp_mod_menu_registered() && mcm.registers == 1 && mcm.connects == 1);
  for (int i = 0; i < 60; i++) ms += 16, tfp_wire_panel_tick(&wire, &platform, ms, 0, NULL), tfp_wire_menu_frame(&wire);
  const unsigned sets = mcm.sets, labels = mcm.labels, disables = mcm.disables;
  /* MCM is open on the Locked view page; two minutes of game updates at 60 a second, with the re-ask once a second */
  bool opened = false;
  CHECK(!tfp_mod_menu_show(&opened) && opened);
  CHECK(mcm.focus_pages == 1 && mcm.open && !strcmp(mcm.page, tfp_wire_menu_page_ids[0]));
  mcm.focus_pages = 0;
  for (int i = 0; i < 120 * 60; i++) ms += 16, tfp_wire_panel_tick(&wire, &platform, ms, 0, NULL), tfp_wire_menu_frame(&wire);
  if (mcm.registers != 1 || mcm.unregisters || mcm.connects != 1 || mcm.focus_pages || mcm.opens || mcm.closes)
    printf("mod menu: in two minutes MCM got %u registers, %u unregisters, %u connects, %u focus_page, %u open, %u close\n",
           mcm.registers, mcm.unregisters, mcm.connects, mcm.focus_pages, mcm.opens, mcm.closes);
  CHECK(mcm.registers == 1 && !mcm.unregisters && mcm.registered);   /* the re-ask never registers again ... */
  CHECK(mcm.connects == 1);                                          /* ... nor even looks for MCM: registered is READY */
  CHECK(!mcm.focus_pages && !mcm.opens && !mcm.closes && mcm.open);  /* the page, its focus and its scroll are MCM's */
  if (mcm.sets != sets || mcm.labels != labels || mcm.disables != disables)
    printf("mod menu: with nothing changed TFP pushed %u sets, %u labels, %u disables\n", mcm.sets - sets, mcm.labels - labels, mcm.disables - disables);
  CHECK(mcm.sets == sets && mcm.labels == labels && mcm.disables == disables);   /* nothing changed: nothing sent */
  CHECK(tfp_wire_panel_choice(&wire) == TFP_CHOICE_MCM);
  /* F10: one press, one close; held, nothing more; let go and pressed again, MCM on this mod's first page */
  f10_down = true;
  for (int i = 0; i < 30; i++) ms += 16, tfp_wire_panel_tick(&wire, &platform, ms, 0, NULL);
  CHECK(mcm.closes == 1 && !mcm.open && !mcm.focus_pages);
  f10_down = false, ms += 16, tfp_wire_panel_tick(&wire, &platform, ms, 0, NULL);
  f10_down = true, ms += 16, tfp_wire_panel_tick(&wire, &platform, ms, 0, NULL);
  CHECK(mcm.focus_pages == 1 && mcm.open && mcm.closes == 1 && mcm.registers == 1);
  /* round 50: what F10 did, or why not, for its log line: closed, opened, MCM typing, MCM refusing */
  CHECK(!tfp_mod_menu_show(&opened) && !opened && mcm.closes == 2 && !mcm.open);
  CHECK(!tfp_mod_menu_show(&opened) && opened && mcm.focus_pages == 2 && mcm.open);
  mcm_editing = 1;
  const char *why = tfp_mod_menu_show(&opened);
  CHECK(why && strstr(why, "DBF-MCM is taking a key") && mcm.closes == 2 && mcm.open);
  mcm_editing = 0, mcm_refuses = 1;
  why = tfp_mod_menu_show(&opened);
  CHECK(why && !strcmp(why, "DBF-MCM did not close its window (not now)") && mcm.open);
  mcm.open = 0;
  why = tfp_mod_menu_show(&opened);
  CHECK(why && !strcmp(why, "DBF-MCM did not open this mod's page (not now)") && !mcm.open);
  mcm_refuses = 0;
  printf("mod menu: %u checks passed (while DBF-MCM holds the menu: one registration, no re-registration, no page or window change "
         "and nothing sent in two minutes of updates with the once-a-second re-ask; F10 one change per press)\n", checks);
  return 0;
}
