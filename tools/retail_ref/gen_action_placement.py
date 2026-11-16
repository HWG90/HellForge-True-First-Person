#!/usr/bin/env python3
"""Cut reference vectors for src/action_select.c from the retail
action_placement.lua, run unmodified under LuaJIT by run_action_placement.lua,
and cut the catalogue data that module takes from the kit's
src/loadout_catalogue.lua.

  gen_action_placement.py --retail "<Goose Gadget Kit folder>" --out tests/vectors --data src
  gen_action_placement.py --retail ... --script-only FILE     (the Lua script only; used by the coverage check)

Writes tests/vectors/action_placement_vectors.h (every operation and the retail
answer) and src/action_placement_data.h (the `gear` rows, the fields the module
reads), both stamped with the retail commit and the files' SHA-256.
"""
import argparse
import hashlib
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
FROZEN = "1d0d453"
NAN, INF = float("nan"), float("inf")
ACTIONS = ["idle", "aim", "ads", "sprint", "reload", "crouch", "prone", "slide", "dive"]
FAMILIES = ["-", "rifle", "compact", "sidearm", "marksman", "shotgun", "energy", "support"]
FIELDS = ["x", "y", "z", "yaw", "pitch", "roll"]
RESOURCE = "4D1C334D294DFA97"


class Raw:
    """A Lua expression used as it is: a value of a type the field does not expect."""
    def __init__(self, text):
        self.text = text

    def __repr__(self):
        return f"Raw({self.text})"


OTHER, ONE, ZERO, EMPTY = Raw("'x'"), Raw("1"), Raw("0"), Raw("''")


def lua_string(text):
    return '"' + "".join(c if c.isalnum() or c in " _-/.:" else "\\%03d" % ord(c) for c in text) + '"'


def lua(value):
    if value is None:
        return "nil"
    if isinstance(value, Raw):
        return value.text
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        value = float(value)
        if value != value:
            return "(0/0)"
        if value in (INF, -INF):
            return "math.huge" if value > 0 else "(-math.huge)"
        return repr(value)
    if isinstance(value, str):
        return lua_string(value)
    if isinstance(value, bytes):
        return '"' + "".join("\\%03d" % b for b in value) + '"'
    if isinstance(value, dict):
        return "{" + ",".join(f"[{lua(k)}]={lua(v)}" for k, v in value.items() if v is not None) + "}"
    if isinstance(value, list):
        return "{" + ",".join(lua(v) for v in value) + "}"
    raise TypeError(value)


def c_double(value):
    if value != value:
        return "NAN"
    if value in (INF, -INF):
        return "INFINITY" if value > 0 else "-INFINITY"
    return float(value).hex()


def c_string(text):
    if text is None:
        return "0"
    return '"' + "".join(c if c.isalnum() or c in " _-/.:!,'()+&" else "\\%03o" % ord(c) for c in text) + '"'


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def reverse_key(type_id):
    return "".join(type_id[i:i + 2] for i in range(14, -1, -2)).lower()


# ------------------------------------------------------------------ building
class Script:
    def __init__(self):
        self.configs, self.mappings, self.ops = [], [], []

    def config(self, value):
        self.configs.append(value)
        return len(self.configs)        # Lua index

    def mapping(self, value):
        self.mappings.append(value)
        return len(self.mappings)

    def add(self, op, **fields):
        self.ops.append(dict(op=op, **fields))


def p(x, y=0.0, z=0.0, **more):
    return dict(x=x, y=y, z=z, **more)


def py_saved(config, key, action, value, all_weapons):
    """What retail's saved() builds for a well-formed config (to feed the fixture's next step; the Lua answer is the reference)."""
    out = {}
    for ident, profile in (config.get("action_placements") or {}).items():
        out[ident] = {a: dict(v) for a, v in profile.items() if not (all_weapons and a == action)}
    out.setdefault("__all" if all_weapons else key, {})[action] = dict(value)
    return out


def flags(**at):
    data = bytearray(24)
    for index, value in at.items():
        data[int(index[1:])] = value
    return bytes(data)


def fixtures(s):
    """The retail fixture tests/first_person/action_placement.lua, statement by statement."""
    c = dict(placement=p(.1), weapon_placements=dict(a=p(.2)))
    for key, action in (("a", "idle"), ("b", "sprint"), ("a", "ads")):
        s.add("resolve", config=s.config(c), key=key, action=action)
    key, other = "1234567890abcdef", "0123456789abcdef"
    def save(config, k, action, value, everyone):
        s.add("saved", config=s.config(dict(config)), key=k, action=action, value=value, all=everyone)
        return dict(config, action_placements=py_saved(config, k, action, value, everyone))
    c = save(c, key, "sprint", p(.3), False)
    s.add("resolve", config=s.config(c), key=key, action="sprint"); s.add("resolve", config=s.config(c), key=other, action="sprint")
    c = save(c, other, "crouch", p(.4), False)
    c = save(c, key, "sprint", p(-.2), True)
    for k, action in ((key, "sprint"), (other, "sprint"), (other, "crouch")):
        s.add("resolve", config=s.config(c), key=k, action=action)
    s.add("select", sample=dict(ads=True, sprint=True))
    s.add("select", sample=dict(sprint=True))
    m = s.mapping(dict(r={"1:2": "crouch"}))
    s.add("select", sample=dict(action_resource="r", action_verified=True, l1=1, l3=2), mapping=m)
    s.add("select", sample=dict(action_resource="x", action_verified=True, l1=1, l3=2), mapping=m)
    s.add("saved", config=s.config(c), key=key, action="bad", value=p(0), all=False)
    s.add("saved", config=s.config(c), key=key, action="idle", value=p(10), all=False)
    # Labelled action-state fixture: 36
    cases = [(15, 4, "0a0200000000040000200000000000000000000000000000"), (16, 4, "0a0200000000040000200000000000000000000000000000"),
             (47, 60, "0a0200000000040000400000000000000000000000000000"), (48, 56, "0a0200000000040000400000000000000000000000000000"),
             (16, 4, "0a0200000000040001200200000000000000000000000000"), (16, 4, "0a0200000000040000200200000000000000000000000000"),
             (45, 37, "0a0200000000040001400000200000000000000000000000"), (48, 56, "0a0200000000040000400000200000000000000000000000"),
             (59, 67, "0a0200000000040000000000000000000000000000000000")]
    for l1, l3, text in cases:
        base = dict(l1=l1, l3=l3, stance_flags=bytes.fromhex(text), action_resource=RESOURCE, action_verified=True)
        s.add("select", sample=base); s.add("select", sample=dict(base, action_resource="foreign"))
        s.add("select", sample=dict(base, action_verified=False)); s.add("select", sample=dict(base, ads=True))
    rotation = dict(x=.1, y=0, z=0, yaw=12, pitch=-8)
    s.add("saved", config=s.config({}), key=key, action="sprint", value=rotation, all=False)
    s.add("resolve", config=s.config(dict(action_placements=py_saved({}, key, "sprint", rotation, False))), key=key, action="sprint")
    for value in (p(0, yaw=91), p(0, pitch=NAN), p(0, yaw=90, pitch=-90), p(0, yaw=90.1)):
        s.add("valid", p=value)
    for l1, l3 in ((16, 3), (20, 3), (25, 5), (16, 4), (15, 4)):         # bridge crouch
        base = dict(l1=l1, l3=l3, stance_flags=flags(b9=0x20), action_resource=RESOURCE, action_verified=True)
        s.add("select", sample=base); s.add("select", sample=dict(base, action_verified=False))
    for l1, l3 in ((47, 33), (47, 22), (48, 22), (48, 50)):              # dive entry, flight, prone recovery
        base = dict(l1=l1, l3=l3, stance_flags=flags(b9=0x40, b12=0x20), action_resource=RESOURCE, action_verified=True)
        s.add("select", sample=base); s.add("select", sample=dict(base, stance_flags=flags(b9=0x40)))
    roll = dict(x=.1, y=.2, z=.3, roll=70)
    s.add("saved", config=s.config({}), key=key, action="sprint", value=roll, all=False)
    s.add("resolve", config=s.config(dict(action_placements=py_saved({}, key, "sprint", roll, False))), key=key, action="sprint")
    # Catalogue-backed composition
    s.add("catalogue", real=True)
    scythe = "5663fbf6d81eee27"
    s.add("info", key=scythe); s.add("info", key="0000000000000000")
    cfg = dict(placement=p(.15, 0, -.12), family_placement_defaults=True)
    for k, action in ((scythe, "idle"), (scythe, "sprint"), (scythe, "ads"), ("0000000000000000", "idle")):
        s.add("resolve", config=s.config(cfg), key=k, action=action)
    s.add("resolve", config=s.config(dict(cfg, family_placement_defaults=False)), key=scythe, action="idle")
    s.add("resolve", config=s.config(dict(cfg, placement=p(.133, 0, -.12))), key=scythe, action="idle")
    cfg = dict(cfg, weapon_placements={scythe: p(.21)})
    s.add("resolve", config=s.config(cfg), key=scythe, action="idle")
    cfg = dict(cfg, action_placements={"__all": dict(idle=p(.22)), scythe: dict(idle=p(.23))})
    s.add("resolve", config=s.config(cfg), key=scythe, action="idle")
    s.add("default", config=s.config(cfg), key=scythe, action="idle"); s.add("default", config=s.config(cfg), key=scythe, action="ads")
    unknown = "0011223344556677"
    s.add("catalogue", rows=[dict(kind=1, name="AR-23 Liberator", type=unknown), dict(kind=3, name="P-2 Peacemaker", type=unknown)])
    s.add("info", key="7766554433221100"); s.add("baseline", key="7766554433221100")
    s.add("catalogue", real=True)


def edges(s, real_keys):
    """Hand-made: every branch of every function, with the Lua-specific values."""
    # --- catalogue: family rules in their order, type shapes, duplicates and ambiguity.
    names = ["AR-23 Liberator", "ARC-12 Blitzer", "AR", "A", "BR-14 Adjudicator", "MA5C Assault Rifle", "MA5C", "StA-52 Assault Rifle",
             "StA-52", "SMG-37 Defender", "SMG", "MP-98 Knight", "MP98", "M7S SMG", "M7S", "StA-11 SMG", "SG-8 Punisher", "SG8",
             "DBS-2 Double Freedom", "M90A Shotgun", "M90A", "R-63 Diligence", "R/36 Eruptor", "R63", "LAS-5 Scythe", "LAS5",
             "PLAS-1 Scorcher", "FLAM-66 Torcher", "CQC-19 Stun Lance", "CQC", "Item 12", "Item", "Unidentified 7", "B/1 Supply Pack",
             "Breaching Hammer", "Breaching Hammer Mk2", "P-2 Peacemaker", "GR-8 Recoilless Rifle", "", "ar-23", "xAR-23", " AR-23",
             "RS-422 Railgun", "ARC-3 Arc Thrower", "CQC-1 One True Flag", "Rifle"]
    rows = []
    for n, name in enumerate(names):
        for kind in (1, 2, 3, 6, 0):
            rows.append(dict(kind=kind, name=name, id=1000 + len(rows), type="%016x" % (0xa000000000000000 + len(rows) * 0x0101010101)))
    s.add("catalogue", rows=rows)
    for row in rows[::7]:
        s.add("info", key=reverse_key(row["type"])); s.add("baseline", key=reverse_key(row["type"]))
    good = "a8a91eb54892b6b2"
    shapes = [dict(kind=1, name="AR-1 A", id=1, type=good.upper()),                 # upper-case hex: key is lower case
              dict(kind=1, name="AR-2 B", id=2, type="a8a91eb54892b6b"),            # 15 characters
              dict(kind=1, name="AR-3 C", id=3, type="a8a91eb54892b6b2a"),          # 17
              dict(kind=1, name="AR-4 D", id=4, type="a8a91eb54892b6bg"),           # not hex
              dict(kind=1, name="AR-5 E", id=5, type=Raw("12345678")),              # a number
              dict(kind=1, name="AR-6 F", id=6),                                    # no type
              dict(kind=1, name=Raw("17"), id=7, type="1111111111111111"),          # name not a string
              dict(kind=Raw("'1'"), name="AR-8 H", id=8, type="2222222222222222"),  # kind not a number
              dict(kind=1, name="AR-9 I", type="3333333333333333"),                 # no id
              Raw("5"), Raw("false"), Raw("'row'"),                                 # rows that are not tables
              dict(kind=2, name="MG-43 Machine Gun", id=10, type="AbCdEf0123456789"),
              dict(kind=3, name="P-19 Redeemer", id=11, type="0000000000000000"),
              dict(kind=1, name="AR-12 L", id=0, type="ffffffffffffffff"),
              dict(kind=1, name="AR-13 M", id=4294967295, type="0123456789abcdef"),
              dict(kind=1, name="AR-14 N", id=14, type="ABCDEFabcdef0099"),         # every hex letter, both cases
              dict(kind=1, name="AR-15 O", id=15, type="FFFFFFFFFFFFFFFA")]
    # One character outside the hex digits, at every place and from just outside each range.
    for at in range(16):
        for n, c in enumerate("/:@G`g"):
            text = "4444444444444444"[:at] + c + "4444444444444444"[at + 1:]
            shapes.append(dict(kind=1, name="AR-16 P", id=100 + at * 8 + n, type=text))
    s.add("catalogue", rows=shapes)
    for key in ("b2b69248b51ea9a8", "B2B69248B51EA9A8", good, "1111111111111111", "2222222222222222", "3333333333333333",
                "8967452301efcdab", "0000000000000000", "ffffffffffffffff", "efcdab8967452301", None, "", "__all", "b2b69248b51ea9a",
                "9900efcdabefcdab", "faffffffffffffff", "4444444444444444"):
        s.add("info", key=key); s.add("baseline", key=key)
    # Duplicates: same name and family (the later row replaces kind and item), another name, another family,
    # and an ambiguous key that a third row cannot revive.
    t = "0102030405060708"
    dup = lambda *rows: s.add("catalogue", rows=list(rows))
    row = lambda name, kind=1, ident=1, type_id=t: dict(kind=kind, name=name, id=ident, type=type_id)
    for rows in ([row("AR-1 A", ident=1), row("AR-1 A", ident=2)],
                 [row("AR-1 A", ident=1), row("AR-1 B", ident=2)],
                 [row("AR-1 A", kind=1), row("AR-1 A", kind=3)],
                 [row("AR-1 A", kind=2, ident=5), row("AR-1 A", kind=2, ident=6), row("AR-1 A", kind=2)],
                 [row("AR-1 A"), row("AR-1 B"), row("AR-1 A"), row("AR-1 A")],
                 [row("AR-1 A"), row("AR-1 B"), row("AR-1 B")],
                 [row("AR-1 A"), row("CQC-1 A"), row("AR-1 A", ident=9)],                 # a row without a family is no witness
                 [row("AR-1 A"), row("AR-1 A", type_id=t.upper().replace("01", "01")), row("LAS-1 A", type_id="0807060504030201")],
                 [row("AR-1 A"), row("AR-1 A", kind=6), row("AR-1 A", ident=3)],
                 [row("SMG-1 A"), row("SMG-1 A", kind=1, ident=4), row("SG-1 A", type_id="1102030405060708")]):
        dup(*rows)
        for key in (reverse_key(t), reverse_key("0807060504030201"), reverse_key("1102030405060708")):
            s.add("info", key=key); s.add("baseline", key=key)
    # gear that is not a table, an empty one, and a hole that ends ipairs.
    for rows in (None, Raw("7"), Raw("'gear'"), [], [row("AR-1 A"), None, row("SG-1 B", type_id="1102030405060708")]):
        s.add("catalogue", rows=rows); s.add("info", key=reverse_key(t)); s.add("info", key=reverse_key("1102030405060708"))
    s.add("catalogue", real=True)

    # --- valid(): every field, both limits, every wrong type.
    for value in (None, Raw("5"), OTHER, Raw("false"), {}, p(0), p(.5, -.5, .5), p(.5000000000000001), p(0, -.5000000000000001),
                  p(0, 0, .51), p(NAN), p(0, NAN), p(0, 0, NAN), p(INF), p(0, -INF), dict(y=0, z=0), dict(x=0, z=0), dict(x=0, y=0),
                  p(OTHER), p(0, OTHER), p(0, 0, OTHER), p(False), p(0, True), p(0, yaw=90, pitch=90, roll=90),
                  p(0, yaw=-90, pitch=-90, roll=-90), p(0, yaw=90.00000000000001), p(0, pitch=-90.00000000000001), p(0, roll=91),
                  p(0, yaw=NAN), p(0, pitch=NAN), p(0, roll=NAN), p(0, yaw=INF), p(0, roll=-INF), p(0, yaw=OTHER), p(0, pitch=OTHER),
                  p(0, roll=OTHER), p(0, yaw=False), p(0, pitch=False), p(0, roll=False), p(0, yaw=True), p(0, roll=True),
                  p(-0.0, yaw=-0.0), p(1e-300, yaw=1e-300), dict(p(0), extra=5)):
        s.add("valid", p=value)

    # --- resolve(): each step of the lookup order, with a valid and an invalid value at each.
    key, other = real_keys["energy"], real_keys["sidearm"]
    bad = p(9)
    ladder = dict(placement=p(.11, .01, -.01, yaw=1), weapon_placements={key: p(.21, yaw=2), "__all": p(.22, pitch=3)},
                  action_placements={key: dict(idle=p(.31, roll=4), sprint=p(.32), ads=p(.33, .1, .2, yaw=5)),
                                     "__all": dict(idle=p(.41), sprint=p(.42), crouch=p(.43), ads=p(.44))})
    keys = (key, other, None, "__all", "0000000000000000", key.upper())
    def every(config):
        index = s.config(config)
        for k in keys:
            for action in (None, "idle", "ads", "sprint", "crouch", "dive"):
                s.add("resolve", config=index, key=k, action=action)
    every(ladder)
    every(dict(ladder, action_placements={key: dict(idle=bad, sprint=OTHER, ads=bad), "__all": ladder["action_placements"]["__all"]}))
    every(dict(ladder, action_placements={key: OTHER, "__all": dict(idle=bad, sprint=bad, ads=bad)}))
    every(dict(ladder, action_placements={"__all": OTHER}))
    every(dict(ladder, action_placements=None))
    every(dict(ladder, action_placements=None, weapon_placements={key: bad, "__all": p(.22, pitch=3)}))
    every(dict(ladder, action_placements=None, weapon_placements={key: OTHER, "__all": bad}))
    every(dict(ladder, action_placements=None, weapon_placements={"__all": OTHER}))
    every(dict(ladder, action_placements=None, weapon_placements=None))
    every(dict(placement=bad)); every(dict(placement=OTHER)); every({}); every(None)
    for action in ("bad", "camera", "IDLE", ""):
        s.add("resolve", config=s.config(ladder), key=key, action=action)
        s.add("resolve", config=None, key=None, action=action)
    # The family baseline: only with the option exactly true, and only over a missing or exactly standard placement.
    standard = p(.15, 0, -.12)
    for family in (True, False, None, ONE, OTHER):
        for placement in (standard, dict(standard, yaw=0, pitch=0, roll=0), dict(standard, yaw=False), dict(standard, yaw=-0.0),
                          p(.15000000000000002, 0, -.12), p(.15, 1e-300, -.12), p(.15, 0, -.12000000000000001),
                          dict(standard, yaw=.5), dict(standard, pitch=-.5), dict(standard, roll=1e-9), None, bad, OTHER, p(.1, .1, .1)):
            index = s.config(dict(placement=placement, family_placement_defaults=family))
            for k in (key, other, real_keys["rifle"], None, "0000000000000000", "__all"):
                for action in ("idle", "sprint", "ads"):
                    s.add("resolve", config=index, key=k, action=action)
                s.add("default", config=index, key=k, action="idle")
    for name, k in sorted(real_keys.items()):                              # every family's baseline
        index = s.config(dict(family_placement_defaults=True))
        s.add("resolve", config=index, key=k, action="idle"); s.add("baseline", key=k)
        for action in (None, "idle", "ads", "aim", "bad"):
            s.add("default", config=index, key=k, action=action)
            s.add("default", config=None, key=k, action=action)
            s.add("default", config=s.config(dict(family_placement_defaults=ONE)), key=k, action=action)
    # A saved profile outranks the family baseline; an ambiguous key has none.
    s.add("catalogue", rows=[dict(kind=1, name="AR-1 A", id=1, type=t), dict(kind=1, name="AR-1 B", id=2, type=t)])
    index = s.config(dict(family_placement_defaults=True))
    s.add("resolve", config=index, key=reverse_key(t), action="idle"); s.add("default", config=index, key=reverse_key(t), action="idle")
    s.add("catalogue", real=True)

    # --- saved(): refusals in their order, a weapon row, the shared row, what is dropped.
    old = dict(action_placements={key: dict(idle=p(.1), sprint=p(.2, yaw=3), bad=p(.3), crouch=bad, prone=OTHER, dive=p(.4, roll=False)),
                                  other: dict(sprint=p(.5), slide=p(.6)), "__all": dict(sprint=p(.7), ads=p(0, 0, 0, pitch=1)),
                                  "junk": OTHER, "empty": {}, "onlybad": dict(idle=bad, nope=p(0))})
    index = s.config(old)
    fresh = "fedcba9876543210"
    for k in (key, other, fresh, "__all", "short", "seventeen_chars__", None, Raw("1234567890123456"), "sixteen__chars__"):
        for action in ("sprint", "idle", "ads", "bad", None):
            for value in (p(.25, -.25, .05, yaw=10, pitch=-20, roll=30), p(.01), bad, None, OTHER, p(0, yaw=False)):
                for everyone in (False, True, None, ONE):
                    s.add("saved", config=index, key=k, action=action, value=value, all=everyone)
    for config in (None, {}, dict(action_placements=None), dict(action_placements={}), dict(action_placements={"__all": {}}),
                   dict(action_placements={fresh: dict(idle=p(.1))}), dict(action_placements={fresh: {}}),
                   dict(action_placements={"__all": dict(idle=p(.1), ads=p(.2))})):
        index = s.config(config)
        for everyone in (False, True):
            s.add("saved", config=index, key=fresh, action="idle", value=p(.3, yaw=1), all=everyone)
            s.add("saved", config=index, key=fresh, action="dive", value=p(.3), all=everyone)

    # --- select(): the priority order and what counts as true.
    def sel(sample, mapping=None):
        s.add("select", sample=sample, mapping=mapping)
    verified = dict(action_resource=RESOURCE, action_verified=True)
    crouch, prone, slide, dive = flags(b9=0x20), flags(b9=0x40), flags(b9=0x20, b10=2), flags(b9=0x40, b12=0x20)
    for ads in (True, False, None, 1, 1.0000001, .9999999, 2, 0, -1, NAN, INF, -INF, OTHER, ONE):
        sel(dict(ads=ads)); sel(dict(verified, ads=ads, stance_flags=dive, reload=True, shoulder_aim=True, sprint=True))
    for stance in (crouch, prone, slide, dive, bytes(24)):
        for extra in ({}, dict(reload=True), dict(shoulder_aim=True), dict(sprint=True), dict(reload=True, shoulder_aim=True, sprint=True),
                      dict(shoulder_aim=True, sprint=True)):
            sel(dict(verified, stance_flags=stance, **extra))
    for truth in (True, False, None, ZERO, EMPTY, ONE, OTHER):          # 0 and '' are true
        sel(dict(reload=truth)); sel(dict(shoulder_aim=truth)); sel(dict(sprint=truth))
        sel(dict(verified, stance_flags=crouch, reload=truth)); sel(dict(verified, stance_flags=slide, shoulder_aim=truth))
    # Posture bits: every value of byte 9's bits 5-6 with the slide and dive bits, and neighbours that must not count.
    for b9 in (0x00, 0x20, 0x40, 0x60, 0x1f, 0x3f, 0x5f, 0x7f, 0x80, 0xa0, 0xc0, 0xe0, 0xff, 0x24, 0x44, 0x84):
        for b10 in (0, 2, 1, 4, 0xfd, 0xff):
            for b12 in (0, 0x20, 0x10, 0x40, 0xdf, 0xff):
                sel(dict(verified, stance_flags=flags(b9=b9, b10=b10, b12=b12)))
    for index in range(24):                                              # no other byte matters
        sel(dict(verified, stance_flags=flags(**{f"b{index}": 0xff})))
    # The stance needs the exact resource, verified exactly true, and 24 flag bytes.
    for resource in (RESOURCE, RESOURCE.lower(), "foreign", None, RESOURCE + "0", RESOURCE[:-1], ""):
        for check in (True, False, None, ONE, OTHER):
            sel(dict(action_resource=resource, action_verified=check, stance_flags=slide))
            sel(dict(action_resource=resource, action_verified=check, stance_flags=crouch, sprint=True))
    for stance in (slide[:23], slide + b"\0", b"", None, Raw("24"), Raw("{}")):
        sel(dict(verified, stance_flags=stance)); sel(dict(verified, stance_flags=stance, sprint=True))
    # The optional mapping: scoped to the resource, any true `verified`, key l1:l3, only actions of the list.
    table = dict(r={"1:2": "crouch", "78:71": "sprint", "-1:0": "dive", "nil:nil": "prone", "5:nil": "ads", "nil:6": "idle", "7:7": "bad",
                    "8:8": Raw("5"), "9:9": "Crouch", "10:10": "reload", "11:11": "aim", "12:12": "slide"}, other=OTHER,
                 **{RESOURCE: {"59:67": "reload", "1:2": "prone"}, "": {"1:2": "dive"}})
    index = s.mapping(table)
    empty = s.mapping({})
    for resource in ("r", "other", "x", RESOURCE, None, ""):
        for check in (True, False, None, ONE, OTHER):
            for l1, l3 in ((1, 2), (78, 71), (-1, 0), (None, None), (5, None), (None, 6), (7, 7), (8, 8), (9, 9), (10, 10), (11, 11),
                           (12, 12), (59, 67), (2, 1), (0, 0)):
                for m in (index, empty, None):
                    sel(dict(action_resource=resource, action_verified=check, l1=l1, l3=l3), m)
    sel(dict(verified, l1=59, l3=67, stance_flags=crouch), index)        # a stance outranks the mapping
    sel(dict(verified, l1=59, l3=67, stance_flags=bytes(24), sprint=True), index)
    sel({}); sel({}, index)


def random_ops(s, real_keys, count, seed):
    rng = random.Random(seed)
    keys = sorted(real_keys.values()) + ["%016x" % rng.getrandbits(64) for _ in range(6)] + ["__all", None, "0000000000000000"]
    def placement():
        roll = rng.random()
        if roll < .04: return rng.choice((None, OTHER, {}))
        out = p(rng.choice((0, .15, -.3, .5, .5000001, .07, rng.uniform(-.55, .55))), rng.choice((0, 0, .01, rng.uniform(-.5, .5))),
                rng.choice((-.12, 0, rng.uniform(-.5, .5))))
        for axis in ("yaw", "pitch", "roll"):
            if rng.random() < .4: out[axis] = rng.choice((0, 12, -90, 90, 90.5, rng.uniform(-95, 95), False))
        if rng.random() < .04: out[rng.choice(FIELDS)] = rng.choice((NAN, INF, OTHER, None, True))
        return out
    def profile():
        if rng.random() < .05: return OTHER
        out = {a: placement() for a in rng.sample(ACTIONS, rng.randint(0, 5))}
        if rng.random() < .1: out["bad"] = placement()
        return out
    def config():
        if rng.random() < .03: return None
        out = {}
        if rng.random() < .7: out["action_placements"] = {k: profile() for k in rng.sample(keys[:-2] + ["k16____________x"], rng.randint(0, 5))}
        if rng.random() < .4: out["weapon_placements"] = {k: placement() for k in rng.sample(keys[:-2], rng.randint(0, 3))}
        if rng.random() < .7: out["placement"] = rng.choice((p(.15, 0, -.12), p(.15, 0, -.12), placement()))
        if rng.random() < .5: out["family_placement_defaults"] = rng.choice((True, True, False, ONE))
        return out
    pool = [s.config(config()) for _ in range(250)]
    stance = [flags(), flags(b9=0x20), flags(b9=0x40), flags(b9=0x20, b10=2), flags(b9=0x40, b12=0x20), flags(b9=0x60, b10=2, b12=0x20)]
    table = s.mapping({RESOURCE: {f"{a}:{b}": rng.choice(ACTIONS + ["bad"]) for a in range(4) for b in range(4)},
                       "r": {f"{a}:{b}": rng.choice(ACTIONS) for a in range(4) for b in range(3)}})
    for _ in range(count):
        roll = rng.random()
        if roll < .34:
            s.add("resolve", config=rng.choice(pool), key=rng.choice(keys), action=rng.choice(ACTIONS + [None, "bad"]))
        elif roll < .67:
            sample = dict(l1=rng.choice((0, 1, 2, 3, None, 78)), l3=rng.choice((0, 1, 2, 3, None, 71)))
            if rng.random() < .15: sample["ads"] = rng.choice((True, True, 1, .5, False))
            if rng.random() < .85: sample["action_resource"] = rng.choice((RESOURCE, RESOURCE, RESOURCE, "r", "foreign"))
            if rng.random() < .85: sample["action_verified"] = rng.choice((True, True, True, False, ONE))
            if rng.random() < .9: sample["stance_flags"] = rng.choice(stance) if rng.random() < .8 else bytes(rng.getrandbits(8) for _ in range(rng.choice((24, 24, 24, 23, 25))))
            for name in ("reload", "shoulder_aim", "sprint"):
                if rng.random() < .25: sample[name] = rng.choice((True, True, False, ZERO))
            s.add("select", sample=sample, mapping=rng.choice((None, table, table)))
        elif roll < .85:
            s.add("saved", config=rng.choice(pool), key=rng.choice(keys + ["short"]), action=rng.choice(ACTIONS + ["bad"]),
                  value=placement(), all=rng.choice((True, False)))
        elif roll < .92:
            s.add("default", config=rng.choice(pool), key=rng.choice(keys), action=rng.choice(ACTIONS + [None]))
        elif roll < .97:
            s.add("valid", p=placement())
        else:
            k = rng.choice(keys)
            s.add("info", key=k); s.add("baseline", key=k)


# -------------------------------------------------------------- Lua and back
def lua_script(s):
    rows = []
    for n, config in enumerate(s.configs, 1):
        rows.append(f"configs[{n}]={lua(config)}")
    for n, mapping in enumerate(s.mappings, 1):
        rows.append(f"mappings[{n}]={lua(mapping)}")
    for op in s.ops:
        fields = [lua(op["op"])] + [f"{k}={lua(v)}" for k, v in op.items() if k != "op" and v is not None]
        rows.append("ops[#ops+1]={" + ",".join(fields) + "}")
    out = ["local configs, mappings, ops = {}, {}, {}"]
    for start in range(0, len(rows), 200):      # one Lua function holds at most 65536 constants
        out += ["do local function part()"] + rows[start:start + 200] + ["end part() end"]
    return "\n".join(out) + "\nreturn {configs=configs, mappings=mappings, ops=ops}\n"


def run(retail, script_text):
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
        script.write(script_text)
    done = subprocess.run(["luajit", str(HERE / "run_action_placement.lua"), str(retail), script.name], capture_output=True, text=True)
    Path(script.name).unlink()
    if done.returncode:
        sys.exit("run_action_placement.lua failed:\n" + done.stderr)
    return done.stdout.split("\n")[:-1]


class Pool:
    def __init__(self):
        self.rows, self.index = [], {}

    def add(self, text):
        if text not in self.index:
            self.index[text] = len(self.rows)
            self.rows.append(text)
        return self.index[text]


def placement_in(value):
    """A Lua placement as (present mask, six doubles), or None when it is not a table."""
    if not isinstance(value, dict):
        return None
    mask, numbers = 0, []
    for bit, name in enumerate(FIELDS):
        v = value.get(name)
        if v is None or v is False:
            numbers.append(0.0)
        elif is_number(v):
            mask |= 1 << bit; numbers.append(float(v))
        else:
            mask |= 1 << bit; numbers.append(NAN)       # true, a string: a value that is not a number
    return mask, numbers


def header(s, lines, stamp):
    placements, profiles, legacies, weapons, gear = Pool(), [], [], [], []

    def place(mask, numbers):
        return placements.add("{%d,{%s}}" % (mask, ",".join(c_double(n) for n in numbers)))

    def place_in(value):
        got = placement_in(value)
        return -1 if got is None else place(*got)

    def profile_row(ident, profile):
        has, cells = 0, [-1] * len(ACTIONS)
        for a, name in enumerate(ACTIONS):
            index = place_in(profile.get(name))
            if index >= 0:
                has |= 1 << a; cells[a] = index
        profiles.append("{%s,%d,{%s}}" % (c_string(ident), has, ",".join(map(str, cells))))

    configs = ["{1,0,-1,0,0,0,0}"]                                      # index 0: nil
    for config in s.configs:
        if config is None:
            configs.append("{1,0,-1,0,0,0,0}"); continue
        first_profile, first_legacy = len(profiles), len(legacies)
        for ident, profile in (config.get("action_placements") or {}).items():
            if isinstance(profile, dict): profile_row(ident, profile)
        for ident, value in (config.get("weapon_placements") or {}).items():
            if isinstance(value, dict): legacies.append("{%s,%d}" % (c_string(ident), place_in(value)))
        configs.append("{0,%d,%d,%d,%d,%d,%d}" % (int(config.get("family_placement_defaults") is True), place_in(config.get("placement")),
                                                  first_profile, len(profiles) - first_profile, first_legacy, len(legacies) - first_legacy))
    map_rows, mappings = [], ["{1,0,0}"]
    for mapping in s.mappings:
        first = len(map_rows)
        for resource, table in mapping.items():
            if isinstance(table, dict):
                for states, action in table.items():
                    map_rows.append("{%s,%s,%s}" % (c_string(resource), c_string(states), c_string(action if isinstance(action, str) else None)))
        mappings.append("{0,%d,%d}" % (first, len(map_rows) - first))
    samples, catalogues = Pool(), []

    def weapon_rows(parts):
        """key name family kind item, five cells a weapon; returns the pool range."""
        first = len(weapons)
        for at in range(0, len(parts), 5):
            key, name, family, kind, item = parts[at:at + 5]
            if name == "!" and family == "-":
                weapons.append("{%s,1,0,0,0,0,0}" % c_string(key))
            else:
                weapons.append("{%s,0,%s,%d,%s,%s,%d}" % (c_string(key), c_string(name), FAMILIES.index(family), kind,
                                                          "0" if item == "-" else item + "u", int(item != "-")))
        return first, len(weapons) - first

    def action_code(name):
        if name is None: return -1
        return ACTIONS.index(name) if name in ACTIONS else 99

    def key_text(key):
        """The key as C sees it; a key that is not a string is nil there."""
        return c_string(key if isinstance(key, str) else None)

    constants = lines[0].split("\t")
    assert constants[0] == "A"
    rows, texts = [], Pool()
    texts.add('""')
    real_gear = None
    for op, line in zip(s.ops, lines[1:]):
        parts = line.split("\t")
        kind = op["op"]
        row = dict(op="AP_" + kind.upper(), config=op.get("config") or 0, key=key_text(op.get("key")), action=action_code(op.get("action")),
                   placement=-1, all=0, sample=-1, mapping=op.get("mapping") or 0, catalogue=-1, ok=1, text=0, answer=-1, explicit=0,
                   first=0, count=0)
        if kind == "gear":
            assert parts[0] == "R"
            real_gear = [parts[2 + i:6 + i] for i in range(0, int(parts[1]) * 4, 4)]
            for g_kind, name, ident, type_id in real_gear:
                gear.append("{%s,%s,%s,%d,%s}" % (g_kind, c_string(name), "0" if ident == "-" else ident + "u", int(ident != "-"),
                                                  c_string(None if type_id == "-" else type_id)))
            row["first"], row["count"] = 0, len(real_gear)
        elif kind == "catalogue":
            assert parts[0] == "G"
            if op.get("real"):
                row["catalogue"] = -1
            else:
                listed = op.get("rows")
                first = len(gear)
                if isinstance(listed, list):
                    for g in listed:
                        if g is None: break                              # ipairs stops at the hole
                        if not isinstance(g, dict):
                            gear.append("{0,0,0,0,0}"); continue        # a row that is not a table has no name
                        name, ident, type_id, g_kind = g.get("name"), g.get("id"), g.get("type"), g.get("kind")
                        gear.append("{%d,%s,%s,%d,%s}" % (g_kind if is_number(g_kind) else 0, c_string(name if isinstance(name, str) else None),
                                                          "%du" % ident if ident is not None else "0", int(ident is not None),
                                                          c_string(type_id if isinstance(type_id, str) else None)))
                    catalogues.append("{0,%d,%d}" % (first, len(gear) - first))
                else:
                    catalogues.append("{1,0,0}")                         # gear is not a table
                row["catalogue"] = len(catalogues) - 1
            row["first"], row["count"] = weapon_rows(parts[2:])
            assert row["count"] == int(parts[1])
        elif kind == "info":
            assert parts[0] == "I"
            row["ok"] = int(parts[1] != "-")
            if row["ok"]: row["first"], row["count"] = weapon_rows(parts[1:])
        elif kind in ("baseline", "resolve", "default"):
            if parts[0] == "E":
                row["ok"], row["text"] = 0, texts.add(c_string(parts[1]))
            elif parts[1] == "-":
                row["ok"] = 0
            else:
                row["answer"] = place(int(parts[1]), [float(v) for v in parts[2:8]])
                row["explicit"] = int(parts[8]) if len(parts) > 8 else 0
        elif kind == "valid":
            row["placement"], row["ok"] = place_in(op.get("p")), int(parts[1])
        elif kind == "saved":
            row["placement"] = place_in(op.get("value"))
            row["all"] = int(op.get("all") is not None and op.get("all") is not False)      # Lua truth
            if parts[0] == "E":
                row["ok"], row["text"] = 0, texts.add(c_string(parts[1]))
            else:
                row["first"], at = len(profiles), 2
                for _ in range(int(parts[1])):
                    ident, n = parts[at], int(parts[at + 1]); at += 2
                    has, cells = 0, [-1] * len(ACTIONS)
                    for _ in range(n):
                        a = ACTIONS.index(parts[at])
                        has |= 1 << a
                        cells[a] = place(int(parts[at + 1]), [float(v) for v in parts[at + 2:at + 8]]); at += 8
                    profiles.append("{%s,%d,{%s}}" % (c_string(ident), has, ",".join(map(str, cells))))
                assert at == len(parts)
                row["count"] = len(profiles) - row["first"]
        elif kind == "select":
            if parts[0] != "S":
                sys.exit("select raised: " + line)
            v = op["sample"]
            ads = v.get("ads")
            ads_kind = 1 if ads is True else 2 if is_number(ads) else 2 if ads is ONE else 0
            ads_value = float(ads) if is_number(ads) else 1.0 if ads is ONE else 0.0
            resource = v.get("action_resource")
            check = v.get("action_verified")
            verified = 1 if check is True else 0 if check is None or check is False else 2
            stance = v.get("stance_flags")
            truth = lambda name: int(v.get(name) is not None and v.get(name) is not False)
            l1, l3 = v.get("l1"), v.get("l3")
            row["sample"] = samples.add("{%d,%s,%s,%d,%d,{%s},%d,%d,%d,%d,%d,%d,%d}" % (
                ads_kind, c_double(ads_value), c_string(resource if isinstance(resource, str) else None), verified,
                len(stance) if isinstance(stance, bytes) else -1, ",".join(map(str, stance)) if isinstance(stance, bytes) and stance else "0",
                truth("reload"), truth("shoulder_aim"), truth("sprint"), int(l1 is not None), l1 or 0, int(l3 is not None), l3 or 0))
            row["answer"] = ACTIONS.index(parts[1])
        rows.append("  {%s,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d}," % (
            row["op"], row["config"], row["key"], row["action"], row["placement"], row["all"], row["sample"], row["mapping"],
            row["catalogue"], row["ok"], row["text"], row["answer"], row["explicit"], row["first"], row["count"]))
    names = constants[1:-1]
    out = [stamp, "#include <math.h>",
           "typedef struct APPlacement { unsigned present; double v[6]; } APPlacement;",
           "typedef struct APProfile { const char *id; unsigned has; int action[9]; } APProfile; /* cells: index into ap_placements */",
           "typedef struct APLegacy { const char *id; int placement; } APLegacy;",
           "typedef struct APConfig { int nil, family, placement, first_profile, profile_count, first_legacy, legacy_count; } APConfig;",
           "typedef struct APMapRow { const char *resource, *states, *action; } APMapRow;",
           "typedef struct APMapping { int nil, first, count; } APMapping;",
           "typedef struct APGear { int kind; const char *name; unsigned id; int has_id; const char *type; } APGear;",
           "typedef struct APCatalogue { int not_a_table, first, count; } APCatalogue;",
           "typedef struct APWeapon { const char *key; int ambiguous; const char *name; int family, kind; unsigned item; int has_item; } APWeapon;",
           "typedef struct APSample { int ads_kind; double ads; const char *resource; int verified, flags_size; unsigned char flags[32];",
           "  int reload, shoulder_aim, sprint, has_l1, l1, has_l3, l3; } APSample;",
           "/* action: -1 nil, 0..8 the list, 99 a name outside it. config and mapping 0: nil. catalogue -1: the kit's gear. */",
           "typedef struct APVector { int op, config; const char *key; int action, placement, all, sample, mapping, catalogue;",
           "  int ok, text, answer, is_explicit, first, count; } APVector;",
           "enum { AP_GEAR, AP_CATALOGUE, AP_INFO, AP_BASELINE, AP_VALID, AP_RESOLVE, AP_DEFAULT, AP_SAVED, AP_SELECT };",
           "static const char *const ap_action_names[] = {%s};" % ",".join(c_string(n) for n in names[0::3]),
           "static const char *const ap_action_labels[] = {%s};" % ",".join(c_string(n) for n in names[1::3]),
           "static const int ap_action_observed[] = {%s};" % ",".join(names[2::3]),
           "static const int ap_observed_count = %s;" % constants[-1],
           "static const char *const ap_family_names[] = {%s};" % ",".join(c_string(None if n == "-" else n) for n in FAMILIES),
           "static const char *const ap_texts[] = {%s};" % ",".join(texts.rows)]
    for name, kind, table in (("ap_placements", "APPlacement", placements.rows), ("ap_profiles", "APProfile", profiles),
                              ("ap_legacies", "APLegacy", legacies or ["{0,0}"]), ("ap_configs", "APConfig", configs),
                              ("ap_map_rows", "APMapRow", map_rows), ("ap_mappings", "APMapping", mappings), ("ap_gear", "APGear", gear),
                              ("ap_catalogues", "APCatalogue", catalogues), ("ap_weapons", "APWeapon", weapons),
                              ("ap_samples", "APSample", samples.rows)):
        out.append("static const %s %s[] = {\n  %s\n};" % (kind, name, ",\n  ".join(table)))
    out += ["static const APVector ap_vectors[] = {"] + rows + ["};"]
    return "\n".join(out) + "\n", real_gear


def data_header(real_gear, weapons_line, commit, digest):
    parts = weapons_line.split("\t")
    count = int(parts[1])
    cells = [parts[2 + i:7 + i] for i in range(0, count * 5, 5)]
    ambiguous = sum(name == "!" and family == "-" for _, name, family, _, _ in cells)
    families = {}
    for _, name, family, _, _ in cells:
        if not (name == "!" and family == "-"): families[family] = families.get(family, 0) + 1
    out = ["/* Generated by tools/retail_ref/gen_action_placement.py. Do not edit.",
           f" * The `gear` table of the kit's src/loadout_catalogue.lua at commit {commit}, sha256 {digest}:",
           f" * {len(real_gear)} rows in the kit's order, the four fields action_placement.lua reads (kind, name, id, type).",
           " * tfp_action_configure_catalogue() turns them into the weapon table, as retail's configure_catalogue does",
           f" * at enable: {count} weapons keyed by the 16 lower-case hex characters of the unit type id in memory order",
           " * (the catalogue's `type` with its eight bytes reversed), which is the placement key the pose backend reads",
           f" * at weapon unit object + 0x358. {ambiguous} of them ambiguous (two rows, one type, different name or family).",
           " * By family: " + ", ".join(f"{name} {families.get(name, 0)}" for name in FAMILIES[1:]) + ". */",
           "#ifndef TFP_ACTION_PLACEMENT_DATA_H", "#define TFP_ACTION_PLACEMENT_DATA_H", '#include "action_select.h"',
           f'#define TFP_ACTION_GEAR_COMMIT "{commit}"', f'#define TFP_ACTION_GEAR_SHA256 "{digest}"',
           f"#define TFP_ACTION_GEAR_COUNT {len(real_gear)}",
           "static const TfpActionGearRow tfp_action_gear[TFP_ACTION_GEAR_COUNT] = {"]
    for kind, name, ident, type_id in real_gear:
        out.append("  {%s, %s, %s, %s, %s}," % (kind, c_string(name), "0" if ident == "-" else ident + "u", "true" if ident != "-" else "false",
                                               c_string(None if type_id == "-" else type_id)))
    out += ["};", "#endif"]
    return "\n".join(out) + "\n"


def build(retail):
    """The whole script. The family keys come from the kit's catalogue, read by the retail module itself."""
    probe = Script()
    probe.add("gear"); probe.add("catalogue", real=True)
    lines = run(retail, lua_script(probe))
    parts = lines[2].split("\t")
    real_keys = {}
    for at in range(2, len(parts), 5):
        key, name, family = parts[at:at + 3]
        if not (name == "!" and family == "-"): real_keys.setdefault(family, key)
    if sorted(real_keys) != sorted(FAMILIES[1:]):
        sys.exit("the catalogue no longer holds every family: " + ", ".join(sorted(real_keys)))
    s = Script()
    s.add("gear")
    fixtures(s)
    edges(s, real_keys)
    random_ops(s, real_keys, 9000, 25480438)
    return s, lines[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--data", type=Path)
    parser.add_argument("--script-only", type=Path)
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    s, weapons_line = build(args.retail)
    if args.script_only:
        args.script_only.write_text(lua_script(s))
        return 0
    digest = hashlib.sha256((args.retail / "src/first_person/action_placement.lua").read_bytes()).hexdigest()
    catalogue_digest = hashlib.sha256((args.retail / "src/loadout_catalogue.lua").read_bytes()).hexdigest()
    lines = run(args.retail, lua_script(s))
    if len(lines) != len(s.ops) + 1:
        sys.exit(f"{len(lines) - 1} results for {len(s.ops)} operations")
    stamp = (f"/* Generated by tools/retail_ref/gen_action_placement.py. Do not edit.\n * Retail file: src/first_person/action_placement.lua "
             f"at commit {commit}, sha256 {digest};\n * catalogue src/loadout_catalogue.lua, sha256 {catalogue_digest}.\n"
             f" * {len(s.ops)} operations replayed against that file under LuaJIT; doubles are exact. */\n"
             f'#define ACTION_PLACEMENT_VECTOR_COMMIT "{commit}"\n#define ACTION_PLACEMENT_VECTOR_SHA256 "{digest}"\n'
             f'#define ACTION_PLACEMENT_CATALOGUE_SHA256 "{catalogue_digest}"')
    text, real_gear = header(s, lines, stamp)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "action_placement_vectors.h").write_text(text)
    if args.data:
        (args.data / "action_placement_data.h").write_text(data_header(real_gear, weapons_line, commit, catalogue_digest))
    counts = {}
    for op in s.ops:
        counts[op["op"]] = counts.get(op["op"], 0) + 1
    print(f"action_placement: {len(s.ops)} operations (" + ", ".join(f"{v} {k}" for k, v in sorted(counts.items())) + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
