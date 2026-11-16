#!/usr/bin/env python3
"""Cut reference vectors for src/action_state.c: one logical scene, written as
fake game memory in retail's layout (answered by the retail action_state.lua
over the retail backend.lua under LuaJIT) and in this build's layout (to be
answered the same by the C).

  gen_action_state.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
  gen_action_state.py --retail ... --script-only FILE        (the Lua script only; used by the coverage check)
  gen_action_state.py --here-image exe-25480438.bin          (check this build's constants against its executable image)

The player-snapshot part of each scene comes from gen_snapshot.py (the method
already proven for src/snapshot.c). Besides the answers, the header carries
the count and a hash of the ordered memory reads made outside the player
snapshot, with retail's executable addresses rewritten to this build's.
"""
import argparse
import hashlib
import random
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_snapshot as snap  # noqa: E402

HERE = Path(__file__).resolve().parent
FROZEN = "1d0d453"
u32 = snap.u32
u64 = lambda value: struct.pack("<Q", value & 0xffffffffffffffff)
EXE, REG, GEN, UNITS, OBJ, VT, INST, ACTIVE, GRAPH = (0x200000000, 0xb00000000, 0xb10000000, 0xb20000000, 0xc00000000,
                                                      0xc10000000, 0xc20000000, 0xc30000000, 0xd00000000)
HIGH = 0x800000000000
ANIM_CODE = bytes.fromhex("488b8178010000c3")
RESOURCE_CODE = bytes.fromhex("488b8158030000488902488bc2c3")
BODY_RESOURCE = bytes.fromhex("97fa4d294d331c4d")
# The executable, per build: PE identity, the unit registry global, the two fingerprints of backend.lua:716-719
# (profile.lua body_pose.executable), the two getters of action_state.lua:54-57, and for locomotion the scene-graph
# getter and the world-pose wrapper.
RETAIL_EXE = dict(stamp=0x6af2d5e6, size=0x29fe000, registry=0x1ab76f8,
                  exists=(0x1f75d0, "48895c2408574883ec208bfae8ef90eaff488bc8488bd84c8b0041ff90b00100"),
                  trigger=(0x1f77c0, "48895c2408574883ec208bda8bf9e8fd8eeaff448bc38bd7488b4818488b5c24"),
                  anim=0x28d510, resource=0x28d520, graph=0x28d3c0,
                  world_pose=(0x1f30e0, "40534883ec204863dae8e2d5eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3c3"))
HF_EXE = dict(stamp=0x6ab382e4, size=0x39e8000, registry=0x1a100f0,
              exists=(0x201710, "48895c2408574883ec208bfae89fc1e9ff488bc8488bd84c8b0041ff90b00100"),
              trigger=(0x201900, "48895c2408574883ec208bda8bf9e8adbfe9ff448bc38bd7488b4818488b5c24"),
              anim=0x2bd9c0, resource=0x2bd9d0, graph=0x2bd870,
              world_pose=(0x1fd220, "40534883ec204863dae89206eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3cc"))


def layer(n=80, **more):
    """One state-machine layer: n states; `current` is the active state's index, None a null pointer."""
    return dict(n=n, current=0, def_offset=None, entries=None, pointer=None, **more)


def scene(body=401):
    """The logical scene: the snapshot scene of gen_snapshot.py plus the body unit's animation state machine."""
    s = snap.scene()
    s["local"] = [11, 22, 101, body, 301, 1]
    s["rows"] = {0: [4, 101, 201, body, 0]}
    layers = [layer() for _ in range(31)]
    layers[13]["current"] = 36                               # layer 14: on foot
    return dict(snap=s, exe=EXE, mz=b"MZ", lfanew=0x80, pe=b"PE\0\0", machine=b"\x64\x86", magic=b"\x0b\x02", stamp_delta=0,
                size_delta=0, flip={}, registry=REG, capacity=4096, generations=GEN, units=UNITS, generation=None, object=OBJ,
                vt=VT, slot_anim=None, slot_resource=None, anim_code=ANIM_CODE, resource_code=RESOURCE_CODE, resource=BODY_RESOURCE,
                instance=INST, graph=GRAPH, active=ACTIVE, zero=0, count=None, table_offset=0x50, layers=layers, omit=set(), body=body)


def def_offset(a, index):
    value = a["layers"][index]["def_offset"]
    return 0x1000 + index * 0x3000 if value is None else value


def entry_offsets(a, index):
    entries = a["layers"][index]["entries"]
    if entries is not None:
        return entries
    return [0x100000 + index * 0x10000 + state * 8 for state in range(min(a["layers"][index]["n"], 2100))]


def build_exe(layout, a, m):
    """The executable image part, and the unit registry lookup of the scene's body (backend.lua:688-725, 783-803)."""
    def put(name, at, data):
        if name not in a["omit"]:
            m[at] = bytes(data)
    # The PE header sits where e_lfanew says when retail accepts that place (0x40..0x1000), else at 0x80.
    pe = a["lfanew"] if 0x40 <= a["lfanew"] <= 0x1000 else 0x80
    header = bytearray(pe + 0x180)
    header[0:2] = a["mz"]
    header[0x3c:0x40] = u32(a["lfanew"])
    header[pe:pe + 4] = a["pe"]
    header[pe + 4:pe + 6] = a["machine"]
    header[pe + 24:pe + 26] = a["magic"]
    header[pe + 8:pe + 12] = u32(layout["stamp"] + a["stamp_delta"])
    header[pe + 80:pe + 84] = u32(layout["size"] + a["size_delta"])
    put("exe_header", a["exe"], header[:a.get("header_size") or len(header)])
    for name in ("exists", "trigger", "world_pose"):
        rva, text = layout[name]
        code = bytearray(bytes.fromhex(text))
        if name in a["flip"]:
            code[a["flip"][name] % len(code)] ^= 0x40
        put(name, a["exe"] + rva, code)
    put("registry_global", a["exe"] + layout["registry"], u64(a["registry"]))


def build_unit(a, m, ref, obj, generation=None, capacity=None):
    def put(name, at, data):
        if name not in a["omit"]:
            m[at] = bytes(data)
    index = ref % 0x400000
    put("metadata", a["registry"] + 0x88, u64(a["units"]) + u64(0) + u32(a["capacity"] if capacity is None else capacity, 0) + u64(a["generations"]))
    put(f"generation_{ref}", a["generations"] + index, bytes([(ref >> 22) & 0xff if generation is None else generation]))
    put(f"unit_{ref}", a["units"] + index * 8, u64(obj))


def build(layout, exe_layout, a):
    """Fake memory for the scene in one build's layout, as {address: bytes}."""
    m = snap.build(layout, a["snap"])
    def put(name, at, data):
        if name not in a["omit"]:
            m[at] = bytes(data)
    build_exe(exe_layout, a, m)
    build_unit(a, m, a["body"], a["object"], a["generation"])
    put("vt", a["object"], u64(a["vt"]))
    put("slot_anim", a["vt"] + 0x1B0, u64(a["exe"] + exe_layout["anim"] if a["slot_anim"] is None else a["slot_anim"]))
    put("slot_resource", a["vt"] + 0x1B8, u64(a["exe"] + exe_layout["resource"] if a["slot_resource"] is None else a["slot_resource"]))
    put("anim_code", a["exe"] + exe_layout["anim"], a["anim_code"])
    put("resource_code", a["exe"] + exe_layout["resource"], a["resource_code"])
    put("resource", a["object"] + 0x358, a["resource"])
    put("instance_ptr", a["object"] + 0x178, u64(a["instance"]))
    put("graph_ptr", a["instance"] + 0x28, u64(a["graph"]))
    put("active_ptr", a["instance"] + 0x50, u64(a["active"]))
    count = len(a["layers"]) if a["count"] is None else a["count"]
    put("header", a["graph"], u32(a["zero"], count, a["table_offset"]))
    table_at = a["graph"] + a["table_offset"]
    put("definitions", table_at + 4, b"".join(u32(def_offset(a, i)) for i in range(len(a["layers"]))))
    pointers = b""
    for i, row in enumerate(a["layers"]):
        at = table_at + def_offset(a, i)
        entries = entry_offsets(a, i)
        put(f"def{i + 1}", at + 8, u32(row["n"]))
        put(f"entries{i + 1}", at + 12, b"".join(u32(e) for e in entries))
        if row["pointer"] is not None:
            value = row["pointer"]
        elif row["current"] is None:
            value = 0
        else:
            value = at + (entries[row["current"]] if row["current"] < len(entries) else 0x900000 + row["current"] * 8)
        pointers += u64(value)
    put("active", a["active"], pointers)
    return {at: data for at, data in m.items() if 0 <= at < 1 << 48}    # nothing beyond the user address space can be read


# ------------------------------------------------------------------ building
class Script:
    """Images (deduplicated, stored against a base image) and sequences of steps."""
    def __init__(self, layouts):
        self.layouts = layouts              # (game layout, exe layout, builder) for retail and for this build
        self.builder = build
        self.images = {}                    # key -> index
        self.retail, self.here = [], []     # per image: dict
        self.sequences = []
        self.base = None

    def image(self, a):
        retail = self.builder(self.layouts[0][0], self.layouts[0][1], a)
        here = self.builder(self.layouts[1][0], self.layouts[1][1], a)
        key = (tuple(sorted(retail.items())), tuple(sorted(here.items())))
        if key not in self.images:
            self.images[key] = len(self.retail)
            self.retail.append(retail); self.here.append(here)
        return self.images[key]

    def sequence(self, steps, exe=None):
        self.sequences.append(dict(steps=steps, exe=exe))

    def step(self, a, **more):
        return dict(image=self.image(a), **more)

    def one(self, a, **more):
        """A fresh sampler and one sample of the scene."""
        self.sequence([self.step(a, **more)])


def edit(base, **change):
    """A copy of the scene with fields changed; `snap_...` reaches into the snapshot scene, `layer` is {index: {...}}."""
    a = dict(base, snap=dict(base["snap"]), layers=[dict(row) for row in base["layers"]], omit=set(base["omit"]), flip=dict(base["flip"]))
    a["snap"]["omit"] = set(base["snap"]["omit"])
    for key, value in change.items():
        if key == "layer":
            for index, fields in value.items():
                a["layers"][index].update(fields)
        elif key.startswith("snap_"):
            a["snap"][key[5:]] = value
        elif key == "omit":
            a["omit"] |= set(value)
        else:
            a[key] = value
    return a


RECORDS = ["exe_header", "exists", "trigger", "registry_global", "metadata", "generation_401", "unit_401", "vt", "slot_anim",
           "slot_resource", "anim_code", "resource_code", "resource", "instance_ptr", "graph_ptr", "active_ptr", "header", "definitions",
           "def1", "entries1", "def3", "entries3", "def14", "entries14", "active", "def2", "entries2"]


def variants(rng=None):
    """Scenes that differ from the base in one thing: (name, scene). Used alone, as a later step and as a mid-read swap."""
    base = scene()
    out = [("l1_78", edit(base, layer={0: dict(current=78)})), ("l1_79", edit(base, layer={0: dict(current=79)})),
           ("l3_71", edit(base, layer={2: dict(current=71)})), ("l14_3", edit(base, layer={13: dict(current=3)})),
           ("l1_null", edit(base, layer={0: dict(current=None)})), ("l3_unknown", edit(base, layer={2: dict(pointer=0xdefa00)})),
           ("l14_unknown", edit(base, layer={13: dict(pointer=GRAPH + 0x77)})),
           ("other_resource", edit(base, resource=bytes([1] * 8))), ("thirty_layers", edit(base, count=30)),
           ("other_instance", edit(base, instance=INST + 0x1000)), ("other_graph", edit(base, graph=GRAPH + 0x4000000)),
           ("other_active", edit(base, active=ACTIVE + 0x1000)), ("other_object", edit(base, object=OBJ + 0x10000)),
           ("header_word0", edit(base, zero=7)), ("table_moved", edit(base, table_offset=0x58)),
           ("def1_moved", edit(base, layer={0: dict(def_offset=0x70000)})), ("def14_moved", edit(base, layer={13: dict(def_offset=0x71000)})),
           ("n1_changed", edit(base, layer={0: dict(n=81)})), ("n14_changed", edit(base, layer={13: dict(n=40)})),
           ("entries1_changed", edit(base, layer={0: dict(entries=[0x200000 + s * 8 for s in range(80)])})),
           ("entries14_shuffled", edit(base, layer={13: dict(entries=[0x100000 + 13 * 0x10000 + (79 - s) * 8 for s in range(80)], pointer=GRAPH + 0x50 + 0x1000 + 13 * 0x3000 + 0x100000 + 13 * 0x10000 + 36 * 8)})),
           ("generation_bumped", edit(base, generation=1)), ("capacity_small", edit(base, capacity=401)),
           ("other_body", edit(scene(402), omit=set())), ("other_player", edit(base, snap_local=[11, 23, 101, 401, 301, 1])),
           ("no_player", edit(base, snap_player_count=0)), ("slot_anim_changed", edit(base, slot_anim=EXE + 0x123400)),
           ("anim_code_changed", edit(base, anim_code=b"x" + ANIM_CODE[1:])), ("unit_gone", edit(base, omit={"unit_401"})),
           ("active_gone", edit(base, omit={"active"})), ("exe_other_build", edit(base, stamp_delta=1)),
           # differences in the last byte only of what retail compares as whole strings
           ("resource_last_byte", edit(base, resource=BODY_RESOURCE[:7] + b"\x4c", layer={0: dict(entries=[0x200000 + st * 8 for st in range(80)])})),
           ("header_last_byte", edit(base, table_offset=0x01000050)),
           ("l1_pointer_top_byte", edit(base, layer={0: dict(pointer=(0xff << 56) | (GRAPH + 0x50 + 0x1000 + 0x100000))})),
           ("definitions_last_byte", edit(base, layer={30: dict(def_offset=0x01000000 | (0x1000 + 30 * 0x3000))}))]
    return out


def hand_made(s):
    base = scene()
    s.base = s.image(base)
    one = s.one
    one(base)
    # States and what they say: the historical sprint hypothesis, the resource and the layer count.
    for l1, l3 in ((78, 0), (79, 0), (0, 71), (77, 70), (80 - 1, 72), (78, 71)):
        for resource, count in ((BODY_RESOURCE, None), (bytes([1] * 8), None), (BODY_RESOURCE[:7] + b"\x4c", None), (BODY_RESOURCE, 30)):
            a = edit(base, layer={0: dict(current=l1), 2: dict(current=l3)}, resource=resource, count=count)
            one(a); one(a, layers=[1]); one(a, layers=[3]); one(a, layers=[14, 2])
    one(edit(base, count=32, layers=[layer() for _ in range(32)]))
    one(edit(base, count=32, layers=[layer() for _ in range(32)]), layers=list(range(1, 33)))
    one(edit(base, count=1, layers=[layer()]), layers=[1])
    one(edit(base, count=1, layers=[layer()]))                           # the default layers need 14
    one(edit(base, count=13)); one(edit(base, count=14)); one(edit(base, count=3)); one(edit(base, count=2))
    # Null, unmatched and invalid current-state pointers.
    for pointer in (0, 0xdefa00, 65535, 65536, 1, HIGH - 1, HIGH, HIGH + 5, 0xffffffffffffffff, 0x100000000, GRAPH + 0x50):
        for index in (0, 2, 13):
            one(edit(base, layer={index: dict(pointer=pointer)}))
    one(edit(base, layer={0: dict(current=None), 2: dict(pointer=0xdefa00), 13: dict(pointer=0xdefa08)}))
    one(edit(base, layer={i: dict(pointer=0xdefa00 + i * 8) for i in range(31)}), layers=[31, 5, 14, 1, 30, 2])
    for index in (0, 2, 13):                                             # a valid state address with one more byte set on top
        one(edit(base, layer={index: dict(pointer=(0xff << 56) | (GRAPH + 0x50 + 0x1000 + index * 0x3000 + 0x100000 + index * 0x10000))}))
        one(edit(base, layer={index: dict(pointer=(1 << 47) | (GRAPH + 0x50 + 0x1000 + index * 0x3000 + 0x100000 + index * 0x10000))}))
    # Two states at one address: the first index wins. A state at offset 12 and at the largest offset.
    twin = [0x100000 + 8 * (s // 2) for s in range(80)]
    one(edit(base, layer={0: dict(entries=twin, current=41), 2: dict(entries=twin, current=79), 13: dict(entries=twin, current=0)}))
    one(edit(base, layer={0: dict(entries=[12] + [0x1000000] * 79, current=0), 2: dict(entries=[12] + [0x1000000] * 79, current=1)}))
    # Bounds: layer count, table offset, definition offsets, state counts, state offsets.
    for count in (0, 33, 0xffffffff, 0x80000000):
        one(edit(base, count=count))
    for table_offset in (0, 11, 12, 0x10000, 0x10001, 0x1000000, 0x1000001, 0xffffffff):
        one(edit(base, table_offset=table_offset))
    for index in (0, 2, 13, 5):
        for value in (0, 11, 12, 0x1000000, 0x1000001, 0xffffffff):
            one(edit(base, layer={index: dict(def_offset=value)}))
        for n in (0, 1, 2, 255, 256, 257, 1024, 2047, 2048, 2049, 0xffffffff):
            current = 0 if n < 1 or n > 2048 else n - 1
            one(edit(base, layer={index: dict(n=n, entries=[0x100000 + s * 8 for s in range(min(max(n, 1), 2048))], current=current)}))
        for position in (0, 1, 40, 79):
            for value in (11, 0, 0x1000001, 0xffffffff, 12, 0x1000000):
                entries = [0x100000 + s * 8 for s in range(80)]
                entries[position] = value
                one(edit(base, layer={index: dict(entries=entries, current=5)}))
    # A long state table is read in 1024-byte blocks: a block that cannot be read, at each place.
    for n, missing in ((2048, 0), (2048, 1024), (2048, 7168), (300, 1024), (257, 1024), (256, 0)):
        a = edit(base, layer={2: dict(n=n, entries=[0x100000 + s * 8 for s in range(n)], current=n - 1)})
        one(a)
        cut = edit(a, omit={"entries3"})
        pair = [build(snap.RETAIL, RETAIL_EXE, cut), build(snap.HF, HF_EXE, cut)]
        at = GRAPH + 0x50 + 0x1000 + 2 * 0x3000 + 12
        whole = b"".join(u32(0x100000 + st * 8) for st in range(n))
        for m in pair:
            if missing: m[at] = whole[:missing]
            if missing + 1024 < len(whole): m[at + missing + 1024] = whole[missing + 1024:]
        s.sequence([dict(image=add_image(s, pair))])
    # A table of exactly one, two and eight blocks, followed by a refusal further on: the text is that refusal's.
    for n in (256, 512, 2048, 255, 257):
        table = [0x100000 + st * 8 for st in range(n)]
        one(edit(base, layer={0: dict(n=n, entries=table, current=n - 1), 13: dict(pointer=5)}))
        one(edit(base, layer={0: dict(n=n, entries=table, current=n - 1), 2: dict(n=0)}))
        s.sequence([dict(image=s.image(edit(base, layer={0: dict(n=n, entries=table, current=n - 1)})),
                         expected=s.image(edit(base, snap_local=[11, 23, 101, 401, 301, 1])))])
    # The last entry of a block holds the largest offset (its top byte is the block's last byte).
    for n, at in ((300, 255), (2048, 255), (2048, 1023), (2048, 2047), (256, 255), (257, 256)):
        table = [0x100000 + st * 8 for st in range(n)]
        table[at] = 0x1000000
        one(edit(base, layer={0: dict(n=n, entries=table, current=at), 2: dict(n=n, entries=table, current=at)}))
    # Layer selections.
    for layers in ([1], [31], [14], [1, 3, 14], [14, 3, 1], [3, 3], [1, 1], [1, 3, 1], [0], [32], [-1], [1, 0], [1, 32], [1, 3, 14, 10, 11],
                   [10, 11], [], list(range(1, 32)), list(range(31, 0, -1)), list(range(1, 33)), list(range(1, 34)), [2] * 33,
                   [5, 6, 7, 8, 9, 2, 4], [31, 30, 29, 1]):
        one(base, layers=layers)
        one(edit(base, layer={0: dict(current=78), 2: dict(pointer=0xdefa00), 9: dict(current=7), 10: dict(current=None)}), layers=layers)
    one(edit(base, count=30), layers=[30]); one(edit(base, count=30), layers=[31]); one(edit(base, count=30), layers=[1, 30, 31])
    # Getters: the two virtual-table slots and the two code prefixes, byte by byte.
    one(edit(base, slot_anim=EXE + 0x123400)); one(edit(base, slot_resource=EXE + 0x123400)); one(edit(base, slot_anim=0)); one(edit(base, slot_resource=1))
    one(edit(base, slot_anim=EXE + HF_EXE["resource"])); one(edit(base, slot_anim=EXE + RETAIL_EXE["resource"]))
    for at in range(len(ANIM_CODE)):
        one(edit(base, anim_code=ANIM_CODE[:at] + bytes([ANIM_CODE[at] ^ 1]) + ANIM_CODE[at + 1:]))
    for at in range(len(RESOURCE_CODE)):
        one(edit(base, resource_code=RESOURCE_CODE[:at] + bytes([RESOURCE_CODE[at] ^ 0x80]) + RESOURCE_CODE[at + 1:]))
    one(edit(base, anim_code=ANIM_CODE[:7])); one(edit(base, resource_code=RESOURCE_CODE[:13]))
    # The executable image.
    one(edit(base, mz=b"ZM")); one(edit(base, mz=b"MX")); one(edit(base, lfanew=0x3f)); one(edit(base, lfanew=0x40)); one(edit(base, lfanew=0x1001))
    one(edit(base, lfanew=0x1000)); one(edit(base, pe=b"NE\0\0")); one(edit(base, pe=b"PE\0\1")); one(edit(base, machine=b"\x4c\x01"))
    one(edit(base, machine=b"\x64\x87")); one(edit(base, magic=b"\x0b\x01")); one(edit(base, magic=b"\x0c\x02")); one(edit(base, stamp_delta=1))
    one(edit(base, stamp_delta=-1)); one(edit(base, size_delta=0x1000)); one(edit(base, size_delta=-0x1000))
    for name in ("exists", "trigger"):
        for at in range(32):
            one(edit(base, flip={name: at}))
    # An image whose first bytes can be read and the rest cannot: the signature only, up to e_lfanew, part of the PE header.
    for size in (1, 2, 0x3c, 0x3f, 0x40, 0x80, 0x81, 0x80 + 95, 0x80 + 96):
        one(edit(base, header_size=size))
        s.sequence([dict(image=s.image(edit(base, header_size=size)), context=401)])
    for exe in (0, 65535, 65536, 0x1000, HIGH, HIGH - 1, EXE + 0x10):
        s.sequence([s.step(base)], exe=exe)
    # The unit registry.
    for capacity in (0, 401, 402, 0x400000, 0x400001, 0xffffffff, 1):
        one(edit(base, capacity=capacity))
    for body in (1 << 22, 7 << 22):                                      # index 0: a capacity of one is enough
        for capacity in (1, 0, 2):
            one(edit(scene(body), capacity=capacity))
    for field, where in (("generations", GEN), ("units", UNITS)):         # a valid pointer with one more byte set on top
        one(edit(base, **{field: (0xff << 56) | where})); one(edit(base, **{field: (1 << 47) | where})); one(edit(base, **{field: 65536}))
    for field in ("generations", "units", "registry", "object", "vt", "instance", "graph", "active"):
        # (values above 2^53 are printed by retail as the double they went through; 2^64 prints as 0)
        for value in (0, 65535, HIGH, 0xffffffffffffffff, HIGH - 8, HIGH - 1, HIGH - 13, HIGH - 0x60, 65536, 65537, 1 << 63, (1 << 63) - 1,
                      (1 << 53) + 1, (1 << 53) + 3, 0xfffffffffffff800, 0xfffffffffffffbff, 0xfffffffffffffc00, 0x8000000000000401,
                      0x0123456789abcdef, 0xfedcba9876543210):
            one(edit(base, **{field: value}))
    for generation in (1, 255, 128):
        one(edit(base, generation=generation))
    for body in (401, 1, 4095, 4096, (1 << 22) | 401, (255 << 22) | 7, (37 << 22) | 4095, (1 << 30) | 5, 0xfffffffe, 0x3fffff):
        a = scene(body)
        one(a); one(edit(a, generation=((body >> 22) + 1) & 0xff)); one(edit(a, capacity=0x400000)); one(edit(a, capacity=(body % 0x400000)))
        one(edit(a, capacity=(body % 0x400000) + 1))
    # Every record unreadable, one at a time.
    for name in RECORDS:
        one(edit(base, omit={name})); one(edit(base, omit={name}), layers=[2])
    for name in ("camera_global", "queue", "player_global", "player_count", "local", "pool_global", "ep", "flags", "rig"):
        a = edit(base); a["snap"]["omit"].add(name); one(a)
    # The snapshot refuses: its text is the answer.
    one(edit(base, snap_player_count=0)); one(edit(base, snap_local=[11, 22, 101, 0, 301, 1])); one(edit(base, snap_pool_count=0))
    # An expected snapshot handed in: fresh, stale (another body, another player), edited.
    other_body, other_player = edit(scene(402)), edit(base, snap_local=[11, 23, 101, 401, 301, 1])
    two_units = edit(scene(402))
    for a, expected in ((base, base), (base, other_body), (other_body, base), (base, other_player), (other_player, base), (other_body, other_body)):
        s.sequence([dict(image=s.image(a), expected=s.image(expected))])
        s.sequence([dict(image=s.image(a), expected=s.image(expected), layers=[1, 2, 3])])
    # Both body units alive at once: the stale expectation resolves, and the player check refuses it.
    both = build_both(two_units, 401)
    s.sequence([dict(image=add_image(s, both), expected=s.image(base))])
    for body in (0, 0xffffffff, 402, 0xfffffffe, 1, (1 << 22) | 401):
        s.sequence([dict(image=s.image(base), expected=s.image(base), edit="body", body=body)])
        s.sequence([dict(image=add_image(s, both), expected=s.image(base), edit="body", body=body)])
    s.sequence([dict(image=s.image(base), expected=s.image(base), edit="identity")])
    s.sequence([dict(image=s.image(edit(base, snap_player_count=0)), expected=s.image(base))])
    # The same sampler again: what the cache keeps, and what replaces it.
    names = dict(variants())
    for name, a in variants():
        s.sequence([s.step(base), s.step(a), s.step(base), s.step(a), s.step(a)])
        s.sequence([s.step(a), s.step(base), s.step(a)])
        s.sequence([s.step(base, layers=[1, 14]), s.step(a, layers=[1, 2, 3, 14]), s.step(base, layers=[3]), s.step(a)])
    # A changed state table behind an unchanged definition address and count is NOT noticed (retail keeps its map):
    stale = [s.step(base), s.step(names["entries1_changed"]), s.step(names["entries14_shuffled"]), s.step(base),
             s.step(names["n1_changed"]), s.step(names["entries1_changed"]), s.step(names["header_word0"]), s.step(names["entries1_changed"])]
    s.sequence(stale)
    s.sequence([s.step(names["entries14_shuffled"]), s.step(base), s.step(names["entries14_shuffled"])])
    # A new sampler starts without the last one's cache (the C test reuses one reader's memory for every sequence).
    s.sequence([s.step(base)]); s.sequence([s.step(names["entries1_changed"])])
    s.sequence([s.step(base)]); s.sequence([s.step(names["entries14_shuffled"])])
    wide = edit(base, count=32, layers=[layer() for _ in range(32)])
    moved = lambda a: edit(a, layer={31: dict(entries=[0x300000 + st * 8 for st in range(80)])})
    s.sequence([s.step(wide, layers=[32])]); s.sequence([s.step(moved(wide), layers=[32])])
    # ... and a replaced cache forgets every layer's map, the last layer's too.
    s.sequence([s.step(wide, layers=[32, 1]), s.step(moved(edit(wide, zero=7)), layers=[32, 1]), s.step(moved(wide), layers=[32]),
                s.step(edit(wide, resource=bytes([3] * 8)), layers=[32]), s.step(moved(edit(wide, resource=bytes([3] * 8))), layers=[32])])
    # A refused table read leaves the layer's old map in place.
    broken = edit(base, layer={0: dict(n=81, entries=[0x100000 + st * 8 for st in range(80)] + [5])})
    s.sequence([s.step(base), s.step(broken), s.step(base), s.step(names["n1_changed"]), s.step(broken), s.step(names["entries1_changed"])])
    s.sequence([s.step(base, layers=[1]), s.step(edit(base, omit={"entries3"})), s.step(names["entries1_changed"]), s.step(base)])
    # The backend's two methods by themselves: every kind of Unit reference, each registry fault, a registry that changes
    # between the two generation reads.
    refs = (0, 0xffffffff, 401, 402, 1, 4095, 4096, (1 << 22) | 401, 0xfffffffe, 0x3fffff, 400)
    faults = [base, edit(base, capacity=0), edit(base, capacity=401), edit(base, capacity=402), edit(base, capacity=0x400001),
              edit(base, generation=1), edit(base, generations=65535), edit(base, units=HIGH), edit(base, object=0), edit(base, registry=5),
              edit(base, stamp_delta=1), edit(base, flip={"exists": 0}), edit(base, flip={"trigger": 31}), edit(base, mz=b"ZM"),
              edit(base, lfanew=0x3f), edit(base, lfanew=0x1001), edit(base, lfanew=0x1000), edit(base, lfanew=0x40),
              edit(base, omit={"metadata"}), edit(base, omit={"generation_401"}), edit(base, omit={"unit_401"}),
              edit(base, omit={"exe_header"}), edit(base, omit={"exists"}), edit(base, omit={"trigger"}), edit(base, omit={"registry_global"}),
              edit(scene((9 << 22) | 401)), edit(scene((9 << 22) | 401), generation=8)]
    for a in faults:
        for ref in refs + ((9 << 22) | 401,):
            s.sequence([dict(image=s.image(a), context=ref)])
    for exe in (0, 65535, 65536, HIGH, HIGH - 1, HIGH - 2, HIGH - 3, EXE + 0x10):
        s.sequence([dict(image=s.image(base), context=401)], exe=exe)
    for name in ("generation_bumped", "other_object", "unit_gone", "capacity_small", "exe_other_build", "other_body"):
        for k in range(1, 22):
            s.sequence([dict(image=s.image(base), context=401, triggers=[[k, s.image(names[name])]])])
            s.sequence([dict(image=s.image(base), context=401, triggers=[[k, s.image(names[name])], [k + 1, s.image(base)]])])
    # Memory that changes while it is being read: every scene variant swapped in before each read in turn, and back.
    probe_reads = 75
    for name, a in variants():
        for k in range(1, probe_reads + 1):
            s.sequence([dict(image=s.image(base), triggers=[[k, s.image(a)]])])
        for k in range(1, probe_reads + 1, 3):
            s.sequence([dict(image=s.image(base), triggers=[[k, s.image(a)], [k + 1 + k % 5, s.image(base)]])])
            s.sequence([dict(image=s.image(a), triggers=[[k, s.image(base)]]), s.step(base)])


def build_both(a, extra_ref):
    """The scene's memory in both layouts with a second live unit `extra_ref` beside the body."""
    out = []
    for game, exe in ((snap.RETAIL, RETAIL_EXE), (snap.HF, HF_EXE)):
        m = build(game, exe, a)
        other = edit(a, object=OBJ + 0x20000, vt=VT, instance=INST + 0x2000)
        build_unit(other, m, extra_ref, OBJ + 0x20000)
        m[OBJ + 0x20000] = u64(VT)
        m[OBJ + 0x20000 + 0x358] = BODY_RESOURCE
        m[OBJ + 0x20000 + 0x178] = u64(INST)
        out.append(m)
    return out


def add_image(s, pair):
    key = (tuple(sorted(pair[0].items())), tuple(sorted(pair[1].items())))
    if key not in s.images:
        s.images[key] = len(s.retail)
        s.retail.append(pair[0]); s.here.append(pair[1])
    return s.images[key]


def random_runs(s, count, seed):
    rng = random.Random(seed)
    base = scene()
    def mutate(a):
        roll = rng.random()
        index = rng.choice((0, 0, 2, 2, 13, 13, rng.randrange(31)))
        if roll < .30: return edit(a, layer={index: dict(current=rng.choice((0, 3, 36, 71, 78, 79, 75, 68, rng.randrange(80), None)), pointer=None)})
        if roll < .38: return edit(a, layer={index: dict(pointer=rng.choice((0, 0xdefa00, 5, HIGH, GRAPH + 0x50 + rng.randrange(0, 0x200000, 8))))})
        if roll < .44: return edit(a, layer={index: dict(n=rng.choice((80, 81, 40, 0, 2049, 300)), current=0, entries=None)})
        if roll < .50: return edit(a, layer={index: dict(entries=[rng.choice((0x100000, 0x200000)) + index * 0x10000 + st * rng.choice((8, 8, 16)) for st in range(a["layers"][index]["n"] if 0 < a["layers"][index]["n"] <= 2048 else 1)])})
        if roll < .55: return edit(a, layer={index: dict(def_offset=rng.choice((None, 0x70000 + index * 0x3000, 5, 0x1000001)))})
        if roll < .60: return edit(a, resource=rng.choice((BODY_RESOURCE, BODY_RESOURCE, bytes([2] * 8))))
        if roll < .64: return edit(a, count=rng.choice((None, None, 30, 31, 0, 33)))
        if roll < .68: return edit(a, table_offset=rng.choice((0x50, 0x50, 0x58, 8, 0x10004)))
        if roll < .72: return edit(a, instance=rng.choice((INST, INST + 0x1000)), active=rng.choice((ACTIVE, ACTIVE + 0x1000)))
        if roll < .76: return edit(a, graph=rng.choice((GRAPH, GRAPH + 0x4000000)))
        if roll < .80: return edit(a, generation=rng.choice((None, None, 1)), capacity=rng.choice((4096, 4096, 300)))
        if roll < .84: return edit(a, omit={rng.choice(RECORDS)})
        if roll < .87: return edit(a, snap_local=[11, rng.choice((22, 23)), 101, 401, 301, 1])
        if roll < .90: return edit(a, zero=rng.choice((0, 7)))
        if roll < .93: return edit(a, slot_anim=rng.choice((None, EXE + 0x5000)), slot_resource=rng.choice((None, None, EXE + 0x5000)))
        if roll < .95: return edit(a, flip=rng.choice(({}, {"exists": 3}, {"trigger": 20})), stamp_delta=rng.choice((0, 0, 1)))
        return scene()
    made = 0
    while made < count:
        steps, a = [], base
        for _ in range(rng.randint(2, 9)):
            if rng.random() < .75: a = mutate(a)
            if rng.random() < .12: a = base
            step = s.step(a)
            pick = rng.random()
            if pick < .25: step["layers"] = rng.choice(([1], [1, 3, 14], [14, 3], [1, 3, 10, 11, 14], [2, 7, 31], [3, 3], [32], [0, 1], list(range(1, 32))))
            if rng.random() < .15: step["expected"] = s.image(rng.choice((a, base, mutate(a))))
            if rng.random() < .3:
                step["triggers"] = [[rng.randint(1, 80), s.image(mutate(a) if rng.random() < .8 else base)]]
                if rng.random() < .4: step["triggers"].append([step["triggers"][0][0] + rng.randint(1, 12), step["image"]])
            steps.append(step); made += 1
        s.sequence(steps)


# -------------------------------------------------------------- Lua and back
def diff(base, image):
    """The image as records over the base: [(at, bytes or None)]."""
    out = [(at, data) for at, data in sorted(image.items()) if base.get(at) != data]
    out += [(at, None) for at in sorted(base) if at not in image]
    return out


def lua_script(s):
    base = s.retail[s.base]
    out = ["local images, sequences = {}, {}"]
    for n, image in enumerate(s.retail):
        records = sorted(image.items()) if n == s.base else diff(base, image)
        rows = ",".join("{%d,%s}" % (at, '"%s"' % data.hex() if data is not None else "false") for at, data in records)
        out.append("images[%d]={%srecords={%s}}" % (n + 1, "" if n == s.base else "base=%d," % (s.base + 1), rows))
    rows = []
    for sequence in s.sequences:
        steps = []
        for step in sequence["steps"]:
            fields = ["image=%d" % (step["image"] + 1)]
            if "expected" in step: fields.append("expected=%d" % (step["expected"] + 1))
            if "edit" in step: fields.append('edit="%s"' % step["edit"])
            if "body" in step: fields.append("body=%d" % step["body"])
            if "context" in step: fields.append("context=%d" % step["context"])
            if step.get("layers") is not None: fields.append("layers={%s}" % ",".join(map(str, step["layers"])))
            if step.get("triggers"): fields.append("triggers={%s}" % ",".join("{%d,%d}" % (k, image + 1) for k, image in step["triggers"]))
            steps.append("{" + ",".join(fields) + "}")
        exe = "" if sequence.get("exe") is None else "exe=%d," % sequence["exe"]
        rows.append("sequences[#sequences+1]={%ssteps={%s}}" % (exe, ",".join(steps)))
    for start in range(0, len(rows), 300):
        out += ["do local function part()"] + rows[start:start + 300] + ["end part() end"]
    out.append("return {game=%d,exe=%d,images=images,sequences=sequences}" % (snap.BASE, EXE))
    return "\n".join(out) + "\n"


def run(runner, retail, text):
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
        script.write(text)
    done = subprocess.run(["luajit", str(HERE / runner), str(retail), script.name], capture_output=True, text=True)
    Path(script.name).unlink()
    if done.returncode:
        sys.exit(f"{runner} failed:\n" + done.stderr[-3000:])
    return done.stdout.split("\n")[:-1]


def to_here(address, exe=EXE):
    """A retail read address as this build's: executable addresses move, everything else is the scene's own."""
    if not exe <= address < exe + 0x4000000:
        return address
    rva = address - exe
    if rva < 0x2000:
        return address
    for name in ("exists", "trigger", "world_pose"):
        if RETAIL_EXE[name][0] <= rva < RETAIL_EXE[name][0] + 64:
            return exe + HF_EXE[name][0] + rva - RETAIL_EXE[name][0]
    for name in ("registry", "anim", "resource", "graph"):
        if RETAIL_EXE[name] <= rva < RETAIL_EXE[name] + 16:
            return exe + HF_EXE[name] + rva - RETAIL_EXE[name]
    sys.exit(f"retail read an executable address this tool cannot place: exe+{rva:#x}")


def trace_hash(text, exe=EXE, place=None):
    """FNV-1a over the reads (address and size as two little-endian 64-bit words), and their count."""
    value, count = 0xcbf29ce484222325, 0
    for item in filter(None, text.split(",")):
        at, size = item.split(":")
        at = (place or to_here)(int(at), exe)
        for byte in struct.pack("<QQ", at, int(size)):
            value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
        count += 1
    return count, value


def c_string(text):
    return '"' + "".join(c if c.isalnum() or c in " _-/.:;,()!" else "\\%03o" % ord(c) for c in text) + '"'


def images_c(s, prefix, kind):
    """The images in this build's layout as C tables."""
    out = []
    base = s.here[s.base]
    table = []
    for n, image in enumerate(s.here):
        records = sorted(image.items()) if n == s.base else diff(base, image)
        rows = []
        for k, (at, data) in enumerate(records):
            if data is None:
                rows.append("{%dULL,0,0}" % at)
            else:
                out.append("static const unsigned char %s_%d_%d[] = {%s};" % (prefix, n, k, ",".join(map(str, data))))
                rows.append("{%dULL,%d,%s_%d_%d}" % (at, len(data), prefix, n, k))
        out.append("static const %sRecord %s_records_%d[] = {%s};" % (kind, prefix, n, ",".join(rows) or "{0,0,0}"))
        table.append("{%d,%s_records_%d,%d}" % (-1 if n == s.base else s.base, prefix, n, len(records)))
    out.append("static const %sImage %s_images[] = {\n  %s\n};" % (kind, prefix, ",\n  ".join(table)))
    return out


def header(s, lines, stamp):
    out = [stamp,
           "typedef struct ASRecord { unsigned long long at; unsigned size; const unsigned char *bytes; } ASRecord; /* bytes 0: withdrawn */",
           "typedef struct ASImage { int base; const ASRecord *records; unsigned count; } ASImage; /* base -1: a whole image */",
           "/* first: the step starts a new sampler. expected -1: nil. edit 0 none, 1 body, 2 identity. layer_count -1: nil.",
           " * triggers: before read number `read` (counted outside the player snapshot) the memory becomes that image.",
           " * kind 1: not a sample but backend:_alignment_context on the Unit reference `edit_body`, then _alignment_executable:",
           " * body = unit, layers_out = index, match = generation, body_object / graph / instance = object / registry / units,",
           " * generations, and `resource` = the refusal of _alignment_executable (empty: accepted). */",
           "typedef struct ASStep { int kind, first; unsigned long long exe; int image, expected, edit; unsigned edit_body; int layer_count; int layers[34];",
           "  int trigger_count; struct { unsigned read; int image; } triggers[2];",
           "  int ok; const char *error; unsigned body; unsigned long long body_object, graph, instance; unsigned layers_out;",
           "  const char *resource; int match, sprint, owner; signed char has[33]; short states[33]; int unknown_count; int unknown[32];",
           "  unsigned long long generations; unsigned reads; unsigned long long reads_hash; } ASStep;"]
    out += images_c(s, "as", "AS")
    rows, at = [], 0
    evidence = None
    for sequence in s.sequences:
        for n, step in enumerate(sequence["steps"]):
            answer, _, trace = lines[at].partition("\t|\t")
            at += 1
            parts = answer.split("\t")
            exe = EXE if sequence.get("exe") is None else sequence["exe"]
            reads, value = trace_hash(trace, exe)
            layers = step.get("layers")
            triggers = step.get("triggers") or []
            fields = [int("context" in step), int(n == 0), "%dULL" % exe, step["image"], step.get("expected", -1),
                      {None: 0, "body": 1, "identity": 2}[step.get("edit")],
                      "%du" % step.get("context", step.get("body", 0)), -1 if layers is None else len(layers),
                      "{%s}" % ",".join(map(str, (layers or []) + [0] * (34 - len(layers or [])))), len(triggers),
                      "{%s}" % ",".join("{%d,%d}" % (k, image) for k, image in (triggers + [[0, 0], [0, 0]])[:2])]
            has, states, unknown, generations = [0] * 33, [0] * 33, [], 0
            if "context" in step:
                answer, _, executable = answer.partition("\t#\t")
                parts = answer.split("\t")
                if parts[0] == "C":
                    _, unit, index, generation, obj, registry, units, generations = parts
                    fields += [1, '""', unit + "u", obj + "ULL", registry + "ULL", units + "ULL", index, c_string(executable), generation, 0, 0]
                else:
                    fields += [0, c_string(parts[1]), "0u", "0ULL", "0ULL", "0ULL", 0, c_string(executable), 0, 0, 0]
            elif parts[0] == "R":
                (_, body, body_object, graph, instance, count, resource, match, sprint, owner, n_states, listed, n_unknown, unknown_text,
                 sprint_evidence) = parts
                evidence = evidence or sprint_evidence
                assert sprint_evidence == evidence
                for item in filter(None, listed.split(",")):
                    index, state = item.split(":")
                    has[int(index)], states[int(index)] = 1, int(state)
                assert sum(has) == int(n_states)
                unknown = [int(v) for v in filter(None, unknown_text.split(","))]
                assert len(unknown) == int(n_unknown)
                fields += [1, '""', body + "u", body_object + "ULL", graph + "ULL", instance + "ULL", count, c_string(resource), match, sprint, owner]
            else:
                fields += [0, c_string(parts[1]), "0u", "0ULL", "0ULL", "0ULL", 0, '""', 0, 0, 0]
            fields += ["{%s}" % ",".join(map(str, has)), "{%s}" % ",".join(map(str, states)), len(unknown),
                       "{%s}" % ",".join(map(str, unknown + [0] * (32 - len(unknown)))), "%sULL" % generations, reads, "%dULL" % value]
            rows.append("  {" + ",".join(str(f) for f in fields) + "},")
    out.append("static const char as_sprint_evidence[] = %s;" % c_string(evidence or ""))
    out += ["static const ASStep as_steps[] = {"] + rows + ["};"]
    return "\n".join(out) + "\n"


def check_image(path):
    """This build's executable constants against its image (a memory-layout image: offset = RVA)."""
    data = Path(path).read_bytes()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    problems = []
    if struct.unpack_from("<I", data, pe + 8)[0] != HF_EXE["stamp"] or struct.unpack_from("<I", data, pe + 80)[0] != HF_EXE["size"]:
        problems.append("PE stamp or image size")
    for name in ("exists", "trigger", "world_pose"):
        rva, text = HF_EXE[name]
        if data[rva:rva + len(text) // 2].hex() != text:
            problems.append(f"{name} at exe+{rva:#x}: image has {data[rva:rva + len(text) // 2].hex()}")
    for name, code in (("anim", ANIM_CODE), ("resource", RESOURCE_CODE), ("graph", bytes.fromhex("488d4160c3"))):
        if data[HF_EXE[name]:HF_EXE[name] + len(code)] != code:
            problems.append(f"{name} getter at exe+{HF_EXE[name]:#x}")
    base = struct.unpack_from("<Q", data, pe + 24 + 24)[0]
    pair = struct.pack("<QQ", base + HF_EXE["anim"], base + HF_EXE["resource"])
    tables = []
    at = data.find(pair)
    while at >= 0:
        tables.append(at - 0x1B0)
        at = data.find(pair, at + 8)
    if len(tables) != 1 or struct.unpack_from("<Q", data, tables[0] + 0xE8)[0] != base + HF_EXE["graph"]:
        problems.append("virtual table slots 0xE8 / 0x1B0 / 0x1B8")
    if problems:
        sys.exit("this build's image disagrees: " + "; ".join(problems))
    print(f"image agrees: PE identity, exists, trigger, world_pose, three getters, and one virtual table at image+{tables[0]:#x} "
          "holding them at +0xE8, +0x1B0, +0x1B8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--script-only", type=Path)
    parser.add_argument("--here-image", type=Path)
    args = parser.parse_args()
    if args.here_image:
        check_image(args.here_image)
        if not args.retail:
            return 0
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    s = Script(((snap.RETAIL, RETAIL_EXE), (snap.HF, HF_EXE)))
    hand_made(s)
    random_runs(s, 4000, 25480438)
    text = lua_script(s)
    if args.script_only:
        args.script_only.write_text(text)
        return 0
    lines = run("run_action_state.lua", args.retail, text)
    steps = sum(len(sequence["steps"]) for sequence in s.sequences)
    if len(lines) != steps:
        sys.exit(f"{len(lines)} results for {steps} steps")
    digest = hashlib.sha256((args.retail / "src/first_person/action_state.lua").read_bytes()).hexdigest()
    backend = hashlib.sha256((args.retail / "src/first_person/backend.lua").read_bytes()).hexdigest()
    stamp = (f"/* Generated by tools/retail_ref/gen_action_state.py. Do not edit.\n * Retail files: src/first_person/action_state.lua at commit "
             f"{commit}, sha256 {digest};\n * src/first_person/backend.lua, sha256 {backend}.\n"
             f" * {steps} samples in {len(s.sequences)} sequences over {len(s.retail)} memory images. The retail files answered on fake memory in\n"
             f" * retail's layout; the bytes below are the same scenes in build 25480438's layout. */\n"
             f'#define ACTION_STATE_VECTOR_COMMIT "{commit}"\n#define ACTION_STATE_VECTOR_SHA256 "{digest}"')
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "action_state_vectors.h").write_text(header(s, lines, stamp))
    answered = sum(line.startswith("R") for line in lines)
    print(f"action_state: {steps} samples in {len(s.sequences)} sequences, {len(s.retail)} images ({answered} answered, {steps - answered} refused)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
