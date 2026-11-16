/* src/action_select.c replayed against the retail action_placement.lua's own
 * answers (tests/vectors/action_placement_vectors.h), and the generated
 * catalogue data (src/action_placement_data.h) checked row by row against the
 * kit's `gear` as the retail run saw it. Numbers are doubles in both and must
 * be the same double; names, texts and flags must be equal. */
#include "action_placement_data.h"
#include "action_select.h"
#include "vectors/action_placement_vectors.h"
#include <stdio.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof *(a))
enum { MAX_ROWS = 1024, MAX_WEAPONS = 1024 };

static bool same(double c, double retail) {
  if (c != c || retail != retail) return c != c && retail != retail;
  return !memcmp(&c, &retail, sizeof c);
}
static bool same_text(const char *a, const char *b) { return (!a && !b) || (a && b && !strcmp(a, b)); }
static TfpActionPlacement placement(int index) {
  const APPlacement *p = ap_placements + index;
  TfpActionPlacement out = {p->v[0], p->v[1], p->v[2], p->v[3], p->v[4], p->v[5], p->present};
  return out;
}
static bool same_placement(const TfpActionPlacement *c, int index) {
  const APPlacement *p = ap_placements + index;
  const double v[6] = {c->x, c->y, c->z, c->yaw, c->pitch, c->roll};
  if (c->present != p->present) return false;
  for (int i = 0; i < 6; i++)
    if ((p->present & (1u << i)) && !same(v[i], p->v[i])) return false;
  return true;
}
static TfpActionProfile profile(const APProfile *p) {
  TfpActionProfile out;
  memset(&out, 0, sizeof out);
  out.id = p->id;
  out.has = p->has;
  for (int a = 0; a < TFP_ACT_COUNT; a++)
    if (p->has & (1u << a)) out.action[a] = placement(p->action[a]);
  return out;
}
static bool same_weapon(const TfpActionWeapon *c, const APWeapon *w) {
  if (strcmp(c->key, w->key) || c->ambiguous != (w->ambiguous != 0)) return false;
  if (w->ambiguous) return true;
  return same_text(c->name, w->name) && (int)c->family == w->family && c->kind == w->kind && c->has_item == (w->has_item != 0) &&
         (!w->has_item || c->item == w->item);
}

static TfpActionProfile profiles[MAX_ROWS], saved[MAX_ROWS];
static TfpActionLegacy legacies[MAX_ROWS];
static TfpActionGearRow gear_rows[MAX_ROWS];
static TfpActionMapRow map_rows[MAX_ROWS];
static TfpActionWeapon weapons[MAX_WEAPONS];

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(ACTION_PLACEMENT_VECTOR_COMMIT, commit) || strlen(ACTION_PLACEMENT_VECTOR_SHA256) != 64) {
    fprintf(stderr, "action placement vectors were cut from retail %s, expected %s\n", ACTION_PLACEMENT_VECTOR_COMMIT, commit);
    return 2;
  }
  if (strcmp(TFP_ACTION_GEAR_COMMIT, commit) || strcmp(TFP_ACTION_GEAR_SHA256, ACTION_PLACEMENT_CATALOGUE_SHA256)) {
    fprintf(stderr, "the catalogue data and the vectors were cut from different catalogue files\n");
    return 2;
  }
  unsigned failures = 0, numbers = 0;
  /* The module's constants: the action list with its labels, the observed set, the family names. */
  if (COUNT(ap_action_names) != TFP_ACT_COUNT || COUNT(ap_family_names) != TFP_FAMILY_COUNT) failures++;
  int observed = 0;
  for (int a = 0; a < TFP_ACT_COUNT && !failures; a++) {
    if (strcmp(tfp_action_name((TfpAct)a), ap_action_names[a]) || strcmp(tfp_action_label((TfpAct)a), ap_action_labels[a]) ||
        tfp_action_observed((TfpAct)a) != (ap_action_observed[a] != 0) || tfp_action_from_name(ap_action_names[a]) != (TfpAct)a ||
        tfp_action_from_name(ap_action_labels[a]) != TFP_ACT_NONE)
      failures++;
    observed += tfp_action_observed((TfpAct)a);
  }
  if (observed != ap_observed_count || tfp_action_name(TFP_ACT_NONE) || tfp_action_name(TFP_ACT_COUNT) || tfp_action_label(TFP_ACT_NONE) ||
      tfp_action_label(TFP_ACT_COUNT) || tfp_action_observed(TFP_ACT_NONE) || tfp_action_observed(TFP_ACT_COUNT) ||
      tfp_action_from_name(NULL) != TFP_ACT_NONE || tfp_action_from_name("") != TFP_ACT_NONE)
    failures++;
  for (int f = 0; f < TFP_FAMILY_COUNT; f++)
    if (!same_text(tfp_action_family_name((TfpActionFamily)f), ap_family_names[f])) failures++;
  if (tfp_action_family_name(TFP_FAMILY_COUNT)) failures++;
  if (failures) {
    fprintf(stderr, "the action list, labels, observed set or family names differ from retail\n");
    return 1;
  }

  TfpActionCatalogue catalogue = {weapons, MAX_WEAPONS, 0};
  unsigned count = COUNT(ap_vectors), tally[9] = {0};
  for (unsigned i = 0; i < count; i++) {
    const APVector *v = ap_vectors + i;
    const APConfig *cv = ap_configs + v->config;
    TfpActionConfig config = {profiles, (size_t)cv->profile_count, legacies, (size_t)cv->legacy_count, NULL, cv->family != 0};
    TfpActionPlacement base, value, out;
    char what[200] = "";
    bool bad = false;
    for (int k = 0; k < cv->profile_count; k++) profiles[k] = profile(ap_profiles + cv->first_profile + k);
    for (int k = 0; k < cv->legacy_count; k++) {
      legacies[k].id = ap_legacies[cv->first_legacy + k].id;
      legacies[k].placement = placement(ap_legacies[cv->first_legacy + k].placement);
    }
    if (cv->placement >= 0) { base = placement(cv->placement); config.placement = &base; }
    const TfpActionConfig *c = cv->nil ? NULL : &config;
    TfpAct action = (TfpAct)v->action;
    if (v->placement >= 0) value = placement(v->placement);
    const TfpActionPlacement *given = v->placement >= 0 ? &value : NULL;
    memset(&out, 0xa5, sizeof out);
    tally[v->op]++;
    switch (v->op) {
    case AP_GEAR: /* the generated data header is the kit's gear, row for row */
      if (v->count != TFP_ACTION_GEAR_COUNT) bad = true;
      for (int k = 0; k < v->count && !bad; k++) {
        const APGear *g = ap_gear + v->first + k;
        const TfpActionGearRow *r = tfp_action_gear + k;
        if (r->kind != g->kind || !same_text(r->name, g->name) || r->has_id != (g->has_id != 0) || r->id != g->id || !same_text(r->type, g->type)) {
          bad = true;
          snprintf(what, sizeof what, "catalogue data row %d differs from the kit's gear", k);
        }
      }
      break;
    case AP_CATALOGUE: {
      const TfpActionGearRow *rows = tfp_action_gear;
      size_t n = TFP_ACTION_GEAR_COUNT;
      if (v->catalogue >= 0) {
        const APCatalogue *cat = ap_catalogues + v->catalogue;
        n = (size_t)cat->count;
        for (int k = 0; k < cat->count; k++) {
          const APGear *g = ap_gear + cat->first + k;
          TfpActionGearRow r = {g->kind, g->name, g->id, g->has_id != 0, g->type};
          gear_rows[k] = r;
        }
        rows = cat->not_a_table ? NULL : gear_rows;
        if (cat->not_a_table) n = 5; /* a count means nothing without rows */
      }
      /* too small a table refuses and leaves it empty (retail has no limit) */
      if (v->count > 0) {
        TfpActionCatalogue small = {weapons, (size_t)v->count - 1, 77};
        if (tfp_action_configure_catalogue(&small, rows, n) || small.count != 0) { bad = true; snprintf(what, sizeof what, "small catalogue accepted"); }
      }
      if (!tfp_action_configure_catalogue(&catalogue, rows, n) || catalogue.count != (size_t)v->count) {
        bad = true;
        snprintf(what, sizeof what, "catalogue holds %u weapons, retail %d", (unsigned)catalogue.count, v->count);
      }
      for (int k = 0; k < v->count && !bad; k++) {
        const APWeapon *w = ap_weapons + v->first + k;
        const TfpActionWeapon *found = NULL;
        for (size_t j = 0; j < catalogue.count; j++)
          if (!strcmp(catalogue.weapons[j].key, w->key)) found = catalogue.weapons + j;
        if (!found || !same_weapon(found, w) || (tfp_action_weapon_info(&catalogue, w->key) != NULL) != !w->ambiguous) {
          bad = true;
          snprintf(what, sizeof what, "weapon %s differs", w->key);
        }
      }
      break;
    }
    case AP_INFO: {
      const TfpActionWeapon *info = tfp_action_weapon_info(&catalogue, v->key);
      if ((info != NULL) != (v->ok != 0) || (info && !same_weapon(info, ap_weapons + v->first))) { bad = true; snprintf(what, sizeof what, "weapon_info"); }
      break;
    }
    case AP_BASELINE: {
      bool ok = tfp_action_family_baseline(&catalogue, v->key, &out);
      if (ok != (v->ok != 0) || (ok && !same_placement(&out, v->answer))) { bad = true; snprintf(what, sizeof what, "family_baseline"); }
      numbers += ok ? 6 : 0;
      break;
    }
    case AP_VALID:
      if (tfp_action_valid(given) != (v->ok != 0)) { bad = true; snprintf(what, sizeof what, "valid"); }
      break;
    case AP_RESOLVE: {
      const char *error = "not cleared";
      bool is_explicit = !v->is_explicit;
      bool ok = tfp_action_resolve(c, &catalogue, v->key, action, &out, &is_explicit, &error);
      if (ok != (v->ok != 0) || !same_text(error ? error : "", ap_texts[v->text]) ||
          (ok && (!same_placement(&out, v->answer) || is_explicit != (v->is_explicit != 0)))) {
        bad = true;
        snprintf(what, sizeof what, "resolve: ok=%d \"%s\" x=%.17g y=%.17g z=%.17g present=%u explicit=%d", ok, error ? error : "", out.x,
                 out.y, out.z, out.present, is_explicit);
      }
      if (ok) { /* the optional answers may be left out */
        TfpActionPlacement again;
        if (!tfp_action_resolve(c, &catalogue, v->key, action, &again, NULL, NULL) || !same_placement(&again, v->answer)) bad = true;
      } else if (tfp_action_resolve(c, &catalogue, v->key, action, &out, NULL, NULL)) bad = true;
      numbers += ok ? 6 : 0;
      break;
    }
    case AP_DEFAULT:
      out = tfp_action_default_placement(c, &catalogue, v->key, action);
      if (!same_placement(&out, v->answer)) { bad = true; snprintf(what, sizeof what, "default_placement"); }
      numbers += 6;
      break;
    case AP_SAVED: {
      const char *error = "not cleared";
      size_t made = 99;
      bool ok = tfp_action_saved(c, v->key, action, given, v->all != 0, saved, MAX_ROWS, &made, &error);
      if (ok != (v->ok != 0) || !same_text(error ? error : "", ap_texts[v->text]) || (ok && made != (size_t)v->count) || (!ok && made != 0)) {
        bad = true;
        snprintf(what, sizeof what, "saved: ok=%d \"%s\" rows=%u, retail rows=%d", ok, error ? error : "", (unsigned)made, v->count);
      }
      for (int k = 0; k < v->count && ok && !bad; k++) { /* retail's answer is a table: rows are matched by id */
        const APProfile *want = ap_profiles + v->first + k;
        const TfpActionProfile *got = NULL;
        int matches = 0;
        for (size_t j = 0; j < made; j++)
          if (same_text(saved[j].id, want->id)) { got = saved + j; matches++; }
        if (matches != 1 || got->has != want->has) { bad = true; snprintf(what, sizeof what, "saved row %s", want->id); break; }
        for (int a = 0; a < TFP_ACT_COUNT; a++)
          if ((want->has & (1u << a)) && !same_placement(got->action + a, want->action[a])) { bad = true; snprintf(what, sizeof what, "saved %s/%s", want->id, tfp_action_name((TfpAct)a)); }
        numbers += 6;
      }
      if (ok && !bad) { /* one row too few refuses without a text; the optional answers may be left out */
        const char *small_error = "not cleared";
        size_t small = 99;
        if (tfp_action_saved(c, v->key, action, given, v->all != 0, saved, (size_t)v->count - 1, &small, &small_error) || small_error || small != 0 ||
            !tfp_action_saved(c, v->key, action, given, v->all != 0, saved, (size_t)v->count, NULL, NULL)) {
          bad = true;
          snprintf(what, sizeof what, "saved: capacity handling");
        }
      } else if (!ok && tfp_action_saved(c, v->key, action, given, v->all != 0, saved, MAX_ROWS, NULL, NULL)) bad = true;
      break;
    }
    case AP_SELECT: {
      const APSample *sv = ap_samples + v->sample;
      const APMapping *mv = ap_mappings + v->mapping;
      TfpActionSample s = {.ads_kind = (TfpActionAds)sv->ads_kind, .ads = sv->ads, .action_resource = sv->resource,
          .action_verified = (TfpActionVerified)sv->verified, .stance_flags = sv->flags_size >= 0 ? sv->flags : NULL,
          .stance_flags_size = sv->flags_size >= 0 ? (size_t)sv->flags_size : 0, .reload = sv->reload != 0,
          .shoulder_aim = sv->shoulder_aim != 0, .sprint = sv->sprint != 0, .has_l1 = sv->has_l1 != 0, .has_l3 = sv->has_l3 != 0,
          .l1 = sv->l1, .l3 = sv->l3};
      TfpActionMapping mapping = {map_rows, (size_t)mv->count};
      for (int k = 0; k < mv->count; k++) {
        const APMapRow *r = ap_map_rows + mv->first + k;
        TfpActionMapRow row = {r->resource, r->states, r->action};
        map_rows[k] = row;
      }
      TfpAct got = tfp_action_select(&s, mv->nil ? NULL : &mapping);
      if ((int)got != v->answer) { bad = true; snprintf(what, sizeof what, "select: C %s, retail %s", tfp_action_name(got), ap_action_names[v->answer]); }
      break;
    }
    default: bad = true;
    }
    if (bad && failures++ < 8) fprintf(stderr, "action placement vector %u (op %d, key %s): %s\n", i, v->op, v->key ? v->key : "nil", what);
  }
  if (failures) {
    fprintf(stderr, "%u of %u operations differ from retail\n", failures, count);
    return 1;
  }
  printf("Action placement: %u operations answered as retail's action_placement.lua (%s): %u select, %u resolve, %u saved, %u default, "
         "%u valid, %u catalogues, %u weapon lookups; %u catalogue rows as the kit's; %u doubles equal bit for bit\n",
         count, ACTION_PLACEMENT_VECTOR_COMMIT, tally[AP_SELECT], tally[AP_RESOLVE], tally[AP_SAVED], tally[AP_DEFAULT], tally[AP_VALID],
         tally[AP_CATALOGUE], tally[AP_INFO] + tally[AP_BASELINE], (unsigned)TFP_ACTION_GEAR_COUNT, numbers);
  return 0;
}
