#!/usr/bin/env python3
"""Cut reference vectors for src/locomotion.c from the retail locomotion.lua,
run unmodified under LuaJIT by run_locomotion.lua.

  gen_locomotion.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
  gen_locomotion.py --retail ... --script-only FILE        (the Lua script only; used by the coverage check)

Two parts in one header, tests/vectors/locomotion_vectors.h:
 * the pure functions (rotation_to_parent, to_parent, sprinting): operations and exact answers;
 * the sampler: one logical scene written as fake game memory in retail's layout (answered by the retail
   locomotion.lua over the retail backend.lua and action_state.lua) and in this build's layout (for the C), with the
   count and a hash of the ordered memory reads made outside the player snapshot.
The scenes build on gen_snapshot.py and gen_action_state.py.
"""
import argparse
import hashlib
import math
import random
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_action_state as gas  # noqa: E402
import gen_snapshot as snap     # noqa: E402

HERE = Path(__file__).resolve().parent
FROZEN = "1d0d453"
NAN, INF = float("nan"), float("inf")
u32, u64, edit = gas.u32, gas.u64, gas.edit
RIG, WB, WR, STATE = 0xc40000000, 0xe00000000, 0xe10000000, 0xf00000000
# game.dll places only the sampler's on_foot test reads (backend.lua:364-369); the rest is gen_snapshot's.
RETAIL_GAME = dict(snap.RETAIL, state=0x2ce1508, scene=0xac29c)
HF_GAME = dict(snap.HF, state=0x3326340, scene=0xac21c)
RESOURCE = "4D1C334D294DFA97"


class Raw:
    def __init__(self, text):
        self.text = text


OTHER = Raw("'x'")


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
        return '"%s"' % value
    if isinstance(value, bytes):
        return '"' + "".join("\\%03d" % b for b in value) + '"'
    if isinstance(value, dict):
        return "{" + ",".join(f"{k}={lua(v)}" for k, v in value.items() if v is not None) + "}"
    if isinstance(value, list):
        return "{" + ",".join(lua(v) for v in value) + "}"
    raise TypeError(value)


def c_double(value):
    if value != value:
        return "NAN"
    if value in (INF, -INF):
        return "INFINITY" if value > 0 else "-INFINITY"
    return float(value).hex()


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


# ------------------------------------------------------------- matrices
def basis(yaw=0.0, sx=1.0, sy=1.0, sz=1.0):
    """The fixture's basis(): yaw about +Z, optional scale per axis."""
    c, s = math.cos(yaw), math.sin(yaw)
    return [c * sx, s * sx, 0, 0, -s * sy, c * sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1]


def turned(yaw, pitch, roll, scale=(1.0, 1.0, 1.0), position=(0.0, 0.0, 0.0)):
    """Rows right, forward, up of a rotation (yaw about Z, pitch about X, roll about Y), scaled per axis."""
    cy, sy, cp, sp, cr, sr = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch), math.cos(roll), math.sin(roll)
    rz = [[cy, sy, 0], [-sy, cy, 0], [0, 0, 1]]
    rx = [[1, 0, 0], [0, cp, sp], [0, -sp, cp]]
    ry = [[cr, 0, -sr], [0, 1, 0], [sr, 0, cr]]
    def mul(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    m = mul(ry, mul(rx, rz))
    out = []
    for i in range(3):
        out += [m[i][j] * scale[i] for j in range(3)] + [0.0]
    return out + list(position) + [1.0]


def floats(values):
    """16 numbers as the engine's float matrix."""
    return struct.pack("<16f", *values)


# ------------------------------------------------------------------ pure
def pure_ops():
    ops = []
    def rotation(value, camera, parent): ops.append(dict(op="rotation", value=value, camera=camera, parent=parent))
    def to_parent(value, camera, parent): ops.append(dict(op="to_parent", value=value, camera=camera, parent=parent))
    def sprinting(action, verified=None, flags=None): ops.append(dict(op="sprinting", action=action, verified=verified, flags=flags))
    half = math.pi / 2
    # ---- the retail fixture tests/first_person/locomotion.lua, statement by statement
    to_parent(dict(dx=.02, dy=.03, dz=-.04), basis(half), basis(0))
    to_parent(dict(dx=.02, dy=.03, dz=-.04), basis(half), basis(half))
    full = dict(resource_match=True, layer_count=31)
    for l1 in (75, 78):
        sprinting(dict(full, l14=36, l3=71, l1=l1))
    for action in (dict(full, l14=36, l3=67, l1=78), dict(full, l14=36, l3=67, l1=79), dict(resource_match=False, layer_count=31, l14=36, l3=71),
                   dict(resource_match=True, layer_count=30, l14=36, l3=71), dict(full, l14=103, l3=71)):
        sprinting(action)
    sprinting(None)
    bridge = dict(full, l14=3, l3=71)
    sprinting(bridge); sprinting(bridge, False); sprinting(bridge, True); sprinting(dict(bridge, l14=103), True)
    angles = dict(pitch=-math.radians(20), yaw=math.radians(15), roll=0)
    rotation(angles, basis(0), basis(0)); rotation(angles, basis(half), basis(0))
    rotation(angles, basis(half, 1.5, .75, 1.25), basis(half, .75, 1.5, 1.25))
    rotation(dict(pitch=0, yaw=0, roll=0), basis(0), basis(0))
    rotation(dict(pitch=-math.radians(40), yaw=math.radians(30), roll=0), basis(0), basis(0))
    rotation(dict(pitch=NAN), basis(0), basis(0)); rotation(dict(pitch=-.1), basis(0), basis(0, -1, 1, 1))
    to_parent(dict(dx=.02, dy=.03, dz=.04), basis(half), basis(half, 1.5, .75, 1.25))
    to_parent(dict(dx=.12, dy=.06, dz=-.03), basis(0, 1.5, .75, 1.25), basis(0, .6, .6, .6))
    skew, flat, bad = basis(0), basis(0), basis(0)
    skew[4], flat[0], bad[0] = .2, 0, NAN
    for value in (skew, flat, bad):
        to_parent(dict(dx=.01, dy=0, dz=0), value, basis(0)); to_parent(dict(dx=.01, dy=0, dz=0), basis(0), value)
    to_parent(dict(dx=.151, dy=0, dz=0), basis(0), basis(0))
    for l1 in (75, 78):
        action = dict(full, l14=3, l1=l1, l3=68)
        sprinting(action, True); sprinting(action, False); sprinting(dict(action, l14=36), True)
    # ---- angles: the bound, both sides; what counts as a number; zero in every spelling
    ident, other = basis(0), turned(.7, -.3, .2)
    for name in ("pitch", "yaw", "roll"):
        for v in (half, -half, math.nextafter(half, 4), -math.nextafter(half, 4), math.nextafter(half, 0), NAN, INF, -INF, 1e8, 1e-300, 5e-324,
                  OTHER, False, None, True, -0.0, 0, .5, -1.2):
            rotation({name: v}, ident, other)
            rotation({"pitch": .1, "yaw": -.2, "roll": .3, name: v}, other, ident)
            to_parent({"dx": .01, "dy": .02, "dz": .03, name: v}, other, ident)
    # All three zero: the identity, whatever the matrices are (they are not looked at).
    broken = [NAN] * 16
    for value in ({}, dict(pitch=0), dict(pitch=-0.0, yaw=0, roll=-0.0), dict(pitch=False, yaw=False), dict(dx=1, dy=OTHER)):
        rotation(value, broken, broken); rotation(value, ident, basis(0, -1, 1, 1)); rotation(value, [0.0] * 16, [0.0] * 16)
    # The order of the angle check: pitch, then yaw, then roll, before any matrix.
    rotation(dict(pitch=9, yaw=NAN, roll=OTHER), broken, broken); rotation(dict(pitch=.1, yaw=9), broken, broken)
    # ---- the matrices: each used entry not finite; unused entries may hold anything
    # (A nil entry is not scripted: retail's ipairs stops at it and the dot product then raises Lua's own arithmetic
    # error. A C matrix has 16 numbers, as every matrix on the live path has.)
    for index in range(16):
        for v in (NAN, INF, -INF, 1e8, -1e8, 99999999.0, OTHER):
            m = list(other)
            m[index] = v
            rotation(dict(pitch=.1), m, ident); rotation(dict(pitch=.1), ident, m)
            to_parent(dict(dx=.01, dy=.01, dz=.01), m, other); to_parent(dict(dx=.01, dy=.01, dz=.01), other, m)
    # Scale: squared axis length must be above .25 and below 4, each axis, both matrices.
    for axis in range(3):
        for scale in (.5, math.nextafter(.5, 1), math.nextafter(.5, 0), .5000001, 2, math.nextafter(2, 0), math.nextafter(2, 3), 1.9999999, 0, 1e-200, .51, 1.99):
            scales = [1.0, 1.0, 1.0]
            scales[axis] = scale
            m = turned(.4, .2, -.1, tuple(scales))
            rotation(dict(yaw=.3), m, ident); rotation(dict(yaw=.3), ident, m)
            to_parent(dict(dx=.1, dy=-.1, dz=.05, roll=.2), m, ident); to_parent(dict(dx=.1, dy=-.1, dz=.05, roll=.2), ident, m)
    # Skew: |a.b| must stay below .03 * sqrt(|a|^2 |b|^2), each pair.
    for i, j in ((0, 1), (0, 2), (1, 2)):
        for amount in (.0299, .03, .0301, .029999999, .030000001, -.0299, -.0301, .2, 0):
            m = list(ident)
            m[i * 4 + j] = amount
            rotation(dict(pitch=.2, roll=-.2), m, ident); rotation(dict(pitch=.2, roll=-.2), ident, m)
            to_parent(dict(dx=.05, dy=.05, dz=.05), m, ident); to_parent(dict(dx=.05, dy=.05, dz=.05), ident, m)
            scaled = [v * 1.5 for v in m[:12]] + m[12:]
            rotation(dict(pitch=.2, roll=-.2), scaled, ident); to_parent(dict(dx=.05, dy=.05, dz=.05), ident, scaled)
    # Mirrored bases: refused for a rotation, accepted for a pure displacement.
    for signs in ((-1, 1, 1), (1, -1, 1), (1, 1, -1), (-1, -1, -1), (-1, -1, 1), (1, 1, 1)):
        m = turned(.3, .1, .2, tuple(float(v) for v in signs))
        rotation(dict(yaw=.2), m, ident); rotation(dict(yaw=.2), ident, m); rotation(dict(yaw=.2), m, m)
        to_parent(dict(dx=.05, dy=.02, dz=-.04), m, ident); to_parent(dict(dx=.05, dy=.02, dz=-.04, pitch=.1), ident, m)
    # The largest rotations: the 120 degree cap and a negative w.
    for pitch in (half, -half, 1.2, -1.2, 0):
        for yaw in (half, -half, 1.2, -1.2, 0):
            for roll in (half, -half, 1.2, -1.2, 0):
                rotation(dict(pitch=pitch, yaw=yaw, roll=roll), other, ident)
                rotation(dict(pitch=pitch, yaw=yaw, roll=roll), ident, turned(-1.1, .4, 2.5, (1.3, .8, 1.1)))
    for angle in (math.radians(120), math.radians(120) - .00001, math.radians(120) - .00002, math.radians(119.99), 2.0, 2.09, 2.1):
        for split in (.5, .25, .75):                         # one axis in two steps is not possible here: yaw and roll about different axes
            rotation(dict(yaw=min(half, angle * split), pitch=min(half, angle * (1 - split))), ident, ident)
    # A composed rotation whose w comes out negative before it is normalised (retail then negates all four parts).
    # Found by search with a port of the composition; the answers are retail's.
    def raw_w(value, camera, parent):
        def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
        c, p = [camera[i * 4:i * 4 + 3] for i in range(3)], [parent[i * 4:i * 4 + 3] for i in range(3)]
        def turn(axis, radians):
            v = [dot(c[axis], p[i]) / math.sqrt(dot(c[axis], c[axis]) * dot(p[i], p[i])) for i in range(3)]
            s = math.sin(radians / 2) / math.sqrt(dot(v, v))
            return [v[0] * s, v[1] * s, v[2] * s, math.cos(radians / 2)]
        def mul(a, b):
            return [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                    a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]]
        return mul(mul(turn(2, value["yaw"]), turn(0, value["pitch"])), turn(1, value["roll"]))[3]
    search, found = random.Random(120), 0
    while found < 40:
        camera, parent = turned(search.uniform(-3, 3), search.uniform(-1, 1), search.uniform(-3, 3)), turned(search.uniform(-3, 3), search.uniform(-1, 1), search.uniform(-3, 3))
        for m in (camera, parent):
            for index in (1, 2, 4, 6, 8, 9):
                m[index] += search.uniform(-.012, .012)
        value = dict(pitch=search.choice((half, -half)), yaw=search.choice((half, -half)), roll=search.choice((half, -half)))
        if raw_w(value, camera, parent) < -1e-4:
            found += 1
            rotation(value, camera, parent); to_parent(dict(value, dx=.05, dy=-.05, dz=.1), camera, parent)
    # ---- to_parent: the displacement bound; nil ends the list; not a table
    for name in ("dx", "dy", "dz"):
        for v in (.15, -.15, math.nextafter(.15, 1), -math.nextafter(.15, 1), math.nextafter(.15, 0), NAN, INF, 1e8, OTHER, False, True, None, 0, -0.0):
            value = dict(dx=.01, dy=-.02, dz=.03)
            value[name] = v
            to_parent(value, other, ident); to_parent(dict(value, yaw=.1), ident, other)
    to_parent(dict(dy=9, dz=9), ident, ident); to_parent(dict(dx=.1, dz=9), ident, ident); to_parent(dict(dx=.1, dy=.1), ident, ident)
    to_parent({}, ident, ident); to_parent(dict(pitch=.2), ident, other); to_parent(dict(dx=9, pitch=9), ident, ident)
    for value in (None, Raw("5"), OTHER, Raw("false")):
        to_parent(value, ident, ident); to_parent(value, broken, broken)
    # Order: camera basis, parent basis, the displacement, then the rotation.
    to_parent(dict(dx=9, pitch=9), broken, basis(0, 3)); to_parent(dict(dx=9, pitch=9), ident, basis(0, 3)); to_parent(dict(dx=.1, pitch=9), ident, ident)
    # The parent scale divides, the camera scale drops out, and an amplified result is scaled back to .15.
    for cs in ((1, 1, 1), (1.5, .75, 1.25), (.51, 1.99, 1), (1.9, 1.9, 1.9)):
        for ps in ((1, 1, 1), (.6, .6, .6), (.51, .51, .51), (1.9, .6, 1), (.6, 1.9, .51)):
            for value in (dict(dx=.15, dy=.15, dz=.15), dict(dx=-.15, dy=.06, dz=-.03), dict(dx=.12, dy=.06, dz=-.03), dict(dx=.0001, dy=0, dz=0),
                          dict(dx=.15, dy=0, dz=0, yaw=.4), dict(dx=0, dy=-.15, dz=0), dict(dx=0, dy=0, dz=.15, pitch=-.3, roll=.3)):
                to_parent(value, turned(.3, -.2, .1, cs), turned(-.8, .1, .4, ps))
                to_parent(value, basis(.5, *cs), basis(.5, *ps))
    # ---- sprinting: every part of the condition
    gait, walk, jog = bytes(9) + b"\x84" + bytes(14), bytes(9) + b"\x88" + bytes(14), bytes(9) + b"\x90" + bytes(14)
    for flags in (gait, walk, jog, bytes(24), bytes(9) + b"\x04" + bytes(14), bytes(9) + b"\xfb" + bytes(14), bytes(9) + b"\xff" + bytes(14),
                  gait[:23], gait + b"\0", b"", None, Raw("24"), bytes(8) + b"\x04" + bytes(15), bytes(10) + b"\x04" + bytes(13)):
        for l14 in (36, 3, 103, None):
            for verified in (True, False, None, Raw("1")):
                for l3, l1 in ((71, 75), (68, 75), (68, 78), (68, 79), (67, 78), (None, None), (68, None), (71, None), (0, 0)):
                    sprinting(dict(full, l14=l14, l3=l3, l1=l1), verified, flags)
    for match in (True, False, None, Raw("1"), OTHER):
        for count in (31, 30, 32, None, Raw("'31'")):
            for flags in (gait, walk):
                sprinting(dict(resource_match=match, layer_count=count, l14=36, l3=71, l1=78), True, flags)
    for action in (None, Raw("5"), OTHER, Raw("false"), {}):
        sprinting(action, True, gait)
    # ---- seeded random runs
    rng = random.Random(25480438)
    def any_basis():
        scale = tuple(rng.choice((1.0, 1.0, rng.uniform(.52, 1.95), rng.uniform(.4, 2.2))) for _ in range(3))
        m = turned(rng.uniform(-math.pi, math.pi), rng.uniform(-1.5, 1.5), rng.uniform(-math.pi, math.pi), scale,
                   (rng.uniform(-900, 900), rng.uniform(-900, 900), rng.uniform(-50, 50)))
        if rng.random() < .5: m = list(struct.unpack("<16f", floats(m)))            # as read from the engine: floats
        if rng.random() < .05: m[rng.choice((1, 2, 4, 6, 8, 9))] += rng.choice((.02, .028, .031, .05, -.03))
        if rng.random() < .03: m[rng.randrange(16)] = rng.choice((NAN, INF, 1e9))
        if rng.random() < .03: m = [-v for v in m[:4]] + m[4:]
        return m
    def any_value():
        value = dict(dx=rng.uniform(-.15, .15), dy=rng.uniform(-.15, .15), dz=rng.uniform(-.15, .15))
        if rng.random() < .7:
            value.update(pitch=rng.uniform(-half, half) * rng.choice((1, 1, .2, 0)), yaw=rng.uniform(-half, half) * rng.choice((1, 1, .2, 0)),
                         roll=rng.uniform(-half, half) * rng.choice((1, .2, 0, 0)))
        if rng.random() < .04: value[rng.choice(("dx", "dy", "dz"))] = rng.choice((.16, -.2, NAN, None))
        if rng.random() < .04: value[rng.choice(("pitch", "yaw", "roll"))] = rng.choice((1.6, -3, NAN, None, False))
        return value
    for _ in range(7000):
        (to_parent if rng.random() < .7 else rotation)(any_value(), any_basis(), any_basis())
    for _ in range(2500):
        action = dict(resource_match=rng.choice((True, True, True, False)), layer_count=rng.choice((31, 31, 31, 30)),
                      l14=rng.choice((36, 36, 3, 3, 103, None)), l3=rng.choice((71, 68, 67, 66, 65, None)), l1=rng.choice((75, 78, 79, 0, None)))
        flags = bytes(rng.getrandbits(8) if i in (0, 9, 10, 12) else 0 for i in range(24))
        sprinting(action, rng.choice((True, False, None)), rng.choice((flags, flags, flags, None, flags[:23])))
    return ops


# --------------------------------------------------------------- sampler
def scene(body=401, rig=801):
    """The action-state scene plus what the sampler reads: the body's root matrix, the rig's node 4, the camera
    matrix, the scene-graph getter, and the game state on_foot looks at. A native first-person view (kind 6)."""
    a = gas.scene(body)
    a["snap"]["rows"] = {0: [6, 101, 201, body, 0]}
    a["snap"]["rig"] = rig
    a.update(rig=rig, rig_object=RIG, rig_vt=None, rig_count=63, rig_generation=None, body_count=1, body_worlds=WB, rig_worlds=WR,
             slot_graph=None, rig_slot_graph=None,
             root=turned(.5, 0, 0, (1, 1, 1), (120.5, -64.25, 3.125)), parent=turned(.45, -.1, .05, (1, 1, 1), (120.6, -64.0, 4.5)),
             camera=turned(.47, -.12, .02, (1, 1, 1), (120.55, -64.1, 4.8)), state=STATE, scene=4, root_bytes=None, parent_bytes=None,
             camera_bytes=None)
    return a


def build(layout, exe_layout, a):
    m = gas.build(layout, exe_layout, a)
    def put(name, at, data):
        if name not in a["omit"]:
            m[at] = bytes(data)
    getter = a["exe"] + exe_layout["graph"]
    put("slot_graph", a["vt"] + 0xE8, u64(getter if a["slot_graph"] is None else a["slot_graph"]))
    put("body_count", a["object"] + 0x60 + 0x10, u32(a["body_count"]))
    put("body_worlds", a["object"] + 0x60 + 0x28, u64(a["body_worlds"]))
    put("root", a["body_worlds"], a["root_bytes"] if a["root_bytes"] is not None else floats(a["root"]))
    gas.build_unit(a, m, a["rig"], a["rig_object"], a["rig_generation"])
    rig_vt = a["vt"] if a["rig_vt"] is None else a["rig_vt"]
    put("rig_vt", a["rig_object"], u64(rig_vt))
    if rig_vt != a["vt"]:
        put("rig_slot_graph", rig_vt + 0xE8, u64(getter if a["rig_slot_graph"] is None else a["rig_slot_graph"]))
    put("rig_count", a["rig_object"] + 0x60 + 0x10, u32(a["rig_count"]))
    put("rig_worlds", a["rig_object"] + 0x60 + 0x28, u64(a["rig_worlds"]))
    put("parent", a["rig_worlds"] + 4 * 0x40, a["parent_bytes"] if a["parent_bytes"] is not None else floats(a["parent"]))
    put("camera_matrix", a["snap"]["context"] + 0x0C, a["camera_bytes"] if a["camera_bytes"] is not None else floats(a["camera"]))
    put("state_global", snap.BASE + layout["state"], u64(a["state"]))
    put("scene", a["state"] + layout["scene"], u32(a["scene"]))
    return {at: data for at, data in m.items() if 0 <= at < 1 << 48}


def place(address, exe=gas.EXE):
    """A retail read address as this build's."""
    if snap.BASE <= address < snap.BASE + 0x5000000:
        rva = address - snap.BASE
        if RETAIL_GAME["state"] <= rva < RETAIL_GAME["state"] + 8:
            return snap.BASE + HF_GAME["state"] + rva - RETAIL_GAME["state"]
        sys.exit(f"retail read a game.dll address this tool cannot place: game+{rva:#x}")
    for state in (STATE, STATE + 0x2000000, 65536):               # the scene number, 4 bytes at state + its place in this build
        if state + RETAIL_GAME["scene"] <= address < state + RETAIL_GAME["scene"] + 4:
            return address - RETAIL_GAME["scene"] + HF_GAME["scene"]
    if snap.POOL + RETAIL_GAME["flags"] <= address < snap.POOL + RETAIL_GAME["flags"] + 8 * RETAIL_GAME["stride"]:
        slot, inside = divmod(address - snap.POOL - RETAIL_GAME["flags"], RETAIL_GAME["stride"])
        return snap.POOL + HF_GAME["flags"] + slot * HF_GAME["stride"] + inside
    return gas.to_here(address, exe)


RECORDS = ["exe_header", "exists", "trigger", "world_pose", "registry_global", "metadata", "generation_401", "unit_401", "generation_801",
           "unit_801", "vt", "slot_graph", "body_count", "body_worlds", "root", "rig_vt", "rig_count", "rig_worlds", "parent",
           "camera_matrix", "state_global", "scene", "slot_anim", "resource", "instance_ptr", "graph_ptr", "active_ptr", "header",
           "definitions", "def1", "entries3", "def14", "active"]


def flagged(**at):
    data = bytearray(24)
    for index, value in at.items():
        data[int(index[1:])] = value
    return bytes(data)


def with_floats(values, **change):
    """A matrix's bytes with float entries replaced (index -> value)."""
    out = list(struct.unpack("<16f", floats(values)))
    for index, value in change.items():
        out[int(index[1:])] = value
    return struct.pack("<16f", *out)


def variants():
    base = scene()
    bridge = edit(base, layer={13: dict(current=3)}, snap_flags=flagged(b0=2))
    return [("other_player", edit(base, snap_local=[11, 23, 101, 401, 301, 1])), ("blocked", edit(base, snap_controls=bytes([2, 0, 0, 0, 0, 0]))),
            ("seated", edit(base, snap_flags=flagged(b14=4))), ("kind4", edit(base, snap_rows={0: [4, 101, 201, 401, 0]})),
            ("other_rig", edit(base, snap_rig=802)), ("rig_object_moved", edit(base, rig_object=RIG + 0x10000)),
            ("body_object_moved", edit(base, object=gas.OBJ + 0x10000)), ("generation_bumped", edit(base, generation=1)),
            ("rig_generation_bumped", edit(base, rig_generation=1)), ("root_moved", edit(base, root=turned(.5, 0, 0, (1, 1, 1), (1, 2, 3)))),
            ("parent_turned", edit(base, parent=turned(1.45, -.1, .05))), ("camera_turned", edit(base, camera=turned(-2.0, .6, .02))),
            ("camera_bad", edit(base, camera_bytes=with_floats(base["camera"], i5=NAN))), ("world_pose_changed", edit(base, flip={"world_pose": 9})),
            ("exe_other_build", edit(base, stamp_delta=1)), ("capacity_small", edit(base, capacity=500)),
            ("getter_changed", edit(base, slot_graph=gas.EXE + 0x123400)), ("rig_small", edit(base, rig_count=50)),
            ("sprint_state", edit(base, layer={2: dict(current=71)})), ("gait", edit(base, snap_flags=flagged(b9=0x84))),
            ("bridge", bridge), ("bridge_scene5", edit(bridge, scene=5)), ("bridge_state_moved", edit(bridge, state=STATE + 0x2000000)),
            ("bridge_flags", edit(bridge, snap_flags=flagged(b0=6))), ("anim_broken", edit(base, omit={"active"})),
            ("other_resource", edit(base, resource=bytes([1] * 8))), ("reloading", edit(base, snap_flags=flagged(b15=2))),
            ("root_gone", edit(base, omit={"root"})), ("unit_gone", edit(base, omit={"unit_801"})), ("no_player", edit(base, snap_player_count=0))]


def sampler_script(s):
    base = scene()
    s.base = s.image(base)
    def one(a, **more):
        s.sequence([s.step(a, rig_object=more.pop("rig_object", a["rig_object"]), **more)])
    one(base)
    # What the sample carries: flags, aim, sights, reload, the action layers, sprint by each of its three ways.
    for flags in (bytes(24), flagged(b9=0x84), flagged(b9=0x88), flagged(b9=0x90), flagged(b10=4, b15=32), flagged(b15=2), flagged(b15=34, b10=4),
                  flagged(b6=1), flagged(b9=0x20, b10=2), flagged(b9=0x44, b12=0x20), bytes(range(1, 14)) + bytes([0]) + bytes(range(15, 25)),
                  flagged(b0=2, b9=4), flagged(b0=2), flagged(b0=6), flagged(b0=3), flagged(b0=0xfb), flagged(b1=2), flagged(b23=0xff, b22=0x80),
                  flagged(b0=2, b3=0x80, b4=1)):
        for controls in (bytes(6), bytes([0, 0, 0, 0, 1, 0])):
            for l1, l3, l14 in ((0, 0, 36), (78, 71, 36), (75, 68, 3), (78, 68, 3), (79, 68, 3), (75, 71, 3), (0, 0, 3), (78, 67, 36), (0, 71, 103)):
                for scene_number in (4, 3) if l14 == 3 else (4,):
                    one(edit(base, snap_flags=flags, snap_controls=controls, scene=scene_number,
                             layer={0: dict(current=l1), 2: dict(current=l3), 13: dict(current=l14)}))
    # on_foot's bridge test (l14 == 3): the control flags, the scene number, each read.
    bridge = edit(base, layer={2: dict(current=71), 13: dict(current=3)}, snap_flags=flagged(b0=2))
    for scene_number in (0, 1, 2, 3, 4, 5, 0xffffffff, 0x10003, 0x104):
        one(edit(bridge, scene=scene_number))
    for b0 in range(0, 16):
        one(edit(bridge, snap_flags=flagged(b0=b0))); one(edit(bridge, snap_flags=flagged(b0=b0 | 0xf0, b3=0x80)))
    for state in (0, 65535, 65536, gas.HIGH, gas.HIGH - 1, STATE + 0x2000000):
        one(edit(bridge, state=state))
    one(edit(bridge, omit={"state_global"})); one(edit(bridge, omit={"scene"})); one(edit(bridge, resource=bytes([1] * 8)))
    one(edit(bridge, count=30)); one(edit(bridge, layer={13: dict(current=None)})); one(edit(bridge, layer={13: dict(pointer=0xdefa00)}))
    for slot in (1, 2, 7):
        a = edit(bridge, snap_slot=slot, snap_pool_count=8)
        a["snap"]["pool_map"] = dict(a["snap"]["pool_map"], index=slot)
        one(a); one(edit(a, scene=5))
    # The view: safe and kind 6.
    one(edit(base, snap_rows={0: [4, 101, 201, 401, 0]})); one(edit(base, snap_rows={0: [10, 101, 201, 401, 0]}))
    one(edit(base, snap_controls=bytes([2, 0, 0, 0, 0, 0]))); one(edit(base, snap_controls=bytes([0, 0, 0, 0, 0, 1])))
    one(edit(base, snap_flags=flagged(b14=4))); one(edit(base, snap_player_count=0)); one(edit(base, snap_rows={0: [6, 102, 201, 401, 0]}))
    # The lease.
    other_player = edit(base, snap_local=[11, 23, 101, 401, 301, 1])
    one(base, no_lease=True); one(base, other_lease=True); one(base, done=True); one(base, cleanup_only=True); one(base, done=False, cleanup_only=False)
    one(base, done=True, cleanup_only=True)
    for delta in (1, -1, 0xe8, -0x200):
        one(base, request_delta=delta)
    s.sequence([s.step(base, rig_object=RIG, origin=s.image(other_player))]); s.sequence([s.step(base, rig_object=RIG, origin=s.image(edit(base, snap_player_count=0)))])
    queue = edit(base, snap_tail=2, snap_rows={0: [6, 101, 201, 401, 0], 1: [6, 101, 201, 401, 0]})
    s.sequence([s.step(queue, rig_object=RIG, origin=s.image(base))]); s.sequence([s.step(base, rig_object=RIG, origin=s.image(queue))]); one(queue)
    for rig_object in (RIG + 8, 0, gas.OBJ, RIG - 1):
        one(base, rig_object=rig_object)
    # The executable and the world-pose wrapper, byte by byte.
    one(edit(base, stamp_delta=1)); one(edit(base, mz=b"ZM")); one(edit(base, flip={"exists": 5})); one(edit(base, flip={"trigger": 5}))
    for at in range(48):
        one(edit(base, flip={"world_pose": at}))
    for exe in (0, 65535, gas.HIGH - 1, gas.EXE + 0x10):
        s.sequence([s.step(base, rig_object=RIG)], exe=exe)
    # The two Unit lookups: capacity, generation, the scene-graph getter, node counts.
    for capacity in (0, 401, 402, 801, 802, 0x400000, 0x400001, 0xffffffff):
        one(edit(base, capacity=capacity))
    one(edit(base, generation=1)); one(edit(base, rig_generation=1)); one(edit(base, generation=255, rig_generation=255))
    for field in ("registry", "generations", "units", "object", "rig_object", "vt", "rig_vt", "body_worlds", "rig_worlds"):
        for value in (0, 65535, 65536, gas.HIGH, gas.HIGH - 8, gas.HIGH - 0x70, gas.HIGH - 0x100, 0xffffffffffffffff, (1 << 53) + 1):
            a = edit(base, **{field: value})
            one(a, rig_object=RIG if field != "rig_object" else value)
    for value in (gas.EXE + 0x123400, 0, gas.EXE + gas.HF_EXE["anim"], gas.EXE + gas.RETAIL_EXE["anim"]):
        one(edit(base, slot_graph=value)); one(edit(base, rig_vt=gas.VT + 0x1000, rig_slot_graph=value))
    one(edit(base, rig_vt=gas.VT + 0x1000))
    for count in (0, 1, 2, 512, 513, 0xffffffff):
        one(edit(base, body_count=count))
    for count in (0, 1, 50, 51, 63, 512, 513, 0xffffffff):
        one(edit(base, rig_count=count))
    for body, rig in ((401, 0), (401, 0xffffffff), (401, 401), ((3 << 22) | 401, (5 << 22) | 801), (1, 2), (4095, 4094), ((255 << 22) | 7, (1 << 22) | 9)):
        a = scene(body, rig)
        one(a); one(edit(a, generation=7)); one(edit(a, rig_generation=9))
    # References whose generation bits reach past eight bits, the largest index, and a registry of one.
    for body, rig, capacity in (((1 << 30) | 401, (3 << 30) | 801, 4096), ((5 << 22) | 0x3fffff, (9 << 22) | 0x3ffffe, 0x400000),
                                (0xfffffffe, 0xfffffffd, 0x400000), (1 << 22, 1 << 22, 1), (1 << 22, 1 << 22, 2), (1 << 22, 1 << 22, 0)):
        one(edit(scene(body, rig), capacity=capacity))
    # The three matrices: every float of each not finite or out of shape; the root position; the facing.
    for which in ("root", "parent", "camera"):
        for index in range(16):
            for value in (NAN, INF, -INF, 1e8, 99999992.0, -1e8, 3e38, 0.0):
                one(edit(base, **{which + "_bytes": with_floats(base[which], **{f"i{index}": value})}))
        for scale in (.5, .50001, .49999, 2, 1.99999, 2.00001):
            one(edit(base, **{which: turned(.5, -.1, .05, (scale, 1, 1), (1, 2, 3))})); one(edit(base, **{which: turned(.5, -.1, .05, (1, 1, scale), (1, 2, 3))}))
        for amount in (.0299, .0301, -.0301, .2):
            m = list(base[which])
            m[1] += amount
            one(edit(base, **{which: m}))
        one(edit(base, **{which: turned(.5, 0, 0, (-1, 1, 1))}))
    for position in ((0, 0, 0), (-0.0, 0, 0), (99999992.0, -99999992.0, 99999992.0), (1e8, 0, 0), (0, -1e8, 0), (0, 0, 1e8), (16384, -16384, 0),
                     (1e-40, 1e-45, -1e-45)):
        one(edit(base, root=turned(.5, 0, 0, (1, 1, 1), position)))
    for pitch in (0, math.pi / 2, -math.pi / 2, 1.5707, 1.57079, 1.570786, 1.5707863, -1.570786, 1.2, -1.2, .3):
        for yaw in (0, 1, -2.5, math.pi, math.pi / 2):
            one(edit(base, camera=turned(yaw, pitch, 0)))
    for forward in ((0, 0, 1), (1e-5, 0, 1), (0, 1.0000001e-5, 1), (7.0e-6, 7.1e-6, 1), (7.1e-6, 7.1e-6, 1), (1.001e-5, 0, -1), (0, 0, -1)):
        m = [1, 0, 0, 0, forward[0], forward[1], forward[2], 0, 0, -1, 0, 0, 0, 0, 0, 1]
        one(edit(base, camera=m))
    # Every record unreadable, one at a time.
    for name in RECORDS:
        one(edit(base, omit={name})); one(edit(bridge, omit={name}))
    for name in ("camera_global", "queue", "player_count", "local", "flags", "rig"):
        a = edit(base); a["snap"]["omit"].add(name); one(a)
    # The same sampler again: the backend keeps one action-state sampler, with its cache.
    names = dict(variants())
    stale = edit(base, layer={13: dict(entries=[0x100000 + 13 * 0x10000 + (79 - st) * 8 for st in range(80)],
                                       pointer=gas.GRAPH + 0x50 + 0x1000 + 13 * 0x3000 + 0x100000 + 13 * 0x10000 + 36 * 8)})
    s.sequence([s.step(base, rig_object=RIG), s.step(stale, rig_object=RIG), s.step(names["bridge"], rig_object=RIG), s.step(stale, rig_object=RIG)])
    s.sequence([s.step(stale, rig_object=RIG)])
    for name, a in variants():
        s.sequence([s.step(base, rig_object=RIG), s.step(a, rig_object=a["rig_object"]), s.step(base, rig_object=RIG), s.step(a, rig_object=RIG)])
    # backend:on_foot by itself: every kind of action answer on every relevant scene, each fault of the snapshot it is
    # given, and the scene changing between its reads.
    flag3, flag4 = edit(bridge, snap_flags=flagged(b0=2, b3=0x80)), edit(bridge, snap_flags=flagged(b0=2, b4=1))
    answers = ["nil"] + [dict(resource_match=m, layer_count=c, l14=l14) for m in (True, False) for c in (31, 30) for l14 in (36, 3, 35, 37, 2, 4, None)]
    targets = [base, bridge, edit(bridge, scene=3), edit(bridge, scene=5), edit(bridge, scene=2), flag3, flag4, edit(bridge, snap_flags=flagged(b0=6)),
               edit(bridge, snap_flags=flagged(b0=0)), edit(bridge, omit={"state_global"}), edit(bridge, omit={"scene"}),
               edit(bridge, state=STATE + 0x2000000), edit(bridge, snap_player_count=0)]
    for a in targets:
        for answer in answers:
            s.sequence([dict(image=s.image(a), rig_object=RIG, on_foot=answer)])
        for fault in ("index", "address"):
            s.sequence([dict(image=s.image(a), rig_object=RIG, on_foot=dict(resource_match=True, layer_count=31, l14=3), edit=fault)])
    for a in targets[2:] + [base]:
        for k in range(1, 9):
            for start in (bridge, edit(bridge, scene=3)):
                s.sequence([dict(image=s.image(start), rig_object=RIG, on_foot=dict(resource_match=True, layer_count=31, l14=3), triggers=[[k, s.image(a)]])])
                s.sequence([dict(image=s.image(start), rig_object=RIG, on_foot=dict(resource_match=True, layer_count=31, l14=3),
                                 triggers=[[k, s.image(a)], [k + 1, s.image(start)]])])
    # The bridge sample with the scene changing under on_foot, read by read.
    for a in targets[2:]:
        for k in range(50, 92):
            s.sequence([dict(image=s.image(bridge), rig_object=RIG, triggers=[[k, s.image(a)]])])
    # Memory that changes while it is being read: every variant swapped in before each read in turn, and back.
    for name, a in variants():
        for k in range(1, 86):
            s.sequence([dict(image=s.image(base), rig_object=RIG, triggers=[[k, s.image(a)]])])
        for k in range(1, 86, 4):
            s.sequence([dict(image=s.image(base), rig_object=RIG, triggers=[[k, s.image(a)], [k + 1 + k % 3, s.image(base)]])])
            s.sequence([dict(image=s.image(names["bridge"]), rig_object=RIG, triggers=[[k, s.image(a)]])])


def random_runs(s, count, seed):
    rng = random.Random(seed)
    base = scene()
    pool = [a for _, a in variants()]
    def mutate(a):
        roll = rng.random()
        if roll < .2:
            return edit(a, layer={0: dict(current=rng.choice((0, 75, 78, 79)), pointer=None), 2: dict(current=rng.choice((0, 71, 68, 67)), pointer=None),
                                  13: dict(current=rng.choice((36, 36, 3, 3, 40)), pointer=None)})
        if roll < .4:
            flags = bytearray(24)
            flags[0] = rng.choice((0, 2, 2, 6)); flags[9] = rng.choice((0, 0x84, 0x88, 0x90, 0x20, 0x44)); flags[10] = rng.choice((0, 2, 4))
            flags[15] = rng.choice((0, 2, 32, 34)); flags[12] = rng.choice((0, 0x20)); flags[14] = rng.choice((0, 0, 0, 4))
            return edit(a, snap_flags=bytes(flags), snap_controls=rng.choice((bytes(6), bytes(6), bytes([0, 0, 0, 0, 1, 0]), bytes([2, 0, 0, 0, 0, 0]))))
        if roll < .55:
            position = (rng.uniform(-2000, 2000), rng.uniform(-2000, 2000), rng.uniform(-100, 300))
            return edit(a, root=turned(rng.uniform(-3, 3), 0, 0, (1, 1, 1), position), parent=turned(rng.uniform(-3, 3), rng.uniform(-.5, .5), rng.uniform(-.2, .2)),
                        camera=turned(rng.uniform(-3.2, 3.2), rng.uniform(-1.55, 1.55), rng.uniform(-.1, .1)))
        if roll < .62: return edit(a, scene=rng.choice((3, 4, 4, 5, 0)))
        if roll < .68: return edit(a, omit={rng.choice(RECORDS)})
        if roll < .74: return edit(a, rig_count=rng.choice((63, 63, 50, 51, 600)), body_count=rng.choice((1, 1, 70, 0)))
        if roll < .80: return edit(a, **{rng.choice(("root", "parent", "camera")) + "_bytes": with_floats(base["camera"], **{f"i{rng.randrange(16)}": rng.choice((NAN, 1e9, 0.0, 5.0))})})
        if roll < .85: return edit(a, resource=rng.choice((gas.BODY_RESOURCE, bytes([2] * 8))), count=rng.choice((None, None, 30)))
        return rng.choice(pool)
    made = 0
    while made < count:
        steps, a = [], base
        for _ in range(rng.randint(2, 7)):
            if rng.random() < .7: a = mutate(a)
            if rng.random() < .15: a = base
            step = s.step(a, rig_object=a["rig_object"] if rng.random() < .97 else RIG + 8)
            pick = rng.random()
            if pick < .03: step[rng.choice(("no_lease", "other_lease", "done", "cleanup_only"))] = True
            elif pick < .06: step["origin"] = s.image(rng.choice(pool))
            elif pick < .08: step["request_delta"] = rng.choice((1, -0xe8))
            if rng.random() < .3:
                step["triggers"] = [[rng.randint(1, 90), s.image(mutate(a))]]
                if rng.random() < .4: step["triggers"].append([step["triggers"][0][0] + rng.randint(1, 10), step["image"]])
            steps.append(step); made += 1
        s.sequence(steps)


# -------------------------------------------------------------- Lua and back
def lua_script(pure, s):
    out = ["local pure, images, sequences = {}, {}, {}"]
    rows = []
    for op in pure:
        fields = ['"%s"' % op["op"]] + [f"{k}={lua(v)}" for k, v in op.items() if k != "op" and v is not None]
        rows.append("pure[#pure+1]={" + ",".join(fields) + "}")
    base = s.retail[s.base]
    for n, image in enumerate(s.retail):
        records = sorted(image.items()) if n == s.base else gas.diff(base, image)
        text = ",".join("{%d,%s}" % (at, '"%s"' % data.hex() if data is not None else "false") for at, data in records)
        out.append("images[%d]={%srecords={%s}}" % (n + 1, "" if n == s.base else "base=%d," % (s.base + 1), text))
    for sequence in s.sequences:
        steps = []
        for step in sequence["steps"]:
            fields = ["image=%d" % (step["image"] + 1), "rig_object=%d" % step["rig_object"]]
            if "on_foot" in step: fields.append("on_foot=%s" % lua(step["on_foot"]))
            if "edit" in step: fields.append('edit="%s"' % step["edit"])
            if "origin" in step: fields.append("origin=%d" % (step["origin"] + 1))
            if step.get("request_delta"): fields.append("request_delta=%d" % step["request_delta"])
            for name in ("done", "cleanup_only", "other_lease", "no_lease"):
                if name in step: fields.append("%s=%s" % (name, lua(step[name])))
            if step.get("triggers"): fields.append("triggers={%s}" % ",".join("{%d,%d}" % (k, image + 1) for k, image in step["triggers"]))
            steps.append("{" + ",".join(fields) + "}")
        exe = "" if sequence.get("exe") is None else "exe=%d," % sequence["exe"]
        rows.append("sequences[#sequences+1]={%ssteps={%s}}" % (exe, ",".join(steps)))
    for start in range(0, len(rows), 150):
        out += ["do local function part()"] + rows[start:start + 150] + ["end part() end"]
    out.append("return {game=%d,exe=%d,pure=pure,images=images,sequences=sequences}" % (snap.BASE, gas.EXE))
    return "\n".join(out) + "\n"


class Pool:
    def __init__(self):
        self.rows, self.index = [], {}

    def add(self, text):
        if text not in self.index:
            self.index[text] = len(self.rows)
            self.rows.append(text)
        return self.index[text]


def header(pure, s, lines, stamp):
    out = [stamp, "#include <math.h>",
           "/* ---- pure functions ---- */",
           "typedef struct LVValue { int table; unsigned present; double v[6]; } LVValue; /* dx dy dz pitch yaw roll; table 0: not a table */",
           "typedef struct LVAction { int table, match, count; signed char has[3]; int state[3]; } LVAction; /* layers 1, 3, 14 */",
           "typedef struct LVFlags { int size; unsigned char bytes[26]; } LVFlags; /* size -1: not a string */",
           "typedef struct LVPure { int op, value, camera, parent, action, verified, flags, ok; const char *error; double out[7]; } LVPure;",
           "enum { LV_ROTATION, LV_TO_PARENT, LV_SPRINTING };"]
    values, matrices, actions, flag_rows, rows = Pool(), Pool(), Pool(), Pool(), []

    def value_index(value):
        if not isinstance(value, dict):
            return values.add("{0,0,{0}}")
        mask, numbers = 0, []
        for bit, name in enumerate(("dx", "dy", "dz", "pitch", "yaw", "roll")):
            v = value.get(name)
            absent = v is None or (v is False and bit >= 3)       # `value.pitch or 0`: false is absent for the angles only
            if absent: numbers.append(0.0)
            elif is_number(v): mask |= 1 << bit; numbers.append(float(v))
            else: mask |= 1 << bit; numbers.append(NAN)
        return values.add("{1,%d,{%s}}" % (mask, ",".join(c_double(n) for n in numbers)))

    def matrix_index(m):
        return matrices.add("{%s}" % ",".join(c_double(float(v)) if is_number(v) else "NAN" for v in m))

    def action_index(action):
        if not isinstance(action, dict):
            return actions.add("{0,0,0,{0,0,0},{0,0,0}}")
        count = action.get("layer_count")
        has = [int(is_number(action.get(k))) for k in ("l1", "l3", "l14")]
        state = [int(action.get(k)) if is_number(action.get(k)) else 0 for k in ("l1", "l3", "l14")]
        return actions.add("{1,%d,%d,{%s},{%s}}" % (int(action.get("resource_match") is True), int(count) if is_number(count) else 0,
                                                    ",".join(map(str, has)), ",".join(map(str, state))))

    at = 0
    for op in pure:
        parts = lines[at].split("\t"); at += 1
        kind = op["op"]
        row = dict(op="LV_" + kind.upper(), value=-1, camera=-1, parent=-1, action=-1, verified=0, flags=-1, ok=1, error='""', out=[0.0] * 7)
        if kind == "sprinting":
            row["action"] = action_index(op["action"])
            row["verified"] = int(op["verified"] is True)
            flags = op["flags"]
            row["flags"] = flag_rows.add("{%d,{%s}}" % (len(flags), ",".join(map(str, flags)) or "0") if isinstance(flags, bytes) else "{-1,{0}}")
            assert parts[0] == "B"
            row["ok"] = int(parts[1])
        else:
            row["value"], row["camera"], row["parent"] = value_index(op["value"]), matrix_index(op["camera"]), matrix_index(op["parent"])
            if parts[0] == "E":
                row["ok"], row["error"] = 0, gas.c_string(parts[1])
            elif parts[0] == "Q":
                row["out"] = [0.0, 0.0, 0.0] + [float(v) for v in parts[1:5]]
            else:
                assert parts[0] == "V"
                row["out"] = [float(v) for v in parts[1:8]]
        rows.append("  {%s,%d,%d,%d,%d,%d,%d,%d,%s,{%s}}," % (row["op"], row["value"], row["camera"], row["parent"], row["action"],
                                                             row["verified"], row["flags"], row["ok"], row["error"],
                                                             ",".join(c_double(v) for v in row["out"])))
    out.append("static const LVValue lv_values[] = {\n  %s\n};" % ",\n  ".join(values.rows))
    out.append("static const double lv_matrices[][16] = {\n  %s\n};" % ",\n  ".join(matrices.rows))
    out.append("static const LVAction lv_actions[] = {\n  %s\n};" % ",\n  ".join(actions.rows))
    out.append("static const LVFlags lv_flags[] = {\n  %s\n};" % ",\n  ".join(flag_rows.rows))
    out += ["static const LVPure lv_pure[] = {"] + rows + ["};"]
    # ---- sampler
    out += ["/* ---- the sampler ---- */",
            "typedef struct LSRecord { unsigned long long at; unsigned size; const unsigned char *bytes; } LSRecord; /* bytes 0: withdrawn */",
            "typedef struct LSImage { int base; const LSRecord *records; unsigned count; } LSImage; /* base -1: a whole image */",
            "/* first: the step starts a new backend (and with it a new action-state sampler). origin: the image whose player the",
            " * lease was made for. triggers: before read number `read` (counted outside the player snapshot) the memory becomes",
            " * that image.",
            " * kind 1: not a sample but backend:on_foot(s, action) on the image's player snapshot: has_action 0 means action nil,",
            " * verified = resource_match, l1 = layer_count, has_l14 / l14 = the layer; done = 1: avatar_index made 8, 2: the flags",
            " * address moved; ok = the answer; owner = whether the snapshot could be taken. */",
            "typedef struct LSStep { int kind, first; unsigned long long exe; int image, origin; long long request_delta; unsigned long long rig_object;",
            "  int done, cleanup_only, other_lease, no_lease, trigger_count; struct { unsigned read; int image; } triggers[2];",
            "  int ok; const char *error; double n[5]; int ads, shoulder_aim, sprint, reload; unsigned char flags[24];",
            "  int has_action; const char *resource; int verified, has_l1, l1, has_l3, l3, has_l14, l14; unsigned long long body_object; int owner;",
            "  double camera[16], parent[16]; unsigned reads; unsigned long long reads_hash; } LSStep;"]
    out += gas.images_c(s, "ls", "LS")
    rows, evidence = [], None
    for sequence in s.sequences:
        for n, step in enumerate(sequence["steps"]):
            answer, _, trace = lines[at].partition("\t|\t"); at += 1
            parts = answer.split("\t")
            exe = gas.EXE if sequence.get("exe") is None else sequence["exe"]
            reads, value = gas.trace_hash(trace, exe, place)
            triggers = step.get("triggers") or []
            truth = lambda name: int(step.get(name) is not None and step.get(name) is not False)
            fields = [int("on_foot" in step), int(n == 0), "%dULL" % exe, step["image"], step.get("origin", step["image"]),
                      "%dLL" % step.get("request_delta", 0), "%dULL" % step["rig_object"],
                      {None: 0, "index": 1, "address": 2}[step.get("edit")] if "on_foot" in step else truth("done"), truth("cleanup_only"),
                      truth("other_lease"), truth("no_lease"), len(triggers),
                      "{%s}" % ",".join("{%d,%d}" % (k, image) for k, image in (triggers + [[0, 0], [0, 0]])[:2])]
            if "on_foot" in step:
                assert parts[0] == "F"
                given = step["on_foot"]
                table = isinstance(given, dict)
                l14 = given.get("l14") if table else None
                fields += [parts[1], '""', "{0}", 0, 0, 0, 0, "{0}", int(table), '""', int(table and given["resource_match"] is True), 0,
                           given["layer_count"] if table else 0, 0, 0, int(l14 is not None), l14 or 0, "0ULL", parts[2], "{0}", "{0}"]
            elif parts[0] == "R":
                (_, x, y, z, yaw, pitch, ads, shoulder, sprint, reload, flags, resource, verified, l1, l3, l14, body_object, owner, text) = parts[:19]
                evidence = evidence or text
                assert text == evidence and len(parts) == 19 + 32
                number = lambda v: (int(v != "-"), 0 if v == "-" else int(v))
                fields += [1, '""', "{%s}" % ",".join(c_double(float(v)) for v in (x, y, z, yaw, pitch)), ads, shoulder, sprint, reload,
                           "{%s}" % ",".join(str(b) for b in bytes.fromhex(flags)), int(resource != "-"),
                           gas.c_string("" if resource == "-" else resource), 0 if verified == "-" else verified, *number(l1), *number(l3),
                           *number(l14), body_object + "ULL", owner, "{%s}" % ",".join(c_double(float(v)) for v in parts[19:35]),
                           "{%s}" % ",".join(c_double(float(v)) for v in parts[35:51])]
            else:
                fields += [0, gas.c_string(parts[1]), "{0}", 0, 0, 0, 0, "{0}", 0, '""', 0, 0, 0, 0, 0, 0, 0, "0ULL", 0, "{0}", "{0}"]
            fields += [reads, "%dULL" % value]
            rows.append("  {" + ",".join(str(f) for f in fields) + "},")
    out.append("static const char ls_sprint_evidence[] = %s;" % gas.c_string(evidence or ""))
    out += ["static const LSStep ls_steps[] = {"] + rows + ["};"]
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--script-only", type=Path)
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    pure = pure_ops()
    s = gas.Script(((RETAIL_GAME, gas.RETAIL_EXE), (HF_GAME, gas.HF_EXE)))
    s.builder = build
    sampler_script(s)
    random_runs(s, 3500, 25480438)
    text = lua_script(pure, s)
    if args.script_only:
        args.script_only.write_text(text)
        return 0
    lines = gas.run("run_locomotion.lua", args.retail, text)
    steps = sum(len(sequence["steps"]) for sequence in s.sequences)
    if len(lines) != len(pure) + steps:
        sys.exit(f"{len(lines)} results for {len(pure) + steps} operations")
    digest = hashlib.sha256((args.retail / "src/first_person/locomotion.lua").read_bytes()).hexdigest()
    stamp = (f"/* Generated by tools/retail_ref/gen_locomotion.py. Do not edit.\n * Retail file: src/first_person/locomotion.lua at commit "
             f"{commit}, sha256 {digest}\n * (its sampler over the retail backend.lua and action_state.lua of the same commit).\n"
             f" * {len(pure)} pure operations with exact doubles; {steps} samples in {len(s.sequences)} sequences over {len(s.retail)} memory images:\n"
             f" * the retail files answered on fake memory in retail's layout, the bytes below are the same scenes in build 25480438's. */\n"
             f'#define LOCOMOTION_VECTOR_COMMIT "{commit}"\n#define LOCOMOTION_VECTOR_SHA256 "{digest}"')
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "locomotion_vectors.h").write_text(header(pure, s, lines, stamp))
    answered = sum(line.startswith("R") for line in lines[len(pure):])
    print(f"locomotion: {len(pure)} pure operations; {steps} samples in {len(s.sequences)} sequences, {len(s.retail)} images "
          f"({answered} answered, {steps - answered} refused)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
