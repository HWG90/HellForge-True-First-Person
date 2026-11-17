#!/usr/bin/env python3
"""Turns a retail Gadget Kit settings snapshot into the settings.json the C mod ships beside its DLL.

    python3 tools/convert_settings.py --config import/retail-config-2026-11-16.cfg \
        [--menu <dbf_mcm/goose_temporary_unlocks.ini>] --out dist/true-first-person-feel/settings.json

--config  the kit's saved config (JSON). Its `first_person` object is what retail's `mod.config`
          answers (src/first_person.lua:13-15). Without --config, or with a file that has no
          `first_person`, the output is retail's defaults.
--menu    the menu framework's own store of the kit (an .ini of `key=value` lines): the values of the
          first-person controls, `first_person_<control id>=...` (menu.lua:343-418). Without it every
          control has its registered default, which is what `handle.get` answers for a control that was
          never touched (src/first_person.lua:58-61).
--out     the file to write; `-` or absent: standard output.

The output is deterministic: sorted keys, one fixed layout, no time stamp. Its shape is what
src/tfp_settings.c loads:

    {"format": 1, "source": {...}, "first_person": {...}, "menu": {...}, "unread": {...}}

`first_person` holds the keys of the saved table that retail 0.5.41-smooth.66 reads or writes, normalised
the way retail itself treats the saved tables (anything retail skips on reading is left out, with a note
on standard error). A key the saved table does not have stays absent, as retail reads it (nil); with no
saved table at all the output is retail's defaults table. A key retail does not read at this version is carried under `unread`, untouched, so nothing
of the user's file is lost. Defaults: src/first_person.lua:9-11 (the table `host.set` lays under every
write, :18-20) and config/defaults.cfg:12-30.
"""
import argparse
import hashlib
import json
import math
import sys

FORMAT = 1
ID_MAX = 31          # src/tfp_settings.h TFP_SETTINGS_ID - 1
ROWS_MAX = 256       # src/tfp_settings.h TFP_SETTINGS_ROWS
AXES = ("x", "y", "z", "yaw", "pitch", "roll")
ACTIONS = ("idle", "aim", "ads", "sprint", "reload", "crouch", "prone", "slide", "dive")  # action_placement.lua:66-67

# src/first_person.lua:9-11. `show_aim_markers`, `family_placement_defaults` and `near_clip_profiles`
# have no default: absent is a state of its own (aim_markers.lua:134-143, action_placement.lua:98, menu.lua:138-143).
DEFAULTS = {
    "placement": {"x": 0.15, "y": 0, "z": -0.12},
    "motion_enabled": True,
    "motion_scale": 1,
    "weapon_placements": {},
    "action_placements": {},
    "action_state_mapping": {},
    "fird_head_camera": {"x": 0, "y": 0, "z": 0.04},
    "camera_kick_enabled": True,
    "aim_markers": {},
}
# kind per key: what the C store can hold for it.
KINDS = {
    "placement": "placement", "fird_head_camera": "placement", "grab_offset": "placement",
    "weapon_placements": "placements", "grab_offsets": "placements",
    "action_placements": "profiles", "action_state_mapping": "mapping", "aim_markers": "markers",
    "near_clip_profiles": "near",
    "motion_enabled": "bool", "camera_kick_enabled": "bool", "show_aim_markers": "bool", "family_placement_defaults": "bool",
    "motion_scale": "number",
}
# Not read by retail 0.5.41-smooth.66 (no file under src/ names them) but present in saved files and of a known shape:
# kept in `first_person` so the store can answer them.
CARRIED = ("grab_offset", "grab_offsets", "camera_kick_enabled")

# The first-person controls with a stored value, and their registered defaults (menu.lua:21-27, 343-418).
MENU = {
    "true_native_ads": False, "show_torso": False, "fird_person_arms": True,
    "motion_enabled": True, "motion_bob_enabled": True, "motion_sway_enabled": True, "motion_sprint_enabled": True,
    "near_clip_aim": False,
    "ads_mode": 1, "motion_preset": 1, "motion_strength": 100, "near_clip_cm": 15,
    "weapon_near_hip_cm": 15, "weapon_near_aim_cm": 5,
    "weapon_bob_strength": 100, "weapon_bob_speed": 100, "weapon_sway_strength": 100, "weapon_sway_speed": 100,
    "sprint_motion_strength": 100,
    "editor_key": 119, "toggle_key": 122,
}
# Controls whose value mirrors a `first_person` key (menu.lua:38-52, 208-212): the config is the truth.
MENU_MIRRORS = ("show_aim_markers", "family_placement_defaults")
MENU_PREFIX = "first_person_"


class Refused(Exception):
    pass


notes = []


def note(text):
    notes.append(text)


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def check_id(text, where):
    raw = text.encode("utf-8")
    if not raw or len(raw) > ID_MAX or b"\0" in raw:
        raise Refused(f"{where}: the key {text!r} is empty or longer than {ID_MAX} bytes")
    return text


def placement(value, where):
    """A placement table: the numeric fields among x, y, z, yaw, pitch, roll. None: retail would treat it as
    invalid for a reason this store cannot hold (a field of another type), so it is left out."""
    if not isinstance(value, dict):
        note(f"{where}: not a table, left out")
        return None
    out = {}
    for axis, number in value.items():
        if axis not in AXES:
            note(f"{where}.{axis}: not a placement field, left out")
        elif not is_number(number):
            note(f"{where}.{axis}: not a number; retail treats the whole placement as invalid, left out")
            return None
        else:
            out[axis] = number
    return out


def rows(value, where):
    if not isinstance(value, dict):
        raise Refused(f"{where}: expected a table")
    if len(value) > ROWS_MAX:
        raise Refused(f"{where}: {len(value)} rows, the store holds {ROWS_MAX}")
    return sorted(value.items())


def convert_field(key, value):
    kind, where = KINDS[key], f"first_person.{key}"
    if kind == "bool":
        if not isinstance(value, bool):
            raise Refused(f"{where}: expected true or false")
        return value
    if kind == "number":
        if not is_number(value):
            raise Refused(f"{where}: expected a number")
        return value
    if kind == "placement":
        if not isinstance(value, dict):
            raise Refused(f"{where}: expected a table")
        out = placement(value, where)
        return {} if out is None else out
    out = {}
    for ident, row in rows(value, where):
        at = f"{where}.{ident}"
        check_id(ident, where)
        if kind == "placements":
            row = placement(row, at)
            if row is not None:
                out[ident] = row
        elif kind == "profiles":  # action_placement.lua:81-86, 112-117: a profile that is no table is skipped
            if not isinstance(row, dict):
                note(f"{at}: not a table, left out")
                continue
            profile = {}
            for action, value_ in sorted(row.items()):
                if action not in ACTIONS:
                    note(f"{at}.{action}: not an action retail knows, left out")
                    continue
                value_ = placement(value_, f"{at}.{action}")
                if value_ is not None:
                    profile[action] = value_
            out[ident] = profile
        elif kind == "mapping":  # action_placement.lua:132-136: resource -> 'l1:l3' -> action name
            if not isinstance(row, dict):
                note(f"{at}: not a table, left out")
                continue
            states = {}
            for state, action in sorted(row.items()):
                check_id(state, at)
                if isinstance(action, str) and action:
                    states[state] = check_id(action, f"{at}.{state}")
                else:
                    note(f"{at}.{state}: not an action name, left out")
            out[ident] = states
        elif kind == "markers":  # aim_markers.lua: only a value of true counts
            if row is True:
                out[ident] = True
            else:
                note(f"{at}: not true, left out")
        elif kind == "near":  # menu.lua:138-143: {hip_cm, aim_cm}; validated where it is used, as retail does
            if not isinstance(row, dict):
                note(f"{at}: not a table, left out")
                continue
            profile = {}
            for name in ("hip_cm", "aim_cm"):
                if name in row:
                    if is_number(row[name]):
                        profile[name] = row[name]
                    else:
                        note(f"{at}.{name}: not a number, left out")
            for name in row:
                if name not in ("hip_cm", "aim_cm"):
                    note(f"{at}.{name}: not a clipping field, left out")
            out[ident] = profile
    return out


def convert_first_person(source):
    """mod.config is `parent.config.first_person or defaults` (first_person.lua:14): a saved table is read as it is,
    without the defaults under it; they are laid under it by the first write (:18-20), and the C store does the same."""
    out, unread = {}, {}
    if source is None:
        for key, value in DEFAULTS.items():
            out[key] = convert_field(key, value)
    for key, value in sorted((source or {}).items()):
        if key in KINDS:
            out[key] = convert_field(key, value)
        else:
            unread[key] = value
            note(f"first_person.{key}: not a setting retail 0.5.41-smooth.66 reads; carried under unread")
    return out, unread


def parse_ini_value(text):
    if text == "true":
        return True
    if text == "false":
        return False
    try:
        number = float(text)
    except ValueError:
        return text
    if not math.isfinite(number):
        return text
    return int(number) if number == int(number) and abs(number) < 1e15 else number


def convert_menu(ini_text):
    out, unread = dict(MENU), {}
    for line_number, line in enumerate((ini_text or "").splitlines(), 1):
        line = line.strip()
        if not line or line[0] in "#;[":
            continue
        if "=" not in line:
            raise Refused(f"menu line {line_number}: expected key=value")
        key, value = line.split("=", 1)
        key, value = key.strip(), parse_ini_value(value.strip())
        if not key.startswith(MENU_PREFIX):
            continue  # the kit's other pages
        ident = key[len(MENU_PREFIX):]
        if ident in MENU_MIRRORS:
            continue
        if ident not in MENU:
            unread[key] = value
            note(f"menu {key}: not a control whose value retail reads; carried under unread")
            continue
        default = MENU[ident]
        if isinstance(default, bool) != isinstance(value, bool) or (not isinstance(default, bool) and not is_number(value)):
            raise Refused(f"menu {key}: expected {'true or false' if isinstance(default, bool) else 'a number'}, found {value!r}")
        out[ident] = value
    return out, unread


def reject_constant(name):
    raise Refused(f"the settings file holds {name}, which is not JSON")


def no_duplicates(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise Refused(f"the settings file holds the key {key!r} twice")
        out[key] = value
    return out


def convert(config_bytes=None, menu_bytes=None, config_name=None, menu_name=None):
    """-> the text of settings.json. Either input may be None."""
    del notes[:]
    first_person = None
    if config_bytes is not None and config_bytes.strip():
        try:
            config = json.loads(config_bytes.decode("utf-8"), parse_constant=reject_constant, object_pairs_hook=no_duplicates)
        except (ValueError, UnicodeDecodeError) as problem:
            raise Refused(f"the settings file is not JSON: {problem}")
        if not isinstance(config, dict):
            raise Refused("the settings file is not a JSON object")
        first_person = config.get("first_person")
        if first_person is not None and not isinstance(first_person, dict):
            raise Refused("first_person: expected a table")
    fields, unread_fields = convert_first_person(first_person)
    try:
        menu_text = None if menu_bytes is None else menu_bytes.decode("utf-8")
    except UnicodeDecodeError as problem:
        raise Refused(f"the menu store is not text: {problem}")
    menu, unread_menu = convert_menu(menu_text)
    source = {
        "config": config_name, "config_sha256": hashlib.sha256(config_bytes).hexdigest() if config_bytes is not None else None,
        "menu": menu_name, "menu_sha256": hashlib.sha256(menu_bytes).hexdigest() if menu_bytes is not None else None,
        "retail": "0.5.41-smooth.66 (1d0d453)",
    }
    unread = {}
    if unread_fields:
        unread["first_person"] = unread_fields
    if unread_menu:
        unread["menu"] = unread_menu
    document = {"format": FORMAT, "source": source, "first_person": fields, "menu": menu, "unread": unread}
    return json.dumps(document, sort_keys=True, indent=1, ensure_ascii=True, allow_nan=False) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--config")
    parser.add_argument("--menu")
    parser.add_argument("--out")
    parser.add_argument("--quiet", action="store_true", help="do not list what was left out or carried unread")
    args = parser.parse_args(argv)

    def read(path):
        if path is None:
            return None
        with open(path, "rb") as handle:
            return handle.read()

    def base(path):
        return None if path is None else path.replace("\\", "/").rsplit("/", 1)[-1]

    try:
        text = convert(read(args.config), read(args.menu), base(args.config), base(args.menu))
    except (Refused, OSError) as problem:
        print(f"convert_settings: {problem}", file=sys.stderr)
        return 1
    if not args.quiet:
        for line in notes:
            print(f"convert_settings: {line}", file=sys.stderr)
    if args.out in (None, "-"):
        sys.stdout.write(text)
    else:
        with open(args.out, "w", encoding="ascii", newline="\n") as handle:
            handle.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
