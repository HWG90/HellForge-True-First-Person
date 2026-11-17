#!/usr/bin/env python3
"""Retail's own answers for the settings chain, as vectors for tests/wire_settings_test.c.

Two cases, each a settings.json made by tools/convert_settings.py and a list of operations whose
answers come from the unmodified retail files under LuaJIT (run_settings.lua: src/first_person.lua,
action_placement.lua, menu.lua):

  defaults   no saved settings at all
  user       import/retail-config-2026-11-16.cfg with the user's menu values
             (tests/vectors/retail-menu-first-person-2026-11-16.ini)
  synthetic  a made-up saved table for what the user's file does not hold (SYNTHETIC below)

Retail is given the RAW saved table; the C store is given the CONVERTED file. Equal answers on every
operation is the proof that the conversion keeps every placement and option.

Also checked here, against retail itself:
  - the converter's output for an empty input is exactly the facade's defaults table (first_person.lua:9-11);
  - the converter's menu defaults are the defaults the menu registers its controls with (menu.lua:343-418).

    python3 tools/retail_ref/gen_settings.py --retail "$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit" --out tests/vectors
    ... --check    compares with the header already there instead of writing it
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import convert_settings  # noqa: E402

RETAIL_COMMIT = "1d0d453"
# A made-up saved table for what the user's file does not hold: older per-weapon placements, a changed global
# placement, an action mapping, markers, clipping profiles retail ignores, placements out of bounds.
SYNTHETIC = {
    "placement": {"x": 0.2, "y": 0.01, "z": -0.2, "yaw": 5},
    "weapon_placements": {
        "__all": {"x": 0.11, "y": 0.01, "z": -0.11},
        "b2b69248b51ea9a8": {"x": 0.12, "y": 0, "z": -0.1, "pitch": 3},
        "0123456789abcdef": {"x": 0.9, "y": 0, "z": 0},
        "1a6b30e75da96e18": {"x": 0.1, "y": 0.1},
    },
    "action_placements": {
        "522bc5e41f0b314e": {"idle": {"x": 0.07, "y": 0.02, "z": -0.03, "yaw": 1, "pitch": 2, "roll": 3}, "aim": {"x": 0.6, "y": 0, "z": 0},
                             "prone": {"x": 0.01, "y": 0.02, "z": 0.03, "roll": 90.5}},
        "__all": {"sprint": {"x": 0.3, "y": 0.2, "z": -0.4, "pitch": 45}, "ads": {"x": 0.001, "y": 0, "z": -0.002}},
        "emptyrow00000000": {},
    },
    "action_state_mapping": {"4D1C334D294DFA97": {"12:34": "crouch", "1:2": "wave"}, "OTHER": {"5:6": "prone"}},
    "aim_markers": {"5663fbf6d81eee27": True},
    "show_aim_markers": False,
    "family_placement_defaults": True,
    "near_clip_profiles": {
        "5663fbf6d81eee27": {"hip_cm": 12, "aim_cm": 8},
        "b2b69248b51ea9a8": {"hip_cm": 12.5, "aim_cm": 8},
        "0123456789abcdef": {"hip_cm": 41, "aim_cm": 5},
        "ABCDEF0123456789": {"hip_cm": 30},
        "1a6b30e75da96e18": {"hip_cm": 5, "aim_cm": 40},
    },
    "motion_enabled": False,
    "motion_scale": 0.5,
    "fird_head_camera": {"x": 0.02, "y": 0, "z": 0.06},
}
SYNTHETIC_MENU = b"first_person_near_clip_cm=25\nfirst_person_near_clip_aim=true\nfirst_person_motion_strength=999\nfirst_person_motion_preset=3\n" \
                 b"first_person_show_torso=true\nfirst_person_fird_person_arms=false\nfirst_person_weapon_sway_speed=150\n"
ACTIONS = convert_settings.ACTIONS
PLACEMENT_FIELDS = ("placement", "fird_head_camera", "grab_offset")
FIELDS = ("placement", "motion_enabled", "motion_scale", "weapon_placements", "action_placements", "action_state_mapping",
          "fird_head_camera", "camera_kick_enabled", "aim_markers", "show_aim_markers", "family_placement_defaults",
          "near_clip_profiles", "grab_offset", "grab_offsets")


def lua(value):
    if value is None:
        return "nil"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, (int, float)):
        return repr(float(value)) if isinstance(value, float) else str(value)
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, dict):
        return "{" + ",".join(f"[{json.dumps(k)}]={lua(v)}" for k, v in value.items()) + "}"
    raise TypeError(value)


def catalogue_keys():
    """One weapon key per family, from the catalogue rows the C build carries."""
    text = (ROOT / "src" / "action_placement_data.h").read_text()
    wanted = (("rifle", 1, r"^AR-"), ("compact", 1, r"^SMG"), ("shotgun", 1, r"^SG-"), ("marksman", 1, r"^R-"), ("energy", 1, r"^LAS-"),
              ("sidearm", 3, r""), ("support", 2, r"^[A-Z]"))
    out = []
    rows = re.findall(r'\{(\d), "([^"]+)", \d+u, true, "([0-9a-f]{16})"\}', text)
    for _family, kind, pattern in wanted:
        for row_kind, name, type_id in rows:
            if int(row_kind) == kind and re.search(pattern, name) and not re.match(r"CQC|Item |Unidentified|B/", name):
                out.append("".join(type_id[at:at + 2] for at in range(14, -1, -2)))
                break
    assert len(out) == len(wanted), "the catalogue rows changed shape"
    return out


def operations(first_person):
    ops = []

    def add(kind, **fields):
        ops.append(dict(fields, op=kind))

    saved = sorted((first_person or {}).get("action_placements", {}))
    others = set()
    for name in ("grab_offsets", "near_clip_profiles", "weapon_placements", "aim_markers"):
        others |= set((first_person or {}).get(name, {}))
    keys = saved + sorted(others - set(saved))
    keys += catalogue_keys() + ["0123456789abcdef", "ABCDEF0123456789", "short", None]

    def survey(some_keys, actions=ACTIONS):
        for name in FIELDS:
            add("field", name=name)
        for key in some_keys:
            for action in actions:
                add("resolve", key=key, action=action)
            add("resolve", key=key, action=None)
            add("default", key=key, action="idle")
            add("default", key=key, action="ads")
            add("near", key=key)
        add("motion")
        add("flags")

    survey(keys)
    for key in keys:
        add("info", key=key)
    for action in ("idle", "crouch", "prone", "wave", "fly", None):
        add("mapped", action=action)
    add("resolve", key=keys[0], action="fly")
    # Saving (runtime.lua:776-786): this weapon, then every weapon, then the refusals.
    target = "5663fbf6d81eee27"
    add("save", key=target, action="sprint", value={"x": 0.1, "y": 0.2, "z": -0.3, "yaw": 10, "pitch": 20, "roll": 30}, all=False)
    add("save", key="0123456789abcdef", action="idle", value={"x": -0.25, "y": 0.5, "z": 0.125}, all=False)
    add("save", key=target, action="aim", value={"x": 0.05, "y": 0, "z": -0.05, "roll": -12.5}, all=True)
    add("save", key=target, action="aim", value={"x": 0.6, "y": 0, "z": 0}, all=False)
    add("save", key="short", action="aim", value={"x": 0.1, "y": 0, "z": 0}, all=False)
    add("save", key=target, action="fly", value={"x": 0.1, "y": 0, "z": 0}, all=False)
    survey(keys[:4] + [target, "0123456789abcdef", None], ("idle", "aim", "sprint", "ads"))
    # The presentation switches and the global placement (runtime.lua:1066-1079, 806-811).
    add("set", name="family_placement_defaults", value=False)
    survey(keys[-9:], ("idle", "reload"))
    add("set", name="family_placement_defaults", value=True)
    add("set", name="placement", value={"x": 0.1, "y": 0.02, "z": -0.1})
    survey(keys[-9:], ("idle", "prone"))
    add("set", name="show_aim_markers", value=False)
    add("set", name="motion_scale", value=1.5)
    add("set", name="motion_enabled", value=False)
    add("set", name="fird_head_camera", value={"x": 0.01, "y": -0.02, "z": 0.05})
    add("reset_all")
    survey(keys[:3] + keys[-6:], ("idle", "aim", "ads", "dive"))
    add("set", name="family_placement_defaults", value=None)
    survey(keys[-6:], ("idle",))
    # A key written as nil is nil afterwards even when it has a default (first_person.lua:21 comes after the defaults).
    add("set", name="camera_kick_enabled", value=None)
    add("set", name="weapon_placements", value=None)
    survey(keys[-3:], ("idle",))
    add("set", name="motion_scale", value=2)
    survey(keys[-3:], ("idle",))
    # The menu's control values (menu.lua:144-182): presets, switches, values out of range.
    for name, value in (("motion_preset", 2), ("motion_preset", 3), ("motion_preset", 1), ("motion_sprint_enabled", False),
                        ("motion_bob_enabled", False), ("motion_sway_enabled", False), ("motion_strength", 250), ("motion_strength", 35),
                        ("weapon_bob_speed", 10), ("weapon_bob_strength", 300), ("weapon_sway_strength", 301), ("sprint_motion_strength", 55),
                        ("motion_enabled", False), ("motion_preset", 2), ("motion_preset", 1), ("motion_enabled", True),
                        ("motion_bob_enabled", True), ("motion_sway_enabled", True), ("motion_sprint_enabled", True),
                        ("weapon_bob_strength", 300), ("weapon_bob_strength", 0), ("weapon_bob_strength", -5), ("weapon_bob_speed", 25),
                        ("weapon_bob_speed", 200), ("weapon_bob_speed", 201), ("weapon_bob_speed", 24), ("weapon_sway_strength", 300),
                        ("weapon_sway_strength", 0), ("weapon_sway_speed", 25), ("weapon_sway_speed", 200), ("weapon_sway_speed", 205),
                        ("sprint_motion_strength", 200), ("sprint_motion_strength", 0), ("sprint_motion_strength", 205),
                        ("motion_strength", 0), ("motion_strength", 200), ("motion_strength", 200.5), ("motion_strength", -1),
                        ("motion_preset", 4), ("motion_preset", 0)):
        add("menu_set", name=name, value=value)
        add("motion")
    for name, value in (("near_clip_cm", 20), ("near_clip_aim", True), ("near_clip_cm", 7.5), ("near_clip_cm", 41), ("near_clip_cm", 5),
                        ("near_clip_cm", 6), ("near_clip_cm", 40), ("near_clip_cm", 4), ("near_clip_aim", False), ("show_torso", True),
                        ("fird_person_arms", False), ("true_native_ads", True), ("ads_mode", 2), ("ads_mode", 3)):
        add("menu_set", name=name, value=value)
        for key in [None, target] + keys[-8:-1]:
            add("near", key=key)
        add("flags")
    return ops


def lua_script(first_person, menu, ops):
    lines = ["return {", f" config={lua(first_person)},", f" menu={lua(menu)},", " ops={"]
    for op in ops:
        parts = [json.dumps(op["op"])]
        for name in ("key", "action", "name", "value", "all"):
            if name in op and op[name] is not None:
                parts.append(f"{name}={lua(op[name])}")
        lines.append("  {" + ",".join(parts) + "},")
    lines += [" },", "}"]
    return "\n".join(lines) + "\n"


def run(retail, script_text):
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as handle:
        handle.write(script_text)
    done = subprocess.run(["luajit", str(HERE / "run_settings.lua"), str(retail), handle.name], capture_output=True, text=True)
    pathlib.Path(handle.name).unlink()
    if done.returncode:
        sys.exit(f"gen_settings: the retail run failed:\n{done.stderr}")
    return [line.split("\t") for line in done.stdout.splitlines()]


def flat(value, path, out):
    if isinstance(value, dict):
        if not value:
            out[path] = "{}"
        for key, inner in value.items():
            flat(inner, f"{path}.{key}", out)
    elif isinstance(value, bool):
        out[path] = "true" if value else "false"
    else:
        out[path] = float(value)
    return out


def check_defaults(rows):
    """The converter's empty-input output against retail's own defaults and registered control defaults."""
    converted = json.loads(convert_settings.convert())
    retail = {}
    for row in rows:
        if row[0] == "D":
            retail[row[1]] = row[2] if row[2] in ("true", "false", "{}") else float(row[2])
    mine = flat(converted["first_person"], "first_person", {})
    if retail != mine:
        sys.exit(f"gen_settings: the converter's defaults differ from retail's defaults table:\n retail {sorted(retail.items())}\n here   {sorted(mine.items())}")
    controls = {row[1]: (row[2], row[3]) for row in rows if row[0] == "C"}
    for name, (kind, default) in controls.items():
        if name in convert_settings.MENU_MIRRORS:
            continue
        if name not in convert_settings.MENU:
            sys.exit(f"gen_settings: retail registers the control {name} ({kind}, default {default}) and the converter does not know it")
        expected = convert_settings.MENU[name]
        value = default == "true" if default in ("true", "false") else float(default)
        if value != expected or isinstance(value, bool) != isinstance(expected, bool):
            sys.exit(f"gen_settings: control {name}: retail's default is {default}, the converter's {expected}")
    for name in convert_settings.MENU:
        if name not in controls and name != "ads_mode":  # the ads_mode control is commented out in menu.lua:375-377; menu:ads_mode() still reads it
            sys.exit(f"gen_settings: the converter knows a control retail does not register: {name}")
    return len(retail), len(controls)


def c_string(text):
    out = []
    for line in text.splitlines(keepends=True):
        out.append('  "' + line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"')
    return "\n".join(out)


def c_text(value):
    return "NULL" if value is None else json.dumps(value)


def c_double(value):
    return float(value).hex()


def encode(op, row):
    """One operation with retail's answer as a C initializer."""
    kind = {"resolve": "R", "default": "D", "info": "I", "near": "N", "motion": "O", "flags": "F", "field": "V", "mapped": "M",
            "save": "S", "reset_all": "X", "set": "W", "menu_set": "U"}[op["op"]]
    f = {"op": f"'{kind}'", "key": c_text(op.get("key")), "name": c_text(op.get("action") if "action" in op else op.get("name"))}
    value = op.get("value")
    if op["op"] == "save":
        f["flag"] = "1" if op["all"] else "0"
    if isinstance(value, dict):
        mask = sum(1 << i for i, axis in enumerate(convert_settings.AXES) if axis in value)
        f["in_mask"] = str(mask)
        f["in"] = "{" + ", ".join(c_double(value.get(axis, 0)) for axis in convert_settings.AXES) + "}"
        if op["op"] != "save":
            f["flag"] = "4"
    elif op["op"] in ("set", "menu_set"):
        if value is None:
            f["flag"] = "0"
        elif isinstance(value, bool):
            f["flag"] = "2" if value else "1"
        else:
            f["flag"] = "3"
            f["in"] = "{" + c_double(value) + "}"
    answer = row[0]
    if answer == "P":
        f["answer"], f["mask"], f["extra"] = "'P'", row[1], row[8]
        f["v"] = "{" + ", ".join(c_double(x) for x in row[2:8]) + "}"
    elif answer == "E":
        f["answer"], f["text"] = "'E'", c_text(row[1])
    elif answer == "I":
        f["answer"] = "'I'"
        if row[1] != "-":
            f["text"], f["text2"] = c_text(row[1]), c_text(row[2])
    elif answer == "N":
        f["answer"], f["extra"] = "'N'", row[3]
        f["v"] = "{" + c_double(row[1]) + ", " + c_double(row[2]) + "}"
    elif answer == "O":
        f["answer"], f["extra"] = "'O'", row[1]
        f["v"] = "{" + ", ".join(c_double(x) for x in row[2:10]) + "}"
    elif answer == "F":
        f["answer"], f["extra"], f["text"] = "'F'", str(int(row[1]) | int(row[2]) << 1 | int(row[3]) << 2), c_text(row[4])
    elif answer == "V":
        if row[1] == "placement":
            f["answer"], f["mask"] = "'p'", row[2]
            f["v"] = "{" + ", ".join(c_double(x) for x in row[3:9]) + "}"
        elif row[1] == "table":
            f["answer"], f["extra"] = "'t'", row[2]
        elif row[1] == "number":
            f["answer"], f["v"] = "'n'", "{" + c_double(row[2]) + "}"
        elif row[1] in ("true", "false"):
            f["answer"], f["extra"] = "'b'", "1" if row[1] == "true" else "0"
        else:
            assert row[1] == "nil", row
            f["answer"] = "'0'"
    elif answer == "M":
        f["answer"], f["extra"] = "'M'", row[1]
    elif answer == "T":
        f["answer"], f["extra"] = "'T'", "1" if row[1] == "true" else "0"
    else:
        raise ValueError(row)
    return "  {" + ", ".join(f".{name} = {text}" for name, text in f.items()) + "},"


def build(retail, config_path, menu_path):
    config_bytes, menu_bytes = config_path.read_bytes(), menu_path.read_bytes()
    stamp = subprocess.run(["git", "-C", str(retail), "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
    if stamp != RETAIL_COMMIT:
        sys.exit(f"gen_settings: the retail tree is at {stamp}, expected {RETAIL_COMMIT}")
    cases, counts, summary = [], None, []
    synthetic = json.dumps({"first_person": SYNTHETIC}, sort_keys=True).encode()
    for name, config, menu in (("defaults", None, None), ("user", config_bytes, menu_bytes), ("synthetic", synthetic, SYNTHETIC_MENU)):
        converted = convert_settings.convert(config, menu, (config_path.name if name == "user" else "synthetic") if config else None,
                                             (menu_path.name if name == "user" else "synthetic") if menu else None)
        if convert_settings.notes:
            sys.exit(f"gen_settings: the converter left something out of the {name} case: {convert_settings.notes}")
        first_person = json.loads(config)["first_person"] if config else None
        menu_values, _ = convert_settings.convert_menu(menu.decode() if menu else None)
        ops = operations(first_person)
        rows = run(retail, lua_script(first_person, menu_values, ops))
        if counts is None:
            counts = check_defaults(rows)
        answers = [row for row in rows if row[0] not in ("D", "C")]
        if len(answers) != len(ops):
            sys.exit(f"gen_settings: {len(ops)} operations, {len(answers)} answers")
        cases.append((name, converted, [encode(op, row) for op, row in zip(ops, answers)]))
        summary.append(f"{name} {len(ops)}")
    sha = hashlib.sha256
    files = ("src/first_person.lua", "src/first_person/action_placement.lua", "src/first_person/menu.lua")
    lines = [
        "/* Generated by tools/retail_ref/gen_settings.py. Do not edit.",
        f" * Retail's own answers (kit commit {RETAIL_COMMIT}) to the settings operations of three cases: no saved settings, a made-up",
        " * table (SYNTHETIC in the generator), and",
        f" * {config_path.name} (sha256 {sha(config_bytes).hexdigest()}) with the menu values of",
        f" * {menu_path.name} (sha256 {sha(menu_bytes).hexdigest()}).",
        " * Retail was given the raw saved table; each case's *_json is what tools/convert_settings.py makes of it for the C store.",
        " * Retail files: " + ", ".join(f"{name} {sha((retail / name).read_bytes()).hexdigest()[:16]}" for name in files) + ".",
        f" * Checked while generating: the converter's empty-input output equals the facade's defaults table ({counts[0]} leaves),",
        f" * and its control defaults equal the {counts[1]} defaults the menu registers. Operations: {', '.join(summary)}. */",
        "#ifndef TFP_SETTINGS_VECTORS_H",
        "#define TFP_SETTINGS_VECTORS_H",
        f'#define SETTINGS_VECTORS_COMMIT "{RETAIL_COMMIT}"',
        "typedef struct SettingsOp {",
        "  char op;             /* R resolve, D default_placement, I weapon_info, N near_options, O motion_options, F the menu's four flags,",
        "                          V a config field, M action mapped, S saved + set, X reset all, W mod.set, U a menu control changed */",
        "  const char *key;     /* NULL: nil */",
        "  const char *name;    /* the action, field or control; NULL: nil */",
        "  int flag;            /* S: all weapons. W, U: the value given: 0 nil, 1 false, 2 true, 3 the number in[0], 4 the placement in */",
        "  unsigned in_mask;",
        "  double in[6];",
        "  char answer;         /* P placement (mask, v, extra = explicit), E a raise (text), I info (text name, text2 family; NULL: nil),",
        "                          N (v hip, aim; extra profile), O (extra enabled; v scale, bob, bob speed, sway, sway speed, sprint, camera bob,",
        "                          camera sway), F (extra bits arms, torso, native ads; text ads mode), p / t / n / b / 0 a field: placement,",
        "                          table with extra rows, number, Boolean, nil; M (extra), T (extra: true) */",
        "  unsigned mask;",
        "  double v[9];",
        "  int extra;",
        "  const char *text, *text2;",
        "} SettingsOp;",
    ]
    for name, converted, encoded in cases:
        lines.append(f"static const char settings_{name}_json[] =\n{c_string(converted)};")
        lines.append(f"static const SettingsOp settings_{name}_ops[] = {{")
        lines += encoded
        lines.append("};")
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--retail", required=True, type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path, default=ROOT / "tests" / "vectors")
    parser.add_argument("--config", type=pathlib.Path, default=ROOT / "import" / "retail-config-2026-11-16.cfg")
    parser.add_argument("--menu", type=pathlib.Path, default=ROOT / "tests" / "vectors" / "retail-menu-first-person-2026-11-16.ini")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    text = build(args.retail, args.config, args.menu)
    target = args.out / "settings_vectors.h"
    if args.check:
        if not target.exists() or target.read_text() != text:
            sys.exit(f"gen_settings: {target} is not what the retail files answer now; regenerate it")
        print(f"gen_settings: {target.name} is current ({text.count(chr(10))} lines)")
        return
    target.write_text(text)
    print(f"gen_settings: wrote {target} ({text.count(chr(10))} lines)")


if __name__ == "__main__":
    main()
