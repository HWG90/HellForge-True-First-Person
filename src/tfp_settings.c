/* See tfp_settings.h. Three parts: a strict JSON reader, the store, and the two menu answers that
 * are pure arithmetic on stored values. Line references "menu:N" are retail menu.lua, "fp:N" retail
 * src/first_person.lua, both at 0.5.41-smooth.66 (1d0d453). */
#include "tfp_settings.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== names */
static const char *const field_names[TFP_SET_COUNT] = {
#define NAME(name, kind, has_default) #name,
  TFP_SETTINGS_FIELDS(NAME)
#undef NAME
};
static const TfpSettingsKind field_kinds[TFP_SET_COUNT] = {
#define KIND(name, kind, has_default) TFP_SETTINGS_##kind,
  TFP_SETTINGS_FIELDS(KIND)
#undef KIND
};
static const char *const menu_names[TFP_MENU_COUNT] = {
#define NAME(name, is_bool, value) #name,
  TFP_SETTINGS_MENU(NAME)
#undef NAME
};
static const TfpSettingsMenuValue menu_defaults[TFP_MENU_COUNT] = {
#define VALUE(name, is_bool, value) {is_bool, (is_bool) && (value), (is_bool) ? 0 : (value)},
  TFP_SETTINGS_MENU(VALUE)
#undef VALUE
};
static const TfpSettingsLockedInfo locked_info[TFP_LOCKED_COUNT] = {
#define INFO(name, is_bool, value, least, most, step) {#name, is_bool, value, least, most, step},
  TFP_SETTINGS_LOCKED(INFO)
#undef INFO
};
const TfpSettingsLockedInfo *tfp_settings_locked_info(TfpSettingsLockedId id) { return (unsigned)id < TFP_LOCKED_COUNT ? &locked_info[id] : NULL; }
int tfp_settings_locked(const char *name) {
  for (int i = 0; name && i < TFP_LOCKED_COUNT; i++) if (!strcmp(name, locked_info[i].name)) return i;
  return -1;
}
static void locked_defaults(TfpSettings *s) {
  for (int i = 0; i < TFP_LOCKED_COUNT; i++)
    s->locked[i] = (TfpSettingsMenuValue){locked_info[i].is_bool, locked_info[i].is_bool && locked_info[i].value != 0, locked_info[i].is_bool ? 0 : locked_info[i].value};
}
const char *tfp_settings_field_name(TfpSettingsField field) { return (unsigned)field < TFP_SET_COUNT ? field_names[field] : NULL; }
TfpSettingsKind tfp_settings_field_kind(TfpSettingsField field) { return field_kinds[field]; }
int tfp_settings_field(const char *name) {
  for (int i = 0; name && i < TFP_SET_COUNT; i++) if (!strcmp(name, field_names[i])) return i;
  return -1;
}
const char *tfp_settings_menu_name(TfpSettingsMenuId id) { return (unsigned)id < TFP_MENU_COUNT ? menu_names[id] : NULL; }
int tfp_settings_menu(const char *name) {
  for (int i = 0; name && i < TFP_MENU_COUNT; i++) if (!strcmp(name, menu_names[i])) return i;
  return -1;
}
static const char *const axis_names[6] = {"x", "y", "z", "yaw", "pitch", "roll"};
static double *axis_of(TfpActionPlacement *p, int axis) {
  double *const at[6] = {&p->x, &p->y, &p->z, &p->yaw, &p->pitch, &p->roll};
  return at[axis];
}

/* ================================================================== JSON (RFC 8259, strict) */
typedef enum JType { J_NULL, J_FALSE, J_TRUE, J_NUMBER, J_STRING, J_ARRAY, J_OBJECT } JType;
typedef struct JNode {
  JType type;
  double number;
  const char *text, *key;  /* STRING: its bytes; a member's name. Both end in a NUL inside `strings`. */
  int child, next;         /* first element / member, next sibling; -1: none */
} JNode;
enum { J_DEPTH = 16, J_NODES = 1 << 16 };
typedef struct JDoc {
  const char *p, *end, *start;
  char *strings;
  size_t strings_used;
  JNode *nodes;
  int count, capacity;
  char why[160];
} JDoc;
static bool jfail(JDoc *d, const char *format, ...) {
  if (!d->why[0]) {
    char text[100];
    va_list args;
    size_t line = 1;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    for (const char *p = d->start; p < d->p && p < d->end; p++) line += *p == '\n';
    snprintf(d->why, sizeof d->why, "line %zu: %s", line, text);
  }
  return false;
}
static void jspace(JDoc *d) {
  while (d->p < d->end && (*d->p == ' ' || *d->p == '\t' || *d->p == '\n' || *d->p == '\r')) d->p++;
}
static int jnode(JDoc *d, JType type) {
  if (d->count == d->capacity) {
    int capacity = d->capacity ? d->capacity * 2 : 256;
    if (capacity > J_NODES) { jfail(d, "more than %d values", J_NODES); return -1; }
    JNode *grown = realloc(d->nodes, (size_t)capacity * sizeof *grown);
    if (!grown) { jfail(d, "out of memory"); return -1; }
    d->nodes = grown, d->capacity = capacity;
  }
  JNode *n = &d->nodes[d->count];
  memset(n, 0, sizeof *n);
  n->type = type, n->child = n->next = -1;
  return d->count++;
}
static int jhex(JDoc *d, unsigned *out) {
  unsigned value = 0;
  if (d->end - d->p < 4) return jfail(d, "a \\u escape is cut short");
  for (int i = 0; i < 4; i++) {
    char c = *d->p++;
    unsigned digit = c >= '0' && c <= '9' ? (unsigned)(c - '0') : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10)
                     : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10) : 16;
    if (digit > 15) return jfail(d, "a \\u escape needs four hex digits");
    value = value * 16 + digit;
  }
  *out = value;
  return true;
}
/* A string, decoded into d->strings (never longer than its source). */
static const char *jstring(JDoc *d) {
  char *out = d->strings + d->strings_used, *start = out;
  d->p++; /* the opening quote */
  for (;;) {
    if (d->p >= d->end) { jfail(d, "a string is not closed"); return NULL; }
    unsigned char c = (unsigned char)*d->p++;
    if (c == '"') break;
    if (c < 0x20) { jfail(d, "a control character inside a string"); return NULL; }
    if (c >= 0x80) { jfail(d, "a byte outside ASCII; the converter writes such characters as \\u escapes"); return NULL; }
    if (c != '\\') { *out++ = (char)c; continue; }
    if (d->p >= d->end) { jfail(d, "a string is not closed"); return NULL; }
    c = (unsigned char)*d->p++;
    unsigned code;
    switch (c) {
      case '"': case '\\': case '/': *out++ = (char)c; continue;
      case 'b': *out++ = '\b'; continue;
      case 'f': *out++ = '\f'; continue;
      case 'n': *out++ = '\n'; continue;
      case 'r': *out++ = '\r'; continue;
      case 't': *out++ = '\t'; continue;
      case 'u': break;
      default: jfail(d, "an unknown escape \\%c", c); return NULL;
    }
    if (!jhex(d, &code)) return NULL;
    if (code >= 0xDC00 && code <= 0xDFFF) { jfail(d, "a lone low surrogate"); return NULL; }
    if (code >= 0xD800 && code <= 0xDBFF) {
      unsigned low;
      if (d->end - d->p < 2 || d->p[0] != '\\' || d->p[1] != 'u') { jfail(d, "a lone high surrogate"); return NULL; }
      d->p += 2;
      if (!jhex(d, &low)) return NULL;
      if (low < 0xDC00 || low > 0xDFFF) { jfail(d, "a lone high surrogate"); return NULL; }
      code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
    }
    if (!code) { jfail(d, "a NUL character inside a string"); return NULL; }
    if (code < 0x80) *out++ = (char)code;
    else if (code < 0x800) *out++ = (char)(0xC0 | code >> 6), *out++ = (char)(0x80 | (code & 63));
    else if (code < 0x10000) *out++ = (char)(0xE0 | code >> 12), *out++ = (char)(0x80 | (code >> 6 & 63)), *out++ = (char)(0x80 | (code & 63));
    else *out++ = (char)(0xF0 | code >> 18), *out++ = (char)(0x80 | (code >> 12 & 63)), *out++ = (char)(0x80 | (code >> 6 & 63)),
         *out++ = (char)(0x80 | (code & 63));
  }
  *out++ = 0;
  d->strings_used = (size_t)(out - d->strings);
  return start;
}
static bool jdigit(char c) { return c >= '0' && c <= '9'; }
/* The number's text, already checked against the grammar, as a double. strtod first; when the C
 * library's locale does not take a '.', the digits are converted here (at most one unit in the last
 * place off, for a decimal comma locale only). */
static double jconvert(const char *text) {
  char *stop = NULL;
  double value = strtod(text, &stop);
  if (stop && !*stop) return value;
  const char *p = text;
  bool negative = *p == '-';
  double mantissa = 0;
  int exponent = 0, shift = 0;
  if (negative) p++;
  for (; jdigit(*p); p++) mantissa = mantissa * 10 + (*p - '0');
  if (*p == '.') for (p++; jdigit(*p); p++) mantissa = mantissa * 10 + (*p - '0'), shift--;
  if (*p == 'e' || *p == 'E') {
    bool down = false;
    p++;
    if (*p == '+' || *p == '-') down = *p++ == '-';
    for (; jdigit(*p); p++) if (exponent < 10000) exponent = exponent * 10 + (*p - '0');
    if (down) exponent = -exponent;
  }
  value = mantissa * pow(10, exponent + shift);
  return negative ? -value : value;
}
static bool jnumber(JDoc *d, double *out) {
  char text[64];
  const char *from = d->p;
  if (d->p < d->end && *d->p == '-') d->p++;
  if (d->p >= d->end || !jdigit(*d->p)) return jfail(d, "a number needs a digit");
  if (*d->p == '0') d->p++;
  else while (d->p < d->end && jdigit(*d->p)) d->p++;
  if (d->p < d->end && jdigit(*d->p)) return jfail(d, "a number must not start with 0");
  if (d->p < d->end && *d->p == '.') {
    d->p++;
    if (d->p >= d->end || !jdigit(*d->p)) return jfail(d, "a number needs a digit after the point");
    while (d->p < d->end && jdigit(*d->p)) d->p++;
  }
  if (d->p < d->end && (*d->p == 'e' || *d->p == 'E')) {
    d->p++;
    if (d->p < d->end && (*d->p == '+' || *d->p == '-')) d->p++;
    if (d->p >= d->end || !jdigit(*d->p)) return jfail(d, "a number needs a digit in its exponent");
    while (d->p < d->end && jdigit(*d->p)) d->p++;
  }
  size_t length = (size_t)(d->p - from);
  if (length >= sizeof text) return jfail(d, "a number of more than %d characters", (int)sizeof text - 1);
  memcpy(text, from, length);
  text[length] = 0;
  *out = jconvert(text);
  if (!(fabs(*out) <= 1.7976931348623157e308)) return jfail(d, "a number out of range");
  return true;
}
static bool jword(JDoc *d, const char *word) {
  size_t n = strlen(word);
  if ((size_t)(d->end - d->p) < n || memcmp(d->p, word, n)) return jfail(d, "an unknown word");
  d->p += n;
  return true;
}
static int jvalue(JDoc *d, int depth) {
  jspace(d);
  if (d->p >= d->end) { jfail(d, "a value is missing"); return -1; }
  if (depth > J_DEPTH) { jfail(d, "nested deeper than %d", J_DEPTH); return -1; }
  char c = *d->p;
  if (c == '{' || c == '[') {
    bool object = c == '{';
    int self = jnode(d, object ? J_OBJECT : J_ARRAY), last = -1;
    if (self < 0) return -1;
    d->p++;
    jspace(d);
    if (d->p < d->end && *d->p == (object ? '}' : ']')) { d->p++; return self; }
    for (;;) {
      const char *key = NULL;
      if (object) {
        jspace(d);
        if (d->p >= d->end || *d->p != '"') { jfail(d, "a member needs a name in quotes"); return -1; }
        key = jstring(d);
        if (!key) return -1;
        for (int at = d->nodes[self].child; at >= 0; at = d->nodes[at].next)
          if (!strcmp(d->nodes[at].key, key)) { jfail(d, "the name \"%.40s\" twice in one object", key); return -1; }
        jspace(d);
        if (d->p >= d->end || *d->p != ':') { jfail(d, "a ':' is missing after a name"); return -1; }
        d->p++;
      }
      int child = jvalue(d, depth + 1);
      if (child < 0) return -1;
      d->nodes[child].key = key;
      if (last < 0) d->nodes[self].child = child; else d->nodes[last].next = child;
      last = child;
      jspace(d);
      if (d->p < d->end && *d->p == ',') { d->p++; continue; }
      if (d->p < d->end && *d->p == (object ? '}' : ']')) { d->p++; return self; }
      jfail(d, object ? "a ',' or '}' is missing" : "a ',' or ']' is missing");
      return -1;
    }
  }
  if (c == '"') {
    int self = jnode(d, J_STRING);
    if (self < 0) return -1;
    const char *text = jstring(d);
    if (!text) return -1;
    d->nodes[self].text = text;
    return self;
  }
  if (c == '-' || jdigit(c)) {
    double number;
    int self = jnode(d, J_NUMBER);
    if (self < 0 || !jnumber(d, &number)) return -1;
    d->nodes[self].number = number;
    return self;
  }
  JType type = c == 't' ? J_TRUE : c == 'f' ? J_FALSE : J_NULL;
  if (!jword(d, c == 't' ? "true" : c == 'f' ? "false" : "null")) return -1;
  return jnode(d, type);
}
static void jfree(JDoc *d) { free(d->nodes); free(d->strings); d->nodes = NULL, d->strings = NULL; }
static bool jparse(JDoc *d, const char *json, size_t n) {
  memset(d, 0, sizeof *d);
  d->p = d->start = json, d->end = json + n;
  if (n >= 3 && !memcmp(json, "\xEF\xBB\xBF", 3)) return jfail(d, "a byte order mark; the file must be plain UTF-8");
  d->strings = malloc(n + 1);
  if (!d->strings) return jfail(d, "out of memory");
  if (jvalue(d, 0) < 0) return false;
  jspace(d);
  if (d->p < d->end) return jfail(d, "text after the end of the document");
  return true;
}
static const JNode *jmember(const JDoc *d, const JNode *object, const char *name) {
  for (int at = object->child; at >= 0; at = d->nodes[at].next)
    if (!strcmp(d->nodes[at].key, name)) return &d->nodes[at];
  return NULL;
}
static size_t jcount(const JDoc *d, const JNode *n) {
  size_t count = 0;
  for (int at = n->child; at >= 0; at = d->nodes[at].next) count++;
  return count;
}

/* ================================================================== the store */
static TfpSettingsValue *make(TfpSettings *s, TfpSettingsKind kind, size_t count) {
  if (count > TFP_SETTINGS_ROWS) return NULL;
  TfpSettingsValue *v = calloc(1, sizeof *v);
  if (!v) return NULL;
  v->kind = kind, v->count = count;
  bool keyed = kind == TFP_SETTINGS_PLACEMENTS || kind == TFP_SETTINGS_PROFILES || kind == TFP_SETTINGS_MARKERS || kind == TFP_SETTINGS_NEAR;
  size_t n = count ? count : 1;
  if (keyed) v->ids = calloc(n, sizeof *v->ids);
  if (kind == TFP_SETTINGS_PLACEMENTS) v->placements = calloc(n, sizeof *v->placements);
  if (kind == TFP_SETTINGS_PROFILES) v->profiles = calloc(n, sizeof *v->profiles);
  if (kind == TFP_SETTINGS_NEAR) v->near_clip = calloc(n, sizeof *v->near_clip);
  if (kind == TFP_SETTINGS_MAPPING) v->rows = calloc(n, sizeof *v->rows), v->texts = calloc(n, 3 * TFP_SETTINGS_ID);
  if ((keyed && !v->ids) || (kind == TFP_SETTINGS_PLACEMENTS && !v->placements) || (kind == TFP_SETTINGS_PROFILES && !v->profiles) ||
      (kind == TFP_SETTINGS_NEAR && !v->near_clip) || (kind == TFP_SETTINGS_MAPPING && (!v->rows || !v->texts))) {
    free(v->ids); free(v->placements); free(v->profiles); free(v->near_clip); free(v->rows); free(v->texts); free(v);
    return NULL;
  }
  v->mapping.rows = v->rows, v->mapping.count = count;
  v->next = s->made, s->made = v;
  return v;
}
static bool fits(const char *key) { return key && *key && strlen(key) < TFP_SETTINGS_ID; }
/* The order rows are kept in: by key, as bytes. order[i] is the source row that comes i-th. */
static int compare_keys(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static bool sorted_order(const char *const *keys, size_t count, size_t *order) {
  const char *copy[TFP_SETTINGS_ROWS];
  if (count > TFP_SETTINGS_ROWS) return false;
  for (size_t i = 0; i < count; i++) {
    if (!fits(keys[i])) return false;
    copy[i] = keys[i];
  }
  qsort(copy, count, sizeof *copy, compare_keys);
  for (size_t i = 0; i < count; i++) {
    if (i && !strcmp(copy[i], copy[i - 1])) return false; /* a key twice */
    for (size_t k = 0; k < count; k++) if (keys[k] == copy[i]) { order[i] = k; break; }
  }
  return true;
}
const TfpSettingsValue *tfp_settings_new_bool(TfpSettings *s, bool value) {
  TfpSettingsValue *v = make(s, TFP_SETTINGS_BOOL, 0);
  if (v) v->boolean = value;
  return v;
}
const TfpSettingsValue *tfp_settings_new_number(TfpSettings *s, double value) {
  TfpSettingsValue *v = make(s, TFP_SETTINGS_NUMBER, 0);
  if (v) v->number = value;
  return v;
}
const TfpSettingsValue *tfp_settings_new_placement(TfpSettings *s, const TfpActionPlacement *value) {
  TfpSettingsValue *v = make(s, TFP_SETTINGS_PLACEMENT, 0);
  if (v) v->placement = *value;
  return v;
}
const TfpSettingsValue *tfp_settings_new_placements(TfpSettings *s, const TfpActionLegacy *rows, size_t count) {
  const char *keys[TFP_SETTINGS_ROWS];
  size_t order[TFP_SETTINGS_ROWS];
  if (count > TFP_SETTINGS_ROWS) return NULL;
  for (size_t i = 0; i < count; i++) keys[i] = rows[i].id;
  if (!sorted_order(keys, count, order)) return NULL;
  TfpSettingsValue *v = make(s, TFP_SETTINGS_PLACEMENTS, count);
  for (size_t i = 0; v && i < count; i++) {
    strcpy(v->ids[i], rows[order[i]].id);
    v->placements[i].id = v->ids[i], v->placements[i].placement = rows[order[i]].placement;
  }
  return v;
}
const TfpSettingsValue *tfp_settings_new_profiles(TfpSettings *s, const TfpActionProfile *rows, size_t count) {
  const char *keys[TFP_SETTINGS_ROWS];
  size_t order[TFP_SETTINGS_ROWS];
  if (count > TFP_SETTINGS_ROWS) return NULL;
  for (size_t i = 0; i < count; i++) keys[i] = rows[i].id;
  if (!sorted_order(keys, count, order)) return NULL;
  TfpSettingsValue *v = make(s, TFP_SETTINGS_PROFILES, count);
  for (size_t i = 0; v && i < count; i++) {
    strcpy(v->ids[i], rows[order[i]].id);
    v->profiles[i] = rows[order[i]];
    v->profiles[i].id = v->ids[i];
  }
  return v;
}
static int compare_map_rows(const void *a, const void *b) {
  const TfpActionMapRow *x = a, *y = b;
  int first = strcmp(x->resource, y->resource);
  return first ? first : strcmp(x->states, y->states);
}
const TfpSettingsValue *tfp_settings_new_mapping(TfpSettings *s, const TfpActionMapRow *rows, size_t count) {
  for (size_t i = 0; i < count; i++) if (!fits(rows[i].resource) || !fits(rows[i].states) || !fits(rows[i].action)) return NULL;
  TfpSettingsValue *v = make(s, TFP_SETTINGS_MAPPING, count);
  if (!v) return NULL;
  for (size_t i = 0; i < count; i++) {
    char *at = v->texts + i * 3 * TFP_SETTINGS_ID;
    strcpy(at, rows[i].resource), strcpy(at + TFP_SETTINGS_ID, rows[i].states), strcpy(at + 2 * TFP_SETTINGS_ID, rows[i].action);
    v->rows[i] = (TfpActionMapRow){at, at + TFP_SETTINGS_ID, at + 2 * TFP_SETTINGS_ID};
  }
  qsort(v->rows, count, sizeof *v->rows, compare_map_rows);
  return v;
}
const TfpSettingsValue *tfp_settings_new_markers(TfpSettings *s, const char *const *keys, size_t count) {
  size_t order[TFP_SETTINGS_ROWS];
  if (!sorted_order(keys, count, order)) return NULL;
  TfpSettingsValue *v = make(s, TFP_SETTINGS_MARKERS, count);
  for (size_t i = 0; v && i < count; i++) strcpy(v->ids[i], keys[order[i]]);
  return v;
}
const TfpSettingsValue *tfp_settings_new_near(TfpSettings *s, const char *const *keys, const TfpSettingsNear *rows, size_t count) {
  size_t order[TFP_SETTINGS_ROWS];
  if (!sorted_order(keys, count, order)) return NULL;
  TfpSettingsValue *v = make(s, TFP_SETTINGS_NEAR, count);
  for (size_t i = 0; v && i < count; i++) strcpy(v->ids[i], keys[order[i]]), v->near_clip[i] = rows[order[i]];
  return v;
}
const TfpSettingsValue *tfp_settings_new_empty(TfpSettings *s, TfpSettingsKind kind) {
  if (kind == TFP_SETTINGS_BOOL || kind == TFP_SETTINGS_NUMBER) return NULL;
  return make(s, kind, 0);
}
const TfpSettingsValue *tfp_settings_owned(const TfpSettings *s, const void *address) {
  for (const TfpSettingsValue *v = s->made; v; v = v->next) if (v == address) return v;
  return NULL;
}
void tfp_settings_free(TfpSettings *s) {
  for (TfpSettingsValue *v = s->made, *next; v; v = next) {
    next = v->next;
    free(v->ids); free(v->placements); free(v->profiles); free(v->near_clip); free(v->rows); free(v->texts); free(v);
  }
  memset(s, 0, sizeof *s);
}
/* The defaults table of fp:9-11, one field of it; NULL for a field without a default (or out of memory). */
static const TfpSettingsValue *default_value(TfpSettings *s, TfpSettingsField field) {
  static const TfpActionPlacement placement = {.15, 0, -.12, 0, 0, 0, TFP_PLACE_XYZ}, head = {0, 0, .04, 0, 0, 0, TFP_PLACE_XYZ};
  switch (field) {
    case TFP_SET_placement: return tfp_settings_new_placement(s, &placement);
    case TFP_SET_fird_head_camera: return tfp_settings_new_placement(s, &head);
    case TFP_SET_motion_enabled: case TFP_SET_camera_kick_enabled: return tfp_settings_new_bool(s, true);
    case TFP_SET_motion_scale: return tfp_settings_new_number(s, 1);
    case TFP_SET_weapon_placements: case TFP_SET_action_placements: case TFP_SET_action_state_mapping: case TFP_SET_aim_markers:
      return tfp_settings_new_empty(s, field_kinds[field]);
    default: return NULL;
  }
}
static const bool has_default[TFP_SET_COUNT] = {
#define HAS(name, kind, has) has,
  TFP_SETTINGS_FIELDS(HAS)
#undef HAS
};
bool tfp_settings_defaults(TfpSettings *s) {
  memset(s, 0, sizeof *s);
  memcpy(s->menu, menu_defaults, sizeof s->menu);
  locked_defaults(s);
  for (int field = 0; field < TFP_SET_COUNT; field++) {
    if (!has_default[field]) continue;
    s->field[field] = default_value(s, (TfpSettingsField)field);
    if (!s->field[field]) { tfp_settings_free(s); return false; }
  }
  return true;
}

bool tfp_settings_set(TfpSettings *s, TfpSettingsField field, const TfpSettingsValue *value, const char **why) {
  if ((unsigned)field >= TFP_SET_COUNT) { if (why) *why = "Unknown first-person setting"; return false; }
  if (value && !tfp_settings_owned(s, value)) { if (why) *why = "Settings value was not made by the settings store"; return false; }
  if (value && value->kind != field_kinds[field]) { if (why) *why = "Settings value has a type this build cannot store for that setting"; return false; }
  /* fp:18-21: the write starts from a copy of the defaults with the saved table over it, so a key the
   * saved table lacked gets its default with the first write of any key. */
  const TfpSettingsValue *filled[TFP_SET_COUNT];
  for (int other = 0; other < TFP_SET_COUNT; other++) {
    filled[other] = s->field[other];
    if (filled[other] || !has_default[other] || other == (int)field) continue;
    filled[other] = default_value(s, (TfpSettingsField)other);
    if (!filled[other]) { if (why) *why = "Out of memory for the setting"; return false; }
  }
  if (s->field[field] != value) s->writes++;
  memcpy(s->field, filled, sizeof filled);
  s->field[field] = value;
  if (why) *why = NULL;
  return true;
}
bool tfp_settings_set_menu(TfpSettings *s, TfpSettingsMenuId id, bool is_bool, bool boolean, double number) {
  if ((unsigned)id >= TFP_MENU_COUNT || menu_defaults[id].is_bool != is_bool) return false;
  TfpSettingsMenuValue next = {is_bool, is_bool && boolean, is_bool ? 0 : number};
  if (s->menu[id].is_bool != next.is_bool || s->menu[id].boolean != next.boolean || s->menu[id].number != next.number) s->menu_writes++;
  s->menu[id] = next;
  return true;
}
bool tfp_settings_set_locked(TfpSettings *s, TfpSettingsLockedId id, bool is_bool, bool boolean, double number) {
  if ((unsigned)id >= TFP_LOCKED_COUNT || locked_info[id].is_bool != is_bool || (!is_bool && number != number)) return false;
  if (!is_bool) number = number < locked_info[id].min ? locked_info[id].min : number > locked_info[id].max ? locked_info[id].max : number;
  TfpSettingsMenuValue next = {is_bool, is_bool && boolean, is_bool ? 0 : number};
  if (s->locked[id].boolean != next.boolean || s->locked[id].number != next.number) s->locked_writes++;
  s->locked[id] = next;
  return true;
}
bool tfp_settings_locked_flag(const TfpSettings *s, TfpSettingsLockedId id) { return (unsigned)id < TFP_LOCKED_COUNT && s->locked[id].is_bool && s->locked[id].boolean; }
double tfp_settings_locked_number(const TfpSettings *s, TfpSettingsLockedId id) { return (unsigned)id < TFP_LOCKED_COUNT ? s->locked[id].number : 0; }
void tfp_settings_action_config(const TfpSettings *s, TfpActionConfig *out) {
  const TfpSettingsValue *profiles = s->field[TFP_SET_action_placements], *legacy = s->field[TFP_SET_weapon_placements],
                         *placement = s->field[TFP_SET_placement], *family = s->field[TFP_SET_family_placement_defaults];
  memset(out, 0, sizeof *out);
  if (profiles) out->profiles = profiles->profiles, out->profile_count = profiles->count;
  if (legacy) out->legacy = legacy->placements, out->legacy_count = legacy->count;
  out->placement = placement ? &placement->placement : NULL;
  out->family_placement_defaults = family && family->boolean;
}
const TfpActionMapping *tfp_settings_mapping(const TfpSettings *s) {
  const TfpSettingsValue *v = s->field[TFP_SET_action_state_mapping];
  return v ? &v->mapping : NULL;
}
int tfp_settings_row(const TfpSettingsValue *table, const char *key) {
  if (!table || !table->ids || !key) return -1;
  for (size_t i = 0; i < table->count; i++) if (!strcmp(table->ids[i], key)) return (int)i;
  return -1;
}
int tfp_settings_flag(const TfpSettings *s, TfpSettingsField field) {
  const TfpSettingsValue *v = (unsigned)field < TFP_SET_COUNT ? s->field[field] : NULL;
  return v && v->kind == TFP_SETTINGS_BOOL ? v->boolean : -1;
}
bool tfp_settings_menu_flag(const TfpSettings *s, TfpSettingsMenuId id) { return s->menu[id].is_bool && s->menu[id].boolean; }
double tfp_settings_menu_number(const TfpSettings *s, TfpSettingsMenuId id) { return s->menu[id].number; }

/* ================================================================== the menu's two pure answers */
void tfp_settings_motion_options(const TfpSettings *s, bool default_enabled, double default_scale, TfpMotionOptions *out) { /* menu:150-169 */
  static const struct { int setting; TfpSettingsMenuId control; double low, high, fallback; double preset[2]; } rows[5] = { /* menu:7-10, 21-27 */
    {TFP_MOTION_BOB_STRENGTH, TFP_MENU_weapon_bob_strength, 0, 300, 100, {.45, .8}},
    {TFP_MOTION_BOB_SPEED, TFP_MENU_weapon_bob_speed, 25, 200, 100, {1, 1}},
    {TFP_MOTION_SWAY_STRENGTH, TFP_MENU_weapon_sway_strength, 0, 300, 100, {.45, .8}},
    {TFP_MOTION_SWAY_SPEED, TFP_MENU_weapon_sway_speed, 25, 200, 100, {1, 1}},
    {TFP_MOTION_SPRINT_STRENGTH, TFP_MENU_sprint_motion_strength, 0, 200, 100, {.35, .55}},
  };
  static const struct { TfpSettingsMenuId control; int setting; } switches[3] = { /* menu:164-166 */
    {TFP_MENU_motion_bob_enabled, TFP_MOTION_BOB_STRENGTH}, {TFP_MENU_motion_sway_enabled, TFP_MOTION_SWAY_STRENGTH},
    {TFP_MENU_motion_sprint_enabled, TFP_MOTION_SPRINT_STRENGTH}};
  (void)default_enabled; /* a Boolean control always answers a Boolean (menu:154) */
  memset(out, 0, sizeof *out);
  out->enabled = tfp_settings_menu_flag(s, TFP_MENU_motion_enabled) ? TFP_MOTION_TRUE : TFP_MOTION_FALSE;
  double percent = tfp_settings_menu_number(s, TFP_MENU_motion_strength);
  if (percent != percent || percent < 0 || percent > 200) percent = default_scale * 100; /* menu:155 */
  out->scale = (TfpMotionNumber){TFP_MOTION_NUMBER, percent / 100};
  out->setting[TFP_MOTION_CAMERA_BOB_STRENGTH] = out->setting[TFP_MOTION_CAMERA_SWAY_STRENGTH] = (TfpMotionNumber){TFP_MOTION_NUMBER, 0};
  double preset = tfp_settings_menu_number(s, TFP_MENU_motion_preset);
  int chosen = preset == 2 ? 0 : preset == 3 ? 1 : -1; /* motion_presets[2], [3] */
  for (int i = 0; i < 5; i++) {
    double n = tfp_settings_menu_number(s, rows[i].control);
    if (n != n || n < rows[i].low || n > rows[i].high) n = rows[i].fallback; /* menu:160 */
    out->setting[rows[i].setting] = (TfpMotionNumber){TFP_MOTION_NUMBER, chosen >= 0 ? rows[i].preset[chosen] : n / 100};
  }
  for (int i = 0; i < 3; i++)
    if (!tfp_settings_menu_flag(s, switches[i].control)) out->setting[switches[i].setting].value = 0;
}
static bool clip_cm(double value) { return value == value && fmod(value, 1) == 0 && value >= 5 && value <= 40; } /* menu:11 */
static bool resource_key(const char *key) { /* menu:12 */
  if (!key || strlen(key) != 16) return false;
  for (int i = 0; i < 16; i++) {
    char c = key[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}
void tfp_settings_near_options(const TfpSettings *s, const char *key, double *hip, double *aim, bool *profile) {
  double cm = tfp_settings_menu_number(s, TFP_MENU_near_clip_cm); /* menu:144-149 */
  if (!clip_cm(cm)) cm = 15;
  double hip_cm = cm, aim_cm = tfp_settings_menu_flag(s, TFP_MENU_near_clip_aim) ? cm : 5;
  const TfpSettingsValue *all = s->field[TFP_SET_near_clip_profiles];
  int row = resource_key(key) ? tfp_settings_row(all, key) : -1; /* menu:132-143 */
  bool found = row >= 0 && all->near_clip[row].has_hip && all->near_clip[row].has_aim && clip_cm(all->near_clip[row].hip_cm) &&
               clip_cm(all->near_clip[row].aim_cm);
  if (found) hip_cm = all->near_clip[row].hip_cm, aim_cm = all->near_clip[row].aim_cm;
  *hip = hip_cm > 5 ? hip_cm / 100 : 0; /* menu:179 */
  *aim = aim_cm > 5 ? aim_cm / 100 : 0;
  if (profile) *profile = found;
}

/* ================================================================== loading */
typedef struct Loader { const JDoc *d; TfpSettings *s; char why[200]; } Loader;
static bool refuse(Loader *l, const char *path, const char *key, const char *what) {
  if (!l->why[0]) snprintf(l->why, sizeof l->why, "%s%s%.40s: %s", path, key ? "." : "", key ? key : "", what);
  return false;
}
static bool load_placement(Loader *l, const JNode *n, const char *path, const char *key, TfpActionPlacement *out) {
  memset(out, 0, sizeof *out);
  if (n->type != J_OBJECT) return refuse(l, path, key, "expected an object");
  for (int at = n->child; at >= 0; at = l->d->nodes[at].next) {
    const JNode *member = &l->d->nodes[at];
    int axis = 0;
    while (axis < 6 && strcmp(member->key, axis_names[axis])) axis++;
    if (axis == 6) return refuse(l, path, key, "a placement holds x, y, z, yaw, pitch, roll only");
    if (member->type != J_NUMBER) return refuse(l, path, key, "a placement field must be a number");
    *axis_of(out, axis) = member->number;
    out->present |= 1u << axis;
  }
  return true;
}
static bool load_keys(Loader *l, const JNode *n, const char *path, const char *keys[TFP_SETTINGS_ROWS], size_t *count) {
  if (n->type != J_OBJECT) return refuse(l, path, NULL, "expected an object");
  *count = jcount(l->d, n);
  if (*count > TFP_SETTINGS_ROWS) return refuse(l, path, NULL, "more rows than the store holds");
  size_t i = 0;
  for (int at = n->child; at >= 0; at = l->d->nodes[at].next) {
    keys[i] = l->d->nodes[at].key;
    if (!fits(keys[i])) return refuse(l, path, keys[i], "a key must be 1 to 31 bytes");
    i++;
  }
  return true;
}
static const TfpSettingsValue *load_field(Loader *l, TfpSettingsField field, const JNode *n) {
  const char *path = field_names[field], *keys[TFP_SETTINGS_ROWS];
  size_t count = 0, i = 0;
  const TfpSettingsValue *made = NULL;
  switch (field_kinds[field]) {
    case TFP_SETTINGS_BOOL:
      if (n->type != J_TRUE && n->type != J_FALSE) { refuse(l, path, NULL, "expected true or false"); return NULL; }
      made = tfp_settings_new_bool(l->s, n->type == J_TRUE);
      break;
    case TFP_SETTINGS_NUMBER:
      if (n->type != J_NUMBER) { refuse(l, path, NULL, "expected a number"); return NULL; }
      made = tfp_settings_new_number(l->s, n->number);
      break;
    case TFP_SETTINGS_PLACEMENT: {
      TfpActionPlacement p;
      if (!load_placement(l, n, path, NULL, &p)) return NULL;
      made = tfp_settings_new_placement(l->s, &p);
      break;
    }
    case TFP_SETTINGS_PLACEMENTS: {
      if (!load_keys(l, n, path, keys, &count)) return NULL;
      TfpActionLegacy *rows = calloc(count ? count : 1, sizeof *rows);
      if (!rows) break;
      bool ok = true;
      for (int at = n->child; ok && at >= 0; at = l->d->nodes[at].next, i++)
        rows[i].id = keys[i], ok = load_placement(l, &l->d->nodes[at], path, keys[i], &rows[i].placement);
      if (ok) made = tfp_settings_new_placements(l->s, rows, count);
      free(rows);
      if (!ok) return NULL;
      break;
    }
    case TFP_SETTINGS_PROFILES: {
      if (!load_keys(l, n, path, keys, &count)) return NULL;
      TfpActionProfile *rows = calloc(count ? count : 1, sizeof *rows);
      if (!rows) break;
      bool ok = true;
      for (int at = n->child; ok && at >= 0; at = l->d->nodes[at].next, i++) {
        const JNode *profile = &l->d->nodes[at];
        rows[i].id = keys[i];
        if (profile->type != J_OBJECT) { ok = refuse(l, path, keys[i], "expected an object of actions"); break; }
        for (int inner = profile->child; ok && inner >= 0; inner = l->d->nodes[inner].next) {
          TfpAct action = tfp_action_from_name(l->d->nodes[inner].key);
          if (action == TFP_ACT_NONE) ok = refuse(l, path, keys[i], "an action retail does not know");
          else ok = load_placement(l, &l->d->nodes[inner], path, keys[i], &rows[i].action[action]), rows[i].has |= 1u << action;
        }
      }
      if (ok) made = tfp_settings_new_profiles(l->s, rows, count);
      free(rows);
      if (!ok) return NULL;
      break;
    }
    case TFP_SETTINGS_MAPPING: {
      if (n->type != J_OBJECT) { refuse(l, path, NULL, "expected an object"); return NULL; }
      for (int at = n->child; at >= 0; at = l->d->nodes[at].next) {
        if (l->d->nodes[at].type != J_OBJECT) { refuse(l, path, l->d->nodes[at].key, "expected an object of states"); return NULL; }
        count += jcount(l->d, &l->d->nodes[at]);
      }
      if (count > TFP_SETTINGS_ROWS) { refuse(l, path, NULL, "more rows than the store holds"); return NULL; }
      TfpActionMapRow *rows = calloc(count ? count : 1, sizeof *rows);
      if (!rows) break;
      bool ok = true;
      for (int at = n->child; ok && at >= 0; at = l->d->nodes[at].next)
        for (int inner = l->d->nodes[at].child; ok && inner >= 0; inner = l->d->nodes[inner].next, i++) {
          const JNode *leaf = &l->d->nodes[inner];
          if (leaf->type != J_STRING) { ok = refuse(l, path, leaf->key, "expected an action name"); break; }
          rows[i] = (TfpActionMapRow){l->d->nodes[at].key, leaf->key, leaf->text};
          if (!fits(rows[i].resource) || !fits(rows[i].states) || !fits(rows[i].action)) ok = refuse(l, path, leaf->key, "a text must be 1 to 31 bytes");
        }
      if (ok) made = tfp_settings_new_mapping(l->s, rows, count);
      free(rows);
      if (!ok) return NULL;
      break;
    }
    case TFP_SETTINGS_MARKERS:
      if (!load_keys(l, n, path, keys, &count)) return NULL;
      for (int at = n->child; at >= 0; at = l->d->nodes[at].next)
        if (l->d->nodes[at].type != J_TRUE) { refuse(l, path, l->d->nodes[at].key, "expected true"); return NULL; }
      made = tfp_settings_new_markers(l->s, keys, count);
      break;
    case TFP_SETTINGS_NEAR: {
      if (!load_keys(l, n, path, keys, &count)) return NULL;
      TfpSettingsNear *rows = calloc(count ? count : 1, sizeof *rows);
      if (!rows) break;
      bool ok = true;
      for (int at = n->child; ok && at >= 0; at = l->d->nodes[at].next, i++) {
        const JNode *row = &l->d->nodes[at];
        if (row->type != J_OBJECT) { ok = refuse(l, path, keys[i], "expected an object"); break; }
        for (int inner = row->child; ok && inner >= 0; inner = l->d->nodes[inner].next) {
          const JNode *leaf = &l->d->nodes[inner];
          bool hip = !strcmp(leaf->key, "hip_cm");
          if (!hip && strcmp(leaf->key, "aim_cm")) ok = refuse(l, path, keys[i], "a clipping profile holds hip_cm and aim_cm only");
          else if (leaf->type != J_NUMBER) ok = refuse(l, path, keys[i], "a clipping distance must be a number");
          else if (hip) rows[i].has_hip = true, rows[i].hip_cm = leaf->number;
          else rows[i].has_aim = true, rows[i].aim_cm = leaf->number;
        }
      }
      if (ok) made = tfp_settings_new_near(l->s, keys, rows, count);
      free(rows);
      if (!ok) return NULL;
      break;
    }
  }
  if (!made) refuse(l, path, NULL, "out of memory");
  return made;
}
/* The locked view's options out of one JSON object. A name this build does not know is skipped and noted in
 * s->locked_skipped. A wrong type on a known name: strict refuses (nothing is taken, `why` says which), otherwise it is
 * skipped and noted too. The change counter is left alone: what was read is what the file holds. */
static void note_skipped(TfpSettings *s, const char *name) {
  size_t used = strlen(s->locked_skipped);
  snprintf(s->locked_skipped + used, sizeof s->locked_skipped - used, "%s%.40s", used ? ", " : "", name);
}
static bool load_locked_options(const JDoc *d, const JNode *object, TfpSettings *s, bool strict, uint32_t *named, char *why, size_t why_size) {
  uint32_t taken = 0;
  TfpSettingsMenuValue staged[TFP_LOCKED_COUNT];
  char skipped[sizeof s->locked_skipped];
  memcpy(staged, s->locked, sizeof staged);
  memcpy(skipped, s->locked_skipped, sizeof skipped);
  const bool has_put_away = jmember(d, object, "put_away") != NULL;
  for (int at = object->child; at >= 0; at = d->nodes[at].next) {
    const JNode *leaf = &d->nodes[at];
    /* The option put_away replaced: its stored value is put_away's, unless the file names put_away itself. */
    const bool old = !strcmp(leaf->key, TFP_SETTINGS_LOCKED_OLD_PUT_AWAY);
    const int id = old ? (int)TFP_LOCKED_put_away : tfp_settings_locked(leaf->key);
    if (id < 0) { note_skipped(s, leaf->key); continue; }
    const bool is_bool = leaf->type == J_TRUE || leaf->type == J_FALSE;
    if (is_bool != locked_info[id].is_bool || (!is_bool && leaf->type != J_NUMBER)) {
      if (!strict) { note_skipped(s, leaf->key); continue; }
      if (why && why_size) snprintf(why, why_size, "locked_view.%.40s: %s", leaf->key, locked_info[id].is_bool ? "expected true or false" : "expected a number");
      memcpy(s->locked_skipped, skipped, sizeof skipped);
      return false;
    }
    if (old && has_put_away) continue;
    double number = is_bool ? 0 : leaf->number;
    if (!is_bool) number = number < locked_info[id].min ? locked_info[id].min : number > locked_info[id].max ? locked_info[id].max : number;
    staged[id] = (TfpSettingsMenuValue){is_bool, leaf->type == J_TRUE, number};
    taken |= UINT32_C(1) << id;
  }
  memcpy(s->locked, staged, sizeof staged);
  if (named) *named = taken;
  return true;
}
static bool load_document(Loader *l) {
  const JDoc *d = l->d;
  const JNode *root = &d->nodes[0];
  if (root->type != J_OBJECT) return refuse(l, "settings", NULL, "the document must be an object");
  for (int at = root->child; at >= 0; at = d->nodes[at].next) {
    const char *key = d->nodes[at].key;
    if (strcmp(key, "format") && strcmp(key, "source") && strcmp(key, "first_person") && strcmp(key, "menu") && strcmp(key, "unread") &&
        strcmp(key, "locked_view"))
      return refuse(l, "settings", key, "an unknown member");
  }
  const JNode *format = jmember(d, root, "format"), *fields = jmember(d, root, "first_person"), *menu = jmember(d, root, "menu");
  if (!format || format->type != J_NUMBER || format->number != 1) return refuse(l, "format", NULL, "expected 1");
  if (!fields || fields->type != J_OBJECT) return refuse(l, "first_person", NULL, "expected an object");
  /* What the file does not name is nil, as in retail's table: the converter writes the defaults out. */
  memset(l->s->field, 0, sizeof l->s->field);
  for (int at = fields->child; at >= 0; at = d->nodes[at].next) {
    int field = tfp_settings_field(d->nodes[at].key);
    if (field < 0) return refuse(l, "first_person", d->nodes[at].key, "an unknown setting");
    const TfpSettingsValue *value = load_field(l, (TfpSettingsField)field, &d->nodes[at]);
    if (!value) return false;
    l->s->field[field] = value;
  }
  if (menu) {
    if (menu->type != J_OBJECT) return refuse(l, "menu", NULL, "expected an object");
    for (int at = menu->child; at >= 0; at = d->nodes[at].next) {
      const JNode *leaf = &d->nodes[at];
      int id = tfp_settings_menu(leaf->key);
      if (id < 0) return refuse(l, "menu", leaf->key, "an unknown control");
      bool is_bool = leaf->type == J_TRUE || leaf->type == J_FALSE;
      if ((!is_bool && leaf->type != J_NUMBER) || !tfp_settings_set_menu(l->s, (TfpSettingsMenuId)id, is_bool, leaf->type == J_TRUE, leaf->number))
        return refuse(l, "menu", leaf->key, menu_defaults[id].is_bool ? "expected true or false" : "expected a number");
    }
  }
  /* The locked view's options as 0.6.0-feel.20 to feel.24 wrote them here. They are taken (locked_view.json, read
   * after this, wins) and nothing in them can refuse this file. */
  const JNode *locked = jmember(d, root, "locked_view");
  if (locked) {
    l->s->locked_in_settings = true;
    if (locked->type == J_OBJECT) load_locked_options(d, locked, l->s, false, &l->s->locked_named, NULL, 0);
    else note_skipped(l->s, "locked_view (not an object)");
  }
  return true;
}
bool tfp_settings_load(TfpSettings *s, const char *json, size_t n, char *why, size_t why_size) {
  JDoc d;
  Loader l = {&d, s, ""};
  if (why && why_size) *why = 0;
  if (!tfp_settings_defaults(s)) {
    if (why && why_size) snprintf(why, why_size, "out of memory");
    return false;
  }
  memset(&d, 0, sizeof d);
  bool ok = false;
  if (!json || !n) snprintf(l.why, sizeof l.why, "no settings file");
  else if (n > TFP_SETTINGS_MAX) snprintf(l.why, sizeof l.why, "the settings file is larger than %d bytes", TFP_SETTINGS_MAX);
  else if (!jparse(&d, json, n)) snprintf(l.why, sizeof l.why, "not JSON: %s", d.why);
  else ok = load_document(&l);
  jfree(&d);
  if (ok) { s->from_file = true; return true; }
  tfp_settings_free(s);
  if (why && why_size) snprintf(why, why_size, "%s", l.why);
  if (!tfp_settings_defaults(s) && why && why_size) snprintf(why, why_size, "out of memory");
  return false;
}
bool tfp_settings_load_locked(TfpSettings *s, const char *json, size_t n, char *why, size_t why_size) {
  JDoc d;
  char problem[200] = "";
  bool ok = false;
  if (why && why_size) *why = 0;
  s->locked_skipped[0] = 0;
  memset(&d, 0, sizeof d);
  if (!json || !n) snprintf(problem, sizeof problem, "no file");
  else if (n > TFP_SETTINGS_MAX) snprintf(problem, sizeof problem, "the file is larger than %d bytes", TFP_SETTINGS_MAX);
  else if (!jparse(&d, json, n)) snprintf(problem, sizeof problem, "not JSON: %s", d.why);
  else if (d.nodes[0].type != J_OBJECT) snprintf(problem, sizeof problem, "the document must be an object");
  else {
    const JNode *options = jmember(&d, &d.nodes[0], "locked_view");
    if (!options || options->type != J_OBJECT) snprintf(problem, sizeof problem, "locked_view: expected an object");
    else ok = load_locked_options(&d, options, s, true, NULL, problem, sizeof problem);
    /* Any other member (a later build's) is left alone: "format" is written for such a build to tell by. */
    const JNode *taken = ok ? jmember(&d, &d.nodes[0], "taken_from_settings") : NULL;
    s->locked_taken = false, s->locked_taken_named = 0;
    if (taken && taken->type == J_OBJECT) {
      TfpSettings scratch = *s; /* only its locked options and its notes are written, and neither is kept */
      load_locked_options(&d, taken, &scratch, false, &s->locked_taken_named, NULL, 0);
      memcpy(s->locked_taken_values, scratch.locked, sizeof s->locked_taken_values);
      s->locked_taken = true;
    }
  }
  jfree(&d);
  if (!ok && why && why_size) snprintf(why, why_size, "%s", problem);
  return ok;
}

/* ================================================================== writing */
typedef struct Writer { char *out; size_t size, used; } Writer;
static void put(Writer *w, const char *text) {
  for (; *text; text++, w->used++) if (w->out && w->used + 1 < w->size) w->out[w->used] = *text;
}
static void put_number(Writer *w, double value) { /* the shortest text that reads back as the same double */
  char text[40];
  if (value == floor(value) && fabs(value) < 1e15) snprintf(text, sizeof text, "%.0f", value == 0 ? 0.0 : value);
  else for (int digits = 15; digits <= 17; digits++) {
    snprintf(text, sizeof text, "%.*g", digits, value);
    if (strtod(text, NULL) == value) break;
  }
  for (char *p = text; *p; p++) if (*p == ',') *p = '.'; /* a decimal comma locale */
  put(w, text);
}
static void put_string(Writer *w, const char *text) {
  put(w, "\"");
  for (; *text; text++) {
    unsigned char c = (unsigned char)*text;
    char one[8];
    if (c == '"' || c == '\\') snprintf(one, sizeof one, "\\%c", c);
    else if (c < 0x20) snprintf(one, sizeof one, "\\u%04x", c);
    else one[0] = (char)c, one[1] = 0;
    put(w, one);
  }
  put(w, "\"");
}
static void put_placement(Writer *w, const TfpActionPlacement *p) {
  static const int order[6] = {4, 5, 0, 1, 3, 2}; /* pitch, roll, x, y, yaw, z: sorted names */
  TfpActionPlacement copy = *p;
  bool first = true;
  put(w, "{");
  for (int i = 0; i < 6; i++) {
    int axis = order[i];
    if (!(p->present & 1u << axis)) continue;
    if (!first) put(w, ", ");
    first = false;
    put_string(w, axis_names[axis]);
    put(w, ": ");
    put_number(w, *axis_of(&copy, axis));
  }
  put(w, "}");
}
static void put_value(Writer *w, const TfpSettingsValue *v) {
  static const int action_order[TFP_ACT_COUNT] = {TFP_ACT_ADS, TFP_ACT_AIM, TFP_ACT_CROUCH, TFP_ACT_DIVE, TFP_ACT_IDLE, TFP_ACT_PRONE,
                                                  TFP_ACT_RELOAD, TFP_ACT_SLIDE, TFP_ACT_SPRINT}; /* sorted names */
  switch (v->kind) {
    case TFP_SETTINGS_BOOL: put(w, v->boolean ? "true" : "false"); return;
    case TFP_SETTINGS_NUMBER: put_number(w, v->number); return;
    case TFP_SETTINGS_PLACEMENT: put_placement(w, &v->placement); return;
    default: break;
  }
  put(w, "{");
  if (v->kind == TFP_SETTINGS_MAPPING) {
    for (size_t i = 0; i < v->count; i++) {
      bool opens = !i || strcmp(v->rows[i].resource, v->rows[i - 1].resource);
      if (opens) { put(w, i ? "},\n   " : "\n   "); put_string(w, v->rows[i].resource); put(w, ": {"); }
      else put(w, ", ");
      put_string(w, v->rows[i].states); put(w, ": "); put_string(w, v->rows[i].action);
    }
    put(w, v->count ? "}\n  }" : "}");
    return;
  }
  for (size_t i = 0; i < v->count; i++) {
    put(w, i ? ",\n   " : "\n   ");
    put_string(w, v->ids[i]);
    put(w, ": ");
    if (v->kind == TFP_SETTINGS_MARKERS) put(w, "true");
    else if (v->kind == TFP_SETTINGS_PLACEMENTS) put_placement(w, &v->placements[i].placement);
    else if (v->kind == TFP_SETTINGS_NEAR) {
      const TfpSettingsNear *row = &v->near_clip[i];
      put(w, "{");
      if (row->has_aim) { put(w, "\"aim_cm\": "); put_number(w, row->aim_cm); }
      if (row->has_aim && row->has_hip) put(w, ", ");
      if (row->has_hip) { put(w, "\"hip_cm\": "); put_number(w, row->hip_cm); }
      put(w, "}");
    } else {
      bool first = true;
      put(w, "{");
      for (int k = 0; k < TFP_ACT_COUNT; k++) {
        int action = action_order[k];
        if (!(v->profiles[i].has & 1u << action)) continue;
        put(w, first ? "\n    " : ",\n    ");
        first = false;
        put_string(w, tfp_action_name((TfpAct)action));
        put(w, ": ");
        put_placement(w, &v->profiles[i].action[action]);
      }
      put(w, first ? "}" : "\n   }");
    }
  }
  put(w, v->count ? "\n  }" : "}");
}
static int compare_names(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
size_t tfp_settings_write(const TfpSettings *s, char *out, size_t size) {
  Writer w = {out, size, 0};
  const char *names[(int)TFP_SET_COUNT + (int)TFP_MENU_COUNT];
  bool first = true;
  put(&w, "{\n \"first_person\": {");
  memcpy(names, field_names, sizeof field_names);
  qsort(names, TFP_SET_COUNT, sizeof *names, compare_names);
  for (int i = 0; i < TFP_SET_COUNT; i++) {
    const TfpSettingsValue *v = s->field[tfp_settings_field(names[i])];
    if (!v) continue;
    put(&w, first ? "\n  " : ",\n  ");
    first = false;
    put_string(&w, names[i]);
    put(&w, ": ");
    put_value(&w, v);
  }
  put(&w, "\n },\n \"format\": 1,\n \"menu\": {");
  memcpy(names, menu_names, sizeof menu_names);
  qsort(names, TFP_MENU_COUNT, sizeof *names, compare_names);
  for (int i = 0; i < TFP_MENU_COUNT; i++) {
    const TfpSettingsMenuValue *v = &s->menu[tfp_settings_menu(names[i])];
    put(&w, i ? ",\n  " : "\n  ");
    put_string(&w, names[i]);
    put(&w, ": ");
    if (v->is_bool) put(&w, v->boolean ? "true" : "false"); else put_number(&w, v->number);
  }
  put(&w, "\n }\n}\n");
  if (out && size) out[w.used < size ? w.used : size - 1] = 0;
  return w.used;
}
/* The members of one object of locked options, sorted by name: those `named` has a bit for. */
static void put_locked_options(Writer *w, const TfpSettingsMenuValue *values, uint32_t named) {
  const char *names[TFP_LOCKED_COUNT];
  bool first = true;
  for (int i = 0; i < TFP_LOCKED_COUNT; i++) names[i] = locked_info[i].name;
  qsort(names, TFP_LOCKED_COUNT, sizeof *names, compare_names);
  for (int i = 0; i < TFP_LOCKED_COUNT; i++) {
    const int id = tfp_settings_locked(names[i]);
    const TfpSettingsMenuValue *v = &values[id];
    if (!(named >> id & 1)) continue;
    put(w, first ? "\n  " : ",\n  ");
    first = false;
    put_string(w, names[i]);
    put(w, ": ");
    if (v->is_bool) put(w, v->boolean ? "true" : "false"); else put_number(w, v->number);
  }
}
size_t tfp_settings_write_locked(const TfpSettings *s, char *out, size_t size) {
  Writer w = {out, size, 0};
  put(&w, "{\n \"format\": 1,\n \"locked_view\": {");
  put_locked_options(&w, s->locked, ~UINT32_C(0));
  if (s->locked_taken) {
    put(&w, "\n },\n \"taken_from_settings\": {");
    put_locked_options(&w, s->locked_taken_values, s->locked_taken_named);
  }
  put(&w, "\n }\n}\n");
  if (out && size) out[w.used < size ? w.used : size - 1] = 0;
  return w.used;
}
