/* All effect policy is native C. The engine's existing SDK functions are
 * invoked through its Lua C API; no mod Lua source or C++ helper is loaded. */
#include "effects.h"
#include "feature_contract.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static const char *const settings[] = {"dof_enabled", "vignette_enabled"};
static const uint32_t keys[] = {0x06f1d0d3, 0x72e8f18c};
static bool cached(const FpMemory *m, unsigned k, bool *value) {
  uintptr_t table = 0, rows = 0;
  uint32_t count = 0;
  if (!fp_pointer(m, m->exe + 0x1a102b8, &table) || !fp_u32(m, table, &count) ||
      !count || count > 4096 || !fp_pointer(m, table + 8, &rows))
    return false;
  unsigned lo = 0, hi = count;
  while (lo < hi) {
    unsigned mid = lo + (hi - lo) / 2;
    uint32_t key = 0;
    if (!fp_u32(m, rows + (uintptr_t)mid * 24 + 16, &key))
      return false;
    if (key < keys[k])
      lo = mid + 1;
    else if (key > keys[k])
      hi = mid;
    else {
      uint8_t row[24], again[24];
      uint32_t type = 0, observed_key = 0;
      uintptr_t after = 0;
      uint32_t final_count = 0;
      if (!fp_read(m, rows + (uintptr_t)mid * 24, row, 24) ||
          !fp_read(m, rows + (uintptr_t)mid * 24, again, 24) ||
          memcmp(row, again, 24) ||
          !fp_pointer(m, m->exe + 0x1a102b8, &after) || after != table ||
          !fp_u32(m, table, &final_count) || final_count != count ||
          !fp_pointer(m, table + 8, &after) || after != rows)
        return false;
      memcpy(&type, row, 4);
      memcpy(&observed_key, row + 16, 4);
      if (type != 4 || observed_key != keys[k] || row[8] > 1)
        return false;
      *value = row[8] != 0;
      return true;
    }
  }
  return false;
}
static bool setting_read(unsigned k, bool *value) {
  EngRef v = 0;
  bool ok = eng_call("Application.render_setting", "s>r", settings[k], &v) &&
            eng_type(v) == 1 && eng_toboolean(v, value);
  eng_unref(&v);
  return ok;
}
static bool setting_command(FpRenderSetting *r, unsigned k, bool value,
                            uint64_t now) {
  r->pending = true;
  r->restoring = value;
  r->next_poll = now + 500;
  EngRef result = 0;
  bool ok = eng_call("Application.set_render_setting", "ss>R", settings[k],
                     value ? "true" : "false", &result);
  if (ok && eng_len(result) > 0) {
    EngRef v = 0;
    bool truth = true;
    eng_index(result, 1, &v);
    if (eng_type(v) == 1 && eng_toboolean(v, &truth) && !truth)
      ok = false;
    eng_unref(&v);
  }
  eng_unref(&result);
  r->writes++;
  return ok;
}
static bool setting_update(FpRenderSetting *r, const FpMemory *m, unsigned k,
                           bool wanted, uint64_t now) {
  if (!wanted && !r->held)
    return true;
  if (now < r->next_poll)
    return !r->held || wanted;
  r->next_poll = now + 500;
  bool value = false;
  bool monitor = wanted && r->observed && !r->pending;
  bool read = monitor ? cached(m, k, &value) : setting_read(k, &value);
  if (!read && monitor)
    read = setting_read(k, &value);
  if (!read)
    return false;
  r->observed = true;
  /* A preserved disabled setting has no lease yet. Fence through the SDK
   * before taking ownership when its cached value first becomes enabled. */
  if (monitor && wanted && value && !r->held && !setting_read(k, &value))
    return false;
  if (r->pending) {
    if (value != r->restoring)
      return false;
    r->pending = false;
    if (r->restoring) {
      r->held = false;
      r->restores++;
      return true;
    }
  }
  if (!wanted) {
    if (!value) {
      setting_command(r, k, true, now);
      return false;
    }
    r->held = false;
    return true;
  }
  if (value) {
    r->held = true;
    setting_command(r, k, false, now);
  }
  return true;
}
static bool instances(EngRef manager, EngRef entity, EngRef *out) {
  *out = 0;
  EngRef results = 0;
  if (!eng_call("RenderDataComponent.instances", "rr>R", manager, entity,
                &results))
    return false;
  int count = eng_len(results);
  if (count == 1) {
    EngRef first = 0;
    eng_index(results, 1, &first);
    if (eng_type(first) == 5) {
      eng_unref(&results);
      results = first;
      count = eng_len(results);
    } else
      eng_unref(&first);
  }
  if (count > 64) {
    eng_unref(&results);
    return false;
  }
  *out = results;
  return true;
}
/* Unknown reads retain ownership; only a positive retirement drops a lease. */
static int valid(FpEffects *e, FpVignetteRow *r) {
  int alive = 0;
  if (!eng_call("EntityManager.alive", "r>b", r->entity, &alive))
    return -1;
  if (!alive)
    return 0;
  EngRef ids = 0;
  if (!instances(e->manager, r->entity, &ids))
    return -1;
  bool found = false;
  for (int i = 1; i <= eng_len(ids); i++) {
    EngRef id = 0;
    eng_index(ids, i, &id);
    if (eng_same(id, r->id))
      found = true;
    eng_unref(&id);
  }
  eng_unref(&ids);
  if (!found)
    return 0;
  char mapping[128];
  if (!eng_call("RenderDataComponent.get_property", "rrrp>s", e->manager,
                r->entity, r->id, "shading_environment_mapping", mapping,
                sizeof mapping))
    return -1;
  return !strcmp(
      mapping,
      "core/stingray_renderer/shading_environment_components/vignette");
}
static bool property(FpEffects *e, FpVignetteRow *r, bool *out) {
  EngRef v = 0;
  bool ok = eng_call("RenderDataComponent.get_property", "rrrp>r", e->manager,
                     r->entity, r->id, "vignette_enabled", &v) &&
            eng_type(v) == 1 && eng_toboolean(v, out);
  eng_unref(&v);
  return ok;
}
static bool write_property(FpEffects *e, FpVignetteRow *r, bool value) {
  EngRef result = 0;
  if (valid(e, r) != 1 ||
      !eng_call("RenderDataComponent.set_property", "rrrpb>R", e->manager,
                r->entity, r->id, "vignette_enabled", value ? 1 : 0, &result))
    return false;
  eng_unref(&result);
  bool readback = false;
  return property(e, r, &readback) && readback == value;
}
static void drop(FpEffects *e) {
  for (int i = 0; i < e->count; i++) {
    eng_unref(&e->rows[i].entity);
    eng_unref(&e->rows[i].id);
  }
  e->count = e->scan = 0;
  eng_unref(&e->entities);
  eng_unref(&e->manager);
  eng_unref(&e->world);
  e->next_scan = 0;
}
void fp_effects_init(FpEffects *e) { memset(e, 0, sizeof *e); }
bool fp_effects_update(FpEffects *e, const FpMemory *m, bool wanted,
                       uint64_t now) {
  bool clean = true;
  bool engine = eng_ready();
  if (e->generation != eng_state_generation()) {
    drop(e);
    e->generation = eng_state_generation();
  }
  if (!engine) {
    snprintf(e->status, sizeof e->status, "Waiting for engine effect SDK");
    return !e->setting[0].held && !e->setting[1].held;
  }
  if (!fp_feature_contract(m, FP_FEATURE_EFFECTS) ||
      eng_cfunction("Application.render_setting") != m->exe + 0x3fa3d0 ||
      eng_cfunction("Application.set_render_setting") != m->exe + 0x3fa120) {
    snprintf(e->status, sizeof e->status, "Renderer settings binding differs");
    return !e->setting[0].held && !e->setting[1].held;
  }
  for (unsigned k = 0; k < 2; k++)
    if (!setting_update(e->setting + k, m, k, wanted, now))
      clean = false;
  EngRef world = 0;
  if (!eng_call("Application.main_world", ">r", &world) || !world)
    return false;
  if (e->world && !eng_same(world, e->world))
    drop(e);
  if (!wanted) {
    eng_unref(&world);
    for (int i = 0; i < e->count; i++)
      if (e->rows[i].held) {
        FpVignetteRow *r = e->rows + i;
        int state = valid(e, r);
        if (state < 0) {
          clean = false;
          continue;
        }
        if (state == 1) {
          bool value = false;
          if (!property(e, r, &value) ||
              (!value && !write_property(e, r, true))) {
            clean = false;
            continue;
          }
        }
        r->held = false;
        e->restores++;
      }
    if (clean) {
      drop(e);
      snprintf(e->status, sizeof e->status, "Effects restored");
    }
    return clean;
  }
  if (!e->world) {
    e->world = world;
    world = 0;
    if (!eng_call("EntityManager.render_data_component", "r>r", e->world,
                  &e->manager) ||
        !e->manager) {
      eng_unref(&e->world);
      return true;
    }
  }
  eng_unref(&world);
  if (now >= e->next_scan && !e->entities) {
    if (eng_call("World.entities", "r>r", e->world, &e->entities) &&
        eng_len(e->entities) <= 32768) {
      e->scan = 1;
      e->next_scan = now + 1000;
    } else
      eng_unref(&e->entities);
  }
  for (unsigned n = 0; n < 96 && e->entities; n++, e->scan++) {
    if (e->scan > eng_len(e->entities)) {
      eng_unref(&e->entities);
      break;
    }
    EngRef entity = 0, ids = 0;
    if (!eng_index(e->entities, e->scan, &entity) || !entity)
      continue;
    if (instances(e->manager, entity, &ids))
      for (int j = 1; j <= eng_len(ids); j++) {
        EngRef id = 0;
        double number = 0;
        eng_index(ids, j, &id);
        if (eng_tonumber(id, &number) && isfinite(number) &&
            fmod(number + 4294967296.0, 4294967296.0) == 0x35b0f6c2) {
          bool known = false;
          for (int i = 0; i < e->count; i++)
            if (eng_same(entity, e->rows[i].entity) &&
                eng_same(id, e->rows[i].id))
              known = true;
          FpVignetteRow row = {entity, id, false};
          if (!known && e->count < 32 && valid(e, &row) == 1) {
            e->rows[e->count++] =
                (FpVignetteRow){eng_copy(entity), eng_copy(id), false};
          }
        }
        eng_unref(&id);
      }
    eng_unref(&ids);
    eng_unref(&entity);
  }
  if (now >= e->next_poll) {
    e->next_poll = now + 100;
    for (int i = e->count - 1; i >= 0; i--) {
      FpVignetteRow *r = e->rows + i;
      int state = valid(e, r);
      if (state < 0)
        continue;
      if (state == 0) {
        eng_unref(&r->entity);
        eng_unref(&r->id);
        e->rows[i] = e->rows[--e->count];
        continue;
      }
      bool value = false;
      if (property(e, r, &value) && value) {
        r->held = true;
        if (write_property(e, r, false))
          e->writes++;
      }
    }
  }
  snprintf(e->status, sizeof e->status, "DOF %s; vignette profiles %d",
           e->setting[0].held && !e->setting[0].pending ? "off" : "pending",
           e->count);
  return true;
}
