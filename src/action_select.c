/* See action_select.h. Line references are to the retail file
 * src/first_person/action_placement.lua at 0.5.41-smooth.66. */
#include "action_select.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const action_names[TFP_ACT_COUNT] = { /* :66-67 */
  "idle", "aim", "ads", "sprint", "reload", "crouch", "prone", "slide", "dive"};
static const char *const action_labels[TFP_ACT_COUNT] = {
  "Idle", "Normal aim", "ADS", "Sprinting", "Reloading", "Crouching", "Prone", "Sliding", "Diving"};
static const char *const family_names[TFP_FAMILY_COUNT] = {
  NULL, "rifle", "compact", "sidearm", "marksman", "shotgun", "energy", "support"};
static const struct { double x, y, z; } baselines[TFP_FAMILY_COUNT] = { /* :7-12 */
  {0, 0, 0}, {.15, 0, -.12}, {.12, 0, -.09}, {.105, 0, -.08}, {.14, .015, -.115}, {.14, .005, -.11}, {.15, 0, -.115},
  {.16, .015, -.14}};
static const char RESOURCE[] = "4D1C334D294DFA97"; /* :50 */
static const char ALL[] = "__all";

static bool allowed(TfpAct action) { return action >= 0 && action < TFP_ACT_COUNT; } /* :68 */
const char *tfp_action_name(TfpAct action) { return allowed(action) ? action_names[action] : NULL; }
const char *tfp_action_label(TfpAct action) { return allowed(action) ? action_labels[action] : NULL; }
TfpAct tfp_action_from_name(const char *name) {
  if (name)
    for (int a = 0; a < TFP_ACT_COUNT; a++)
      if (!strcmp(name, action_names[a])) return (TfpAct)a;
  return TFP_ACT_NONE;
}
bool tfp_action_observed(TfpAct action) { /* :51 */
  return action == TFP_ACT_CROUCH || action == TFP_ACT_PRONE || action == TFP_ACT_SLIDE || action == TFP_ACT_DIVE;
}
const char *tfp_action_family_name(TfpActionFamily family) {
  return family > TFP_FAMILY_NONE && family < TFP_FAMILY_COUNT ? family_names[family] : NULL;
}

static bool starts(const char *name, const char *prefix) { return !strncmp(name, prefix, strlen(prefix)); } /* name:match('^...') */
static TfpActionFamily family(const TfpActionGearRow *row) { /* :13-24 */
  const char *name = row->name;
  if (!name || starts(name, "CQC") || starts(name, "Item ") || starts(name, "Unidentified") || starts(name, "B/") ||
      !strcmp(name, "Breaching Hammer"))
    return TFP_FAMILY_NONE;
  if (row->kind == 3) return TFP_FAMILY_SIDEARM;
  if (row->kind == 2) return TFP_FAMILY_SUPPORT;
  if (row->kind != 1) return TFP_FAMILY_NONE;
  if (starts(name, "SMG") || starts(name, "MP-") || starts(name, "M7S ") || starts(name, "StA-11 ")) return TFP_FAMILY_COMPACT;
  if (starts(name, "SG-") || starts(name, "DBS-") || starts(name, "M90A ")) return TFP_FAMILY_SHOTGUN;
  if (starts(name, "R-") || starts(name, "R/")) return TFP_FAMILY_MARKSMAN;
  if (starts(name, "LAS-") || starts(name, "PLAS-") || starts(name, "ARC-") || starts(name, "FLAM-")) return TFP_FAMILY_ENERGY;
  if (starts(name, "AR") || starts(name, "BR-") || starts(name, "MA5C ") || starts(name, "StA-52 ")) return TFP_FAMILY_RIFLE;
  return TFP_FAMILY_NONE;
}
static bool hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
static TfpActionWeapon *find(const TfpActionCatalogue *c, const char *key) {
  if (c && key)
    for (size_t i = 0; i < c->count; i++)
      if (!strcmp(c->weapons[i].key, key)) return c->weapons + i;
  return NULL;
}

bool tfp_action_configure_catalogue(TfpActionCatalogue *c, const TfpActionGearRow *rows, size_t count) { /* :25-41 */
  c->count = 0;
  if (!rows) return true;
  for (size_t r = 0; r < count; r++) {
    const TfpActionGearRow *row = rows + r;
    TfpActionFamily f = family(row);
    const char *id = f ? row->type : NULL;
    if (!id || strlen(id) != 16) continue;
    bool all_hex = true;
    for (int i = 0; i < 16; i++) all_hex = all_hex && hex(id[i]);
    if (!all_hex) continue;
    char key[17];
    for (int pair = 0; pair < 8; pair++) { /* :31, the type's bytes in memory order */
      key[pair * 2] = lower(id[14 - pair * 2]);
      key[pair * 2 + 1] = lower(id[15 - pair * 2]);
    }
    key[16] = 0;
    TfpActionWeapon *old = find(c, key);
    if (old && !old->ambiguous && (strcmp(old->name, row->name) || old->family != f)) { /* :36 */
      old->ambiguous = true;
    } else if (!old || !old->ambiguous) { /* :37 */
      if (!old) {
        if (c->count >= c->capacity) { c->count = 0; return false; }
        old = c->weapons + c->count++;
      }
      memcpy(old->key, key, sizeof key);
      old->ambiguous = false;
      old->name = row->name;
      old->family = f;
      old->kind = row->kind;
      old->item = row->id;
      old->has_item = row->has_id;
    }
  }
  return true;
}

const TfpActionWeapon *tfp_action_weapon_info(const TfpActionCatalogue *c, const char *key) { /* :42-45 */
  const TfpActionWeapon *v = find(c, key);
  return v && !v->ambiguous ? v : NULL;
}

bool tfp_action_family_baseline(const TfpActionCatalogue *c, const char *key, TfpActionPlacement *out) { /* :46-49 */
  const TfpActionWeapon *v = find(c, key);
  if (!v || v->ambiguous) return false;
  out->x = baselines[v->family].x; out->y = baselines[v->family].y; out->z = baselines[v->family].z;
  out->yaw = 0; out->pitch = 0; out->roll = 0;
  out->present = TFP_PLACE_ALL;
  return true;
}

/* stance(sample), :54-65. TFP_ACT_NONE is nil. */
static TfpAct stance(const TfpActionSample *s) {
  if (!s->action_resource || strcmp(s->action_resource, RESOURCE) || s->action_verified != TFP_VERIFIED_TRUE) return TFP_ACT_NONE;
  const uint8_t *f = s->stance_flags;
  if (!f || s->stance_flags_size != 24) return TFP_ACT_NONE;
  unsigned posture = f[9] / 32 % 4 * 32;                                /* f:byte(10) */
  if (posture == 0x40 && f[12] / 32 % 2 != 0) return TFP_ACT_DIVE;       /* f:byte(13) */
  if (posture == 0x20 && f[10] / 2 % 2 != 0) return TFP_ACT_SLIDE;       /* f:byte(11) */
  if (posture == 0x20) return TFP_ACT_CROUCH;
  if (posture == 0x40) return TFP_ACT_PRONE;
  return TFP_ACT_NONE;
}

bool tfp_action_valid(const TfpActionPlacement *p) { /* :69-77 */
  if (!p) return false;
  const double position[3] = {p->x, p->y, p->z}, angles[3] = {p->yaw, p->pitch, p->roll};
  for (int axis = 0; axis < 3; axis++) {
    double n = position[axis];
    if (!(p->present & (1u << axis)) || n != n || fabs(n) > .5) return false;
  }
  for (int axis = 0; axis < 3; axis++) {
    double n = p->present & (8u << axis) ? angles[axis] : 0;
    if (n != n || fabs(n) > 90) return false;
  }
  return true;
}
static TfpActionPlacement copy(const TfpActionPlacement *p) { /* :78 */
  TfpActionPlacement out = {p->x, p->y, p->z, p->present & TFP_PLACE_YAW ? p->yaw : 0, p->present & TFP_PLACE_PITCH ? p->pitch : 0,
                            p->present & TFP_PLACE_ROLL ? p->roll : 0, TFP_PLACE_ALL};
  return out;
}
static TfpActionPlacement position_only(double x, double y, double z) {
  TfpActionPlacement out = {x, y, z, 0, 0, 0, TFP_PLACE_XYZ};
  return out;
}
static const TfpActionProfile *profile(const TfpActionConfig *config, const char *id) {
  if (config && id)
    for (size_t i = 0; i < config->profile_count; i++)
      if (config->profiles[i].id && !strcmp(config->profiles[i].id, id)) return config->profiles + i;
  return NULL;
}
static const TfpActionPlacement *profile_action(const TfpActionProfile *p, TfpAct action) {
  return p && (p->has & (1u << action)) ? p->action + action : NULL;
}
static const TfpActionPlacement *legacy(const TfpActionConfig *config, const char *id) {
  if (config && id)
    for (size_t i = 0; i < config->legacy_count; i++)
      if (config->legacy[i].id && !strcmp(config->legacy[i].id, id)) return &config->legacy[i].placement;
  return NULL;
}

bool tfp_action_resolve(const TfpActionConfig *config, const TfpActionCatalogue *catalogue, const char *key, TfpAct action,
                        TfpActionPlacement *out, bool *is_explicit, const char **error) { /* :79-102 */
  if (error) *error = NULL;
  if (action == TFP_ACT_NONE) action = TFP_ACT_IDLE; /* :80 */
  if (!allowed(action)) {
    if (error) *error = "Unknown placement action";
    return false;
  }
  const TfpActionPlacement *v = profile_action(profile(config, key), action); /* :82-84 */
  if (!tfp_action_valid(v)) v = profile_action(profile(config, ALL), action); /* :85 */
  if (tfp_action_valid(v)) { /* :86 */
    *out = copy(v);
    if (is_explicit) *is_explicit = true;
    return true;
  }
  if (is_explicit) *is_explicit = false;
  if (action == TFP_ACT_ADS) { /* :87 */
    *out = position_only(0, 0, 0);
    return true;
  }
  if (action != TFP_ACT_IDLE) /* :88 */
    return tfp_action_resolve(config, catalogue, key, TFP_ACT_IDLE, out, NULL, error);
  v = legacy(config, key);                         /* :89-90 */
  if (!tfp_action_valid(v)) v = legacy(config, ALL); /* :91 */
  if (tfp_action_valid(v)) { /* :92 */
    *out = copy(v);
    return true;
  }
  v = config ? config->placement : NULL; /* :95 */
  bool good = tfp_action_valid(v);
  bool standard = good && v->x == .15 && v->y == 0 && v->z == -.12 && (v->present & TFP_PLACE_YAW ? v->yaw : 0) == 0 &&
                  (v->present & TFP_PLACE_PITCH ? v->pitch : 0) == 0 && (v->present & TFP_PLACE_ROLL ? v->roll : 0) == 0; /* :96-97 */
  if (config && config->family_placement_defaults && (!good || standard)) { /* :98-100 */
    if (tfp_action_family_baseline(catalogue, key, out)) return true;
  }
  *out = good ? copy(v) : position_only(.15, 0, -.12); /* :101 */
  return true;
}

TfpActionPlacement tfp_action_default_placement(const TfpActionConfig *config, const TfpActionCatalogue *catalogue,
                                                const char *key, TfpAct action) { /* :103-107 */
  TfpActionPlacement out = {0, 0, 0, 0, 0, 0, TFP_PLACE_ALL};
  if (action == TFP_ACT_ADS) return out;
  if (config && config->family_placement_defaults && tfp_action_family_baseline(catalogue, key, &out)) return out;
  out.x = .15; out.y = 0; out.z = -.12;
  return out;
}

bool tfp_action_saved(const TfpActionConfig *config, const char *key, TfpAct action, const TfpActionPlacement *value, bool all,
                      TfpActionProfile *out, size_t out_capacity, size_t *out_count, const char **error) { /* :108-121 */
  size_t count = 0;
  if (out_count) *out_count = 0;
  if (error) *error = NULL;
  if (!allowed(action) || !tfp_action_valid(value)) { /* :109 */
    if (error) *error = "Invalid action placement";
    return false;
  }
  if (!key || strlen(key) != 16) { /* :110 */
    if (error) *error = "Invalid weapon identity";
    return false;
  }
  for (size_t i = 0; config && i < config->profile_count; i++) { /* :112-118 */
    const TfpActionProfile *p = config->profiles + i;
    if (count >= out_capacity) return false;
    TfpActionProfile *row = out + count++;
    row->id = p->id;
    row->has = 0;
    for (int a = 0; a < TFP_ACT_COUNT; a++)
      if ((p->has & (1u << a)) && tfp_action_valid(p->action + a) && !(all && a == (int)action)) {
        row->action[a] = copy(p->action + a);
        row->has |= 1u << a;
      }
  }
  const char *id = all ? ALL : key; /* :119 */
  TfpActionProfile *row = NULL;
  for (size_t i = 0; i < count && !row; i++)
    if (out[i].id && !strcmp(out[i].id, id)) row = out + i;
  if (!row) {
    if (count >= out_capacity) return false;
    row = out + count++;
    row->id = id;
    row->has = 0;
  }
  row->action[action] = copy(value);
  row->has |= 1u << action;
  if (out_count) *out_count = count;
  return true;
}

TfpAct tfp_action_select(const TfpActionSample *s, const TfpActionMapping *mapping) { /* :122-138 */
  if (s->ads_kind == TFP_ACTION_ADS_TRUE || (s->ads_kind == TFP_ACTION_ADS_NUMBER && s->ads >= 1)) return TFP_ACT_ADS; /* :123 */
  TfpAct pose = stance(s);                                           /* :124 */
  if (pose == TFP_ACT_SLIDE || pose == TFP_ACT_DIVE) return pose;    /* :125 */
  if (s->reload) return TFP_ACT_RELOAD;                              /* :127 */
  if (s->shoulder_aim) return TFP_ACT_AIM;                           /* :128 */
  if (s->sprint) return TFP_ACT_SPRINT;                              /* :129 */
  if (pose != TFP_ACT_NONE) return pose;                             /* :130 */
  if (mapping && s->action_resource && s->action_verified != TFP_VERIFIED_FALSY) { /* :132-136 */
    char states[40], l1[16] = "nil", l3[16] = "nil";                 /* tostring(sample.l1)..':'..tostring(sample.l3) */
    if (s->has_l1) snprintf(l1, sizeof l1, "%d", s->l1);
    if (s->has_l3) snprintf(l3, sizeof l3, "%d", s->l3);
    snprintf(states, sizeof states, "%s:%s", l1, l3);
    for (size_t i = 0; i < mapping->count; i++) {
      const TfpActionMapRow *row = mapping->rows + i;
      if (row->resource && row->states && !strcmp(row->resource, s->action_resource) && !strcmp(row->states, states)) {
        TfpAct action = tfp_action_from_name(row->action);
        if (action != TFP_ACT_NONE) return action;
        break;
      }
    }
  }
  return TFP_ACT_IDLE; /* :137 */
}
