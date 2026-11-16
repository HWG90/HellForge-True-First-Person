#!/usr/bin/env python3
"""Cut the backend-operations reference vectors.

Every run is a script of steps (memory edits, scripted answers of the engine and of
the helpers, calls of Backend methods). One logical game state is written twice: as
fake memory in retail's layout, answered by the unmodified retail backend.lua and
action_state.lua under LuaJIT (run_backend_ops.lua), and in build 25480438's layout,
to be answered the same by src/backend_ops.c (tests/backend_ops_test.c).

  gen_backend_ops.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
      [--coverage <file>]   also write the retail files' line coverage there
      [--runs N]            number of random runs (default 260)
"""
import argparse
import copy
import hashlib
import random
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FROZEN = "1d0d453"
u32 = lambda *values: b"".join(struct.pack("<I", v & 0xffffffff) for v in values)
u64 = lambda value: struct.pack("<Q", value & 0xffffffffffffffff)

PRESENTATION_HEX = [  # backend.lua:530-538; retail's bytes, also used at the test's places in this build's image
    "405641564881ec88000000488b05be1bc8014833c448894424683b1570bc60028bf24c8b1577ea4a02450fb6f04488742430",
    "4869d0401200004869c0b8010000498dbab06a54004c896c24784903d24c897c24704889542458",
    "4c8daac0e95300498b84ca10010000418bd68b480c488b056cdf4a0244887770894c2434418b4d004c8b481841ff9158060000",
    "41ff90f8000000488b0557dd4a02488d4c24344084f68bd3490f45cd4533ff4c8b50084533c944897c2420448b01488b0d200a4b02488b0941ff92d0000000",
    "488b0ddb1ebd0141b101450fb6c18bd70f10000f1144247ce8253472ff", "418b560441b1014889b424600200", "e8f82972ff",
    "48895c24084889742410574883ec20410fb6f08bfa8bd9e87451eaff4c8b5018b8",
    "43c78411880001001500000043899c1138000100488b5c24304388b41144000100488b7424384389bc1140000100"]
RETAIL = dict(
    stamp=0x6af322c1, size=0x3382000, camera=0x2e287d0, controller=0x2e381a8, world=0x2ce41b8, player=0x2ce1618,
    avatar=0x2ce1ee0, wield=0x2ce1fd0, weapons=0x2ce1ea0, light_map=0xf24990, entity_map=0xf1c978, entity_rows=0xf349e0,
    flags=0x53e888, rig=0x53e9c0, stride=0x1240, enter=0x110fc80, exit=0x11108a0, allowed=0x1144d60,
    entry_pin=(0x110fcc2, "0f57d2488bcbe8a3860000"), inventory=0x2ce18e8, state=0x2ce1508, scene=0xac29c, api_root=0x2ce14c8,
    exe_stamp=0x6af2d5e6, exe_size=0x29fe000, registry=0x1ab76f8,
    exists=(0x1f75d0, "48895c2408574883ec208bfae8ef90eaff488bc8488bd84c8b0041ff90b00100"),
    trigger=(0x1f77c0, "48895c2408574883ec208bda8bf9e8fd8eeaff448bc38bd7488b4818488b5c24"),
    anim_getter=0x28d510, resource_getter=0x28d520,
    render_get=(0x3d43d0, "488bd1e908fdffff"), render_set=(0x3d3ff0, "488bd1e988faffff"),
    render_load=(0x3d419a, "488b0d1f376e01"), render_type=(0x3d420e, "83f804751a41387108"), render_table=0x1ab78c0,
    presentation=0x833440, mode=0x546b20, mode_stride=0x1b8,
    presentation_pins=[(False, 0x833440), (False, 0x833513), (False, 0x833540), (False, 0x833763), (False, 0x110fffe),
                       (False, 0x11109eb), (False, 0x1110a43), (True, 0x1fb540), (True, 0x1fb574)])
HF = dict(
    stamp=0x6ab3b43f, size=0x4744000, camera=0x346d560, controller=0x347ce70, world=0x346bf98, player=0x3326468,
    avatar=0x3326d20, wield=0x3326420, weapons=0x3326ce0, light_map=0xf22ec8, entity_map=0xf1aeb0, entity_rows=0xf32f18,
    flags=0x53e880, rig=0x53e9b8, stride=0x1238, enter=0x127ddb0, exit=0x127e9d0, allowed=0x12b2d20,
    entry_pin=(0x127ddf2, "0f57d2488bcbe833850000"), inventory=0x3326738, state=0x3326340, scene=0xac21c, api_root=0x3326308,
    exe_stamp=0x6ab382e4, exe_size=0x39e8000, registry=0x1a100f0,
    exists=(0x201710, "48895c2408574883ec208bfae89fc1e9ff488bc8488bd84c8b0041ff90b00100"),
    trigger=(0x201900, "48895c2408574883ec208bda8bf9e8adbfe9ff448bc38bd7488b4818488b5c24"),
    anim_getter=0x2bd9c0, resource_getter=0x2bd9d0,
    render_get=(0x3fa3d0, "488bd1e938feffff"), render_set=(0x3fa120, "488bd1e998fbffff"),
    render_load=(0x3fa262, "4c8b354f606101"), render_type=(0x3fa2f8, "83f80475120fb65708"), render_table=0x1a102b8,
    # OPEN on this build. These are places in the test's fake image (tests/backend_ops_open.h), not build data.
    presentation=0x900440, mode=0x546c40, mode_stride=0x1c8,
    presentation_pins=[(False, 0x900440), (False, 0x900513), (False, 0x900540), (False, 0x900763), (False, 0x1380ffe),
                       (False, 0x13819eb), (False, 0x1381a43), (True, 0x2c0540), (True, 0x2c0574)])

(BASE, EXE, CTX, WORLD, CTRL, PLAYER, POOL, EP, WIELD, WEAPONS) = (
    0x100000000, 0x200000000, 0x300000000, 0x400000000, 0x500000000, 0x600000000, 0x700000000, 0x800000000, 0x900000000,
    0xa00000000)
(REG, UNITS, GENS, OBJ_BODY, OBJ_RIG, OBJ_WEAPON, CAMOBJ, VT, INST, GRAPH, ACTIVE, STATE, INV, APIROOT, API1, UAPI, CWORLD, RTAB,
 RROWS, DETACH, BASE2, EXE2) = (
    0xb00000000, 0xb10000000, 0xb20000000, 0xc00000000, 0xc10000000, 0xc20000000, 0xc40000000, 0xd00000000, 0xd10000000,
    0xd20000000, 0xd30000000, 0xe00000000, 0xe10000000, 0xe20000000, 0xe30000000, 0xe40000000, 0xe50000000, 0xe60000000,
    0xe70000000, 0xe80000000, 0x180000000, 0x280000000)
EXPECTED_RESOURCE = bytes([151, 250, 77, 41, 77, 51, 28, 77])
HAMMER = bytes([83, 133, 189, 162, 189, 201, 62, 95])
ANIM_GETTER = bytes.fromhex("488b8178010000c3")
RESOURCE_GETTER = bytes.fromhex("488b8158030000488902488bc2c3")
KEY_LAYERS = (1, 3, 10, 11, 14)


def checks(layout_name, retail):
    """The 14 game.dll code pins: retail's from profile.lua, this build's from src/profile.h."""
    if layout_name == "retail":
        text = (retail / "src/first_person/profile.lua").read_text()
        rows = re.findall(r'\{name = "(\w+)", rva = (0x[0-9a-f]+), hex = "([0-9a-f]+)"\}', text)
    else:
        text = (ROOT / "src/profile.h").read_text()
        rows = re.findall(r'\{"(\w+)",UINT32_C\((0x[0-9a-f]+)\),"([0-9a-f]+)"\}', text)
    assert len(rows) == 14, (layout_name, len(rows))
    return [(name, int(rva, 16), hexa) for name, rva, hexa in rows]


def scene():
    """The logical scene: one local player on foot holding one rifle, a normal camera request."""
    hmap = lambda key, index, buckets: dict(cap=8, empty=0xffffffff, mult=3, keys={key: index}, buckets=buckets)
    return dict(
        game=BASE, exe=EXE, mz=b"MZ", lfanew=0x40, pe=b"PE\0\0", machine=b"\x64\x86", magic=b"\x0b\x02", stamp_delta=0,
        size_delta=0, pins={}, entry_pin=0,
        context=CTX, camera=CTX + 0x3000, head=0, tail=1, rows={0: [4, 101, 201, 401, 0]}, fallback=None,
        world=WORLD, controller=CTRL, controls=bytes(6), player=PLAYER, player_count=1, light=301,
        light_map=hmap(301, 0, WORLD + 0x100), local=[11, 22, 101, 401, 301, 1], pool=POOL, pool_map=hmap(101, 0, POOL + 0x300),
        pool_count=1, ep=EP, ep_bytes=None, flags=bytes(24), rig=801, slot=0,
        wield=WIELD, wield_map=hmap(101, 0, WIELD + 0x200), wield_owner=None, weapon=201,
        entity_map=dict(cap=8, empty=0xffffffff, mult=3, keys={201: 1, 901: 2}, buckets=WORLD + 0x200),
        wd=[33, 44, 201, 501, 601, 1], weapons=WEAPONS, weapons_map=hmap(201, 0, WEAPONS + 0x200), weapon_entry=None,
        # executable
        exe_mz=b"MZ", exe_lfanew=0x40, exe_pe=b"PE\0\0", exe_machine=b"\x64\x86", exe_magic=b"\x0b\x02", exe_stamp_delta=0,
        exe_pins={}, registry=REG, reg_cap=4096, units=UNITS, gens=GENS, objects={401: OBJ_BODY, 801: OBJ_RIG, 501: OBJ_WEAPON,
                                                                                  951: 0xc30000000},
        generations={}, weapon_resource=bytes.fromhex("1122334455667788"),
        # body animation
        vt=VT, anim_slot=0, resource_slot=0, anim_code=ANIM_GETTER, resource_code=RESOURCE_GETTER, body_resource=EXPECTED_RESOURCE,
        instance=INST, graph=GRAPH, active=ACTIVE, layer_count=31, table_offset=0x40, states={1: 5, 3: 6, 10: 7, 11: 8, 14: 36},
        state_counts={}, def_offsets={}, entry_edit={},
        # scene, inventory, engine tables, camera object
        state=STATE, scene_id=4, inventory=INV, inv_map=hmap(101, 0, INV + 0x400), inv_owner=None, pack=901,
        pack_d=[55, 66, 901, 951, 0, 1], api_root=APIROOT, api1=API1, detach=DETACH, uapi=UAPI, exists_slot=0, trigger_slot=0,
        camera_graph=CAMOBJ + 0x60, camera_parent=0, camera_world=CWORLD, camera_unit=701, sight=0.534,
        # renderer settings
        render_code={}, render_table=RTAB, render_count=None, render_rows_at=RROWS,
        render_rows=[(4, 0, 0x01000000), (4, 1, 0x06f1d0d3), (4, 0, 0x30000000), (4, 0, 0x72e8f18c), (4, 1, 0x90000000)],
        presentation_pins={}, mode=0,
        omit=set())


def build(layout, s):
    """Fake memory for the scene in one build's layout, as {address: bytes}; plus the two module bases."""
    m, where = {}, {}

    def put(name, at, data):
        where[name] = (at, len(data))
        if name not in s["omit"] and 0 <= at < 2 ** 47:  # nothing can be mapped higher; a wild pointer has nothing behind it
            m[at] = bytes(data)

    def table(name, at, t):
        put(name, at, u64(t["buckets"]) + u32(t["cap"], t["empty"], t["mult"]))
        if t.get("fill"):  # every slot another key; the wanted key `fill` slots after its own
            cap = t["cap"]
            rows = bytearray(b"".join(u32(0x70000000 + i, 0) for i in range(cap)))
            for key, index in t["keys"].items():
                slot = (key * t["mult"] + t["fill"]) % cap
                rows[slot * 8:slot * 8 + 8] = u32(key, index)
            for start in range(0, cap * 8, 1024):
                put(name + "_rows%d" % start, t["buckets"] + start, rows[start:start + 1024])
            return
        cap = max(1, min(t["cap"], 32))  # 256 bytes: the room each table has before the next record
        rows = bytearray(u32(t["empty"], 0) * cap)
        for key, index in t["keys"].items():
            slot = (key * t["mult"]) % cap
            while rows[slot * 8:slot * 8 + 4] != u32(t["empty"]) and t["cap"] > 1 and len(t["keys"]) > 1:
                slot = (slot + 1) % cap
            rows[slot * 8:slot * 8 + 8] = u32(key, index)
        put(name + "_rows", t["buckets"], rows)

    def image(name, base, mz, lfanew, pe, machine, magic, stamp, size):
        header = bytearray(0x200)
        header[0:2] = mz
        header[0x3c:0x40] = u32(lfanew)
        at = 0x40
        header[at:at + 4] = pe
        header[at + 4:at + 6] = machine
        header[at + 24:at + 26] = magic
        header[at + 8:at + 12] = u32(stamp)
        header[at + 80:at + 84] = u32(size)
        put(name, base, header)

    def pin(name, base, rva, hexa, edit):
        if edit == "missing":
            return
        code = bytearray(bytes.fromhex(hexa))
        if edit:
            code[-1] ^= edit
        put(name, base + rva, code)

    game, exe = s["game"], s["exe"]
    image("image", game, s["mz"], s["lfanew"], s["pe"], s["machine"], s["magic"], layout["stamp"] + s["stamp_delta"],
          layout["size"] + s["size_delta"])
    for name, rva, hexa in layout["checks"]:
        pin("pin_" + name, game, rva, hexa, s["pins"].get(name, 0))
    pin("entry_pin", game, layout["entry_pin"][0], layout["entry_pin"][1], s["entry_pin"])
    put("camera_global", game + layout["camera"], u64(s["context"]))
    put("camera_object", s["context"], u64(s["camera"]))
    put("camera_unit", s["context"] + 8, u32(s["camera_unit"]))
    put("camera_world", s["context"] + 0x180, u64(s["camera_world"]))
    put("queue", s["context"] + 0x1f8, u32(s["head"], s["tail"]))
    for slot, row in s["rows"].items():
        put(f"row{slot}", s["context"] + 0x200 + slot * 0xe8, u32(*row))
        put(f"sight{slot}", s["context"] + 0x200 + slot * 0xe8 + 0x98, struct.pack("<f", s["sight"]))
    if s["fallback"] is not None:
        put("fallback", s["context"] + 0x1f00, u32(*s["fallback"]))
    put("camera_graph", s["camera"] + 0x18, u64(s["camera_graph"]))
    put("camera_parent", s["camera_graph"] - 0x60 + 0x1d0, u64(s["camera_parent"]))
    put("world_global", game + layout["world"], u64(s["world"]))
    put("controller_global", game + layout["controller"], u64(s["controller"]))
    put("controls", s["controller"], s["controls"])
    put("player_global", game + layout["player"], u64(s["player"]))
    put("player_count", s["player"] + 0x88, u32(s["player_count"]))
    put("light", s["player"] + 0x3a8, u32(s["light"]))
    table("light_map", s["world"] + layout["light_map"], s["light_map"])
    light_index = next(iter(s["light_map"]["keys"].values()))
    put("local", s["world"] + layout["entity_rows"] + light_index * 24, u32(*s["local"]))
    put("pool_global", game + layout["avatar"], u64(s["pool"]))
    table("pool_map", s["pool"] + 0xf8, s["pool_map"])
    pool_index = next(iter(s["pool_map"]["keys"].values()))
    put("pool_count", s["pool"] + 0x6c, u32(s["pool_count"]))
    put("ep_pointer", s["pool"] + 0x110 + pool_index * 8, u64(s["ep"]))
    put("ep", s["ep"], s["ep_bytes"] if s["ep_bytes"] is not None else u32(*s["local"][:5]))
    put("flags", s["pool"] + layout["flags"] + s["slot"] * layout["stride"], s["flags"])
    put("rig", s["pool"] + layout["rig"] + s["slot"] * layout["stride"], u32(s["rig"]))
    put("mode", s["pool"] + layout["mode"] + s["slot"] * layout["mode_stride"], bytes([s["mode"]]))
    put("wield_global", game + layout["wield"], u64(s["wield"]))
    table("wield_map", s["wield"] + 0x30, s["wield_map"])
    wield_index = next(iter(s["wield_map"]["keys"].values()))
    put("wield_owners", s["wield"] + 0x48, u64(s["wield"] + 0x300))
    put("wield_owner_pointer", s["wield"] + 0x300 + wield_index * 8, u64(s["ep"] + 0x100))
    put("wield_owner", s["ep"] + 0x100, s["wield_owner"] if s["wield_owner"] is not None else u32(*s["local"][:5]))
    put("wield_rows", s["wield"] + 0x60, u64(s["wield"] + 0x1000))
    put("wield_row", s["wield"] + 0x1000 + wield_index * 0x1d0, u32(s["weapon"]))
    table("entity_map", s["world"] + layout["entity_map"], s["entity_map"])
    keys = s["entity_map"]["keys"]
    weapon_index = keys.get(s["wd"][2], keys.get(201, 1))
    put("wd", s["world"] + layout["entity_rows"] + weapon_index * 24, u32(*s["wd"]))
    if s["pack"] in keys:
        put("pack_d", s["world"] + layout["entity_rows"] + keys[s["pack"]] * 24, u32(*s["pack_d"]))
    put("weapons_global", game + layout["weapons"], u64(s["weapons"]))
    table("weapons_map", s["weapons"] + 0x30, s["weapons_map"])
    weapons_index = next(iter(s["weapons_map"]["keys"].values()))
    put("weapons_entries", s["weapons"] + 0x48, u64(s["weapons"] + 0x300))
    put("weapon_entry_pointer", s["weapons"] + 0x300 + weapons_index * 8, u64(s["weapons"] + 0x400))
    put("weapon_entry", s["weapons"] + 0x400, s["weapon_entry"] if s["weapon_entry"] is not None else u32(*s["wd"][:5]))
    # scene state, inventory, engine API tables
    put("state_global", game + layout["state"], u64(s["state"]))
    put("scene", s["state"] + layout["scene"], u32(s["scene_id"]))
    put("inventory_global", game + layout["inventory"], u64(s["inventory"]))
    table("inv_map", s["inventory"] + 40, s["inv_map"])
    inv_index = next(iter(s["inv_map"]["keys"].values()))
    put("inv_owners", s["inventory"] + 64, u64(s["inventory"] + 0x500))
    put("inv_owner_pointer", s["inventory"] + 0x500 + inv_index * 8, u64(s["ep"] + 0x200))
    put("inv_owner", s["ep"] + 0x200, s["inv_owner"] if s["inv_owner"] is not None else u32(*s["local"][:5]))
    put("inv_rows", s["inventory"] + 80, u64(s["inventory"] + 0x1000))
    put("inv_row", s["inventory"] + 0x1000 + inv_index * 48, u32(0, 0, 0, s["pack"]) + bytes(32))
    put("api_root", game + layout["api_root"], u64(s["api_root"]))
    put("api1", s["api_root"] + 8, u64(s["api1"]))
    put("detach", s["api1"] + 0xf0, u64(s["detach"]))
    put("uapi", s["api_root"] + 0x18, u64(s["uapi"]))
    put("exists_slot", s["uapi"] + 0x350, u64(exe + layout["exists"][0] + s["exists_slot"]))
    put("trigger_slot", s["uapi"] + 0x370, u64(exe + layout["trigger"][0] + s["trigger_slot"]))
    for index, ((in_exe, rva), hexa) in enumerate(zip(layout["presentation_pins"], PRESENTATION_HEX)):
        pin(f"presentation_pin{index}", exe if in_exe else game, rva, hexa, s["presentation_pins"].get(index, 0))
    # executable
    image("exe_image", exe, s["exe_mz"], s["exe_lfanew"], s["exe_pe"], s["exe_machine"], s["exe_magic"],
          layout["exe_stamp"] + s["exe_stamp_delta"], layout["exe_size"])
    pin("exe_pin_exists", exe, *layout["exists"], s["exe_pins"].get("exists", 0))
    pin("exe_pin_trigger", exe, *layout["trigger"], s["exe_pins"].get("trigger", 0))
    put("registry_global", exe + layout["registry"], u64(s["registry"]))
    put("registry", s["registry"] + 0x88, u64(s["units"]) + bytes(8) + u32(s["reg_cap"], 0) + u64(s["gens"]))
    for ref, obj in s["objects"].items():
        index, generation = ref & 0x3fffff, (ref >> 22) & 0xff
        put(f"unit{ref}", s["units"] + index * 8, u64(obj))
        put(f"gen{ref}", s["gens"] + index, bytes([s["generations"].get(ref, generation)]))
    weapon_object = s["objects"].get(s["wd"][3], OBJ_WEAPON)
    put("weapon_resource", weapon_object + 0x358, s["weapon_resource"])
    body = s["objects"].get(s["local"][3], OBJ_BODY)
    put("body_vt", body, u64(s["vt"]))
    put("vt_anim", s["vt"] + 0x1b0, u64(exe + layout["anim_getter"] + s["anim_slot"]))
    put("vt_resource", s["vt"] + 0x1b8, u64(exe + layout["resource_getter"] + s["resource_slot"]))
    put("anim_code", exe + layout["anim_getter"], s["anim_code"])
    put("resource_code", exe + layout["resource_getter"], s["resource_code"])
    put("body_resource", body + 0x358, s["body_resource"])
    put("body_instance", body + 0x178, u64(s["instance"]))
    put("instance_graph", s["instance"] + 0x28, u64(s["graph"]))
    put("instance_active", s["instance"] + 0x50, u64(s["active"]))
    put("graph", s["graph"], u32(s.get("graph_tag", 0), s["layer_count"], s["table_offset"]))
    table_at = s["graph"] + s["table_offset"]
    count = max(0, min(s["layer_count"], 32))
    offsets = [s["def_offsets"].get(layer, 0x2000 * layer) for layer in range(1, count + 1)]
    put("definitions", table_at + 4, u32(*offsets))
    for layer in KEY_LAYERS:
        if layer > count:
            continue
        definition = table_at + offsets[layer - 1]
        if definition < table_at + 4 + 4 * count:
            continue  # a definition offset that lands inside the offset table itself: nothing of its own to map
        n = s["state_counts"].get(layer, 100)
        put(f"def{layer}_count", definition + 8, u32(n))
        entries = [0x100 + i * 0x10 for i in range(max(0, min(n, 2048)))]
        for index, value in s["entry_edit"].get(layer, {}).items():
            if index < len(entries):
                entries[index] = value
        put(f"def{layer}_entries", definition + 12, u32(*entries))
        state = s["states"].get(layer, 0)
        if state == "null":
            pointer = 0
        elif state == "unknown":
            pointer = definition + 0x7ff0
        elif state == "bad":
            pointer = 0x1234
        elif state == "low":
            pointer = 0x10000
        elif state == "high":
            pointer = 0x800000000000
        elif state == "top":
            pointer = 0x7fffffffffff
        else:
            pointer = definition + 0x100 + state * 0x10
        put(f"active{layer}", s["active"] + (layer - 1) * 8, u64(pointer))
    # renderer settings
    for name in ("render_get", "render_set", "render_load", "render_type"):
        pin(name, exe, *layout[name], s["render_code"].get(name, 0))
    put("render_table_global", exe + layout["render_table"], u64(s["render_table"]))
    rows = s["render_rows"]
    put("render_table", s["render_table"], u32(len(rows) if s["render_count"] is None else s["render_count"], 0) + u64(s["render_rows_at"]))
    for index, (kind, value, key) in enumerate(rows):
        put(f"render_row{index}", s["render_rows_at"] + index * 24, u32(kind, 0) + bytes([value]) + bytes(7) + u32(key, 0))
    last = None
    for at in sorted(m):  # two records must never share a byte: a patch that removes one would take the other's with it
        if last is not None and last[0] + last[1] > at:
            raise SystemExit("records overlap at %#x and %#x: %s" % (last[0], at, [n for n, w in where.items() if w[0] in (last[0], at)]))
        last = (at, len(m[at]))
    return m, (game, exe), where


def diff(before, after):
    """Records to write (or remove) to turn one memory image into the other."""
    (m0, bases0, _), (m1, bases1, _) = before, after
    out = []
    for at in sorted(set(m0) | set(m1)):
        if at not in m1:
            out.append((at, None, len(m0[at])))
        elif m0.get(at) != m1[at]:
            if at in m0 and len(m0[at]) > len(m1[at]):
                out.append((at, None, len(m0[at])))
            out.append((at, m1[at], len(m1[at])))
    if bases0[0] != bases1[0]:
        out.append((1, u64(bases1[0]), 8))
    if bases0[1] != bases1[1]:
        out.append((2, u64(bases1[1]), 8))
    return out


def edit(s, **change):
    s = copy.deepcopy(s)
    for key, value in change.items():
        if isinstance(value, dict) and isinstance(s.get(key), dict) and key not in ("rows", "objects"):
            s[key].update(value)
        elif key == "rows":
            s["rows"].update(value)
        else:
            s[key] = value
    return s


def entered(s, kind=6, weapon=None, body=None, controls=None, flags=None):
    """What the game's enter leaves behind: a request of kind 6 at the old tail."""
    s = copy.deepcopy(s)
    s["rows"][s["tail"]] = [kind, s["local"][2], s["weapon"] if weapon is None else weapon, s["local"][3] if body is None else body, 0]
    s["tail"] = (s["tail"] + 1) % 32
    if controls is not None:
        s["controls"] = controls
    if flags is not None:
        s["flags"] = flags
    return s


def exited(s):
    s = copy.deepcopy(s)
    s["tail"] = (s["tail"] + 31) % 32
    return s


class Run:
    def __init__(self, name, start=None, wine=0, pose=1, alignment=1):
        self.name, self.cur, self.patches, self.steps, self.traps = name, start or scene(), [], [], []
        self.initial = copy.deepcopy(self.cur)
        self.leases = self.tokens = 0
        self.strict = True
        self.step("new", wine, pose, alignment)

    def step(self, op, *nums, s=None):
        self.steps.append((op, [float(n) for n in nums], s))
        return self

    def effect(self, scene_after=None, **change):
        """A patch that turns the current scene into another one; returns its index."""
        after = scene_after if scene_after is not None else edit(self.cur, **change)
        self.patches.append((self.cur, after))
        self.cur = after
        return len(self.patches) - 1

    def mem(self, scene_after=None, **change):
        """With keywords: the current scene, edited. With a scene: that scene, whatever happened before."""
        if scene_after is not None and self.strict:
            return self.load(scene_after)
        return self.step("patch", self.effect(scene_after, **change))

    def load(self, scene_after):
        """Start again from the run's first image and go to this scene. A scripted answer that was never asked for
        (so its scene change never happened) cannot leave the fake memory different from what the script believes;
        answers still queued and traps still armed are dropped."""
        self.patches.append((self.initial, scene_after))
        self.cur = scene_after
        return self.step("load", len(self.patches) - 1)

    def trap(self, record, count=1, scene_after=None, **change):
        """When the named record is read for the count-th time from now, the scene changes (after that read)."""
        self.traps.append((record, self.cur))
        return self.step("trap", len(self.traps) - 1, self.effect(scene_after, **change), count)

    def untrap(self):
        return self.step("trap", -1, -1, 0)

    def ans(self, port, code=1, text=None, patch=-1, nums=(), sticky=0):
        return self.step("ans", code, patch, sticky, *nums, s=port if text is None else port + "|" + text)

    def clear(self, port):
        return self.step("ans", 0, -1, 2, s=port)

    def hook(self, name, on=1):
        return self.step("hook", on, s=name)

    def set(self, name, *nums):
        return self.step("set", *nums, s=name)

    def lease(self):
        self.leases += 1
        assert self.leases < 63, self.name
        return self.leases - 1

    def token(self):
        self.tokens += 1
        assert self.tokens < 63, self.name
        return self.tokens - 1

    # A full normal entry: snapshot into slot 0, prepare, enter; returns the lease slot.
    def enter_pose(self, remaining=0, blend=-1, snap=0):
        self.step("snap", 0, snap)
        token = self.token()
        self.step("prepare_pose", snap, token)
        self.ans("enter", patch=self.effect(entered(self.cur)))
        lease = self.lease()
        self.step("enter_pose", snap, token, remaining, blend, lease)
        return lease


PLACE = lambda x, y, z, mask=7, yaw=0, pitch=0, roll=0: (mask, x, y, z, yaw, pitch, roll)
NAN = float("nan")


def phase(epoch=1, progress=0.5, ads=1, identity=0, player=0, started=NAN, duration=NAN, continuous=-1, reason=0, action=-1, present=1):
    return (present, epoch, progress, ads, identity, player, started, duration, continuous, reason, action)


def motion(dx=0.01, dy=0, dz=-0.02, mask=0, q=(0, 0, 0, 1), authored=0):
    return (mask, dx, dy, dz, 0, *q, authored)


def hand_made():
    runs = []

    def run(name, **kw):
        r = Run(name, **kw)
        runs.append(r)
        return r

    # ---- the ordinary session: snapshot kinds, entry, per-frame work, release ----
    r = run("session")
    r.step("camera_ready").step("snap", 0, 0).step("snap", 1, 1).step("snap", 2, 2)
    r.step("begin_frame").step("snap", 0, 0).step("snap", 0, 0).step("camera_config").step("fov_config", -1).step("blur_config", -1)
    r.step("backpack", 0).step("backpack", 1).step("camera_only", 0).step("camera_only", 0).step("camera_only", 1)
    r.step("action", 1, 0).step("on_foot", 1, 0).step("action", -1, 1).step("on_foot", 0, -1)
    r.step("context", 0, 0).step("context", 0, 1).step("context", 0, 2).step("context", 0, 3).step("context", 1, 3)
    r.hook("camera_before_entry").hook("placement_resolver").hook("presentation_transition").hook("foreign_camera")
    r.step("prepare_pose", 0, r.token())
    r.ans("enter", patch=r.effect(entered(r.cur)))
    lease = r.lease()
    r.step("enter_pose", 0, 0, 0, -1, lease)
    r.step("snap", 0, 3).step("snap", 1, 4).step("pose_status", lease).step("fov_config", lease).step("blur_config", lease)
    r.step("set_pose_transition", lease, *phase(identity=0, player=0))
    r.step("set_pose_transition", lease, *phase(identity=0, player=0))
    r.step("set_pose_transition", lease, *phase(identity=0, player=0, progress=0.75))
    r.step("set_pose_transition", lease, *phase(identity=-2, player=0))
    r.step("set_pose_transition", lease, *phase(present=-1))
    r.ans("presentation_transition", nums=phase(epoch=2, identity=0, player=0))
    r.step("set_pose_motion", lease, *motion())
    r.step("set_pose_motion", lease, *motion(mask=15, q=(0, 0, 0.1, 0.99498743710662), authored=1))
    r.step("set_ads_placement", lease, *PLACE(0.01, 0.02, -0.03))
    r.step("set_pose_placement", lease, *PLACE(0.1, 0, -0.1, mask=63, yaw=5, pitch=-4, roll=3))
    r.step("set_pose_zoom_policy", lease, 1)
    r.step("end_frame").step("begin_frame")
    r.mem(controls=bytes([0, 0, 0, 0, 1, 0]), flags=bytes([0] * 6 + [1] + [0] * 17))
    r.step("snap", 1, 5).step("pose_status", lease).step("keep_aim_view", 5).step("snap", 1, 6).step("keep_aim_view", 6)
    r.mem(flags=bytes(24), controls=bytes(6))
    r.step("pose_status", lease)
    r.ans("exit", patch=r.effect(exited(r.cur)))
    r.step("release", lease).step("release", lease).step("pose_status", lease).step("end_frame")
    r.step("prepare_pose", 0, r.token())
    r.ans("enter", patch=r.effect(entered(r.cur)))
    lease2 = r.lease()
    r.step("enter_pose", 0, 1, 0, 1, lease2).step("fov_config", lease).step("fov_config", lease2).step("set_pose_placement", lease, *PLACE(0, 0, 0))
    r.mem(exited(r.cur)).step("release", lease2)

    # ---- the game image and its pins, in every scope ----
    r = run("module")
    for change in (dict(mz=b"ZM"), dict(lfanew=0x3f), dict(lfanew=0x1001), dict(pe=b"NE\0\0"), dict(machine=b"\x4c\x01"),
                   dict(magic=b"\x0b\x01"), dict(stamp_delta=1), dict(size_delta=0x1000), dict(omit={"image"}), dict(game=0),
                   dict(game=0x8000), dict(pins={"evaluator": "missing"}), dict(pins={"evaluator": 1}),
                   dict(pins={"enter_wrapper": "missing"}), dict(pins={"allowed": 4}), dict(pins={"root_rotation_call": "missing"}),
                   dict(entry_pin="missing"), dict(entry_pin=1)):
        r.mem(edit(scene(), **change))
        r.step("camera_ready").step("snap", 0, 0).step("snap", 1, 1).step("warm_start", 9, 63).step("backpack", 9)
    r.mem(scene()).step("snap", 0, 9)
    for change in (dict(pins={"evaluator": "missing"}), dict(pins={"enter_wrapper": "missing"}), dict(entry_pin="missing"),
                   dict(entry_pin=1), dict(pins={"pose_branch_position_call": "missing", "root_position_call": "missing"})):
        r.mem(edit(scene(), **change))
        lease = r.lease()
        r.step("warm_start", 9, lease).step("prepare_pose", 9, r.token()).step("backpack", 9)
        r.mem(scene()).step("release", lease)

    # ---- frame caches: what one frame re-reads and what it does not ----
    r = run("frames")
    r.step("begin_frame").step("snap", 0, 0).step("camera_ready").step("snap", 1, 1)
    r.mem(stamp_delta=1).step("snap", 0, 2).step("camera_ready").step("end_frame").step("snap", 0, 2).step("begin_frame").step("snap", 0, 2)
    r.mem(stamp_delta=0).step("snap", 0, 2).step("end_frame").step("begin_frame").step("snap", 0, 2)
    r.mem(exe_stamp_delta=1).step("snap", 0, 3).step("camera_config").step("fov_config", -1).step("end_frame").step("snap", 0, 3)
    r.mem(exe_stamp_delta=0).step("begin_frame").step("snap", 0, 3).step("camera_config")
    r.mem(game=BASE2).step("snap", 0, 4).step("camera_ready")
    r.mem(exe=EXE2).step("snap", 0, 5).step("camera_config").step("end_frame").step("begin_frame").step("snap", 0, 6)
    r.mem(game=0).step("snap", 0, 7).step("camera_ready").mem(game=BASE2).step("snap", 0, 7)
    r.mem(exe=0).step("snap", 0, 8).step("camera_config").step("context", 6, 0).mem(exe=EXE2).step("snap", 0, 8).step("end_frame")
    # an engine call inside a frame re-proves everything
    r.step("begin_frame").step("snap", 0, 0)
    r.mem(pins={"exit": 1})
    token = r.token()
    r.step("snap", 0, 1).step("prepare_pose", 0, token).mem(pins={"exit": 0}).step("prepare_pose", 0, token)
    r.mem(pins={"exit": 1}).step("enter_pose", 0, token, 0, -1, 63).mem(pins={"exit": 0}).step("end_frame")

    # ---- snapshots: every refusal of the chain (the snapshot generator's list, on the shipped path) ----
    r = run("snapshot_refusals")
    base = scene()
    changes = [
        dict(tail=2, rows={1: [6, 101, 201, 401, 0]}), dict(tail=2, rows={1: [6, 101, 202, 401, 0]}),
        dict(tail=2, rows={1: [6, 101, 201, 402, 0]}), dict(controls=bytes([0, 0, 0, 0, 1, 0]), flags=bytes([0] * 6 + [1] + [0] * 17)),
        dict(flags=bytes([0] * 10 + [4, 0, 0, 0, 0, 32] + [0] * 8)),
        dict(controls=bytes([0, 0, 0, 0, 1, 0]), flags=bytes([0] * 10 + [4, 0, 0, 0, 0, 32] + [0] * 8)),
        dict(flags=bytes([0] * 15 + [2] + [0] * 8)), dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(controls=bytes([64, 0, 0, 0, 0, 0])),
        dict(controls=bytes([0x20, 0, 0, 0, 0, 0])), dict(controls=bytes([0, 0, 0, 0, 0, 1])), dict(controls=bytes([2, 0, 0, 0, 0, 1])),
        dict(controls=bytes([0, 0, 0, 1, 0, 0])), dict(flags=bytes([0] * 14 + [4] + [0] * 9)),
        dict(flags=bytes([0] * 14 + [4] + [0] * 9), controls=bytes([2, 0, 0, 0, 0, 0])), dict(rows={0: [10, 101, 201, 401, 0]}),
        dict(rows={0: [0x10004, 101, 201, 401, 0]}), dict(rows={0: [4, 102, 201, 401, 0]}), dict(tail=0, fallback=[4, 101, 201, 401, 0]),
        dict(tail=0), dict(head=31, tail=0, rows={31: [6, 101, 201, 401, 0]}), dict(head=32), dict(tail=40), dict(weapon=0xffffffff),
        dict(weapon=0), dict(player_count=0), dict(light_map=dict(keys={302: 0})), dict(light=0xffffffff), dict(light=0),
        dict(local=[11, 22, 101, 401, 301, 0]), dict(local=[11, 22, 101, 401, 305, 1]),
        dict(local=[11, 22, 0, 401, 301, 1], rows={0: [4, 0, 201, 401, 0]}),
        dict(local=[11, 22, 0xffffffff, 401, 301, 1], rows={0: [4, 0xffffffff, 201, 401, 0]}), dict(pool_count=0), dict(pool_count=9),
        dict(pool_map=dict(keys={101: 8})), dict(ep_bytes=u32(11, 22, 101, 401, 999)), dict(local=[11, 22, 101, 0, 301, 1]),
        dict(wield_owner=u32(11, 22, 101, 401, 999)), dict(wd=[33, 44, 201, 501, 601, 0]), dict(wd=[33, 44, 202, 501, 601, 1]),
        dict(weapon_entry=u32(33, 44, 201, 501, 999)), dict(wd=[33, 44, 201, 0, 601, 1]), dict(light_map=dict(cap=0)),
        dict(light_map=dict(cap=6)), dict(light_map=dict(cap=2097152)), dict(light_map=dict(buckets=0x1000)),
        dict(light_map=dict(keys={301: 1048576})), dict(weapons_map=dict(keys={201: 4096})), dict(weapons_map=dict(keys={201: 4095})),
        dict(wield_map=dict(keys={101: 4096})), dict(wield_map=dict(keys={101: 4095})), dict(light_map=dict(keys={301: 1048575})),
        dict(entity_map=dict(keys={201: 1048576, 901: 2})), dict(entity_map=dict(keys={201: 1048575, 901: 2})), dict(context=0x10), dict(camera=0),
        dict(world=0x900000000000), dict(camera=0xffffffffffffffff), dict(camera=0x8000000000000123), dict(camera=0xfffffffffffff800),
        dict(context=0x7fffffffff00), dict(camera_unit=0), dict(camera_unit=0xffffffff),
        dict(wield_map=dict(cap=4, empty=0, mult=0, keys={5: 0, 9: 0, 13: 0, 17: 0})),
        dict(entity_map=dict(cap=8, keys={209: 3, 201: 1, 901: 2})),
    ]
    for name in ("camera_global", "camera_object", "queue", "row0", "world_global", "controller_global", "controls", "player_global",
                 "player_count", "light", "light_map", "light_map_rows", "local", "pool_global", "pool_map", "pool_count",
                 "ep_pointer", "ep", "flags", "rig", "wield_global", "wield_map", "wield_owners", "wield_owner_pointer",
                 "wield_owner", "wield_rows", "wield_row", "entity_map", "entity_map_rows", "wd", "weapons_global", "weapons_map",
                 "weapons_entries", "weapon_entry_pointer", "weapon_entry", "camera_unit"):
        changes.append(dict(omit={name}))
    for slot in (1, 2, 7):
        changes.append(dict(slot=slot, pool_count=8, pool_map=dict(keys={101: slot}),
                            flags=bytes([0] * 6 + [1] + [0] * 3 + [4, 0, 0, 0, 4, 34] + [0] * 8), rig=800 + slot,
                            objects={401: OBJ_BODY, 800 + slot: OBJ_RIG, 501: OBJ_WEAPON}))
    r.hook("foreign_camera")
    for change in changes:
        r.mem(edit(base, **change))
        r.step("snap", 0, 0).step("snap", 1, 1).step("snap", 2, 2)

    # ---- foreign camera, in every place it is asked ----
    r = run("foreign_camera")
    r.step("snap", 0, 0).step("snap", 2, 1)
    r.hook("foreign_camera")
    for code in (0, 1, -1, 99):
        r.ans("foreign_camera", code, "camera hook broke", sticky=1)
        lease = r.lease()
        r.step("snap", 0, 2).step("snap", 1, 3).step("snap", 2, 4).step("camera_config").step("warm_start", 0, lease).step("fov_config", -1)
        r.step("release", lease).step("prepare_pose", 0, 63)
    r.clear("foreign_camera")
    lease = r.enter_pose()
    r.ans("foreign_camera", 1, sticky=1).step("pose_status", lease).step("release", lease)
    r.mem(rows={0: [4, 102, 201, 401, 0]}, tail=1).step("snap", 2, 5).clear("foreign_camera").step("snap", 2, 5)

    # ---- action state and the on-foot test ----
    r = run("action_state")
    base = scene()
    r.step("snap", 1, 0)
    for change in (dict(states={14: 36}), dict(states={14: 3}), dict(states={14: 3}, flags=bytes([2] + [0] * 23)),
                   dict(states={14: 3}, flags=bytes([2] + [0] * 23), scene_id=3), dict(states={14: 3}, flags=bytes([2] + [0] * 23), scene_id=5),
                   dict(states={14: 3}, flags=bytes([6] + [0] * 23)), dict(states={14: 3}, flags=bytes([2] + [0] * 23), omit={"scene"}),
                   dict(states={14: 3}, flags=bytes([2] + [0] * 23), omit={"state_global"}), dict(states={14: 4}),
                   dict(states={14: "null"}), dict(states={14: "unknown"}), dict(states={14: "bad"}), dict(states={1: 78}),
                   dict(states={1: 79}), dict(states={3: 71}), dict(states={1: 78}, body_resource=bytes(8)), dict(layer_count=30),
                   dict(layer_count=32), dict(layer_count=0), dict(layer_count=33), dict(layer_count=13), dict(layer_count=2),
                   dict(table_offset=8), dict(table_offset=0x10004), dict(table_offset=0x2000000), dict(def_offsets={3: 4}),
                   dict(def_offsets={14: 0x1000001}), dict(state_counts={1: 0}), dict(state_counts={1: 2049}),
                   dict(state_counts={3: 600}, states={3: 599}), dict(state_counts={3: 2048}, states={3: 2047}),
                   dict(entry_edit={14: {5: 4}}, state_counts={14: 101}), dict(entry_edit={14: {36: 0x100}}, state_counts={14: 102}),
                   dict(entry_edit={14: {0: 0x100 + 36 * 0x10}}, state_counts={14: 103}),
                   dict(entry_edit={14: {35: 0x100 + 36 * 0x10}}, state_counts={14: 104}), dict(states={1: 0, 3: 0, 14: 0}),
                   dict(entry_edit={14: {0: 4}}, state_counts={14: 105}), dict(entry_edit={1: {0: 0x1000001}}, state_counts={1: 106}),
                   dict(states={1: 99}), dict(state_counts={3: 250}, states={3: 249}), dict(state_counts={3: 256}, states={3: 255}),
                   dict(state_counts={3: 257}, states={3: 256}), dict(state_counts={1: 1}, states={1: 0}), dict(state_counts={1: 2048}, states={1: 2047}),
                   dict(layer_count=14), dict(layer_count=3), dict(layer_count=1), dict(table_offset=12), dict(table_offset=0x10000),
                   dict(table_offset=11), dict(table_offset=0x10001), dict(def_offsets={3: 12}), dict(def_offsets={3: 0x1000000}),
                   dict(def_offsets={3: 11}), dict(states={14: "low"}), dict(states={14: "high"}), dict(states={14: "top"}),
                   dict(states={14: 3}, flags=bytes([3] + [0] * 23)), dict(states={14: 3}, flags=bytes([2, 0, 0, 1] + [0] * 20)),
                   dict(states={14: 36}, layer_count=32), dict(body_resource=bytes([151, 250, 77, 41, 77, 51, 28, 78])),
                   dict(anim_slot=8), dict(resource_slot=8), dict(anim_code=bytes(8)), dict(resource_code=bytes(14)),
                   dict(body_resource=HAMMER), dict(generations={401: 1}), dict(reg_cap=100), dict(reg_cap=0), dict(reg_cap=0x400001),
                   dict(units=0x100), dict(gens=0x900000000000), dict(registry=0),
                   dict(omit={"body_vt"}), dict(omit={"vt_anim"}), dict(omit={"vt_resource"}), dict(omit={"anim_code"}),
                   dict(omit={"resource_code"}), dict(omit={"body_resource"}), dict(omit={"body_instance"}), dict(omit={"instance_graph"}),
                   dict(omit={"instance_active"}), dict(omit={"graph"}), dict(omit={"definitions"}), dict(omit={"def1_count"}),
                   dict(omit={"def3_entries"}), dict(omit={"active14"}), dict(omit={"unit401"}), dict(omit={"gen401"}),
                   dict(omit={"registry"}), dict(omit={"registry_global"}), dict(omit={"exe_image"}), dict(omit={"exe_pin_exists"}),
                   dict(exe_pins={"trigger": 1}), dict(exe_pins={"exists": 1}), dict(exe_mz=b"ZM"), dict(exe_lfanew=1),
                   dict(exe_pe=b"XX\0\0"), dict(exe_machine=b"\0\0"), dict(exe_magic=b"\0\0"), dict(exe_stamp_delta=3), dict(exe=0),
                   dict(local=[11, 22, 101, (2 << 22) | 401, 301, 1], rows={0: [4, 101, 201, (2 << 22) | 401, 0]},
                        objects={(2 << 22) | 401: OBJ_BODY, 801: OBJ_RIG, 501: OBJ_WEAPON})):
        r.mem(edit(base, **change))
        r.step("snap", 0, 1).step("snap", 1, 2).step("action", 2, 0).step("on_foot", 2, 0).step("action", -1, 1).step("camera_config")
        r.step("on_foot", 0, 0)
    # the per-layer map is kept while the definition and its count stay; support layers
    r.mem(base).step("snap", 1, 0).step("action", 0, 0)
    r.mem(entry_edit={14: {36: 0x9000}}).step("action", 0, 1).mem(state_counts={14: 101}).step("action", 0, 1)
    r.mem(entry_edit={14: {36: 4}}, state_counts={14: 102}).step("action", 0, 1).mem(entry_edit={14: {}}).step("action", 0, 1)
    r.set("support_grip_tracking", 1).step("action", 0, 2).step("snap", 0, 3).mem(states={10: "unknown", 11: "null"}).step("action", 0, 2)
    r.mem(layer_count=10).step("action", 0, 2).set("support_grip_tracking", 0).mem(base)
    # the player changes while the layers are read: the hook runs inside the sampler's own player snapshot
    r.step("action", 0, 2).step("action", 9, 2)

    # ---- camera_config and the fird variants ----
    r = run("camera_config")
    r.step("camera_config").step("fird_fov_config")
    r.set("fird", 1, 0).step("camera_config").set("fird", 1, 1, 0.01, 0.2, 0.05).step("camera_config").step("fird_fov_config")
    r.set("fird", 0, 1, 0.01, 0.2, 0.05).step("camera_config").set("fird", 0, 0)
    r.set("camera_profile", 0).step("camera_config").step("keep_aim_view", 0).step("detach_seated_camera").set("camera_profile", 1)
    for change in (dict(rows={0: [10, 101, 201, 401, 0]}), dict(flags=bytes([0] * 14 + [4] + [0] * 9)), dict(tail=2, rows={1: [6, 101, 201, 401, 0]}),
                   dict(controls=bytes([0, 0, 0, 0, 1, 0])), dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(omit={"camera_unit"}),
                   dict(states={14: 3}), dict(omit={"exe_image"}), dict(rows={0: [4, 102, 201, 401, 0]})):
        r.mem(edit(scene(), **change)).step("camera_config").step("fird_fov_config").step("fov_config", -1)

    # ---- backpack and the hammer ----
    r = run("backpack")
    base = scene()
    r.step("snap", 0, 0).step("snap", 1, 1).step("backpack", -1)
    for change in (dict(pack=0), dict(pack=0xffffffff), dict(pack_d=[55, 66, 901, 0, 0, 1]), dict(pack_d=[55, 66, 901, 401, 0, 1]),
                   dict(pack_d=[55, 66, 901, 801, 0, 1]), dict(pack_d=[55, 66, 901, 501, 0, 1]), dict(pack_d=[55, 66, 901, 951, 0, 0]),
                   dict(pack_d=[55, 66, 902, 951, 0, 1]), dict(pack=902), dict(inv_owner=u32(1, 2, 3, 4, 5)),
                   dict(inv_map=dict(keys={102: 0})), dict(inv_map=dict(keys={101: 4096})), dict(inventory=0), dict(omit={"inv_row"}),
                   dict(omit={"inv_rows"}), dict(omit={"inv_owner"}), dict(omit={"inv_owners"}), dict(omit={"inv_owner_pointer"}),
                   dict(omit={"inv_map"}), dict(omit={"inventory_global"}), dict(omit={"pack_d"}), dict(stamp_delta=1),
                   dict(pins={"evaluator": "missing"}), dict(game=BASE2)):
        r.mem(edit(base, **change)).step("backpack", 0).step("backpack", 1)
    r.mem(base).step("camera_only", 0).mem(weapon_resource=HAMMER).step("camera_only", 0).step("snap", 0, 2)
    r.mem(wd=[33, 45, 201, 501, 601, 1]).step("snap", 0, 2).step("camera_only", 2).step("camera_only", 2).step("camera_only", -1)
    for change in (dict(omit={"weapon_resource"}), dict(omit={"unit501"}), dict(generations={501: 9}), dict(omit={"exe_image"})):
        r.mem(edit(base, wd=[33, 46, 201, 501, 601, 1], **change)).step("snap", 0, 3).step("camera_only", 3)
    r.mem(edit(base, wd=[33, 47, 201, (7 << 22) | 501, 601, 1], objects={401: OBJ_BODY, 801: OBJ_RIG, (7 << 22) | 501: OBJ_WEAPON}))
    r.step("snap", 0, 4).step("camera_only", 4).step("context", 4, 3).step("context", 4, 2)

    # ---- entry: every refusal and every lease that comes back for cleanup only ----
    r = run("enter_refusals")
    base = scene()
    r.step("snap", 0, 0).step("enter_pose", 0, -1, 0, -1, 63).step("enter", -1, -1, 63)
    for remaining in (-1, 0.5, 30501, NAN, float("inf")):
        r.step("enter_pose", 0, 0, remaining, -1, 63)
    r.step("enter_pose", 0, 0, 0, 2, 63).step("enter_pose", 0, -1, 0, 2, 63)
    token = r.token()
    r.step("prepare_pose", 0, token)
    r.hook("camera_before_entry")
    for code, text in ((0, "blur still stopping"), (0, None), (-1, None), (-1, "camera not ready"), (99, "hook raised")):
        r.ans("camera_before_entry", code, text).step("enter_pose", 0, token, 0, -1, 63)
    r.hook("camera_before_entry", 0)
    r.mem(stamp_delta=1).step("enter_pose", 0, token, 0, -1, 63).mem(stamp_delta=0)
    r.mem(wd=[33, 99, 201, 501, 601, 1]).step("enter_pose", 0, token, 0, -1, 63).step("snap", 0, 1).step("enter_pose", 1, token, 0, -1, 63)
    r.mem(base)
    align_token = r.token()
    r.step("prepare_alignment", 0, align_token).step("enter_pose", 0, align_token, 0, -1, 63).step("enter", 0, token, 63)
    for change in (dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(tail=2, rows={1: [6, 101, 201, 401, 0]}),
                   dict(flags=bytes([0] * 6 + [1] + [0] * 17)), dict(controls=bytes([0, 0, 0, 0, 1, 0])), dict(states={14: 4})):
        r.mem(edit(base, **change)).step("enter_pose", 0, token, 0, -1, 63)
    r.mem(base)
    r.ans("allowed", 0).step("enter_pose", 0, token, 0, -1, 63)
    r.ans("allowed", 99, "allowed crashed").step("enter_pose", 0, token, 0, -1, 63)
    r.mem(pins={"allowed": 1}).step("enter_pose", 0, token, 0, -1, 63).mem(pins={"allowed": 0})
    for region in ((0x2000, 0x1000000, 0x20, 1, 1), (0x1000, 0x20000, 0x20, 1, 1), (0x1000, 0x1000000, 0x04, 1, 1),
                   (0x1000, 0x1000000, 0x20, 1, 0), (0x1000, 0x40000, 0x20, 1, 1), (0x1000, 0x1000000, 0x40, 0, 1),
                   (0x1000, 0x1000000, 0x80, 0, 1)):
        r.mem(base).step("snap", 0, 0)
        token = r.token()
        r.step("prepare_pose", 0, token).set("region", *region)
        lease = r.lease()
        r.step("enter_pose", 0, token, 0, -1, lease)
        r.set("region", 0x1000, 0x1000000, 0x20, 1, 1).mem(base).step("release", lease)
    r.mem(base).step("snap", 0, 0)
    token = r.token()
    r.step("prepare_pose", 0, token)
    r.ans("allowed", 1, patch=r.effect(controls=bytes([2, 0, 0, 0, 0, 0]))).step("enter_pose", 0, token, 0, -1, 63)
    r.mem(base).ans("allowed", 1, patch=r.effect(omit={"queue"})).step("enter_pose", 0, token, 0, -1, 63)
    r.mem(base).ans("allowed", 1, patch=r.effect(objects={401: OBJ_BODY, 801: OBJ_RIG + 0x100, 501: OBJ_WEAPON}))
    r.step("enter_pose", 0, token, 0, -1, 63)
    r.mem(base).ans("allowed", 1, patch=r.effect(omit={"unit801"})).step("enter_pose", 0, token, 0, -1, 63)
    # the pose configuration raises after the token is consumed
    r.mem(base).ans("allowed", 1, patch=r.effect(weapon_resource=bytes(8))).step("enter_pose", 0, token, 0, -1, 63)
    r.step("enter_pose", 0, token, 0, -1, 63)
    # helper refusals at Begin
    for code, text, owned in ((0, "helper refused", None), (0, None, None), (-1, None, None), (99, "begin raised", None),
                              (2, "partial install", None), (2, None, None), (98, "raised after arming", None),
                              (2, "owner lookup raises", 99), (2, "owner lookup says none", 0), (0, "foreign handle", 3)):
        r.mem(base).step("snap", 0, 0)
        token = r.token()
        r.step("prepare_pose", 0, token)
        r.ans("P.begin", code, text)
        if owned is not None:
            r.ans("P.owned_handle", owned, "owned raised")
        lease = r.lease()
        r.step("enter_pose", 0, token, 0, -1, lease)
        r.mem(base).step("release", lease).step("snap", 0, 0)
    r.hook("P.owned_handle", 0)
    token = r.token()
    r.step("prepare_pose", 0, token).ans("P.begin", 2, "partial, no lookup").step("enter_pose", 0, token, 0, -1, r.lease())
    r.hook("P.owned_handle", 1)
    # changes between the arm and the game's entry
    for change in (dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(tail=2, rows={1: [4, 101, 201, 401, 0]}), dict(omit={"controls"}),
                   dict(objects={401: OBJ_BODY, 801: OBJ_RIG, 501: OBJ_WEAPON + 0x40}), dict(omit={"gen501"}),
                   dict(flags=bytes([0] * 6 + [1] + [0] * 17)), dict(controls=bytes([0, 0, 0, 0, 1, 0])), dict(rows={0: [6, 101, 201, 401, 0]}),
                   dict(head=1, tail=1, fallback=[4, 101, 201, 401, 0])):
        r.mem(base).step("snap", 0, 0)
        token = r.token()
        r.step("prepare_pose", 0, token)
        r.ans("P.begin", 1, patch=r.effect(**change))
        lease = r.lease()
        r.step("enter_pose", 0, token, 0, 0, lease).step("pose_status", lease).step("set_pose_motion", lease, *motion())
        r.mem(base).step("release", lease).step("snap", 0, 0)
    # the game's entry raises, changes the player, leaves no request, or is refused by its page
    for code, text, after in ((99, "enter crashed", None), (1, None, dict(omit={"queue"})), (1, None, dict(rig=802)),
                              (1, None, "aim"), (1, None, "nothing"), (99, "crashed after the request", "entered")):
        r.mem(base).step("snap", 0, 0)
        token = r.token()
        r.step("prepare_pose", 0, token)
        if after == "aim":
            patch = r.effect(entered(r.cur, controls=bytes([0, 0, 0, 0, 1, 0]), flags=bytes([0] * 6 + [1] + [0] * 17)))
        elif after == "entered":
            patch = r.effect(entered(r.cur))
        elif after == "nothing" or after is None:
            patch = -1
        else:
            patch = r.effect(**after)
        r.ans("enter", code, text, patch=patch)
        lease = r.lease()
        r.step("enter_pose", 0, token, 5000, 1, lease).step("pose_status", lease).step("release", lease)
        r.mem(base).step("release", lease).step("snap", 0, 0)
    # plain entry without a helper, and with an alignment token
    r.mem(base).step("snap", 0, 0)
    r.ans("enter", patch=r.effect(entered(r.cur)))
    lease = r.lease()
    r.step("enter", 0, -1, lease).step("alignment_status", lease).step("pose_status", lease).step("fov_config", lease)
    r.ans("exit", patch=r.effect(exited(r.cur))).step("release", lease)
    token = r.token()
    r.step("prepare_alignment", 0, token)
    r.ans("enter", patch=r.effect(entered(r.cur)))
    lease = r.lease()
    r.step("enter", 0, token, lease).step("alignment_status", lease).step("pose_status", lease).step("set_pose_placement", lease, *PLACE(0, 0, 0))
    r.step("set_pose_transition", lease, *phase()).step("fov_config", lease).step("prepare_alignment", 0, 63).step("prepare_pose", 0, 63)
    r.ans("A.stats", 1, nums=(2, 3, 0)).step("alignment_status", lease).ans("A.stats", 1, nums=(1, 0, 7)).step("alignment_status", lease)
    r.step("alignment_status", lease).mem(exited(r.cur)).step("release", lease).step("alignment_status", -1)
    for case in ("unreadable", "false", "raised", "moved", "unreadable_unit", "aiming", "error_text", "finish_refused"):
        r.mem(base).step("snap", 0, 0)
        token = r.token()
        r.step("prepare_alignment", 0, token)
        r.ans("enter", patch=r.effect(entered(r.cur)))
        lease = r.lease()
        r.step("enter", 0, token, lease)
        inside = r.cur
        if case == "unreadable":
            r.ans("A.stats", 0, "alignment stats unreadable")
        elif case == "false":
            r.ans("A.stats", -1)
        elif case == "raised":
            r.ans("A.stats", 99, "alignment stats raised")
        elif case == "moved":
            r.mem(objects={401: OBJ_BODY, 801: OBJ_RIG + 0x20, 501: OBJ_WEAPON})
        elif case == "unreadable_unit":
            r.mem(omit={"unit801"})
        elif case == "aiming":
            r.mem(flags=bytes([0] * 6 + [1] + [0] * 17))
        elif case == "error_text":
            r.ans("A.stats", 1, nums=(1, 0, 6))
        elif case == "finish_refused":
            r.ans("A.stats", 0).ans("A.finish", 0, "alignment end refused")
        r.step("alignment_status", lease).step("alignment_status", lease).step("pose_status", lease)
        r.mem(exited(inside)).step("release", lease)

    # ---- the second look: what changes while a method is reading ----
    r = run("changed_during")
    base = scene()
    r.step("snap", 0, 0).step("snap", 1, 1)
    moved = {401: OBJ_BODY, 801: OBJ_RIG + 0x40, 501: OBJ_WEAPON, 951: 0xc30000000}
    for record, count, change, steps in (
            ("gen801", 1, dict(generations={801: 1}), (("context", 0, 0),)),
            ("unit801", 1, dict(objects=moved), (("context", 0, 0),)),
            ("gen501", 1, dict(generations={501: 1}), (("context", 0, 3),)),
            ("unit501", 1, dict(objects={401: OBJ_BODY, 801: OBJ_RIG, 501: OBJ_WEAPON + 0x40}), (("context", 0, 3),)),
            ("weapon_entry", 1, dict(wd=[33, 77, 201, 501, 601, 1]), (("context", 0, 3),)),
            ("body_vt", 1, dict(local=[11, 29, 101, 401, 301, 1]), (("action", 1, 0), ("snap", 0, 2))),
            ("body_vt", 1, dict(units=UNITS + 0x100000), (("action", 1, 0), ("snap", 0, 2))),
            ("body_vt", 1, dict(generations={401: 1}), (("action", 1, 0),)),
            ("body_instance", 1, dict(instance=INST + 0x1000), (("action", 1, 0), ("snap", 0, 2))),
            ("body_resource", 1, dict(body_resource=HAMMER), (("action", 1, 0),)),
            ("instance_graph", 1, dict(graph=GRAPH + 0x100000), (("action", 1, 0),)),
            ("instance_active", 1, dict(active=ACTIVE + 0x1000), (("action", 1, 0),)),
            ("graph", 1, dict(layer_count=32), (("action", 1, 0),)),
            ("definitions", 1, dict(def_offsets={2: 0x3000}), (("action", 1, 0),)),
            ("active14", 1, dict(states={14: 3}), (("action", 1, 0), ("snap", 0, 2))),
            ("active14", 1, dict(instance=INST + 0x1000), (("action", 1, 0),)),
            ("active14", 1, dict(graph=GRAPH + 0x100000), (("action", 1, 0),)),
            ("active14", 1, dict(active=ACTIVE + 0x1000), (("action", 1, 0),)),
            ("active14", 1, dict(body_resource=HAMMER), (("action", 1, 0),)),
            ("active14", 1, dict(graph_tag=9), (("action", 1, 0),)),
            ("active14", 1, dict(def_offsets={2: 0x3000}), (("action", 1, 0),)),
            ("active14", 1, dict(states={1: 9}), (("action", 1, 0),)),
            ("active14", 1, dict(state_counts={3: 99}), (("action", 1, 0),)),
            ("active14", 1, dict(local=[11, 29, 101, 401, 301, 1]), (("action", 1, 0),)),
            ("active14", 1, dict(units=UNITS + 0x100000), (("action", 1, 0),)),
            ("active1", 1, dict(states={1: 9}), (("action", 1, 0),)),
            ("def14_count", 2, dict(state_counts={14: 99}), (("action", 1, 0),)),
            ("def3_count", 1, dict(state_counts={3: 99}), (("action", 1, 0),)),
            ("pool_count", 1, dict(pool_count=9), (("snap", 1, 2), ("snap", 0, 2))),
            ("camera_object", 1, dict(camera=CTX + 0x5000), (("fird_fov_config",),)),
            ("light", 1, dict(local=[11, 31, 101, 401, 301, 1]), (("fird_fov_config",),)),
            ("inv_row", 1, dict(pack=0), (("backpack", 0),)),
            ("inv_row", 1, dict(pack=902), (("backpack", 1),)),
            ("inv_owner", 1, dict(inv_owner=u32(9, 9, 9, 9, 9)), (("backpack", 0),)),
            ("render_row1", 1, dict(render_rows=[(4, 0, 0x01000000), (4, 0, 0x06f1d0d3), (4, 0, 0x30000000), (4, 0, 0x72e8f18c), (4, 1, 0x90000000)]),
             (("renderer", 1, 0, 1, 1, 0), ("renderer", 1, 0, 1, 1, 0))),
            ("render_table", 1, dict(render_count=4), (("renderer", 1, 0, 1, 1, 0), ("renderer", 1, 0, 1, 1, 0))),
            ("render_table_global", 1, dict(render_table=RTAB + 0x1000), (("renderer", 1, 0, 1, 1, 0), ("renderer", 1, 0, 1, 1, 0))),
            ("render_row3", 1, dict(render_rows=[(4, 0, 0x01000000), (4, 1, 0x06f1d0d3), (4, 0, 0x30000000), (4, 1, 0x72e8f18c), (4, 1, 0x90000000)]),
             (("renderer", 1, 0, 1, 1, 1), ("renderer", 1, 0, 1, 1, 1)))):
        r.mem(base).step("snap", 0, 0).step("snap", 1, 1).step("renderer", 1, 0, 1, 1, 0)
        r.trap(record, count, **change)
        for one in steps:
            r.step(*one)
        r.untrap()
    bridge = edit(base, states={14: 3}, flags=bytes([2] + [0] * 23), scene_id=3)
    for record, count, change in (("scene", 1, dict(scene_id=4)), ("scene", 2, dict(scene_id=4)), ("state_global", 1, dict(state=STATE + 0x100000)),
                                  ("state_global", 2, dict(state=STATE + 0x100000)), ("flags", 1, dict(flags=bytes([2, 1] + [0] * 22))),
                                  ("flags", 2, dict(flags=bytes([2, 1] + [0] * 22))), ("flags", 3, dict(flags=bytes([2, 1] + [0] * 22)))):
        r.mem(bridge).step("snap", 1, 3).step("action", 3, 1).step("on_foot", 3, 1)
        r.trap(record, count, **change).step("on_foot", 3, 1).untrap().step("on_foot", 3, 1)

    # ---- prepare: refusals ----
    for pose in (1, 0):
        op = "prepare_pose" if pose else "prepare_alignment"
        tag = "P" if pose else "A"
        r = run(op + "_refusals")
        base = scene()
        r.step(op, -1, 63).step("snap", 1, 1).step(op, 1, 63).step("snap", 0, 0)
        for change in (dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(tail=2, rows={1: [6, 101, 201, 401, 0]}), dict(omit={"queue"}),
                       dict(flags=bytes([0] * 6 + [1] + [0] * 17)), dict(controls=bytes([0, 0, 0, 0, 1, 0])),
                       dict(pins={"evaluator": "missing"}), dict(pins={"root_position_call": 1}), dict(omit={"unit801"}),
                       dict(omit={"unit501"}), dict(generations={801: 3}), dict(generations={501: 3}), dict(omit={"weapon_entry"}),
                       dict(weapon_resource=bytes(8)), dict(omit={"weapon_resource"}), dict(rig=0), dict(reg_cap=500), dict(reg_cap=802),
                       dict(wd=[33, 44, 201, 501, 601, 1], weapon_entry=u32(33, 44, 201, 502, 601))):
            r.mem(edit(base, **change)).step(op, 0, 63)
        r.mem(base)
        for code, text in ((0, "validation refused"), (0, None), (-1, None), (99, "prepare raised")):
            r.ans(tag + ".prepare", code, text).step(op, 0, 63)
        for change in (dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(omit={"queue"}), dict(rig=803), dict(omit={"unit801"}),
                       dict(objects={401: OBJ_BODY, 801: OBJ_RIG + 8, 501: OBJ_WEAPON}),
                       dict(objects={401: OBJ_BODY, 801: OBJ_RIG, 501: OBJ_WEAPON + 8})):
            r.mem(base).ans(tag + ".prepare", 1, patch=r.effect(**change)).step(op, 0, 63)
        r.mem(base)
        if pose:
            r.hook("placement_resolver")
            r.ans("placement_resolver", 99, "resolver raised").step(op, 0, 63)
            r.ans("placement_resolver", 1, nums=PLACE(0.2, 0.1, -0.2, mask=63, yaw=1, pitch=2, roll=3)).step(op, 0, r.token())
            r.ans("placement_resolver", 1, nums=PLACE(0.3, 0.2, -0.3)).step(op, 0, r.token())
            r.step(op, 0, r.token()).hook("placement_resolver", 0).step(op, 0, r.token())
            r.step("set_placement_defaults", *PLACE(0.4, 0.3, -0.4, mask=15, yaw=45)).step(op, 0, r.token())
        # a lease that still owes its cleanup blocks the next preparation
        token = r.token()
        r.step(op, 0, token)
        r.ans("enter", patch=r.effect(entered(r.cur)))
        lease = r.lease()
        if pose:
            r.step("enter_pose", 0, token, 0, -1, lease)
        else:
            r.step("enter", 0, token, lease)
        r.step(op, 0, 63).mem(exited(r.cur)).step(op, 0, 63).step("release", lease).step(op, 0, r.token())

    # ---- helpers that have to be loaded ----
    r = run("helper_loading", pose=0, alignment=0)
    r.step("snap", 0, 0).ans("load_P", 99, "pose library missing").step("prepare_pose", 0, 63).step("prepare_pose", 0, r.token())
    r.step("prepare_pose", 0, r.token()).ans("load_A", 99, "alignment library missing").step("prepare_alignment", 0, 63)
    r.step("prepare_alignment", 0, r.token())
    r = run("helper_loader_missing", pose=0, alignment=0)
    r.step("snap", 0, 0).set("loader", 0).step("prepare_pose", 0, 63).step("prepare_alignment", 0, 63).step("snap", 0, 0)
    r.set("loader", 1).step("prepare_pose", 0, r.token()).step("prepare_alignment", 0, r.token())

    # ---- placements ----
    r = run("placements")
    for args in (PLACE(0, 0, 0, mask=-1), PLACE(0, 0, 0, mask=3), PLACE(0, 0, 0, mask=6), PLACE(NAN, 0, 0), PLACE(0.5, -0.5, 0.5),
                 PLACE(0.5000001, 0, 0), PLACE(0, -0.51, 0), PLACE(0, 0, float("inf")), PLACE(0.1, 0.1, 0.1, mask=15, yaw=90),
                 PLACE(0.1, 0.1, 0.1, mask=15, yaw=90.5), PLACE(0.1, 0.1, 0.1, mask=23, pitch=-91), PLACE(0.1, 0.1, 0.1, mask=39, roll=NAN),
                 PLACE(0.1, 0.2, 0.3, mask=63, yaw=-90, pitch=90, roll=0.5)):
        r.step("set_placement_defaults", *args).step("set_pose_placement", -1, *args).step("set_ads_placement", -1, *args)
    r.step("set_pose_zoom_policy", -1, 1)
    lease = r.enter_pose()
    for args in (PLACE(0, 0, 0, mask=-1), PLACE(0.6, 0, 0), PLACE(0.1, 0.2, 0.3)):
        r.step("set_pose_placement", lease, *args).step("set_ads_placement", lease, *args)
    for port in ("P.set_placement", "P.set_ads_placement", "P.set_zoom_policy"):
        op = {"P.set_placement": "set_pose_placement", "P.set_ads_placement": "set_ads_placement", "P.set_zoom_policy": "set_pose_zoom_policy"}[port]
        args = (lease, 0) if op == "set_pose_zoom_policy" else (lease, *PLACE(0.01, 0.02, 0.03))
        for code, text in ((0, "not now"), (0, None), (-1, None), (99, "setter raised"), (1, "accepted with a note")):
            r.ans(port, code, text).step(op, *args)
    base = r.cur
    for change in (dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(omit={"queue"}), dict(rig=804), dict(tail=3, rows={2: [6, 101, 201, 401, 0]}),
                   dict(tail=1), dict(omit={"unit501"}), dict(objects={401: OBJ_BODY, 801: OBJ_RIG, 501: OBJ_WEAPON + 16})):
        r.mem(edit(base, **change))
        r.step("set_pose_placement", lease, *PLACE(0.1, 0.2, 0.3)).step("set_ads_placement", lease, *PLACE(0.1, 0.2, 0.3))
        r.step("set_pose_zoom_policy", lease, 1).step("set_pose_motion", lease, *motion()).step("fov_config", lease)
    r.mem(base)
    r.ans("P.finish", 0, "busy").step("release", lease).step("set_pose_placement", lease, *PLACE(0.1, 0.2, 0.3))
    r.mem(omit={"queue"}).step("release", lease).step("set_pose_placement", lease, *PLACE(0.1, 0.2, 0.3)).step("set_ads_placement", lease, *PLACE(0.1, 0.2, 0.3))
    r.step("set_pose_zoom_policy", lease, 1).step("set_pose_motion", lease, *motion()).step("set_pose_transition", lease, *phase())
    r.mem(exited(base)).step("release", lease).step("set_ads_placement", lease, *PLACE(0.1, 0.2, 0.3)).step("set_pose_zoom_policy", lease, 0)

    # ---- transition packets and motion ----
    r = run("transition_motion")
    lease = r.enter_pose()
    r.step("snap", 0, 1).step("set_pose_transition", lease, *phase()).step("begin_frame")
    for args in (phase(epoch=NAN), phase(epoch=0), phase(epoch=1.5), phase(epoch=9007199254740992.0), phase(epoch=9007199254740991.0),
                 phase(progress=NAN), phase(progress=-0.01), phase(progress=1.01), phase(progress=1), phase(progress=0), phase(ads=-1),
                 phase(identity=-1), phase(player=-1), phase(identity=1), phase(player=-2), phase(ads=0), phase(ads=0),
                 phase(ads=0, started=1.5), phase(ads=0, started=1.5), phase(ads=0, started=1.5, duration=0.18),
                 phase(ads=0, started=1.5, duration=0.18, continuous=1), phase(ads=0, started=1.5, duration=0.18, continuous=0),
                 phase(ads=0, started=1.5, duration=0.18, continuous=0, reason=3), phase(ads=0, started=1.5, duration=0.18, continuous=0, reason=3),
                 phase(ads=0, started=1.5, duration=0.18, continuous=0, reason=3, action=2), phase(epoch=2), phase(epoch=2)):
        r.step("set_pose_transition", lease, *args)
    for code, text in ((0, "phase refused"), (0, None), (-1, None), (99, "phase raised")):
        r.ans("P.transition", code, text).step("set_pose_transition", lease, *phase(epoch=3)).step("set_pose_transition", lease, *phase(epoch=3))
    r.step("set_pose_transition", lease, *phase(epoch=3)).step("end_frame").step("begin_frame").step("set_pose_transition", lease, *phase(epoch=3))
    r.ans("P.owned_handle", 0).step("set_pose_transition", lease, *phase(epoch=4))
    r.ans("P.owned_handle", 99, "lookup raised").step("set_pose_transition", lease, *phase(epoch=4))
    r.ans("P.owned_handle", 3).step("set_pose_transition", lease, *phase(epoch=4))
    r.hook("P.owned_handle", 0).step("set_pose_transition", lease, *phase(epoch=4)).hook("P.owned_handle", 1)
    r.hook("P.transition", 0).step("set_pose_transition", lease, *phase(epoch=4)).hook("P.transition", 1)
    r.step("set_pose_transition", -1, *phase(epoch=4)).step("set_pose_transition", 5, *phase(epoch=4))
    for args in (motion(mask=-1), motion(dx=NAN), motion(dy=0.151), motion(dz=-0.151), motion(dx=0.15, dy=-0.15, dz=0.15), motion(mask=7),
                 motion(mask=8), motion(mask=15, q=(0, 0, 0, NAN)), motion(mask=15, q=(1.002, 0, 0, 0)), motion(mask=15, q=(0, 0, 0, 1.001)),
                 motion(mask=15, q=(0, 0, 0, 1.0011)), motion(mask=15, q=(0, 0, 0, 0.99)), motion(mask=15, q=(0.8660254037844386, 0, 0, 0.5)), motion(mask=15, q=(0.5, 0.5, 0.5, 0.5)),
                 motion(mask=15, q=(0.5, 0.5, 0.5, -0.5)), motion(mask=15, q=(0.5, 0.5, 0.5, 0.4999)),
                 motion(mask=15, q=(0.87, 0, 0, 0.493)), motion(mask=15, q=(0, 0, 0, -1)), motion(mask=15, q=(0.6, 0, 0, 0.8), authored=1)):
        r.step("set_pose_motion", lease, *args)
    r.step("set_pose_motion", -1, *motion())
    for code, text in ((0, "motion refused"), (0, None), (-1, None), (99, "motion raised")):
        r.ans("P.set_motion", code, text).step("set_pose_motion", lease, *motion())
    r.hook("presentation_transition")
    r.step("set_pose_motion", lease, *motion())
    r.ans("presentation_transition", 1, nums=phase(epoch=7)).step("set_pose_motion", lease, *motion())
    r.ans("presentation_transition", 1, nums=phase(epoch=7)).step("set_pose_motion", lease, *motion())
    r.ans("presentation_transition", 1, nums=phase(epoch=0)).step("set_pose_motion", lease, *motion())
    r.ans("presentation_transition", 1, nums=phase(epoch=8)).ans("P.transition", 0).step("set_pose_motion", lease, *motion())
    r.ans("presentation_transition", 99, "packet hook raised").step("set_pose_motion", lease, *motion())
    r.step("end_frame").ans("presentation_transition", 1, nums=phase(epoch=9)).step("set_pose_motion", lease, *motion())
    r.mem(exited(r.cur)).step("release", lease).step("begin_frame").step("set_pose_transition", lease, *phase(epoch=9)).step("end_frame")

    # ---- pose_status ----
    r = run("pose_status")
    r.step("pose_status", -1).step("alignment_status", -1)
    base = scene()
    cases = [(("P.stats", 0, "stats unreadable"), None), (("P.stats", 0, None), None), (("P.stats", -1, None), None),
             (("P.stats", 99, "stats raised"), None), (("P.stats", 1, None, (2, 0, 0)), None), (("P.stats", 1, None, (3, 2, NAN, 0.5, 0.25)), None),
             (("P.stats", 1, None, (0, 1, 0)), None), (("P.stats", 1, None, (1, 0, 14)), None), (("P.stats", 1, None, (1, 0, 3)), None),
             (("P.stats", 1, None, (1, 0, 2.5)), None), (("P.stats", 1, None, (1, 0, -1)), None), (("P.stats", 1, None, (1, 0, 1e15)), None),
             (None, dict(controls=bytes([2, 0, 0, 0, 0, 0]))), (None, dict(omit={"queue"})), (None, dict(rig=805)),
             (None, dict(tail=3, rows={2: [6, 101, 201, 401, 0]})), (None, dict(tail=1)), (None, dict(states={14: 4})),
             (None, dict(states={14: "bad"})), (None, dict(omit={"unit501"})), (None, dict(objects={401: OBJ_BODY, 801: OBJ_RIG, 501: OBJ_WEAPON + 24})),
             (None, dict(controls=bytes([0, 0, 0, 0, 1, 0]), flags=bytes([0] * 6 + [1] + [0] * 17))), (None, "foreign")]
    for answer, change in cases:
        r.mem(base)
        lease = r.enter_pose()
        entered_scene = r.cur
        r.step("pose_status", lease)
        for finish in ((0, "end refused"), (1, None, (1, 0, 0, 0.75, 1)), None):
            if answer:
                r.ans(answer[0], answer[1], answer[2], nums=answer[3] if len(answer) > 3 else ())
            if change == "foreign":
                r.hook("foreign_camera").ans("foreign_camera", 1, sticky=1)
            elif change:
                r.mem(edit(entered_scene, **change))
            if finish and len(finish) > 2:
                r.ans("P.finish", finish[0], finish[1], nums=finish[2])
            elif finish:
                r.ans("P.finish", finish[0], finish[1])
            r.step("pose_status", lease).step("alignment_status", lease)
        r.clear("P.finish").clear("foreign_camera")
        if change == "foreign":
            r.hook("foreign_camera", 0)
        r.mem(exited(entered_scene)).step("release", lease).step("pose_status", lease).step("alignment_status", lease)
    r.mem(base)
    lease = r.enter_pose()
    r.ans("P.finish", 4, "ended with a note").mem(controls=bytes([2, 0, 0, 0, 0, 0])).step("pose_status", lease)
    r.mem(exited(edit(r.cur, controls=bytes(6)))).step("release", lease)

    # ---- release: every way out, the orphan exit included ----
    r = run("release")
    base = scene()
    r.step("release", -1)
    for case in ("replaced_context", "replaced_camera", "module_changed", "other_kind", "other_avatar", "other_weapon", "other_record",
                 "unreadable", "pins_cold", "aim_took", "aim_took_entry_changed", "identity_changed", "exit_ok", "exit_raises",
                 "exit_page", "exit_still_present", "exit_unreadable", "exit_other_context", "finish_refused", "finish_raises", "retiring",
                 "retiring_refused"):
        r.mem(base)
        if case == "aim_took_entry_changed":
            r.step("snap", 0, 0)
            token = r.token()
            r.step("prepare_pose", 0, token)
            r.ans("enter", patch=r.effect(entered(r.cur, controls=bytes([0, 0, 0, 0, 1, 0]))))
            lease = r.lease()
            r.step("enter_pose", 0, token, 0, -1, lease)
        else:
            lease = r.enter_pose()
        inside = r.cur
        if case == "replaced_context":
            r.mem(edit(inside, context=CTX + 0x100000))
        elif case == "replaced_camera":
            r.mem(camera=CTX + 0x4000)
        elif case == "module_changed":
            r.mem(game=BASE2)
        elif case == "other_kind":
            r.mem(rows={1: [4, 101, 201, 401, 0]})
        elif case == "other_avatar":
            r.mem(rows={1: [6, 102, 201, 401, 0]})
        elif case == "other_weapon":
            r.mem(rows={1: [6, 101, 202, 401, 0]})
        elif case == "other_record":
            r.mem(tail=3, rows={2: [6, 101, 201, 401, 0]})
        elif case == "unreadable":
            r.mem(omit={"queue"})
        elif case == "pins_cold":
            r.mem(pins={"evaluator": "missing"})
        elif case in ("aim_took", "aim_took_entry_changed"):
            r.mem(controls=bytes([0, 0, 0, 0, 1, 0]), flags=bytes([0] * 6 + [1] + [0] * 17))
        elif case == "identity_changed":
            r.mem(rig=806, objects={401: OBJ_BODY, 806: OBJ_RIG, 501: OBJ_WEAPON})
        elif case == "exit_ok":
            r.ans("exit", patch=r.effect(exited(inside)))
        elif case == "exit_raises":
            r.ans("exit", 99, "exit crashed")
        elif case == "exit_page":
            r.set("region", 0x1000, 0x1000000, 0x02, 1, 1)
        elif case == "exit_still_present":
            r.ans("exit")
        elif case == "exit_unreadable":
            r.ans("exit", patch=r.effect(omit={"queue"}))
        elif case == "exit_other_context":
            r.ans("exit", patch=r.effect(context=CTX + 0x200000))
        elif case == "finish_refused":
            r.ans("P.finish", 0, "end refused").ans("P.finish", 0).ans("P.finish", -1)
        elif case == "finish_raises":
            r.ans("P.finish", 99, "end raised")
        elif case in ("retiring", "retiring_refused"):
            # the wielded weapon changed (a swap): only the retiring form can still prove the request
            r.mem(weapon=202, entity_map=dict(keys={201: 1, 901: 2, 202: 3}))
            if case == "retiring_refused":
                r.mem(rig=807, objects={401: OBJ_BODY, 807: OBJ_RIG, 501: OBJ_WEAPON})
            else:
                r.ans("exit", patch=r.effect(exited(r.cur)))
        r.step("release", lease).step("release", lease).step("release", lease)
        r.set("region", 0x1000, 0x1000000, 0x20, 1, 1).clear("exit").clear("P.finish")
        r.mem(exited(inside)).step("release", lease).step("release", lease)
    # orphan: the request's weapon is gone
    for case in ("exit", "stim", "controller_active", "other_kind", "player_changed", "reset", "exit_raises", "exit_still", "exit_unreadable",
                 "other_mask", "context_changed", "entry_pin", "recovered", "exit_page"):
        r.mem(base)
        lease = r.enter_pose()
        inside = r.cur
        gone = edit(inside, weapon=0xffffffff, entity_map=dict(keys={901: 2}))
        r.mem(gone).step("release", lease).step("release", lease)
        if case == "stim":
            r.mem(controls=bytes([64, 0, 0, 0, 0, 0]))
        elif case == "controller_active":
            r.mem(controls=bytes([0, 0, 0, 0, 1, 0]))
        elif case == "other_kind":
            r.mem(wd=[33, 44, 201, 501, 601, 0])
        elif case == "player_changed":
            r.mem(local=[11, 23, 101, 401, 301, 1])
        elif case == "reset":
            r.mem(controls=bytes([64, 0, 0, 0, 0, 0])).step("release", lease).mem(controls=bytes(6))
        elif case == "exit_raises":
            r.ans("exit", 99, "exit crashed", sticky=1)
        elif case == "exit_still":
            pass
        elif case == "exit_unreadable":
            r.ans("exit", patch=r.effect(omit={"queue"}))
        elif case == "other_mask":
            r.mem(controls=bytes([8, 0, 0, 0, 0, 0]))
            r.ans("exit", patch=r.effect(exited(r.cur)))
        elif case == "context_changed":
            r.ans("exit", patch=r.effect(context=CTX + 0x300000))
        elif case == "entry_pin":
            r.mem(entry_pin=1)
            r.ans("exit", patch=r.effect(exited(r.cur)))
        elif case == "recovered":  # the weapon is back before the count is full: the count starts again
            r.mem(inside).step("release", lease).mem(gone)
        elif case == "exit_page":
            r.set("region", 0x1000, 0x1000000, 0x01, 1, 1)
        else:
            r.ans("exit", patch=r.effect(exited(r.cur)))
        for _ in range(5):
            r.step("release", lease)
        r.set("region", 0x1000, 0x1000000, 0x20, 1, 1).clear("exit").mem(exited(inside)).step("release", lease)

    # ---- keep_aim_view and detach_seated_camera ----
    r = run("one_shot_writes")
    base = scene()
    aim = edit(entered(base), controls=bytes([0, 0, 0, 0, 1, 0]))
    r.step("keep_aim_view", -1).mem(aim).step("snap", 1, 0).step("snap", 0, 1).step("snap", 2, 2)
    r.step("keep_aim_view", 0).step("snap", 1, 3).step("keep_aim_view", 0).step("keep_aim_view", 3)
    rearm = lambda: r.mem(controls=bytes([7, 0, 0, 0, 0, 0])).mem(aim)
    rearm().step("keep_aim_view", 1)
    rearm().step("keep_aim_view", 2)
    rearm().step("keep_aim_view", 0).step("keep_aim_view", 0)
    for change in (dict(controls=bytes([0, 0, 0, 0, 2, 0])), dict(omit={"controls"}), dict(controls=bytes([0, 0, 0, 0, 1, 1])),
                   dict(controls=bytes([1, 0, 0, 0, 1, 0])), dict(flags=bytes([0] * 14 + [4] + [0] * 9)), dict(tail=1),
                   dict(rows={1: [6, 102, 201, 401, 0]})):
        r.mem(edit(aim, **change)).step("snap", 1, 4).step("snap", 2, 5).step("keep_aim_view", 0).step("keep_aim_view", 4).step("keep_aim_view", 5)
    rearm().set("keep_aim_view", 0).step("keep_aim_view", 0).set("keep_aim_view", 1).step("keep_aim_view", 0)
    rearm().set("camera_profile", 0).step("keep_aim_view", 0).set("camera_profile", 1).step("keep_aim_view", 0)
    seated = edit(base, flags=bytes([0] * 14 + [4] + [0] * 9), camera_parent=0xc10000000)
    r.mem(base).step("detach_seated_camera").mem(seated).step("detach_seated_camera").step("detach_seated_camera")
    r.set("seated_head", 0).step("detach_seated_camera").set("seated_head", 1)
    for change in (dict(camera_parent=0), dict(rows={0: [6, 101, 201, 401, 0]}), dict(omit={"queue"}), dict(omit={"camera_graph"}),
                   dict(omit={"camera_parent"}), dict(omit={"api_root"}), dict(omit={"api1"}), dict(omit={"detach"}), dict(detach=0),
                   dict(omit={"camera_world"}), dict(camera_world=5), dict(omit={"camera_unit"}), dict(camera_graph=0x10010),
                   dict(rows={0: [4, 102, 201, 401, 0]}), dict(camera_unit=0)):
        r.mem(edit(seated, **change)).step("detach_seated_camera")
    r.mem(seated)
    for region in ((0x2000, 0, 0x20, 0, 1), (0x1000, 0, 0x04, 0, 1), (0x1000, 0, 0x20, 0, 0), (0x1000, 0, 0x40, 0, 1), (0x1000, 0x40000, 0x80, 2, 1)):
        r.set("region", *region).step("detach_seated_camera")
    r.set("region", 0x1000, 0x1000000, 0x20, 1, 1).ans("detach", 99, "detach crashed").step("detach_seated_camera")

    # ---- Wine: a mapped image page is accepted only there, and only inside the game image ----
    for wine in (0, 1):
        r = run(f"mapped_pages_wine{wine}", wine=wine)
        base = scene()
        for region in ((0x1000, 0x40000, 0x20, 1, 1), (0x1000, 0x40000, 0x20, 2, 1), (0x1000, 0x40000, 0x20, 0, 1), (0x1000, 0x1000000, 0x20, 0, 1)):
            r.mem(base).set("region", *region).step("snap", 0, 0)
            token = r.token()
            r.step("prepare_pose", 0, token)
            r.ans("enter", patch=r.effect(entered(r.cur)))
            lease = r.lease()
            r.step("enter_pose", 0, token, 0, -1, lease)
            r.ans("exit", patch=r.effect(exited(r.cur))).step("release", lease)
            r.set("region", 0x1000, 0x1000000, 0x20, 1, 1).clear("enter").clear("exit")
            r.mem(exited(entered(base))).step("release", lease).mem(base).step("release", lease)
            body = r.token()
            r.step("prepare_body_pose", 0, body)
            lease = r.lease()
            r.ans("enter", patch=r.effect(entered(r.cur))).step("enter", 0, -1, lease)
            r.set("region", *region).step("release_body_pose", lease, body)
            r.set("region", 0x1000, 0x1000000, 0x20, 1, 1).mem(base).step("release", lease)

    # ---- the paired exit body event ----
    r = run("body_event")
    base = scene()
    r.step("prepare_body_pose", -1, 63).step("snap", 0, 0).step("release_body_pose", -1, -1)
    for change in (dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(omit={"queue"}), dict(game=BASE2), dict(exe=0), dict(exe_mz=b"ZM"),
                   dict(exe_lfanew=0x2000), dict(exe_machine=b"\0\0"), dict(exe_stamp_delta=1), dict(exe_pins={"exists": 1}),
                   dict(exe_pins={"trigger": 1}), dict(exe_pins={"exists": "missing"}), dict(omit={"api_root"}), dict(omit={"uapi"}),
                   dict(exists_slot=16), dict(trigger_slot=16), dict(omit={"exists_slot"}), dict(omit={"trigger_slot"}),
                   dict(pins={"evaluator": "missing"})):
        r.mem(edit(base, **change)).step("prepare_body_pose", 0, 63)
    r.mem(base)
    body = r.token()
    r.step("prepare_body_pose", 0, body)
    cases = ["ok", "no_token", "wrong_token", "used_token", "not_exists", "exists_raises", "exists_page", "trigger_raises",
             "trigger_changes_aim", "api_changed_before", "api_changed_between", "owner_changed_between", "not_owned", "aiming",
             "api_unreadable", "token_other_api", "done"]
    for case in cases:
        r.mem(base).step("snap", 0, 0)
        body = r.token()
        r.step("prepare_body_pose", 0, body)
        other = r.token()
        r.step("prepare_pose", 0, other)
        if case == "token_other_api":
            r.mem(uapi=UAPI + 0x1000)
        r.ans("enter", patch=r.effect(entered(r.cur)))
        lease = r.lease()
        r.step("enter", 0, -1, lease)
        inside = r.cur
        token_arg = body
        if case == "no_token":
            token_arg = -1
        elif case == "wrong_token":
            token_arg = other
        elif case == "used_token":
            r.step("release_body_pose", lease, body)
            lease2 = lease
            r.mem(exited(inside)).step("release", lease2).mem(base)
            r.ans("enter", patch=r.effect(entered(r.cur)))
            lease = r.lease()
            r.step("enter", 0, -1, lease)
        elif case == "not_exists":
            r.ans("body_exists", 0)
        elif case == "exists_raises":
            r.ans("body_exists", 99, "query crashed")
        elif case == "exists_page":
            r.set("region", 0x1000, 0x20000, 0x20, 2, 1)
        elif case == "trigger_raises":
            r.ans("body_trigger", 99, "trigger crashed")
        elif case == "trigger_changes_aim":
            r.ans("body_trigger", patch=r.effect(controls=bytes([0, 0, 0, 0, 1, 0]), flags=bytes([0] * 6 + [1] + [0] * 17)))
        elif case == "api_changed_before":
            r.mem(uapi=UAPI + 0x2000)
        elif case == "api_changed_between":
            r.ans("body_exists", 1, patch=r.effect(uapi=UAPI + 0x3000))
        elif case == "owner_changed_between":
            r.ans("body_exists", 1, patch=r.effect(controls=bytes([2, 0, 0, 0, 0, 0])))
        elif case == "not_owned":
            r.mem(tail=3, rows={2: [6, 101, 201, 401, 0]})
        elif case == "aiming":
            r.mem(flags=bytes([0] * 6 + [1] + [0] * 17))
        elif case == "api_unreadable":
            r.mem(omit={"uapi"})
        elif case == "done":
            r.mem(exited(inside)).step("release", lease).mem(inside)
        r.step("release_body_pose", lease, token_arg).step("release_body_pose", lease, token_arg)
        r.set("region", 0x1000, 0x1000000, 0x20, 1, 1)
        r.mem(inside).step("release", lease).mem(exited(inside)).step("release", lease)

    # ---- warm (bootstrap) entry ----
    r = run("warm")
    base = scene()
    r.step("warm_start", -1, 63).step("snap", 0, 0).step("warm_finish", -1)
    for change in (dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(omit={"queue"}), dict(rig=808), dict(entry_pin=1),
                   dict(pins={"controller_enter": "missing"})):
        r.mem(edit(base, **change)).step("warm_start", 0, 63)
    r.mem(base).ans("allowed", 0).step("warm_start", 0, 63).ans("allowed", 99, "allowed crashed").step("warm_start", 0, 63)
    r.ans("allowed", 1, patch=r.effect(tail=2, rows={1: [4, 101, 201, 401, 0]})).step("warm_start", 0, 63)
    r.mem(base).ans("allowed", 1, patch=r.effect(omit={"controls"})).step("warm_start", 0, 63)
    for case in ("ok", "enter_raises", "no_request", "unreadable_after", "player_aims", "player_aims_entry", "retired", "replaced",
                 "exit_raises", "exit_still", "exit_unreadable", "blocked", "foreign", "identity", "top_unreadable", "cold_pins"):
        r.mem(base).step("snap", 0, 0)
        lease = r.lease()
        if case == "enter_raises":
            r.ans("enter", 99, "enter crashed", patch=r.effect(entered(r.cur)))
        elif case == "no_request":
            r.ans("enter")
        elif case == "unreadable_after":
            r.ans("enter", patch=r.effect(omit={"queue"}))
        elif case == "player_aims_entry":
            r.ans("enter", patch=r.effect(entered(r.cur, controls=bytes([0, 0, 0, 0, 1, 0]), flags=bytes([0] * 6 + [1] + [0] * 17))))
        elif case == "cold_pins":
            r.mem(pins={"evaluator": "missing", "root_position_call": "missing"})
            r.ans("enter", patch=r.effect(entered(r.cur)))
        else:
            r.ans("enter", patch=r.effect(entered(r.cur)))
        r.step("warm_start", 0, lease).step("warm_start", 0, 63).step("prepare_pose", 0, 63)
        inside = edit(entered(base), pins=r.cur["pins"])
        if case == "unreadable_after":
            r.mem(inside)
        if case in ("player_aims", "player_aims_entry"):
            r.mem(edit(inside, controls=bytes([0, 0, 0, 0, 1, 0]), flags=bytes([0] * 6 + [1] + [0] * 17)))
        elif case == "retired":
            r.mem(exited(inside))
        elif case == "replaced":
            r.mem(edit(inside, context=CTX + 0x400000))
        elif case == "exit_raises":
            r.ans("exit", 99, "exit crashed")
        elif case == "exit_still":
            r.ans("exit")
        elif case == "exit_unreadable":
            r.ans("exit", patch=r.effect(omit={"queue"}))
        elif case == "blocked":
            r.mem(controls=bytes([2, 0, 0, 0, 0, 0]))
        elif case == "foreign":
            r.hook("foreign_camera")
            for code in (1, -1, 99):  # the snapshot asks first and is told no; then the cleanup's own question
                r.ans("foreign_camera", 0).ans("foreign_camera", code, "hook raised")
        elif case == "identity":
            r.mem(rig=809, objects={401: OBJ_BODY, 809: OBJ_RIG, 501: OBJ_WEAPON})
        elif case == "top_unreadable":
            r.mem(omit={"camera_global"})
        elif case in ("ok", "no_request", "enter_raises", "cold_pins", "unreadable_after"):
            if case != "no_request":
                r.ans("exit", patch=r.effect(exited(r.cur)))
        for _ in range(4):
            r.step("release", lease)
        r.hook("foreign_camera", 0).clear("exit").clear("foreign_camera")
        r.mem(exited(inside)).step("warm_finish", lease).step("warm_finish", lease).mem(base)
    lease = r.lease()
    r.ans("enter", patch=r.effect(entered(r.cur))).step("warm_start", 0, lease)
    r.mem(exited(r.cur)).step("release", lease)
    lease2 = r.lease()
    r.mem(base).ans("enter", patch=r.effect(entered(r.cur))).step("warm_start", 0, lease2).step("warm_finish", lease)
    r.mem(exited(r.cur)).step("warm_finish", lease2)
    # a pose lease blocks the warm entry
    r.mem(base)
    lease = r.enter_pose()
    r.step("warm_start", 0, 63).mem(exited(r.cur)).step("release", lease)

    # ---- the presentation switch ----
    r = run("presentation")
    base = scene()
    r.step("presentation", -1, 1).step("snap", 0, 0).step("snap", 1, 1)
    r.ans("presentation", patch=r.effect(mode=1)).step("presentation", 0, 1).step("presentation", 1, 1)
    r.ans("presentation", patch=r.effect(mode=0)).step("presentation", 0, 0)
    r.step("presentation", 0, 1).ans("presentation", 99, "switch crashed").step("presentation", 0, 0)
    changes = [dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(omit={"queue"}), dict(rig=810), dict(controls=bytes([0, 0, 0, 0, 1, 0])),
               dict(tail=2, rows={1: [6, 101, 201, 401, 0]}), dict(mode=2), dict(omit={"mode"}), dict(stamp_delta=1), dict(exe_stamp_delta=1),
               dict(pins={"evaluator": "missing"})]
    changes += [dict(presentation_pins={i: 1}) for i in range(9)] + [dict(presentation_pins={3: "missing"})]
    for change in changes:
        r.mem(edit(base, **change)).step("presentation", 0, 1)
    r.mem(base).hook("foreign_camera")
    for code in (1, -1, 99, 0):
        r.ans("foreign_camera", code, "hook raised", sticky=1).step("presentation", 0, 1)
    r.hook("foreign_camera", 0).clear("foreign_camera")
    for region in ((0x2000, 0x1000000, 0x20, 1, 1), (0x1000, 0x40000, 0x20, 1, 1), (0x1000, 0x1000000, 0x20, 1, 0)):
        r.set("region", *region).step("presentation", 0, 1)
    r.set("region", 0x1000, 0x1000000, 0x20, 1, 1)
    for change in (dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(rig=811), dict(tail=2, rows={1: [6, 101, 201, 401, 0]}), dict(omit={"mode"}),
                   dict(context=CTX + 0x500000)):
        r.mem(base).ans("presentation", patch=r.effect(**change)).step("presentation", 0, 1)
    r.step("begin_frame").mem(base).ans("presentation", patch=r.effect(mode=1)).step("presentation", 0, 1).step("end_frame")

    # ---- the renderer's Boolean settings ----
    r = run("renderer")
    base = scene()
    G, S, W = 0, 1, 2
    r.step("renderer", 1, G, 1, S, 0).step("renderer", 1, G, 1, S, 0).step("renderer", 1, G, 1, S, 1).step("renderer", 1, G, 1, S, 1)
    r.step("renderer", 1, G, 1, S, 2).step("renderer", 0, G, 1, S, 0).step("renderer", 1, G, 0, S, 0).step("renderer", 1, W, 1, S, 0)
    r.step("renderer", 1, G, 1, W, 0).step("renderer", 2, G, 1, S, 0).step("renderer", 2, G, 2, S, 1).step("renderer", 1, G, 1, S, 3)
    rows = base["render_rows"]
    for change in (dict(render_rows=[(4, 0, 0x01000000), (4, 0, 0x06f1d0d3), (4, 0, 0x30000000), (4, 1, 0x72e8f18c), (4, 1, 0x90000000)]),
                   dict(render_rows=[(4, 1, 0x06f1d0d3)] + rows[2:]), dict(render_rows=rows[:1] + rows[2:]),
                   dict(render_rows=[(5, 1, 0x06f1d0d3)] + rows[2:]), dict(render_rows=[(4, 2, 0x06f1d0d3)] + rows[2:]),
                   dict(render_count=0), dict(render_count=4097), dict(render_count=2), dict(render_rows_at=0x100),
                   dict(render_rows_at=0x7ffffffffff0), dict(render_rows_at=0xffff800000000000), dict(render_table=0),
                   dict(omit={"render_table"}), dict(omit={"render_row1"}), dict(omit={"render_row3"}), dict(render_code={"render_get": 1}),
                   dict(render_code={"render_set": 1}), dict(render_code={"render_load": 1}), dict(render_code={"render_type": 1}),
                   dict(render_code={"render_type": "missing"}), dict(exe_stamp_delta=1), dict(exe=EXE2),
                   dict(render_rows=[(4, 0, 0x00000001), (4, 0, 0x00000002)] + rows), dict(render_rows=rows + [(4, 0, 0xa0000000)] * 40)):
        r.mem(edit(base, **change))
        r.step("renderer", 1, G, 1, S, 0).step("renderer", 1, G, 1, S, 1).step("renderer", 3, G, 3, S, 0).step("renderer", 1, G, 1, S, 0)
        r.mem(base).step("renderer", 1, G, 1, S, 0).step("renderer", 1, G, 1, S, 1)
    r.step("begin_frame").step("renderer", 1, G, 1, S, 0).mem(render_code={"render_get": 1}).step("renderer", 1, G, 1, S, 0)
    r.step("renderer", 4, G, 1, S, 0).step("end_frame")

    # ---- the readers other modules use ----
    r = run("contexts")
    base = scene()
    for change in (dict(), dict(rig=0), dict(rig=0xffffffff), dict(rig=(5 << 22) | 801, objects={401: OBJ_BODY, (5 << 22) | 801: OBJ_RIG, 501: OBJ_WEAPON}),
                   dict(generations={801: 1}), dict(reg_cap=801), dict(reg_cap=802), dict(units=0), dict(gens=0), dict(omit={"gen801"}),
                   dict(omit={"unit801"}), dict(objects={401: OBJ_BODY, 801: 0x100, 501: OBJ_WEAPON}), dict(omit={"registry"}),
                   dict(reg_cap=502), dict(reg_cap=501), dict(generations={501: 2}), dict(omit={"unit501"}), dict(omit={"gen501"}),
                   dict(omit={"weapon_entry"}), dict(objects={401: OBJ_BODY, 801: OBJ_RIG, 501: 0})):
        r.mem(base).step("snap", 0, 0).step("snap", 1, 1).mem(edit(base, **change))
        if "rig" in change:
            r.step("snap", 0, 0).step("snap", 1, 1)
        for which in (0, 1, 2, 3):
            r.step("context", 0, which)
        r.step("context", 1, 3).step("context", 1, 2)
    # ---- the readers and checks other modules call directly ----
    r = run("readers")
    base = scene()
    r.step("snap", 0, 0)
    for change in (dict(), dict(omit={"controls"}), dict(camera=0x20), dict(pins={"evaluator": "missing"}), dict(entry_pin=1),
                   dict(stamp_delta=1), dict(exe_stamp_delta=1), dict(exe=0), dict(game=0), dict(exe_pins={"exists": "missing"}),
                   dict(entity_map=dict(cap=8, keys={209: 3, 201: 1, 901: 2})), dict(wd=[33, 44, 201, 501, 601, 2]),
                   dict(light_map=dict(cap=16)), dict(pool_map=dict(keys={101: 7})), dict(pool_map=dict(keys={101: 8})),
                   dict(pool_map=dict(keys={101: 4095})), dict(pool_map=dict(keys={101: 4096}))):
        r.mem(edit(base, **change))
        for scope in (0, 1, 2):
            r.step("module", scope).step("executable", scope)
        for kind in range(7):
            for where in range(6):
                r.step("peek", kind, where, 0)
        for which, key, limit in ((0, 201, -1), (0, 201, 1), (0, 201, 2), (0, 901, 1048576), (0, 209, -1), (0, 0, -1), (0, 0xffffffff, -1),
                                  (0, 777, -1), (1, 101, 8), (1, 101, -1), (2, 301, 1048576), (2, 301, 0)):
            r.step("map", which, key, limit, 0)
        for key in (201, 901, 209, 0, 5):
            r.step("descriptor", key, 0)
    r.mem(base).step("begin_frame")
    for _ in range(2):
        for scope in (0, 1, 2):
            r.step("module", scope).step("executable", scope)
        r.mem(stamp_delta=1, exe_stamp_delta=1)
    r.step("invalidate")
    for scope in (0, 1, 2):
        r.step("module", scope).step("executable", scope)
    r.mem(base).step("invalidate").step("module", 0).step("executable", 1).step("end_frame").step("invalidate").step("module", 0)
    return runs


PLAYER_RECORDS = ["camera_global", "camera_object", "queue", "row0", "world_global", "controller_global", "controls", "player_global",
                  "player_count", "light", "light_map", "light_map_rows", "local", "pool_global", "pool_map", "pool_map_rows", "pool_count",
                  "ep_pointer", "ep", "flags", "rig"]
WEAPON_RECORDS = ["wield_global", "wield_map", "wield_map_rows", "wield_owners", "wield_owner_pointer", "wield_owner", "wield_rows",
                  "wield_row", "entity_map", "entity_map_rows", "wd", "weapons_global", "weapons_map", "weapons_map_rows", "weapons_entries",
                  "weapon_entry_pointer", "weapon_entry"]
EXE_RECORDS = ["exe_image", "exe_pin_exists", "exe_pin_trigger", "registry_global", "registry", "gen401", "unit401", "gen801", "unit801",
               "gen501", "unit501"]
ANIM_RECORDS = ["body_vt", "vt_anim", "vt_resource", "anim_code", "resource_code", "body_resource", "body_instance", "instance_graph",
                "instance_active", "graph", "definitions", "def1_count", "def3_count", "def14_count", "def1_entries", "def14_entries",
                "active1", "active3", "active14"]
OTHER_RECORDS = ["image", "pin_evaluator", "pin_exit", "state_global", "scene", "inventory_global", "inv_map", "inv_owners",
                 "inv_owner_pointer", "inv_owner", "inv_rows", "inv_row", "pack_d", "api_root", "uapi", "exists_slot", "trigger_slot",
                 "camera_unit", "weapon_resource", "render_table_global", "render_table", "render_row1", "render_row3", "render_get",
                 "render_load"]


def spread():
    """The same scene with every index and slot away from zero."""
    s = scene()
    s["light_map"]["keys"] = {301: 5}
    s["pool_map"]["keys"] = {101: 3}
    s["slot"], s["pool_count"] = 3, 8
    s["wield_map"]["keys"] = {101: 2}
    s["weapons_map"]["keys"] = {201: 3}
    s["inv_map"]["keys"] = {101: 4}
    s["entity_map"]["keys"] = {201: 6, 901: 7}
    return s


def more_hand_made():
    runs = []

    def run(name, **kw):
        r = Run(name, **kw)
        runs.append(r)
        return r

    base = scene()
    flags = lambda **bits: bytes(bits.get("b%d" % i, 0) for i in range(24))
    big_rig, far_rig, far_weapon = (255 << 22) | 0x3fffff, (300 << 22) | 801, (300 << 22) | 501

    # ---- exactly at the limits ----
    r = run("boundaries")
    scenes = [edit(base, **change) for change in (
        dict(camera=0x10000), dict(camera=0x800000000000), dict(camera=0x7fffffffffff), dict(camera=0xffff), dict(context=0x10000),
        dict(game=0x10000), dict(game=0xffff), dict(exe=0x10000), dict(exe=0xffff), dict(light_map=dict(buckets=0x10000)),
        dict(light_map=dict(buckets=0x800000000000)), dict(light_map=dict(buckets=0xffff)), dict(light_map=dict(cap=1048576)),
        dict(light_map=dict(cap=1)), dict(light_map=dict(cap=2)), dict(light_map=dict(empty=301)), dict(tail=32), dict(head=31, tail=31),
        dict(head=31, tail=31, fallback=[4, 101, 201, 401, 0]), dict(lfanew=0x1000), dict(mz=b"MX"), dict(mz=b"XZ"), dict(exe_lfanew=0x1000),
        dict(exe_mz=b"MX"), dict(exe_mz=b"XZ"), dict(flags=flags(b14=1)), dict(flags=flags(b10=1, b15=32)), dict(flags=flags(b10=4, b15=1)),
        dict(flags=flags(b15=1)), dict(flags=flags(b6=2)), dict(flags=flags(b10=4, b15=32), controls=bytes([0, 0, 0, 0, 1, 0])),
        dict(flags=flags(b14=4), tail=2, rows={1: [6, 101, 201, 401, 0]}), dict(controls=bytes([2, 0, 0, 0, 0, 0]), rows={0: [10, 101, 201, 401, 0]}),
        dict(controls=bytes([0x20, 0, 0, 0, 0, 0]), tail=2, rows={1: [6, 101, 201, 401, 0]}), dict(controls=bytes([3, 0, 0, 0, 0, 0])),
        dict(controls=bytes([0x21, 0, 0, 0, 0, 0])), dict(controls=bytes([0x20, 0, 0, 0, 0, 1])),
        dict(wield_map=dict(cap=8192, mult=1, fill=4096, buckets=WIELD + 0x100000)),
        dict(wield_map=dict(cap=8192, mult=1, fill=4095, buckets=WIELD + 0x100000)),
        dict(wield_map=dict(cap=4096, mult=1, fill=4095, buckets=WIELD + 0x100000)),
        dict(wield_map=dict(cap=4096, mult=5, fill=1, buckets=WIELD + 0x100000)), dict(units=0x10000),
        dict(units=0x800000000000), dict(units=0xffff), dict(gens=0x10000), dict(gens=0x800000000000), dict(gens=0xffff),
        dict(reg_cap=0x400000), dict(reg_cap=0x400001),
        dict(rig=big_rig, reg_cap=0x400000, objects={401: OBJ_BODY, big_rig: OBJ_RIG, 501: OBJ_WEAPON}),
        dict(rig=far_rig, objects={401: OBJ_BODY, far_rig: OBJ_RIG, 501: OBJ_WEAPON}),
        dict(rig=(200 << 22) | 5, objects={401: OBJ_BODY, (200 << 22) | 5: OBJ_RIG, 501: OBJ_WEAPON}),
        dict(wd=[33, 44, 201, (200 << 22) | 7, 601, 1], objects={401: OBJ_BODY, 801: OBJ_RIG, (200 << 22) | 7: OBJ_WEAPON}),
        dict(wd=[33, 44, 201, far_weapon, 601, 1], objects={401: OBJ_BODY, 801: OBJ_RIG, far_weapon: OBJ_WEAPON}),
        dict(wd=[33, 44, 201, 1501, 601, 1], reg_cap=1501, objects={401: OBJ_BODY, 801: OBJ_RIG, 1501: OBJ_WEAPON}),
        dict(wd=[33, 44, 201, 1501, 601, 1], reg_cap=1502, objects={401: OBJ_BODY, 801: OBJ_RIG, 1501: OBJ_WEAPON}),
        dict(local=[11, 22, 101, 401, 301, 3]), dict(local=[11, 22, 101, 401, 301, 2]), dict(wd=[33, 44, 201, 501, 601, 3]),
        dict(pool_count=8), dict(pool_map=dict(keys={101: 7}), slot=7, pool_count=8), dict(pool_map=dict(keys={101: 7}), slot=7, pool_count=7),
        dict(local=[11, 22, 1, 401, 301, 1], rows={0: [4, 1, 201, 401, 0]}, pool_map=dict(keys={1: 0}), wield_map=dict(keys={1: 0}),
             inv_map=dict(keys={1: 0})),
        dict(local=[11, 22, 0xfffffffe, 401, 301, 1], rows={0: [4, 0xfffffffe, 201, 401, 0]}, pool_map=dict(keys={0xfffffffe: 0}),
             wield_map=dict(keys={0xfffffffe: 0}), inv_map=dict(keys={0xfffffffe: 0})),
    )] + [spread(), edit(spread(), tail=2, rows={1: [6, 101, 201, 401, 0]}), edit(spread(), states={14: 3}, flags=bytes([2] + [0] * 23))]
    r.hook("foreign_camera")
    for one in scenes:
        r.mem(one)
        r.step("snap", 0, 0).step("snap", 1, 1).step("snap", 2, 2).step("context", 0, 0).step("context", 0, 3).step("context", 0, 1)
        r.step("backpack", 0).step("backpack", 1).step("action", 1, 0).step("on_foot", 0, 0).step("camera_config").step("camera_only", 0)
        r.step("keep_aim_view", 1).step("detach_seated_camera").step("prepare_body_pose", 0, 63).step("renderer", 1, 0, 1, 1, 0)
    # a whole session where nothing sits at index zero
    r.mem(spread())
    lease = r.enter_pose()
    r.step("pose_status", lease).step("fov_config", lease)
    r.mem(controls=bytes([0, 0, 0, 0, 1, 0])).step("snap", 1, 3).step("keep_aim_view", 3).mem(controls=bytes([1, 0, 0, 0, 0, 0])).mem(controls=bytes(6))
    r.ans("exit", patch=r.effect(exited(r.cur))).step("release", lease)
    seated = edit(spread(), flags=bytes([0] * 14 + [4] + [0] * 9), camera_parent=0xc10000000)
    r.mem(seated).step("detach_seated_camera")

    # ---- every code pin, in every scope ----
    r = run("pins")
    names = [name for name, _, _ in HF["checks"]]
    for name in names:
        for change in ("missing", 1):
            r.mem(edit(base, pins={name: change}))
            r.step("module", 0).step("module", 1).step("module", 2).step("camera_ready").step("snap", 1, 0)
    for change in ("missing", 1):
        r.mem(edit(base, entry_pin=change)).step("module", 0).step("module", 1).step("module", 2)
    r.mem(edit(base, pins={"evaluator": "missing", "root_position_call": "missing", "root_rotation_call": "missing",
                           "pose_branch_position_call": "missing"}))
    r.step("module", 0).step("module", 1).step("module", 2).step("snap", 0, 0)
    lease = r.lease()
    r.ans("enter", patch=r.effect(entered(r.cur))).step("warm_start", 0, lease)
    r.ans("exit", patch=r.effect(exited(r.cur))).step("release", lease)

    # ---- what a frame may skip, from a clean start ----
    r = run("frame_caches")
    r.step("begin_frame").step("snap", 0, 0).step("executable", 0).step("executable", 1).step("executable", 2)
    r.mem(exe_stamp_delta=1).step("snap", 0, 1).step("context", 0, 0).step("camera_config").step("executable", 0).step("executable", 1)
    r.step("executable", 2).step("fov_config", -1).step("renderer", 1, 0, 1, 1, 0)
    r.mem(stamp_delta=1).step("module", 0).step("module", 1).step("module", 2).step("snap", 0, 1).step("camera_ready")
    r.step("invalidate").step("module", 1).step("executable", 0).step("end_frame")
    r.mem(base).step("begin_frame").step("module", 1).mem(pins={"exit": 1}).step("module", 1).step("module", 0).step("module", 2)
    r.step("module", 0).mem(pins={"exit": 0}).step("module", 0).step("end_frame").step("module", 0)
    r.step("begin_frame").step("executable", 0).mem(exe_pins={"exists": 1}).step("executable", 0).step("executable", 1).step("executable", 1)
    r.mem(exe_pins={"exists": 0}).step("executable", 1).step("executable", 2).step("end_frame")
    # a frame that begins after the module moved keeps nothing of the old one
    r.step("begin_frame").step("module", 0).step("executable", 0).step("end_frame").mem(game=BASE2, exe=EXE2)
    r.step("begin_frame").step("module", 0).step("executable", 0).step("module", 0).step("end_frame")

    # ---- entry, more ----
    r = run("enter_more")
    r.step("snap", 0, 0)
    token = r.token()
    r.step("prepare_pose", 0, token)
    r.ans("enter", patch=r.effect(entered(r.cur)))
    lease = r.lease()
    r.step("enter_pose", 0, token, 30500, 1, lease)
    r.ans("P.stats", 1, nums=(1, 0, float("inf"))).step("pose_status", lease)
    r.ans("exit", patch=r.effect(exited(r.cur))).step("release", lease)
    lease = r.enter_pose()
    r.ans("P.stats", 1, nums=(1, 0, float("-inf"))).step("pose_status", lease).mem(exited(r.cur)).step("release", lease)
    # the module moves between the snapshot and the engine call
    r.mem(base).hook("foreign_camera").step("snap", 0, 0)
    token = r.token()
    r.step("prepare_pose", 0, token)
    r.ans("foreign_camera", 0, patch=r.effect(game=BASE2))
    lease = r.lease()
    r.step("enter_pose", 0, token, 0, -1, lease).mem(base).step("release", lease).step("snap", 0, 0)
    token = r.token()
    r.step("prepare_pose", 0, token)
    r.ans("foreign_camera", 0).ans("foreign_camera", 0, patch=r.effect(game=BASE2))
    lease = r.lease()
    r.step("enter_pose", 0, token, 0, -1, lease).mem(base).step("release", lease).hook("foreign_camera", 0)
    # an unrelated flag changes while the helper arms: the entry goes on, with the newer snapshot as its reference
    r.mem(base).step("snap", 0, 0)
    token = r.token()
    r.step("prepare_pose", 0, token)
    r.ans("P.begin", 1, patch=r.effect(flags=flags(b15=2)))
    r.ans("enter", patch=r.effect(entered(r.cur)))
    lease = r.lease()
    r.step("enter_pose", 0, token, 0, -1, lease).mem(exited(r.cur)).step("release", lease)
    # a phase packet of the same player with another weapon is not this lease's
    r.mem(wd=[33, 45, 201, 501, 601, 1]).step("snap", 0, 2).mem(base)
    lease = r.enter_pose()
    r.step("begin_frame").step("set_pose_transition", lease, *phase(identity=2, player=0)).step("set_pose_transition", lease, *phase(identity=0, player=2))
    r.step("set_pose_transition", lease, *phase(identity=0, player=0)).step("end_frame").mem(exited(r.cur)).step("release", lease)
    # the game's own aim on our request: released to the game unless the entry itself caused it
    for controls, bits, entry_controls in ((bytes([0, 0, 0, 0, 1, 0]), {}, None), (bytes(6), dict(b6=1), None),
                                           (bytes([0, 0, 0, 0, 1, 0]), {}, bytes([0, 0, 0, 0, 1, 0])), (bytes(6), dict(b6=1), "flags")):
        r.mem(base).step("snap", 0, 0)
        token = r.token()
        r.step("prepare_pose", 0, token)
        if entry_controls == "flags":
            r.ans("enter", patch=r.effect(entered(r.cur, flags=flags(b6=1))))
        else:
            r.ans("enter", patch=r.effect(entered(r.cur, controls=entry_controls)))
        lease = r.lease()
        r.step("enter_pose", 0, token, 0, -1, lease)
        r.mem(controls=controls, flags=flags(**bits))
        r.ans("exit", patch=r.effect(exited(r.cur))).step("release", lease).clear("exit").mem(base).step("release", lease)
    # the entry pin is not asked of an ordinary exit
    r.mem(base)
    lease = r.enter_pose()
    r.mem(entry_pin=1)
    r.ans("exit", patch=r.effect(exited(r.cur))).step("release", lease)

    # ---- warm entry, more ----
    r = run("warm_more")
    for case in ("snapshot_fails", "aim_only", "controller_only", "aim_entry", "controller_entry"):
        r.mem(base).step("snap", 0, 0)
        lease = r.lease()
        if case == "aim_entry":
            r.ans("enter", patch=r.effect(entered(r.cur, flags=flags(b6=1))))
        elif case == "controller_entry":
            r.ans("enter", patch=r.effect(entered(r.cur, controls=bytes([0, 0, 0, 0, 1, 0]))))
        else:
            r.ans("enter", patch=r.effect(entered(r.cur)))
        r.step("warm_start", 0, lease)
        if case == "snapshot_fails":
            r.mem(weapon=0xffffffff)
        elif case == "aim_only":
            r.mem(flags=flags(b6=1))
        elif case == "controller_only":
            r.mem(controls=bytes([0, 0, 0, 0, 1, 0]))
        r.ans("exit", patch=r.effect(exited(r.cur))).step("release", lease).step("release", lease).clear("exit")
        r.mem(base).step("release", lease)

    # ---- the presentation switch, more ----
    r = run("presentation_more")
    r.step("snap", 0, 0).hook("foreign_camera")
    for code, change in ((99, None), (1, None), (-1, None), (0, dict(game=BASE2)), (0, dict(exe_stamp_delta=1)), (0, dict(mode=2)),
                         (0, dict(omit={"presentation_pin8"}))):
        r.mem(base).ans("foreign_camera", 0)
        r.ans("foreign_camera", code, "hook raised", patch=r.effect(**change) if change else -1)
        r.step("presentation", 0, 1).clear("foreign_camera")
    r.hook("foreign_camera", 0)
    for change in (dict(omit={"queue"}), dict(mode=1, flags=bytes([0] * 6 + [1] + [0] * 17)), dict(mode=1, local=[11, 23, 101, 401, 301, 1])):
        r.mem(base).ans("presentation", patch=r.effect(**change)).step("presentation", 0, 1)

    # ---- the body event, more: what changes between its own checks ----
    broken = (dict(game=BASE2), dict(exe=0), dict(exe_mz=b"ZM"), dict(exe_lfanew=0x2000), dict(exe_machine=b"\0\0"), dict(exe_stamp_delta=1),
              dict(exe_pins={"exists": 1}), dict(exe_pins={"trigger": 1}), dict(exe_pins={"exists": "missing"}),
              dict(exe_pins={"trigger": "missing"}), dict(omit={"exe_image"}), dict(omit={"api_root"}), dict(omit={"uapi"}),
              dict(exists_slot=16), dict(trigger_slot=16), dict(omit={"exists_slot"}), dict(omit={"trigger_slot"}), dict(uapi=UAPI + 0x1000),
              dict(controls=bytes([2, 0, 0, 0, 0, 0])), dict(omit={"queue"}), dict(pins={"evaluator": "missing"}), dict(exe=EXE2))
    for at in (1, 2, 3):  # the scene changes when the n-th snapshot of the preparation has been taken
        r = run(f"body_prepare_changes{at}")
        r.step("snap", 0, 0).hook("foreign_camera")
        for change in broken:
            r.mem(base)
            for _ in range(at - 1):
                r.ans("foreign_camera", 0)
            r.ans("foreign_camera", 0, patch=r.effect(**change))
            r.step("prepare_body_pose", 0, r.token()).clear("foreign_camera")
    for at in (1, 2, 3):
        r = run(f"body_release_changes{at}")
        r.step("snap", 0, 0)
        for change in broken:
            r.hook("foreign_camera", 0).mem(base).step("snap", 0, 0)
            body = r.token()
            r.step("prepare_body_pose", 0, body)
            lease = r.lease()
            r.ans("enter", patch=r.effect(entered(r.cur))).step("enter", 0, -1, lease)
            inside = r.cur
            r.hook("foreign_camera")
            for _ in range(at - 1):
                r.ans("foreign_camera", 0)
            r.ans("foreign_camera", 0, patch=r.effect(**change))
            r.step("release_body_pose", lease, body).clear("foreign_camera").step("release_body_pose", lease, body)
            r.mem(exited(inside)).step("release", lease)
    # between the two looks at the Unit API inside one dispatch
    for record in ("uapi", "exists_slot", "trigger_slot", "api_root", "exe_image", "exe_pin_exists", "image"):
        r = run("body_traps_" + record)
        for count in (1, 2, 3, 4, 5, 6, 7, 8):
            for change in (dict(uapi=UAPI + 0x2000), dict(omit={record})):
                r.mem(base).step("snap", 0, 0)
                body = r.token()
                r.step("prepare_body_pose", 0, body)
                lease = r.lease()
                r.ans("enter", patch=r.effect(entered(r.cur))).step("enter", 0, -1, lease)
                inside = r.cur
                r.trap(record, count, **change).step("release_body_pose", lease, body).untrap()
                r.mem(exited(inside)).step("release", lease)
    r = run("body_prepare_traps")
    for record in ("uapi", "exists_slot", "exe_image", "image"):
        for count in (1, 2, 3, 4, 5, 6):
            for change in (dict(uapi=UAPI + 0x2000), dict(omit={record})):
                r.mem(base).step("snap", 0, 0).trap(record, count, **change).step("prepare_body_pose", 0, r.token()).untrap()
    # the body event's own aim change does not hand the request to the game
    r = run("body_event_aim")
    r.step("snap", 0, 0)
    for bits, controls in ((dict(b6=1), bytes(6)), ({}, bytes([0, 0, 0, 0, 1, 0]))):
        r.mem(base)
        lease = r.lease()
        r.ans("enter", patch=r.effect(entered(r.cur))).step("enter", 0, -1, lease).step("begin_frame")
        r.ans("body_trigger", patch=r.effect(flags=flags(**bits), controls=controls)).step("release_body_pose", lease, -1).step("end_frame")
        r.ans("exit", patch=r.effect(exited(r.cur))).step("release", lease).clear("exit").mem(base).step("release", lease)

    # ---- the renderer table, more ----
    r = run("renderer_more")
    many = [(4, i % 2, 0x10000000 + i) for i in range(4094)] + [(4, 1, 0x06f1d0d3), (4, 0, 0x72e8f18c)]
    many.sort(key=lambda row: row[2])
    for change in (dict(render_rows=many), dict(render_rows=many + [(4, 0, 0xf0000000)]), dict(render_rows_at=0x10000),
                   dict(render_rows_at=0x800000000000 - 5 * 24), dict(render_rows_at=0x800000000000 - 5 * 24 - 8),
                   dict(render_rows_at=0x800000000000 - 5 * 24 - 4), dict(render_rows_at=0x800000000000),
                   dict(render_rows_at=0xffff), dict(render_rows=[(4, 1, 0x06f1d0d3)]), dict(render_rows=[(4, 1, 0x72e8f18c)]),
                   dict(render_rows=[(4, 0, 0x06f1d0d2), (4, 1, 0x06f1d0d3), (4, 0, 0x06f1d0d4), (4, 0, 0x72e8f18b), (4, 1, 0x72e8f18c),
                                     (4, 0, 0x72e8f18d)])):
        r.mem(edit(base, **change))
        r.step("renderer", 1, 0, 1, 1, 0).step("renderer", 1, 0, 1, 1, 1).step("renderer", 1, 0, 1, 1, 0).step("renderer", 1, 0, 1, 1, 1)
    r.mem(base).step("renderer", 1, 0, 1, 1, 0)
    for record in ("render_table_global", "render_table", "render_row1", "render_row3", "render_row0", "exe_image"):
        for count in (1, 2, 3):
            for change in (dict(render_table=RTAB + 0x2000), dict(omit={record}), dict(render_count=3),
                           dict(render_rows=[(4, 0, 0x01000000), (4, 0, 0x06f1d0d3), (4, 0, 0x30000000), (4, 1, 0x72e8f18c), (4, 1, 0x90000000)])):
                for setting in (0, 1):
                    r.mem(base).step("renderer", 2, 0, 2, 1, setting).trap(record, count, **change)
                    r.step("renderer", 2, 0, 2, 1, setting).untrap().step("renderer", 2, 0, 2, 1, setting)
                    r.mem(base).trap(record, count, **change).step("renderer", 3, 0, 3, 1, setting).untrap()

    # ---- a record that cannot be read the second (third) time it is needed ----
    bridge = edit(base, states={14: 3}, flags=bytes([2] + [0] * 23), scene_id=3)
    inside = entered(base)
    for number, (records, counts, start, steps) in enumerate((
            (PLAYER_RECORDS + WEAPON_RECORDS + EXE_RECORDS + ANIM_RECORDS + ["image"], (1, 2, 3), base, (("snap", 0, 2),)),
            (PLAYER_RECORDS + WEAPON_RECORDS + ["image", "row1"], (1, 2), inside, (("snap", 0, 2), ("snap", 1, 2))),
            (["state_global", "scene", "flags"], (1, 2, 3, 4), bridge, (("on_foot", 1, 1),)),
            (PLAYER_RECORDS + ["camera_unit", "image"], (1, 2), base, (("snap", 2, 2), ("snap", 1, 2))),
            (PLAYER_RECORDS + EXE_RECORDS + ANIM_RECORDS, (1, 2, 3, 4), base, (("action", 1, 0), ("action", -1, 0))),
            (PLAYER_RECORDS + EXE_RECORDS + ANIM_RECORDS + ["state_global", "scene"], (1, 2, 3), bridge, (("camera_config",), ("on_foot", 1, 1))),
            (EXE_RECORDS + ["weapon_entry", "weapon_resource"], (1, 2, 3), base, (("context", 0, 3), ("context", 0, 0), ("camera_only", 0))),
            (OTHER_RECORDS + ["wd", "entity_map", "entity_map_rows"], (1, 2), base, (("backpack", 0),)),
            (PLAYER_RECORDS + OTHER_RECORDS + ["camera_graph", "camera_parent", "camera_world", "detach"], (1, 2),
             edit(base, flags=bytes([0] * 14 + [4] + [0] * 9), camera_parent=0xc10000000), (("detach_seated_camera",),)))):
        r = run(f"second_reads{number}", start=start)
        r.step("snap", 0, 0).step("snap", 1, 1).step("action", 1, 1)
        for record in records:
            for count in counts:
                for how in ("gone", "changed"):
                    r.mem(start)
                    if how == "gone":
                        r.trap(record, count, omit={record})
                    else:
                        r.trap(record, count, local=[12, 22, 101, 401, 301, 1], wd=[34, 44, 201, 501, 601, 1], graph_tag=7, scene_id=4,
                               controls=bytes([0, 0, 1, 0, 0, 0]), pack=0)
                    for one in steps:
                        r.step(*one)
                    r.untrap()
    # the same while a lease is being entered, polled and released
    for record in PLAYER_RECORDS + WEAPON_RECORDS + EXE_RECORDS + ["weapon_resource", "image", "pin_exit"]:
        r = run("lease_reads_" + record)
        for count in (1, 3, 6, 10, 15, 21, 28):
            r.mem(base).step("snap", 0, 0)
            token = r.token()
            r.step("prepare_pose", 0, token)
            lease = r.lease()
            r.trap(record, count, omit={record})
            r.ans("enter", patch=r.effect(entered(r.cur))).step("enter_pose", 0, token, 0, -1, lease).untrap()
            r.clear("enter").mem(entered(base)).trap(record, count, omit={record}).step("pose_status", lease).untrap()
            r.mem(entered(base)).trap(record, count, omit={record})
            r.ans("exit", patch=r.effect(exited(r.cur))).step("release", lease).untrap().clear("exit")
            r.mem(base).step("release", lease)
    # the local player is another avatar now, while the old avatar's request is still on top
    r = run("release_respawned")
    lease = r.enter_pose()
    inside = r.cur
    r.mem(local=[11, 22, 102, 401, 301, 1], pool_map=dict(keys={102: 0}), wield_map=dict(keys={102: 0}), inv_map=dict(keys={102: 0}))
    for _ in range(5):
        r.step("release", lease)
    r.mem(exited(inside)).step("release", lease)
    # the per-layer state map is rebuilt when the graph's header changes, also for the first layer
    r = run("layer_maps")
    r.step("snap", 1, 0).step("action", 0, 0)
    r.mem(graph_tag=1, entry_edit={1: {5: 0x9000}, 3: {6: 0x9000}, 14: {36: 0x9000}}).step("action", 0, 1)
    r.mem(graph_tag=1, entry_edit={1: {}, 3: {}, 14: {}}).step("action", 0, 1).mem(graph_tag=2).step("action", 0, 1)
    r.set("support_grip_tracking", 1).step("action", 0, 2)
    r.mem(graph_tag=3, entry_edit={1: {5: 0x9000}, 3: {6: 0x9000}, 10: {7: 0x9000}, 11: {8: 0x9000}, 14: {36: 0x9000}}).step("action", 0, 2)
    r.mem(entry_edit={1: {}, 3: {}, 10: {}, 11: {}, 14: {}}).step("action", 0, 2).mem(graph_tag=4).step("action", 0, 2)
    r.mem(states={10: 0, 11: 99}).step("action", 0, 2).step("on_foot", -1, 2)
    r.mem(states={14: 3}).step("action", 0, 3).step("on_foot", -1, 3).step("on_foot", 0, 3)
    return runs


def random_runs(count):
    rng = random.Random(20261116)
    runs = []
    regions = [(0x1000, 0x1000000, 0x20, 1, 1)] * 12 + [(0x2000, 0x1000000, 0x20, 1, 1), (0x1000, 0x40000, 0x20, 1, 1),
                                                        (0x1000, 0x1000000, 0x04, 1, 1), (0x1000, 0x1000000, 0x40, 0, 0)]
    ports = ["allowed", "enter", "exit", "detach", "body_exists", "body_trigger", "P.prepare", "P.begin", "P.owned_handle", "P.finish",
             "P.stats", "P.set_placement", "P.set_zoom_policy", "P.set_ads_placement", "P.transition", "P.set_motion", "A.prepare",
             "A.begin", "A.finish", "A.stats", "foreign_camera", "camera_before_entry", "placement_resolver", "presentation_transition"]

    def mutation(s):
        pick = rng.randrange(34)
        flags = bytearray(s["flags"])
        if pick == 0:
            return dict(controls=bytes(rng.choice((0, 0, 0, 2, 0x20, 64, 8)) if i == 0 else 0 for i in range(6)))
        if pick == 1:
            return dict(controls=bytes([0, 0, 0, 0, rng.choice((0, 1)), rng.choice((0, 0, 0, 1))]))
        if pick == 2:
            flags[6] ^= 1
            return dict(flags=bytes(flags))
        if pick == 3:
            flags[14] ^= 4
            return dict(flags=bytes(flags))
        if pick == 4:
            flags[rng.randrange(24)] = rng.choice((0, 2, 4, 32, 36, 255))
            return dict(flags=bytes(flags))
        if pick == 5:
            return dict(flags=bytes(24), controls=bytes(6))
        if pick == 6:
            return dict(weapon=rng.choice((201, 201, 202, 0, 0xffffffff)))
        if pick == 7:
            return dict(rig=rng.choice((801, 801, 0, 802)))
        if pick == 8:
            return dict(states={14: rng.choice((36, 36, 3, 4, "null", "unknown")), 1: rng.choice((5, 78, 79)), 3: rng.choice((6, 71))})
        if pick == 9:
            return dict(scene_id=rng.choice((3, 4, 5)))
        if pick == 10:
            return dict(omit={rng.choice(("queue", "controls", "flags", "wield_row", "wd", "weapon_entry", "unit801", "unit501", "gen501",
                                          "weapon_resource", "body_instance", "graph", "active14", "exe_image", "scene", "camera_unit",
                                          "pin_evaluator", "local", "row0", "registry", "uapi", "inv_row"))})
        if pick == 11:
            return dict(omit=set())
        if pick == 12:
            return dict(pins={rng.choice(("evaluator", "exit", "allowed", "root_position_call")): rng.choice((0, 0, 1, "missing"))})
        if pick == 13:
            return dict(stamp_delta=rng.choice((0, 0, 0, 1)), exe_stamp_delta=rng.choice((0, 0, 0, 1)))
        if pick == 14:
            return dict(game=rng.choice((BASE, BASE, BASE, BASE2, 0)))
        if pick == 15:
            return dict(exe=rng.choice((EXE, EXE, EXE, EXE2, 0)))
        if pick == 16:
            return "enter"
        if pick == 17:
            return "exit"
        if pick == 18:
            return dict(wd=[33, rng.choice((44, 45)), 201, 501, 601, rng.choice((1, 1, 1, 0))])
        if pick == 19:
            return dict(weapon_resource=rng.choice((bytes.fromhex("1122334455667788"), HAMMER, bytes(8))))
        if pick == 20:
            return dict(camera_parent=rng.choice((0, 0xc10000000)))
        if pick == 21:
            return dict(local=[11, rng.choice((22, 22, 23)), 101, 401, 301, 1])
        if pick == 22:
            return dict(pack=rng.choice((901, 0, 902)))
        if pick == 23:
            return dict(generations={rng.choice((401, 801, 501)): rng.choice((0, 0, 1))})
        if pick == 24:
            return dict(rows={rng.randrange(3): [rng.choice((4, 6, 10)), rng.choice((101, 101, 102)), rng.choice((201, 201, 202)), 401, 0]})
        if pick == 25:
            return dict(head=rng.choice((0, 0, 1)), tail=rng.choice((0, 1, 1, 2, 3)))
        if pick == 26:
            return dict(body_resource=rng.choice((EXPECTED_RESOURCE, EXPECTED_RESOURCE, HAMMER)), layer_count=rng.choice((31, 31, 31, 30, 13)))
        if pick == 27:
            return dict(mode=rng.choice((0, 1, 2)))
        if pick == 28:
            return dict(uapi=rng.choice((UAPI, UAPI, UAPI + 0x1000)))
        if pick == 29:
            return dict(render_rows=[(4, rng.choice((0, 1)), 0x06f1d0d3), (rng.choice((4, 4, 5)), rng.choice((0, 1, 2)), 0x72e8f18c)])
        if pick == 30:
            return dict(entry_pin=rng.choice((0, 0, 1, "missing")))
        if pick == 31:
            return dict(state_counts={14: rng.choice((100, 101, 37, 36))})
        if pick == 32:
            return dict(camera=rng.choice((CTX + 0x3000, CTX + 0x3000, CTX + 0x4000)))
        return dict(context=rng.choice((CTX, CTX, CTX + 0x100000)))

    def scene_after(s):
        change = mutation(s)
        if change == "enter":
            return entered(s, kind=rng.choice((6, 6, 6, 4)))
        if change == "exit":
            return exited(s) if s["tail"] != s["head"] else s
        if "omit" in change and change["omit"]:
            change = dict(omit=set(s["omit"]) | change["omit"])
        return edit(s, **change)

    def some_phase():
        return phase(epoch=rng.choice((1, 2, 3, 0, 1.5)), progress=rng.choice((0, 0.25, 1, 1.5)), ads=rng.choice((0, 1, 1, -1)),
                     identity=rng.choice((0, 0, 0, 1, 2, -1, -2)), player=rng.choice((0, 0, 0, 1, -1)),
                     started=rng.choice((NAN, 1.0)), continuous=rng.choice((-1, 0, 1)), reason=rng.randrange(4))

    def some_place():
        return PLACE(rng.choice((0, 0.1, 0.5, 0.6)), rng.choice((0, -0.2)), rng.choice((0, -0.12, NAN)), mask=rng.choice((7, 7, 7, 15, 63, 3, -1)),
                     yaw=rng.choice((0, 30, 91)), pitch=rng.choice((0, -10)), roll=rng.choice((0, 5)))

    for n in range(count):
        r = Run(f"random{n}", wine=rng.randrange(2), pose=rng.choice((1, 1, 1, 0)), alignment=rng.choice((1, 1, 0)))
        r.strict = False  # a random walk has no intent to protect: scene changes stay small differences
        r.step("snap", 0, 0)
        for hook in ("foreign_camera", "camera_before_entry", "placement_resolver", "presentation_transition"):
            if rng.random() < 0.35:
                r.hook(hook)
        base = r.cur
        for _ in range(rng.randint(25, 60)):
            roll = rng.random()
            macro = rng.random()
            if macro < 0.07 and r.leases < 55 and r.tokens < 55:  # a whole entry attempt, so that leases exist to act on
                if rng.random() < 0.5:
                    r.mem(base)
                r.step("snap", 0, 0)
                made = r.token()
                r.step("prepare_pose", 0, made)
                if rng.random() < 0.85:
                    r.ans("enter", patch=r.effect(entered(r.cur)))
                r.step("enter_pose", 0, made, rng.choice((0, 0, 0, 5000)), rng.choice((-1, 0, 1)), r.lease())
                continue
            if macro < 0.12 and r.leases:
                if rng.random() < 0.7:
                    r.ans("exit", patch=r.effect(exited(r.cur)))
                r.step("release", r.leases - 1 if rng.random() < 0.7 else rng.randrange(r.leases))
                continue
            if r.leases > 58 or r.tokens > 58:
                break
            if macro < 0.15:  # something changes, or goes away, in the middle of a later method
                record = rng.choice(PLAYER_RECORDS + WEAPON_RECORDS + EXE_RECORDS + ANIM_RECORDS + OTHER_RECORDS)
                if rng.random() < 0.5:
                    r.trap(record, rng.randint(1, 6), omit=set(r.cur["omit"]) | {record})
                else:
                    r.trap(record, rng.randint(1, 6), scene_after(r.cur))
                continue
            if macro < 0.16:
                r.untrap()
                continue
            lease = rng.randrange(max(1, r.leases)) if rng.random() < 0.9 else -1
            snap = rng.choice((0, 0, 0, 1, 2, 3, -1))
            token = rng.randrange(max(1, r.tokens)) if rng.random() < 0.9 else -1
            if roll < 0.13:
                r.mem(scene_after(r.cur))
            elif roll < 0.17:
                r.mem(base)
            elif roll < 0.27:
                port = rng.choice(ports)
                code = rng.choice((0, 0, 1, 1, -1, 99, 2, 3))
                patch = r.effect(scene_after(r.cur)) if rng.random() < 0.5 else -1
                nums = ()
                if port.endswith(".stats") or port.endswith(".finish"):
                    code = rng.choice((1, 1, 1, 0, 99, 4 if port.endswith("finish") else -1))
                    nums = (rng.choice((0, 1, 1, 2, 3)), rng.choice((0, 0, 1)), rng.choice((0, 0, 0, 14, 3, NAN)))
                elif port == "presentation_transition":
                    nums = some_phase()
                elif port == "placement_resolver":
                    nums = PLACE(rng.choice((0.1, 0.2)), 0, -0.1, mask=rng.choice((7, 63)), yaw=3)
                elif port in ("enter", "exit"):
                    code = rng.choice((1, 1, 1, 99))
                    patch = r.effect(entered(r.cur) if port == "enter" else exited(r.cur)) if rng.random() < 0.8 else patch
                r.ans(port, code, rng.choice((None, "scripted refusal")), patch=patch, nums=nums, sticky=1 if rng.random() < 0.15 else 0)
            elif roll < 0.29:
                r.clear(rng.choice(ports))
            elif roll < 0.31:
                r.hook(rng.choice(("foreign_camera", "camera_before_entry", "placement_resolver", "presentation_transition",
                                   "P.owned_handle", "P.transition")), rng.randrange(2))
            elif roll < 0.33:
                r.set("region", *rng.choice(regions))
            elif roll < 0.35:
                r.set(rng.choice(("keep_aim_view", "seated_head", "support_grip_tracking", "camera_profile")), rng.choice((1, 1, 0)))
            elif roll < 0.39:
                r.step(rng.choice(("begin_frame", "end_frame")))
            elif roll < 0.49:
                r.step("snap", rng.choice((0, 0, 1, 2)), rng.randrange(4))
            elif roll < 0.55:
                r.step("prepare_pose" if rng.random() < 0.8 else rng.choice(("prepare_alignment", "prepare_body_pose")), snap, r.token())
            elif roll < 0.63:
                if rng.random() < 0.8:
                    r.step("enter_pose", snap, token, rng.choice((0, 0, 0, 5000, -1)), rng.choice((-1, 0, 1)), r.lease())
                else:
                    r.step("enter", snap, rng.choice((-1, token)), r.lease())
            elif roll < 0.71:
                r.step(rng.choice(("pose_status", "pose_status", "alignment_status")), lease)
            elif roll < 0.80:
                r.step("release", lease)
            elif roll < 0.83:
                r.step("set_pose_transition", lease, *some_phase())
            elif roll < 0.86:
                r.step("set_pose_motion", lease, *motion(dx=rng.choice((0, 0.1, 0.2)), mask=rng.choice((0, 0, 15, 7)),
                                                         q=rng.choice(((0, 0, 0, 1), (0.6, 0, 0, 0.8), (1, 0, 0, 0)))))
            elif roll < 0.89:
                r.step(rng.choice(("set_pose_placement", "set_ads_placement")), lease, *some_place())
            elif roll < 0.90:
                r.step("set_placement_defaults", *some_place())
            elif roll < 0.91:
                r.step("set_pose_zoom_policy", lease, rng.randrange(2))
            elif roll < 0.93:
                r.step(rng.choice(("fov_config", "blur_config")), rng.choice((-1, lease)))
            elif roll < 0.95:
                r.step(rng.choice(("camera_config", "fird_fov_config", "camera_ready", "detach_seated_camera")))
            elif roll < 0.96:
                r.step("keep_aim_view", snap)
            elif roll < 0.97:
                r.step(rng.choice(("backpack", "camera_only")), snap)
            elif roll < 0.98:
                r.step("action", rng.choice((-1, snap)), rng.randrange(2)).step("on_foot", snap, rng.randrange(2))
            elif roll < 0.985:
                r.step("warm_start", snap, r.lease())
            elif roll < 0.99:
                r.step("release_body_pose", lease, token)
            elif roll < 0.9915:
                r.step("presentation", snap, rng.randrange(2))
            elif roll < 0.994:
                r.step("module", rng.randrange(3)).step("executable", rng.randrange(3))
            elif roll < 0.996:
                r.step("peek", rng.randrange(7), rng.randrange(6), snap).step("map", rng.randrange(3), rng.choice((201, 101, 301, 901)), -1, snap)
            elif roll < 0.997:
                r.step("invalidate")
            else:
                r.step("renderer", rng.choice((1, 1, 2, 0)), rng.choice((0, 0, 2)), 1, 1, rng.choice((0, 1, 2)))
        runs.append(r)
    return runs


def lua_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def c_string(text):
    out = []
    for ch in text.encode("utf-8"):
        if ch == 0x22:
            out.append('\\"')
        elif ch == 0x5c:
            out.append("\\\\")
        elif ch == 0x0a:
            out.append('\\n"\n    "')
        elif 0x20 <= ch < 0x7f:
            out.append(chr(ch))
        else:
            out.append("\\%03o" % ch)
    return '"' + "".join(out) + '"'


def number(value):
    if value != value:
        return "NAN"
    if value in (float("inf"), float("-inf")):
        return "INFINITY" if value > 0 else "-INFINITY"
    return repr(float(value))


def lua_number(value):
    if value != value:
        return "0/0"
    if value in (float("inf"), float("-inf")):
        return "1/0" if value > 0 else "-1/0"
    return repr(float(value))


def names(layout):
    game = {0: "base", layout["enter"]: "enter", layout["exit"]: "exit", layout["allowed"]: "allowed", layout["presentation"]: "presentation"}
    for key in ("camera", "controller", "world", "player", "avatar", "wield", "weapons", "inventory", "state", "api_root"):
        game[layout[key]] = key
    exe = {0: "base", layout["exists"][0]: "exists", layout["trigger"][0]: "trigger", layout["registry"]: "registry",
           layout["render_table"]: "render_table", layout["render_get"][0]: "render_get", layout["render_set"][0]: "render_set"}
    return game, exe


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--coverage", type=Path)
    parser.add_argument("--runs", type=int, default=260)
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    for name in ("backend.lua", "action_state.lua", "profile.lua"):
        path = "Goose Gadget Kit/src/first_person/" + name
        if subprocess.run(["git", "-C", str(args.retail), "diff", "--quiet", "HEAD", "--", "src/first_person/" + name]).returncode:
            sys.exit(f"{path} is modified in the retail tree.")
    digests = {name: hashlib.sha256((args.retail / "src/first_person" / name).read_bytes()).hexdigest()
               for name in ("backend.lua", "action_state.lua", "profile.lua")}
    RETAIL["checks"] = checks("retail", args.retail)
    HF["checks"] = checks("here", args.retail)
    runs = hand_made() + more_hand_made() + random_runs(args.runs)

    # the script for the retail side
    retail_game, retail_exe = names(RETAIL)
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
        script.write("return {\n")
        for r in runs:
            memory, bases, _ = build(RETAIL, r.initial)
            rows = ",".join('{%d,"%s"}' % (at, data.hex()) for at, data in sorted(memory.items()))
            # one function per run: a Lua function holds at most 65536 constants
            script.write("(function() return {name=%s,game=%d,exe=%d,stride=%d,flags=%d,renderer={[0]=%d,%d,%d},entity_map=%d,light_map=%d,\n" % (
                lua_string(r.name), bases[0], bases[1], RETAIL["stride"], RETAIL["flags"], RETAIL["render_get"][0], RETAIL["render_set"][0], 0x1234,
                RETAIL["entity_map"], RETAIL["light_map"]))
            script.write(" names={game={%s},exe={%s}},\n" % (
                ",".join('[%d]="%s"' % item for item in retail_game.items()), ",".join('[%d]="%s"' % item for item in retail_exe.items())))
            script.write(" traps={%s},\n" % ",".join("{%d,%d}" % build(RETAIL, before)[2].get(record, (0, 0)) for record, before in r.traps))
            script.write(" init={%s},\n patches={\n" % rows)
            for before, after in r.patches:
                rows = diff(build(RETAIL, before), build(RETAIL, after))
                script.write("  {%s},\n" % ",".join(
                    '{%d,%s,%d}' % (at, "false" if data is None else '"' + data.hex() + '"', size) for at, data, size in rows))
            script.write(" },\n steps={\n")
            for op, nums, s in r.steps:
                script.write('  {"%s",{%s}%s},\n' % (op, ",".join(lua_number(n) for n in nums), "" if s is None else "," + lua_string(s)))
            script.write(" }} end)(),\n")
        script.write("}\n")
    command = ["luajit", str(HERE / "run_backend_ops.lua"), str(args.retail), script.name]
    if args.coverage:
        command.append(str(args.coverage))
    try:
        done = subprocess.run(command, capture_output=True, text=True)
    finally:
        Path(script.name).unlink()
    if done.returncode:
        sys.exit("run_backend_ops.lua failed:\n%s" % done.stderr[-4000:])
    answers, current = [], None
    for line in done.stdout.split("\n"):
        if line.startswith("RUN "):
            current = []
            answers.append([[]])
        elif line == ".":
            answers[-1].append([])
        elif answers:
            answers[-1][-1].append(line)
    for r, a in zip(runs, answers):
        a.pop()
        if len(a) != len(r.steps):
            sys.exit(f"{r.name}: {len(a)} answers for {len(r.steps)} steps")
    if len(answers) != len(runs):
        sys.exit(f"{len(answers)} runs answered of {len(runs)}")

    # the vectors for this build
    out = ["/* Generated by tools/retail_ref/gen_backend_ops.py. Do not edit.",
           f" * Retail files at commit {commit}: backend.lua sha256 {digests['backend.lua']},",
           f" * action_state.lua {digests['action_state.lua']}, profile.lua {digests['profile.lua']}.",
           " * Each run is one script. The retail files answered it on fake memory in retail's layout; the bytes",
           " * below are the same states in build 25480438's layout, and each step's text is what retail answered. */",
           "#include <math.h>", f'#define BACKEND_OPS_VECTOR_COMMIT "{commit}"', f'#define BACKEND_OPS_VECTOR_SHA256 "{digests["backend.lua"]}"',
           "typedef struct OpsRecord { unsigned long long at; unsigned size; const unsigned char *bytes; } OpsRecord;",
           "typedef struct OpsPatch { const OpsRecord *records; unsigned count; } OpsPatch;",
           "typedef struct OpsStep { const char *op; unsigned count; const double *nums; const char *text; const char *expected; } OpsStep;",
           "typedef struct OpsTrap { unsigned long long at; unsigned size; } OpsTrap;",
           "typedef struct OpsRun { const char *name; unsigned long long game, exe; const OpsRecord *init; unsigned init_count;",
           "  const OpsPatch *patches; unsigned patch_count; const OpsTrap *traps; const OpsStep *steps; unsigned step_count; } OpsRun;",
           "typedef struct OpsName { unsigned long long offset; const char *name; } OpsName;"]
    here_game, here_exe = names(HF)
    out.append("static const OpsName ops_game_names[] = {%s};" % ",".join('{%dULL,"%s"}' % item for item in here_game.items()))
    out.append("static const OpsName ops_exe_names[] = {%s};" % ",".join('{%dULL,"%s"}' % item for item in here_exe.items()))
    out.append("#define OPS_STRIDE %dULL\n#define OPS_FLAGS %dULL\n#define OPS_RENDER_GET %dULL\n#define OPS_RENDER_SET %dULL" % (
        HF["stride"], HF["flags"], HF["render_get"][0], HF["render_set"][0]))
    out.append("#define OPS_ENTITY_MAP %dULL\n#define OPS_LIGHT_MAP %dULL" % (HF["entity_map"], HF["light_map"]))
    blobs = {}

    def blob(data):
        if data not in blobs:
            blobs[data] = len(blobs)
            out.append("static const unsigned char ops_b%d[] = {%s};" % (blobs[data], ",".join(map(str, data))))
        return "ops_b%d" % blobs[data]

    def records(rows):
        return ",".join("{%dULL,%d,%s}" % (at, size, "0" if data is None else blob(data)) for at, data, size in rows)

    table, steps_total = [], 0
    for index, (r, a) in enumerate(zip(runs, answers)):
        memory, bases, _ = build(HF, r.initial)
        out.append("static const OpsRecord ops_init%d[] = {%s};" % (index, records((at, data, len(data)) for at, data in sorted(memory.items()))))
        patch_rows = []
        for n, (before, after) in enumerate(r.patches):
            rows = diff(build(HF, before), build(HF, after))
            out.append("static const OpsRecord ops_patch%d_%d[] = {%s};" % (index, n, records(rows) or "{0,0,0}"))
            patch_rows.append("{ops_patch%d_%d,%d}" % (index, n, len(rows)))
        out.append("static const OpsPatch ops_patches%d[] = {%s};" % (index, ",".join(patch_rows) or "{0,0}"))
        out.append("static const OpsTrap ops_traps%d[] = {%s};" % (
            index, ",".join("{%dULL,%d}" % build(HF, before)[2].get(record, (0, 0)) for record, before in r.traps) or "{0,0}"))
        step_rows = []
        for n, ((op, nums, s), lines) in enumerate(zip(r.steps, a)):
            if nums:
                out.append("static const double ops_n%d_%d[] = {%s};" % (index, n, ",".join(number(v) for v in nums)))
            step_rows.append('{"%s",%d,%s,%s,\n    %s}' % (op, len(nums), "ops_n%d_%d" % (index, n) if nums else "0",
                                                           "0" if s is None else c_string(s), c_string("\n".join(lines))))
        out.append("static const OpsStep ops_steps%d[] = {%s};" % (index, ",\n  ".join(step_rows)))
        steps_total += len(r.steps)
        table.append('  {%s,%dULL,%dULL,ops_init%d,%d,ops_patches%d,%d,ops_traps%d,ops_steps%d,%d},' % (
            c_string(r.name), bases[0], bases[1], index, len(memory), index, len(r.patches), index, index, len(r.steps)))
    out += ["static const OpsRun ops_runs[] = {"] + table + ["};"]
    (args.out / "backend_ops_vectors.h").write_text("\n".join(out) + "\n")

    # the test-only places of the open constants
    pins = ", \\\n  ".join('{%s, UINT32_C(%#x), "%s"}' % ("true" if in_exe else "false", rva, hexa)
                          for (in_exe, rva), hexa in zip(HF["presentation_pins"], PRESENTATION_HEX))
    (ROOT / "tests/backend_ops_open.h").write_text(
        "/* Generated by tools/retail_ref/gen_backend_ops.py. Do not edit.\n"
        " * TEST ONLY. Places in the test's fake game image for the constants that are open on build 25480438\n"
        " * (the presentation switch), with retail's code bytes copied there, so that the logic of\n"
        " * `_invoke_presentation` can be compared with retail's. They are NOT this build's values.\n"
        " * tests/run-vectors-backend-ops.sh builds a second object of src/backend_ops.c with\n"
        " * -DTFP_OPS_TEST_OPEN_CONSTANTS='\"backend_ops_open.h\"'; no shipped build does. */\n"
        "#undef B_PRESENTATION_ESTABLISHED\n#undef B_PRESENTATION_FUNCTION\n#undef B_PRESENTATION_MODE\n"
        "#undef B_PRESENTATION_MODE_STRIDE\n#undef B_PRESENTATION_PINS\n"
        "#define B_PRESENTATION_ESTABLISHED 1\n"
        f"#define B_PRESENTATION_FUNCTION UINT32_C({HF['presentation']:#x})\n"
        f"#define B_PRESENTATION_MODE UINT32_C({HF['mode']:#x})\n"
        f"#define B_PRESENTATION_MODE_STRIDE UINT32_C({HF['mode_stride']:#x})\n"
        f"#define B_PRESENTATION_PINS \\\n  {pins}\n")
    calls = sum(1 for r in runs for op, _, _ in r.steps if op not in ("new", "patch", "load", "ans", "hook", "set", "trap", "invalidate"))
    print(f"backend ops: {len(runs)} runs ({len(runs) - args.runs} hand-written, {args.runs} random), {steps_total} steps, {calls} method calls")
    return 0


if __name__ == "__main__":
    sys.exit(main())
