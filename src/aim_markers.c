#include "aim_markers.h"
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(TfpAimMarkersRegion) == 48, "NativeADS_Region is 48 bytes");

/* A raised Lua error: its text, or the error value nil. */
typedef struct Raised { bool is_nil; char text[TFP_AIM_MARKERS_TEXT]; } Raised;
static Raised shared;              /* what the public adapter and source calls hand out */

/* assert(false, message) at retail line `line`: LuaJIT puts the position before the text. */
static bool fail(Raised *e, int line, const char *message) {
  e->is_nil = false;
  snprintf(e->text, sizeof e->text, "%s:%d: %s", TFP_AIM_MARKERS_LUA_CHUNK, line, message);
  return false;
}
/* An error raised by the host passes through unchanged. */
static bool raised(Raised *e, const char *text) {
  e->is_nil = text == NULL;
  if (text) snprintf(e->text, sizeof e->text, "%s", text);
  return false;
}
static const char *text_of(const Raised *e) { return e->is_nil ? "nil" : e->text; }
static void hand(const Raised *e, const char **error) { if (error) *error = e->is_nil ? NULL : e->text; }

static bool mem_ptr(const TfpAimMarkersHost *h, uint64_t at, uint64_t *out, Raised *e) {
  const char *why = NULL;
  return h->backend_ptr(h->user, at, out, &why) || raised(e, why);
}
static bool mem_u32(const TfpAimMarkersHost *h, uint64_t at, uint32_t *out, Raised *e) {
  const char *why = NULL;
  return h->backend_u32(h->user, at, out, &why) || raised(e, why);
}
static bool mem_u8(const TfpAimMarkersHost *h, uint64_t at, uint8_t *out, Raised *e) {
  const char *why = NULL;
  return h->backend_u8(h->user, at, out, &why) || raised(e, why);
}
static bool mem_read(const TfpAimMarkersHost *h, uint64_t at, uint32_t size, uint8_t *out, Raised *e) {
  const char *why = NULL;
  return h->backend_read(h->user, at, size, out, &why) || raised(e, why);
}

/* ---- the adapter (retail lines 6-63) ---- */

/* retail's `checks`: code that must be in place before the HUD is trusted. */
static const struct { uint32_t at, size; uint8_t bytes[32]; } checks[] = {
  {TFP_AIM_MARKERS_SET_SHOWN_RVA, 32, {0x48, 0x83, 0xec, 0x28, 0x44, 0x8b, 0x01, 0x4c, 0x8b, 0xd1, 0x41, 0x8b, 0xc0, 0x45, 0x8b, 0xc8,
                                      0x83, 0xe0, 0xef, 0x41, 0x83, 0xc9, 0x10, 0x84, 0xd2, 0x44, 0x0f, 0x44, 0xc8, 0x44, 0x89, 0x09}},
  {0x1635ccf, 7, {0x4d, 0x8d, 0xa5, 0x10, 0x01, 0x00, 0x00}},                      /* fixed crosshair group */
  {0x1635d61, 7, {0x49, 0x8d, 0xb5, 0x20, 0x02, 0x00, 0x00}},                      /* moving aiming-circle group */
  {0x117c88d, 19, {0x49, 0x8d, 0x8f, 0x78, 0xa6, 0x19, 0x00, 0xe8, 0x87, 0x93, 0x4b, 0x00, 0x49, 0x8d, 0x97, 0x78, 0xa6, 0x19, 0x00}},
};

static bool validate(const TfpAimMarkersHost *h, uint64_t *game, Raised *e) {
  const char *why = NULL;
  uint8_t bytes[32];
  if (!h->backend_module_identity(h->user, game, &why)) return raised(e, why);
  for (size_t i = 0; i < sizeof checks / sizeof checks[0]; i++) {
    if (!mem_read(h, *game + checks[i].at, checks[i].size, bytes, e)) return false;
    if (memcmp(bytes, checks[i].bytes, checks[i].size) != 0) return fail(e, 24, "Aiming HUD fingerprint differs");
  }
  return true;
}

static TfpAimMarkersProbe probe(const TfpAimMarkersHost *h, bool mission_only, TfpAimMarkersHud *out, Raised *e) {
  uint64_t game, state, root, crosshair, parent, context, world, owner;
  uint32_t word;
  uint8_t byte;
  if (!validate(h, &game, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
  if (!mem_ptr(h, game + 0x2ce1508, &state, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
  if (mission_only) {
    if (!mem_u32(h, state + 0xac29c, &word, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
    if (word != 4) return TFP_AIM_MARKERS_PROBE_NONE;
  }
  if (!mem_ptr(h, game + 0x2e28788, &root, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
  if (!mem_u8(h, root + 0x24e334, &byte, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
  if (byte != 1) return TFP_AIM_MARKERS_PROBE_NONE;
  crosshair = root + 0x24e340 + 0x19a678;
  if (!mem_ptr(h, crosshair + 0xf0, &parent, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
  if (parent != root + 0x820) { fail(e, 36, "Aiming HUD parent differs"); return TFP_AIM_MARKERS_PROBE_RAISED; }
  if (!mem_ptr(h, crosshair + 0xf8, &context, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
  if (!mem_u8(h, context, &byte, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
  if (byte != 1) return TFP_AIM_MARKERS_PROBE_NONE;
  if (!mem_ptr(h, context + 0x10, &world, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
  out->game = game; out->root = root; out->state = state; out->parent = parent; out->context = context; out->world = world;
  out->widgets[0] = crosshair + 0x110;
  out->widgets[1] = crosshair + 0x220;
  for (int i = 0; i < 2; i++) {
    uint64_t widget = out->widgets[i];
    bool owned;
    if (!mem_ptr(h, widget + 0xf0, &owner, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
    owned = owner == crosshair;
    if (owned) {
      if (!mem_ptr(h, widget + 0xf8, &owner, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
      owned = owner == context;
    }
    if (!owned) { fail(e, 42, "Aiming HUD child ownership differs"); return TFP_AIM_MARKERS_PROBE_RAISED; }
    if (!mem_u32(h, widget, &word, e)) return TFP_AIM_MARKERS_PROBE_RAISED;
    if (word == 0) { fail(e, 44, "Aiming HUD child is not initialized"); return TFP_AIM_MARKERS_PROBE_RAISED; }
  }
  return TFP_AIM_MARKERS_PROBE_HUD;
}

bool tfp_aim_markers_same(const TfpAimMarkersHud *a, const TfpAimMarkersHud *b) {
  return a && b && a->game == b->game && a->root == b->root && a->state == b->state && a->parent == b->parent && a->context == b->context &&
         a->world == b->world;
}

static bool visible(const TfpAimMarkersHost *h, uint64_t widget, bool *out, Raised *e) {
  uint32_t word;
  if (!mem_u32(h, widget, &word, e)) return false;
  *out = word / 16 % 2 == 1;
  return true;
}

static bool show(const TfpAimMarkersHost *h, const TfpAimMarkersHud *hud, uint64_t widget, bool value, Raised *e) {
  TfpAimMarkersHud live;
  TfpAimMarkersProbe got;
  const char *why = NULL;
  bool now;
  if (!(widget == hud->widgets[0] || widget == hud->widgets[1])) return fail(e, 51, "Unowned aiming HUD target");
  got = probe(h, false, &live, e);
  if (got == TFP_AIM_MARKERS_PROBE_RAISED) return false;
  if (got != TFP_AIM_MARKERS_PROBE_HUD || !tfp_aim_markers_same(&live, hud)) return fail(e, 52, "Aiming HUD changed before write");
  if (h->setter) {
    if (!h->setter(h->user, widget, value, &why)) return raised(e, why);
  } else {
    TfpAimMarkersRegion r;
    bool wine = false;
    uint64_t address;
    if (!h->backend_windows(h->user, &wine, &why)) return raised(e, why);
    address = hud->game + TFP_AIM_MARKERS_SET_SHOWN_RVA;
    memset(&r, 0, sizeof r);
    if (!(h->windows_virtual_query(h->user, address, &r, sizeof r) == sizeof r && r.state == 0x1000 &&
          (r.kind == 0x1000000 || (r.kind == 0x40000 && wine && r.allocation == hud->game)) &&
          (r.protect == 0x20 || r.protect == 0x40 || r.protect == 0x80)))
      return fail(e, 56, "HUD setter is not executable");
    if (!h->game_set_widget_shown(h->user, address, widget, value ? 1 : 0, &why)) return raised(e, why);
  }
  if (!visible(h, widget, &now, e)) return false;
  if (now != value) return fail(e, 62, "Aiming HUD visibility readback differs");
  return true;
}

TfpAimMarkersProbe tfp_aim_markers_probe(const TfpAimMarkersHost *host, bool mission_only, TfpAimMarkersHud *out, const char **error) {
  TfpAimMarkersProbe got = probe(host, mission_only, out, &shared);
  if (got == TFP_AIM_MARKERS_PROBE_RAISED) hand(&shared, error);
  return got;
}
bool tfp_aim_markers_visible(const TfpAimMarkersHost *host, uint64_t widget, bool *out, const char **error) {
  if (visible(host, widget, out, &shared)) return true;
  hand(&shared, error);
  return false;
}
bool tfp_aim_markers_show(const TfpAimMarkersHost *host, const TfpAimMarkersHud *hud, uint64_t widget, bool shown, const char **error) {
  if (show(host, hud, widget, shown, &shared)) return true;
  hand(&shared, error);
  return false;
}

/* ---- the source (retail lines 65-109) ---- */

/* Camera kind 6 also covers ordinary native ADS with the F11 mode switched off. A pinned
 * action/vehicle camera supplies only view ownership, never a weapon identity or authorization for
 * a legacy per-weapon preference. */
static bool pinned_view(TfpAimMarkersSource *src, bool fresh, TfpAimMarkersTarget *out) {
  static const char tail[] = ":pinned-view";
  const TfpAimMarkersHost *h = src->host;
  TfpAimMarkersSnapshot s;
  size_t n;
  if (fresh || !h->backend_pin_first_person || !h->backend_pin_player_snapshot || !h->pinned_ready || !h->controller_is_enabled(h->user) ||
      !h->controller_pose_offset(h->user))
    return false;
  if (!h->backend_pin_player_snapshot(h->user, &s)) return false;
  if (s.foreign_camera || !s.has_player_identity || s.player_identity_len == 0 || !h->pinned_ready(h->user, &s)) return false;
  n = s.player_identity_len < TFP_AIM_MARKERS_IDENTITY ? s.player_identity_len : TFP_AIM_MARKERS_IDENTITY;
  memset(out, 0, sizeof *out);
  out->view_only = true;
  out->has_identity = true;
  memcpy(out->identity, s.player_identity, n);
  memcpy(out->identity + n, tail, sizeof tail - 1);
  out->identity_len = n + sizeof tail - 1;
  out->has_player_identity = true;
  memcpy(out->player_identity, s.player_identity, n);
  out->player_identity_len = n;
  out->aiming = s.shoulder_aim || s.controller_active || s.aiming;
  return true;
}

static TfpAimMarkersRead current_of(TfpAimMarkersSource *src, bool fresh, TfpAimMarkersTarget *out, Raised *e) {
  const TfpAimMarkersHost *h = src->host;
  TfpAimMarkersSnapshot s;
  bool in_fp, has_key;
  char key[TFP_AIM_MARKERS_KEY];
  if (h->foreign(h->user)) return TFP_AIM_MARKERS_READ_NONE;
  /* retail asks options.fird() here; Fird Person is parked and the answer is always nil. */
  if (!h->backend_snapshot(h->user, &s) || !s.safe) return pinned_view(src, fresh, out) ? TFP_AIM_MARKERS_READ_TARGET : TFP_AIM_MARKERS_READ_NONE;
  if (s.identity_len > TFP_AIM_MARKERS_IDENTITY) s.identity_len = TFP_AIM_MARKERS_IDENTITY;
  if (s.player_identity_len > TFP_AIM_MARKERS_IDENTITY) s.player_identity_len = TFP_AIM_MARKERS_IDENTITY;
  in_fp = s.camera_kind == 6;
  if (s.camera_kind == 4) in_fp = h->controller_is_enabled(h->user) && h->controller_pose_offset(h->user) && h->fallback_ready(h->user);
  if (!in_fp) return pinned_view(src, fresh, out) ? TFP_AIM_MARKERS_READ_TARGET : TFP_AIM_MARKERS_READ_NONE;
  if (!fresh && src->has_cached_identity == s.has_identity &&
      (!s.has_identity || (src->cached_identity_len == s.identity_len && memcmp(src->cached_identity, s.identity, s.identity_len) == 0))) {
    has_key = src->has_cached_key;
    memcpy(key, src->cached_key, sizeof key);
  } else {
    const char *why = NULL;
    uint64_t weapon_object = 0;
    uint8_t bytes[8];
    if (!(h->backend_pose_context(h->user, &s, &weapon_object, &why) && h->backend_read(h->user, weapon_object + 0x358, 8, bytes, &why))) {
      raised(e, why);
      if (pinned_view(src, fresh, out)) return TFP_AIM_MARKERS_READ_TARGET;
      return TFP_AIM_MARKERS_READ_RAISED;
    }
    for (int i = 0; i < 8; i++) snprintf(key + 2 * i, 3, "%02x", bytes[i]);
    has_key = true;
    src->has_cached_identity = s.has_identity;
    src->cached_identity_len = s.identity_len;
    memcpy(src->cached_identity, s.identity, s.identity_len);
    src->has_cached_key = true;
    memcpy(src->cached_key, key, sizeof key);
  }
  memset(out, 0, sizeof *out);
  out->has_key = has_key;
  if (has_key) memcpy(out->key, key, sizeof key);
  out->has_identity = s.has_identity;
  out->identity_len = s.identity_len;
  memcpy(out->identity, s.identity, s.identity_len);
  out->has_player_identity = s.has_player_identity;
  out->player_identity_len = s.player_identity_len;
  memcpy(out->player_identity, s.player_identity, s.player_identity_len);
  out->aiming = s.shoulder_aim || s.controller_active || s.aiming;
  return TFP_AIM_MARKERS_READ_TARGET;
}

void tfp_aim_markers_source_init(TfpAimMarkersSource *s, const TfpAimMarkersHost *host) {
  memset(s, 0, sizeof *s);
  s->host = host;
}
TfpAimMarkersRead tfp_aim_markers_source_current(TfpAimMarkersSource *s, bool fresh, TfpAimMarkersTarget *out, const char **error) {
  TfpAimMarkersRead got = current_of(s, fresh, out, &shared);
  if (got == TFP_AIM_MARKERS_READ_RAISED) hand(&shared, error);
  return got;
}

/* ---- the lease (retail lines 111-260) ---- */

static bool valid_key(const char *key) {
  bool zero = true;
  if (strlen(key) != 16) return false;
  for (int i = 0; i < 16; i++) {
    if (!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f'))) return false;
    if (key[i] != '0') zero = false;
  }
  return !zero;
}
static bool same_text(const char *a, size_t a_len, const char *b, size_t b_len) { return a_len == b_len && memcmp(a, b, a_len) == 0; }

void tfp_aim_markers_init(TfpAimMarkers *m, const TfpAimMarkersHost *host) {
  memset(m, 0, sizeof *m);
  m->host = host;
  tfp_aim_markers_source_init(&m->source, host);
}
/* A second result that is not a fault: copied, so that it outlives the host's own text. */
static bool refuse(TfpAimMarkers *m, const char *text, const char **why) {
  snprintf(m->why, sizeof m->why, "%s", text);
  if (why) *why = m->why;
  return false;
}
static bool done(const char **why) {
  if (why) *why = NULL;
  return true;
}
static bool problem(TfpAimMarkers *m, const char *text, const char **why) {
  char copy[TFP_AIM_MARKERS_TEXT], line[TFP_AIM_MARKERS_TEXT + 32];
  snprintf(copy, sizeof copy, "%s", text ? text : "nil");
  if (!m->has_fault || strcmp(m->fault, copy) != 0) {
    snprintf(line, sizeof line, "Aiming markers: %s", copy);
    if (m->host->log) m->host->log(m->host->user, line);
  }
  m->has_fault = true;
  memcpy(m->fault, copy, sizeof copy);
  if (why) *why = m->fault;
  return false;
}

/* current(fresh) supplies the owned FP weapon and aim state. A fresh read is required for menu
 * clicks so a delayed click cannot change the next weapon. */
static TfpAimMarkersRead current(TfpAimMarkers *m, bool fresh, TfpAimMarkersTarget *t, Raised *e) {
  const TfpAimMarkersHost *h = m->host;
  TfpAimMarkersRead got = current_of(&m->source, fresh, t, e);
  if (got != TFP_AIM_MARKERS_READ_TARGET) return got;
  if (!t->has_identity || !t->has_player_identity) return TFP_AIM_MARKERS_READ_NONE;
  if (t->view_only) {
    /* The all-view hide choice needs no firearm. A pin-only owner must never become a fabricated
     * per-weapon setting or menu target. */
    if (t->has_key || !h->global_setting || h->global_setting(h->user) != TFP_AIM_MARKERS_HIDE) return TFP_AIM_MARKERS_READ_NONE;
  } else if (!t->has_key || !valid_key(t->key)) return TFP_AIM_MARKERS_READ_NONE;
  return TFP_AIM_MARKERS_READ_TARGET;
}
static TfpAimMarkersTri global_setting(const TfpAimMarkers *m) {
  return m->host->global_setting ? m->host->global_setting(m->host->user) : TFP_AIM_MARKERS_UNSET;
}
/* key NULL: retail's nil key (a view-only target), which no table holds. */
static bool legacy_allowed(const TfpAimMarkers *m, const char *key) {
  TfpAimMarkersSaved saved;
  saved.count = 0;
  if (!m->host->settings(m->host->user, &saved) || !key) return false;
  for (size_t i = 0; i < saved.count && i < TFP_AIM_MARKERS_SAVED; i++) if (strcmp(saved.keys[i], key) == 0) return true;
  return false;
}
static bool allowed(const TfpAimMarkers *m, const char *key) {
  TfpAimMarkersTri value = global_setting(m);
  if (value != TFP_AIM_MARKERS_UNSET) return value == TFP_AIM_MARKERS_SHOW;
  return legacy_allowed(m, key);
}

bool tfp_aim_markers_setting(TfpAimMarkers *m) {
  TfpAimMarkersTri value = global_setting(m);
  TfpAimMarkersTarget t;
  Raised e;
  if (value != TFP_AIM_MARKERS_UNSET) return value == TFP_AIM_MARKERS_SHOW;
  if (current(m, false, &t, &e) == TFP_AIM_MARKERS_READ_TARGET && t.aiming) return legacy_allowed(m, t.has_key ? t.key : NULL);
  /* Legacy suppression applied only during aim. Before entry and at hip the game still owns
   * visible markers until the user chooses. */
  return true;
}

bool tfp_aim_markers_set_setting(TfpAimMarkers *m, bool value, const char **why) {
  const TfpAimMarkersHost *h = m->host;
  const char *text = NULL;
  if (m->closing) return refuse(m, "Mod is stopping", why);
  if (!h->save_global) return refuse(m, "Aim-marker setting is unavailable", why);
  switch (h->save_global(h->user, value, &text)) {
    case TFP_AIM_MARKERS_SAVE_DONE: break;
    case TFP_AIM_MARKERS_SAVE_RAISED: return refuse(m, text ? text : "nil", why);
    default: return refuse(m, text ? text : "Aim-marker setting was not saved", why);
  }
  return tfp_aim_markers_update(m, why);
}

TfpAimMarkersRead tfp_aim_markers_target(TfpAimMarkers *m, TfpAimMarkersMenuTarget *out, const char **error) {
  TfpAimMarkersTarget t;
  Raised e;
  TfpAimMarkersRead got;
  if (m->closing) return TFP_AIM_MARKERS_READ_NONE;
  got = current(m, false, &t, &e);
  if (got == TFP_AIM_MARKERS_READ_RAISED) {
    if (!e.is_nil) snprintf(m->why, sizeof m->why, "%s", e.text);
    if (error) *error = e.is_nil ? NULL : m->why;
    return got;
  }
  if (got != TFP_AIM_MARKERS_READ_TARGET || t.view_only) return TFP_AIM_MARKERS_READ_NONE;
  memcpy(out->key, t.key, sizeof out->key);
  memcpy(out->identity, t.identity, t.identity_len);
  out->identity_len = t.identity_len;
  out->allowed = allowed(m, t.key);
  return TFP_AIM_MARKERS_READ_TARGET;
}

/* The protected body of stop(), retail lines 181-190. */
static bool release(TfpAimMarkers *m, Raised *e) {
  const TfpAimMarkersHost *h = m->host;
  TfpAimMarkersLease *lease = &m->lease;
  TfpAimMarkersHud live;
  TfpAimMarkersProbe got = probe(h, false, &live, e);
  if (got == TFP_AIM_MARKERS_PROBE_RAISED) return false;
  if (got == TFP_AIM_MARKERS_PROBE_HUD && tfp_aim_markers_same(&live, &lease->hud)) {
    for (size_t i = 0; i < lease->count; i++) {
      TfpAimMarkersRow *entry = &lease->rows[i];
      if (!entry->done) {
        bool shown;
        if (!visible(h, entry->widget, &shown, e)) return false;
        if (!shown && !show(h, &lease->hud, entry->widget, entry->before, e)) return false;
        entry->done = true;
      }
    }
  } /* A retired/rebuilt HUD must never receive old visibility. */
  m->has_lease = false;
  return true;
}
bool tfp_aim_markers_stop(TfpAimMarkers *m, const char **why) {
  Raised e;
  if (!m->has_lease) return done(why);
  m->lease.releasing = true;
  if (!release(m, &e)) return problem(m, text_of(&e), why);
  return done(why);
}

typedef enum Outcome { OUTCOME_TRUE, OUTCOME_FALSE, OUTCOME_RAISED } Outcome;
/* The protected body of update(), retail lines 197-232. OUTCOME_FALSE: `false, *reason`. */
static Outcome refresh(TfpAimMarkers *m, const char **reason, Raised *e) {
  const TfpAimMarkersHost *h = m->host;
  TfpAimMarkersTarget t;
  TfpAimMarkersHud hud;
  TfpAimMarkersTri global;
  TfpAimMarkersProbe found;
  bool have_target = false, suppress;
  if (m->has_lease && m->lease.releasing) {
    if (!tfp_aim_markers_stop(m, reason)) return OUTCOME_FALSE;
  }
  if (!m->closing) {
    TfpAimMarkersRead got = current(m, false, &t, e);
    if (got == TFP_AIM_MARKERS_READ_RAISED) return OUTCOME_RAISED;
    have_target = got == TFP_AIM_MARKERS_READ_TARGET;
  }
  global = global_setting(m);
  /* Hip view keeps the game's native crosshair and moving circle. The explicit hide choice covers
   * aiming in every owned FP view; unset preferences retain the legacy per-weapon aim behavior. */
  suppress = have_target && t.aiming &&
             (global == TFP_AIM_MARKERS_HIDE || (global == TFP_AIM_MARKERS_UNSET && !legacy_allowed(m, t.has_key ? t.key : NULL)));
  if (!suppress) return tfp_aim_markers_stop(m, reason) ? OUTCOME_TRUE : OUTCOME_FALSE;
  /* An explicit all-view choice also covers the initialized ship HUD and pinned action views.
   * Legacy weapon preferences stay mission-only. */
  found = probe(h, global != TFP_AIM_MARKERS_HIDE, &hud, e);
  if (found == TFP_AIM_MARKERS_PROBE_RAISED) return OUTCOME_RAISED;
  if (found == TFP_AIM_MARKERS_PROBE_NONE) return tfp_aim_markers_stop(m, reason) ? OUTCOME_TRUE : OUTCOME_FALSE;
  if (m->has_lease && (!same_text(m->lease.weapon, m->lease.weapon_len, t.identity, t.identity_len) ||
                       !same_text(m->lease.player, m->lease.player_len, t.player_identity, t.player_identity_len) ||
                       !tfp_aim_markers_same(&hud, &m->lease.hud))) {
    if (!tfp_aim_markers_stop(m, reason)) return OUTCOME_FALSE;
  }
  if (!m->has_lease) {
    memset(&m->lease, 0, sizeof m->lease);
    m->lease.hud = hud;
    memcpy(m->lease.weapon, t.identity, t.identity_len);
    m->lease.weapon_len = t.identity_len;
    memcpy(m->lease.player, t.player_identity, t.player_identity_len);
    m->lease.player_len = t.player_identity_len;
    m->has_lease = true;
  }
  for (int i = 0; i < 2; i++) {
    uint64_t widget = hud.widgets[i];
    bool shown;
    if (!visible(h, widget, &shown, e)) return OUTCOME_RAISED;
    if (shown) {
      TfpAimMarkersLease *lease = &m->lease;
      size_t at = 0;
      while (at < lease->count && lease->rows[at].widget != widget) at++;
      if (at == lease->count && at < 2) {
        lease->rows[at].widget = widget;
        lease->rows[at].before = true;
        lease->rows[at].done = false;
        lease->count++;
      }
      /* Record ownership before calling: a native call can fail after writing. */
      if (!show(h, &hud, widget, false, e)) return OUTCOME_RAISED;
    }
  }
  m->has_fault = false;
  return OUTCOME_TRUE;
}
bool tfp_aim_markers_update(TfpAimMarkers *m, const char **why) {
  const char *reason = NULL;
  Raised e;
  switch (refresh(m, &reason, &e)) {
    case OUTCOME_RAISED:
      tfp_aim_markers_stop(m, NULL);
      return problem(m, text_of(&e), why);
    case OUTCOME_FALSE: return problem(m, reason, why);
    default: break;
  }
  m->has_fault = false;
  return done(why);
}

bool tfp_aim_markers_toggle(TfpAimMarkers *m, const char *key, const char *identity, size_t identity_len, const char **why) {
  const TfpAimMarkersHost *h = m->host;
  char values[TFP_AIM_MARKERS_SAVED + 1][TFP_AIM_MARKERS_KEY];
  TfpAimMarkersSaved previous;
  TfpAimMarkersTarget t;
  TfpAimMarkersRead got;
  const char *text = NULL;
  size_t count = 0, kept = 0;
  bool listed = false;
  Raised e;
  if (m->closing) return refuse(m, "Mod is stopping", why);
  if (global_setting(m) != TFP_AIM_MARKERS_UNSET) return refuse(m, "Use Show aim markers to change all weapons", why);
  got = current(m, true, &t, &e);
  if (got == TFP_AIM_MARKERS_READ_RAISED) return refuse(m, text_of(&e), why);
  if (got != TFP_AIM_MARKERS_READ_TARGET || !t.has_key || strcmp(t.key, key) != 0 || !same_text(t.identity, t.identity_len, identity, identity_len))
    return refuse(m, "Weapon changed; select the aiming-marker setting again", why);
  previous.count = 0;
  if (h->settings(h->user, &previous)) {
    for (size_t i = 0; i < previous.count && i < TFP_AIM_MARKERS_SAVED; i++) {
      bool twice = false;
      if (!valid_key(previous.keys[i])) continue;
      for (size_t j = 0; j < count; j++) if (strcmp(values[j], previous.keys[i]) == 0) twice = true;
      if (!twice) memcpy(values[count++], previous.keys[i], TFP_AIM_MARKERS_KEY);
    }
  }
  /* values[key] = not allowed(key) or nil */
  if (allowed(m, key)) {
    for (size_t i = 0; i < count; i++) if (strcmp(values[i], key) != 0) memmove(values[kept++], values[i], TFP_AIM_MARKERS_KEY);
    count = kept;
  } else {
    for (size_t i = 0; i < count; i++) if (strcmp(values[i], key) == 0) listed = true;
    if (!listed) memcpy(values[count++], key, TFP_AIM_MARKERS_KEY);
  }
  if (!h->save(h->user, (const char (*)[TFP_AIM_MARKERS_KEY])values, count, &text))
    return refuse(m, text ? text : "Aiming-marker setting was not saved", why);
  return tfp_aim_markers_update(m, why);
}

bool tfp_aim_markers_shutdown(TfpAimMarkers *m, const char **why) {
  m->closing = true;
  return tfp_aim_markers_stop(m, why);
}
void tfp_aim_markers_result(const TfpAimMarkers *m, TfpAimMarkersResult *out) {
  out->hidden = m->has_lease;
  out->error = m->has_fault ? m->fault : NULL;
  out->cleanup_pending = m->has_lease && (m->closing || m->has_fault);
}
