#!/usr/bin/env python3
"""The Python side of tests/run-wire-settings.sh: tools/convert_settings.py against the retail defaults
and the user's file, and the C store's own answers after loading the converted file, key by key.

    python3 tools/retail_ref/check_settings.py --binary work/tests/wire_settings_test --work work/tests

--binary  tests/wire_settings_test.c built; its `--dump <file>` prints the loaded store as `path=value`
          lines and `--write <file>` writes the store back as JSON.
"""
import argparse
import json
import pathlib
import random
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import convert_settings as C  # noqa: E402

CONFIG = ROOT / "import" / "retail-config-2026-11-16.cfg"
MENU = ROOT / "tests" / "vectors" / "retail-menu-first-person-2026-11-16.ini"
checks = 0


def check(condition, what):
    global checks
    checks += 1
    if not condition:
        sys.exit(f"settings conversion: FAILED {what}")


def flat(value, path, out):
    if isinstance(value, dict):
        if not value:
            out[path] = "{}"
        for key, inner in value.items():
            flat(inner, f"{path}.{key}", out)
    elif isinstance(value, bool):
        out[path] = "true" if value else "false"
    elif isinstance(value, str):
        out[path] = value
    else:
        out[path] = float(value)
    return out


def parse_dump(text):
    out = {}
    for line in text.splitlines():
        path, value = line.split("=", 1)
        check(path not in out, f"the store's dump names {path} twice")
        out[path] = value if value in ("true", "false", "{}") or not value.replace(".", "").replace("-", "").replace("e", "").replace("+", "").isdigit() else float(value)
    return out


def refused(config=None, menu=None):
    try:
        C.convert(config, menu)
    except C.Refused as problem:
        return str(problem)
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--work", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.work, args.binary = args.work.resolve(), args.binary.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    config_bytes, menu_bytes = CONFIG.read_bytes(), MENU.read_bytes()
    imported = json.loads(config_bytes)["first_person"]

    # ---- (1) the retail defaults: an empty input produces exactly them ----
    empty = json.loads(C.convert())
    check(empty["first_person"] == C.DEFAULTS and list(empty["first_person"]) == sorted(C.DEFAULTS), "an empty input gives the defaults table")
    check(empty["menu"] == C.MENU and empty["unread"] == {} and empty["format"] == 1, "an empty input gives the control defaults")
    check(C.notes == [], "an empty input leaves nothing out")
    for nothing in (b"", b"  \n", b"{}", b'{"enabled": true, "weapons": true}'):
        check(json.loads(C.convert(nothing))["first_person"] == C.DEFAULTS, f"a settings file without first_person ({nothing!r}) gives the defaults")
    # A saved table is read as it is: retail lays no defaults under it until the first write.
    check(json.loads(C.convert(b'{"first_person": {}}'))["first_person"] == {}, "an empty saved table stays empty")
    check(json.loads(C.convert(b'{"first_person": {"motion_scale": 0.5}}'))["first_person"] == {"motion_scale": 0.5}, "a saved table keeps only its own keys")
    check(json.loads(C.convert(None, b""))["menu"] == C.MENU, "an empty menu store gives the control defaults")

    # ---- (2) the user's file: every placement and option survives ----
    text = C.convert(config_bytes, menu_bytes, CONFIG.name, MENU.name)
    check(C.notes == [], f"nothing of the user's file is left out or unread: {C.notes}")
    converted = json.loads(text)
    check(converted["first_person"] == imported, "first_person of the converted file equals the import, value for value")
    check(converted["unread"] == {}, "nothing of the user's file is carried unread")
    check(set(imported) <= set(C.KINDS), "every key of the import is one the store holds")
    stored = 0
    for line in menu_bytes.decode().splitlines():
        if not line.startswith(C.MENU_PREFIX):
            continue
        key, value = line.split("=", 1)
        ident = key[len(C.MENU_PREFIX):]
        if ident in C.MENU_MIRRORS:
            check(imported[ident] == C.parse_ini_value(value), f"the mirrored control {ident} agrees with the saved setting")
            continue
        check(converted["menu"][ident] == C.parse_ini_value(value), f"control {ident} survives")
        stored += 1
    check(stored == 20 and set(converted["menu"]) == set(C.MENU), "all 20 stored controls are in the output, the rest at their defaults")
    placements = sum(len(profile) for profile in imported["action_placements"].values())
    check(len(imported["action_placements"]) == 19 and placements == 40, "the import holds 19 profiles with 40 action placements")

    # ---- deterministic: the same bytes every time, from any directory, whatever the order of the input's keys ----
    check(C.convert(config_bytes, menu_bytes, CONFIG.name, MENU.name) == text, "the same input gives the same bytes")
    out_a, out_b = args.work / "settings-user-a.json", args.work / "settings-user-b.json"
    command = [sys.executable, str(ROOT / "tools" / "convert_settings.py"), "--config", str(CONFIG), "--menu", str(MENU), "--quiet"]
    subprocess.run(command + ["--out", str(out_a)], check=True, cwd=str(ROOT))
    subprocess.run(command + ["--out", str(out_b)], check=True, cwd="/")
    check(out_a.read_bytes() == out_b.read_bytes() == text.encode(), "the command line writes the same bytes from any directory")
    check(b"\r" not in out_a.read_bytes() and out_a.read_bytes().isascii(), "the output is ASCII with line feeds")
    printed = subprocess.run(command, check=True, capture_output=True, cwd=str(ROOT)).stdout
    check(printed == text.encode(), "without --out the text goes to standard output")
    random.seed(7)

    def shuffled(value):
        if isinstance(value, dict):
            items = list(value.items())
            random.shuffle(items)
            return {key: shuffled(inner) for key, inner in items}
        return value

    for _ in range(5):
        again = json.loads(C.convert(json.dumps(shuffled(json.loads(config_bytes))).encode(), menu_bytes))
        check(again["first_person"] == converted["first_person"] and again["menu"] == converted["menu"], "the order of the input's keys does not matter")
        check(json.dumps(again["first_person"], sort_keys=True, indent=1) == json.dumps(converted["first_person"], sort_keys=True, indent=1),
              "nor does it change the text")

    # ---- the C store after loading the converted file: its answers against the import, key by key ----
    dump = subprocess.run([str(args.binary), "--dump", str(out_a)], check=True, capture_output=True, text=True).stdout
    store = parse_dump(dump)
    expected = flat(imported, "first_person", {})
    expected.update(flat(converted["menu"], "menu", {}))
    for path, value in expected.items():
        check(path in store, f"the store has {path}")
        check(store[path] == value, f"the store's {path} is {store[path]!r}, the import's {value!r}")
    check(set(store) == set(expected), f"the store holds nothing the import does not: {sorted(set(store) - set(expected))}")
    written = subprocess.run([str(args.binary), "--write", str(out_a)], check=True, capture_output=True, text=True).stdout
    back = json.loads(written)
    check(back["first_person"] == imported and back["menu"] == converted["menu"] and back["format"] == 1, "the store written back as JSON is the import again")
    (args.work / "settings-written.json").write_text(written)
    again = subprocess.run([str(args.binary), "--write", str(args.work / "settings-written.json")], check=True, capture_output=True, text=True).stdout
    check(again == written, "written, loaded and written again: the same bytes")
    defaults_file = args.work / "settings-defaults.json"
    defaults_file.write_text(C.convert())
    store = parse_dump(subprocess.run([str(args.binary), "--dump", str(defaults_file)], check=True, capture_output=True, text=True).stdout)
    expected = flat(C.DEFAULTS, "first_person", {})
    expected.update(flat(C.MENU, "menu", {}))
    check(store == expected, "the store loaded from the converted defaults holds exactly the defaults")

    # ---- what the converter refuses, and what it leaves out the way retail skips it ----
    for bad, why in ((b"{", "not JSON"), (b"[]", "not a JSON object"), (b'{"first_person": 3}', "expected a table"),
                     (b'{"first_person": {"motion_scale": NaN}}', "NaN"), (b'{"first_person": {"motion_scale": "1"}}', "expected a number"),
                     (b'{"first_person": {"motion_enabled": 1}}', "expected true or false"),
                     (b'{"first_person": {"placement": 5}}', "expected a table"), (b'{"first_person": {"a": 1, "a": 2}}', "twice"),
                     (b'{"first_person": {"weapon_placements": {"' + b"k" * 32 + b'": {}}}}', "longer than 31"),
                     (b'{"first_person": {"weapon_placements": {"": {}}}}', "empty"), (b"\xff", "not JSON")):
        problem = refused(bad)
        check(problem is not None and why in problem, f"the converter refuses {bad!r} ({problem})")
    too_many = json.dumps({"first_person": {"weapon_placements": {f"{i:016x}": {"x": 0, "y": 0, "z": 0} for i in range(257)}}}).encode()
    check("257 rows" in (refused(too_many) or ""), "the converter refuses more rows than the store holds")
    for bad, why in ((b"first_person_show_torso=1\n", "expected true or false"), (b"first_person_near_clip_cm=true\n", "expected a number"),
                     (b"first_person_near_clip_cm\n", "expected key=value"), (b"first_person_near_clip_cm=abc\n", "expected a number")):
        problem = refused(None, bad)
        check(problem is not None and why in problem, f"the converter refuses the menu line {bad!r} ({problem})")
    messy = {"first_person": {
        "placement": {"x": 0.1, "y": 0, "z": 0, "label": "mine"},
        "weapon_placements": {"aaaa": {"x": "0.1", "y": 0, "z": 0}, "bbbb": {"x": 0.1, "y": 0, "z": 0}, "cccc": 4},
        "action_placements": {"aaaa": {"idle": {"x": 0, "y": 0, "z": 0}, "fly": {"x": 0, "y": 0, "z": 0}, "aim": {"x": None}}, "bbbb": True},
        "action_state_mapping": {"R": {"1:2": "crouch", "3:4": 7}, "S": 1},
        "aim_markers": {"aaaa": True, "bbbb": False, "cccc": 1},
        "near_clip_profiles": {"aaaa": {"hip_cm": 10, "aim_cm": "5", "other": 1}, "bbbb": 3},
        "future_option": {"anything": [1, 2, 3]},
    }}
    out = json.loads(C.convert(json.dumps(messy).encode(), b"first_person_future_control=7\nother_page=1\n[section]\n# note\n"))
    check(out["first_person"] == {
        "placement": {"x": 0.1, "y": 0, "z": 0},
        "weapon_placements": {"bbbb": {"x": 0.1, "y": 0, "z": 0}},
        "action_placements": {"aaaa": {"idle": {"x": 0, "y": 0, "z": 0}}},
        "action_state_mapping": {"R": {"1:2": "crouch"}},
        "aim_markers": {"aaaa": True},
        "near_clip_profiles": {"aaaa": {"hip_cm": 10}},
    }, f"what retail skips is left out: {out['first_person']}")
    check(out["unread"] == {"first_person": {"future_option": {"anything": [1, 2, 3]}}, "menu": {"first_person_future_control": 7}},
          "a key retail does not read is carried under unread")
    check(len(C.notes) == 15, f"each thing left out or carried unread is noted ({len(C.notes)}): {C.notes}")
    messy_file = args.work / "settings-messy.json"
    messy_file.write_text(json.dumps(out, sort_keys=True, indent=1))
    store = parse_dump(subprocess.run([str(args.binary), "--dump", str(messy_file)], check=True, capture_output=True, text=True).stdout)
    expected = flat(out["first_person"], "first_person", {})
    expected.update(flat(out["menu"], "menu", {}))
    check(store == expected, "the store loads a file with an unread part and holds the rest")
    print(f"settings conversion: {checks} checks passed (the retail defaults, the user's file key by key in the converter and in the C store, "
          f"determinism, refusals)")


if __name__ == "__main__":
    main()
