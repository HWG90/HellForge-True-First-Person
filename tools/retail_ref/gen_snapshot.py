#!/usr/bin/env python3
"""Cut snapshot reference vectors: one logical scene, written as fake game
memory in retail's layout (answered by the retail backend.lua under LuaJIT)
and in this build's layout (to be answered the same by src/snapshot.c).

  gen_snapshot.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import argparse
import copy
import hashlib
import random
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
FROZEN = "1d0d453"
RETAIL = dict(stamp=0x6af322c1, size=0x3382000, camera=0x2e287d0, controller=0x2e381a8, world=0x2ce41b8,
              player=0x2ce1618, avatar=0x2ce1ee0, wield=0x2ce1fd0, weapons=0x2ce1ea0, light_map=0xf24990,
              entity_map=0xf1c978, entity_rows=0xf349e0, flags=0x53e888, rig=0x53e9c0, stride=0x1240,
              pin=(0x111a809, "41ff5138"))
HF = dict(stamp=0x6ab3b43f, size=0x4744000, camera=0x346d560, controller=0x347ce70, world=0x346bf98,
          player=0x3326468, avatar=0x3326d20, wield=0x3326420, weapons=0x3326ce0, light_map=0xf22ec8,
          entity_map=0xf1aeb0, entity_rows=0xf32f18, flags=0x53e880, rig=0x53e9b8, stride=0x1238,
          pin=(0x12887c9, "41ff5138"))
BASE, CTX, WORLD, CTRL, PLAYER, POOL, EP, WIELD, WEAPONS = (0x100000000, 0x300000000, 0x400000000, 0x500000000,
    0x600000000, 0x700000000, 0x800000000, 0x900000000, 0xa00000000)
u32 = lambda *values: b"".join(struct.pack("<I", v & 0xffffffff) for v in values)
u64 = lambda value: struct.pack("<Q", value)


def scene():
    """The logical scene: one local player holding one weapon, a normal camera request."""
    return dict(
        mz=b"MZ", lfanew=0x40, pe=b"PE\0\0", machine=b"\x64\x86", magic=b"\x0b\x02", stamp_delta=0, pin=None,
        context=CTX, camera=CTX + 0x3000, head=0, tail=1, rows={0: [4, 101, 201, 401, 0]}, fallback=None,
        world=WORLD, controller=CTRL, controls=bytes(6), player=PLAYER, player_count=1, light=301,
        light_map=dict(cap=8, empty=0xffffffff, mult=3, key=301, index=0, buckets=WORLD + 0x100),
        local=[11, 22, 101, 401, 301, 1], pool=POOL, pool_map=dict(cap=8, empty=0xffffffff, mult=3, key=101, index=0,
                                                                    buckets=POOL + 0x300),
        pool_count=1, ep=EP, ep_bytes=None, flags=bytes(24), rig=801, slot=0,
        wield=WIELD, wield_map=dict(cap=8, empty=0xffffffff, mult=3, key=101, index=0, buckets=WIELD + 0x200),
        wield_owner=None, weapon=201, entity_map=dict(cap=8, empty=0xffffffff, mult=3, key=201, index=1,
                                                      buckets=WORLD + 0x200),
        wd=[33, 44, 201, 501, 601, 1], weapons=WEAPONS,
        weapons_map=dict(cap=8, empty=0xffffffff, mult=3, key=201, index=0, buckets=WEAPONS + 0x200),
        weapon_entry=None, omit=set())


def build(layout, s):
    """Fake memory for the scene in one build's layout, as {address: bytes}."""
    m = {}
    def put(name, at, data):
        if name not in s["omit"]:
            m[at] = bytes(data)
    def table(name, at, t):
        put(name, at, u64(t["buckets"]) + u32(t["cap"], t["empty"], t["mult"]))
        cap = max(1, min(t["cap"], 64))
        rows = bytearray(u32(t["empty"], 0) * cap)
        slot = (t["key"] * t["mult"]) % cap
        rows[slot * 8:slot * 8 + 8] = u32(t["key"], t["index"])
        put(name + "_rows", t["buckets"], rows)
    header = bytearray(0x200)
    header[0:2] = s["mz"]
    header[0x3c:0x40] = u32(s["lfanew"])
    pe = 0x40
    header[pe:pe + 4] = s["pe"]
    header[pe + 4:pe + 6] = s["machine"]
    header[pe + 24:pe + 26] = s["magic"]
    header[pe + 8:pe + 12] = u32(layout["stamp"] + s["stamp_delta"])
    header[pe + 80:pe + 84] = u32(layout["size"])
    put("image", BASE, header)
    if s["pin"] is not None:
        rva, hexa = layout["pin"]
        code = bytearray(bytes.fromhex(hexa))
        code[-1] ^= s["pin"]
        put("pin", BASE + rva, code)
    put("camera_global", BASE + layout["camera"], u64(s["context"]))
    put("camera_object", s["context"], u64(s["camera"]))
    put("queue", s["context"] + 0x1f8, u32(s["head"], s["tail"]))
    for slot, row in s["rows"].items():
        put(f"row{slot}", s["context"] + 0x200 + slot * 0xe8, u32(*row))
    if s["fallback"] is not None:
        put("fallback", s["context"] + 0x1f00, u32(*s["fallback"]))
    put("world_global", BASE + layout["world"], u64(s["world"]))
    put("controller_global", BASE + layout["controller"], u64(s["controller"]))
    put("controls", s["controller"], s["controls"])
    put("player_global", BASE + layout["player"], u64(s["player"]))
    put("player_count", s["player"] + 0x88, u32(s["player_count"]))
    put("light", s["player"] + 0x3a8, u32(s["light"]))
    table("light_map", s["world"] + layout["light_map"], s["light_map"])
    put("local", s["world"] + layout["entity_rows"] + s["light_map"]["index"] * 24, u32(*s["local"]))
    put("pool_global", BASE + layout["avatar"], u64(s["pool"]))
    table("pool_map", s["pool"] + 0xf8, s["pool_map"])
    put("pool_count", s["pool"] + 0x6c, u32(s["pool_count"]))
    put("ep_pointer", s["pool"] + 0x110 + s["pool_map"]["index"] * 8, u64(s["ep"]))
    put("ep", s["ep"], s["ep_bytes"] if s["ep_bytes"] is not None else u32(*s["local"][:5]))
    put("flags", s["pool"] + layout["flags"] + s["slot"] * layout["stride"], s["flags"])
    put("rig", s["pool"] + layout["rig"] + s["slot"] * layout["stride"], u32(s["rig"]))
    put("wield_global", BASE + layout["wield"], u64(s["wield"]))
    table("wield_map", s["wield"] + 0x30, s["wield_map"])
    put("wield_owners", s["wield"] + 0x48, u64(s["wield"] + 0x300))
    put("wield_owner_pointer", s["wield"] + 0x300 + s["wield_map"]["index"] * 8, u64(s["ep"] + 0x100))
    put("wield_owner", s["ep"] + 0x100, s["wield_owner"] if s["wield_owner"] is not None else u32(*s["local"][:5]))
    put("wield_rows", s["wield"] + 0x60, u64(s["wield"] + 0x1000))
    put("wield_row", s["wield"] + 0x1000 + s["wield_map"]["index"] * 0x1d0, u32(s["weapon"]))
    table("entity_map", s["world"] + layout["entity_map"], s["entity_map"])
    put("wd", s["world"] + layout["entity_rows"] + s["entity_map"]["index"] * 24, u32(*s["wd"]))
    put("weapons_global", BASE + layout["weapons"], u64(s["weapons"]))
    table("weapons_map", s["weapons"] + 0x30, s["weapons_map"])
    put("weapons_entries", s["weapons"] + 0x48, u64(s["weapons"] + 0x300))
    put("weapon_entry_pointer", s["weapons"] + 0x300 + s["weapons_map"]["index"] * 8, u64(s["weapons"] + 0x400))
    put("weapon_entry", s["weapons"] + 0x400,
        s["weapon_entry"] if s["weapon_entry"] is not None else u32(*s["wd"][:5]))
    return m


def hand_made():
    out = []
    def add(name, **change):
        s = scene()
        for key, value in change.items():
            if isinstance(value, dict) and isinstance(s.get(key), dict):
                s[key].update(value)
            else:
                s[key] = value
        out.append((name, s))
    add("hip_rifle")
    add("rig_view", tail=2, rows={0: [4, 101, 201, 401, 0], 1: [6, 101, 201, 401, 0]})
    add("rig_view_other_weapon", tail=2, rows={1: [6, 101, 202, 401, 0]})
    add("rig_view_other_body", tail=2, rows={1: [6, 101, 201, 402, 0]})
    add("aiming_ads", controls=bytes([0, 0, 0, 0, 1, 0]), flags=bytes([0] * 6 + [1] + [0] * 17))
    add("shoulder_aim", flags=bytes([0] * 10 + [4, 0, 0, 0, 0, 32] + [0] * 8))
    add("shoulder_bits_under_sights", controls=bytes([0, 0, 0, 0, 1, 0]), flags=bytes([0] * 10 + [4, 0, 0, 0, 0, 32] + [0] * 8))
    add("reloading", flags=bytes([0] * 15 + [2] + [0] * 8))
    add("swap_masked", controls=bytes([2, 0, 0, 0, 0, 0]))
    add("stim", controls=bytes([64, 0, 0, 0, 0, 0]))
    add("vault", controls=bytes([0x20, 0, 0, 0, 0, 0]))
    add("guard", controls=bytes([0, 0, 0, 0, 0, 1]))
    add("swap_with_guard", controls=bytes([2, 0, 0, 0, 0, 1]))
    add("high_mask_byte", controls=bytes([0, 0, 0, 1, 0, 0]))
    add("seated", flags=bytes([0] * 14 + [4] + [0] * 9))
    add("seated_swap", flags=bytes([0] * 14 + [4] + [0] * 9), controls=bytes([2, 0, 0, 0, 0, 0]))
    add("other_camera_kind", rows={0: [10, 101, 201, 401, 0]})
    add("kind_in_high_word", rows={0: [0x10004, 101, 201, 401, 0]})
    add("foreign_request", rows={0: [4, 102, 201, 401, 0]})
    add("empty_queue_fallback", tail=0, fallback=[4, 101, 201, 401, 0])
    add("empty_queue_no_fallback", tail=0)
    add("queue_wrap", head=31, tail=0, rows={31: [6, 101, 201, 401, 0]})
    add("bad_queue_head", head=32)
    add("bad_queue_tail", tail=40)
    add("no_weapon_all_ones", weapon=0xffffffff)
    add("no_weapon_zero", weapon=0)
    add("player_count_zero", player_count=0)
    add("light_absent", light_map=dict(key=302))
    add("light_is_empty_key", light=0xffffffff)
    add("light_zero", light=0)
    add("light_generation_even", local=[11, 22, 101, 401, 301, 0])
    add("light_row_other_entity", local=[11, 22, 101, 401, 305, 1])
    add("avatar_zero", local=[11, 22, 0, 401, 301, 1], rows={0: [4, 0, 201, 401, 0]})
    add("avatar_all_ones", local=[11, 22, 0xffffffff, 401, 301, 1], rows={0: [4, 0xffffffff, 201, 401, 0]})
    add("pool_count_zero", pool_count=0)
    add("pool_count_nine", pool_count=9)
    add("pool_index_out_of_bounds", pool_map=dict(index=8))
    add("avatar_entry_differs", ep_bytes=u32(11, 22, 101, 401, 999))
    add("body_zero", local=[11, 22, 101, 0, 301, 1])
    add("wield_owner_differs", wield_owner=u32(11, 22, 101, 401, 999))
    add("weapon_generation_even", wd=[33, 44, 201, 501, 601, 0])
    add("weapon_row_other_entity", wd=[33, 44, 202, 501, 601, 1])
    add("weapon_entry_differs", weapon_entry=u32(33, 44, 201, 501, 999))
    add("weapon_unit_zero", wd=[33, 44, 201, 0, 601, 1])
    add("map_capacity_zero", light_map=dict(cap=0))
    add("map_capacity_not_power_of_two", light_map=dict(cap=6))
    add("map_capacity_too_large", light_map=dict(cap=2097152))
    add("map_buckets_low", light_map=dict(buckets=0x1000))
    add("map_index_too_large", light_map=dict(index=1048576))
    add("weapons_map_index_4096", weapons_map=dict(index=4096))
    add("context_pointer_low", context=0x10)
    add("camera_pointer_zero", camera=0)
    add("world_pointer_high", world=0x900000000000)
    for name in ("camera_global", "camera_object", "queue", "row0", "world_global", "controller_global", "controls",
                 "player_global", "player_count", "light", "light_map", "light_map_rows", "local", "pool_global",
                 "pool_map", "pool_count", "ep_pointer", "ep", "flags", "rig", "wield_global", "wield_map",
                 "wield_owners", "wield_owner_pointer", "wield_owner", "wield_rows", "wield_row", "entity_map", "wd",
                 "weapons_global", "weapons_map", "weapons_entries", "weapon_entry_pointer", "weapon_entry"):
        add("unreadable_" + name, omit={name})
    # A second local slot: the avatar row stride and the flag and rig offsets must all be this build's.
    for slot in (1, 2, 7):
        add(f"slot{slot}", slot=slot, pool_count=8, pool_map=dict(index=slot),
            flags=bytes([0] * 6 + [1] + [0] * 3 + [4, 0, 0, 0, 4, 34] + [0] * 8), rig=800 + slot)
    add("slot1_missing_flags", slot=1, pool_count=2, pool_map=dict(index=1), omit={"flags"})
    # The game image itself.
    add("not_mz", mz=b"ZM")
    add("lfanew_low", lfanew=0x3f)
    add("lfanew_high", lfanew=0x1001)
    add("not_pe", pe=b"NE\0\0")
    add("not_amd64", machine=b"\x4c\x01")
    add("not_pe32_plus", magic=b"\x0b\x01")
    add("another_build", stamp_delta=1)
    add("image_unreadable", omit={"image"})
    add("pin_matches", pin=0)
    add("pin_differs", pin=1)
    return out


def fuzzed(count):
    rng = random.Random(1020)
    out = []
    for n in range(count):
        s = scene()
        s["controls"] = bytes(rng.choice((0, 0, 0, 2, 0x20, 64, 1, 255)) if i < 4 else rng.choice((0, 0, 1)) for i in range(6))
        s["flags"] = bytes(rng.choice((0, 0, 0, 1, 2, 4, 32, 36, 255)) for _ in range(24))
        kind = rng.choice((4, 4, 6, 6, 10, 0))
        avatar = rng.choice((101, 101, 101, 102))
        s["rows"] = {0: [kind, avatar, rng.choice((201, 201, 202)), rng.choice((401, 401, 402)), 0]}
        if rng.random() < 0.15:
            s["tail"], s["rows"][1] = 2, [rng.choice((4, 6)), 101, 201, 401, 0]
        if rng.random() < 0.1:
            slot = rng.randint(1, 7)
            s["slot"], s["pool_count"], s["pool_map"]["index"] = slot, 8, slot
        s["rig"] = rng.choice((801, 0, 0xffffffff))
        for chance, edit in ((0.04, lambda: s.update(weapon=rng.choice((0, 0xffffffff, 202)))),
                             (0.04, lambda: s["local"].__setitem__(rng.randrange(6), rng.choice((0, 1, 101, 301, 999)))),
                             (0.04, lambda: s["wd"].__setitem__(rng.randrange(6), rng.choice((0, 1, 201, 999)))),
                             (0.03, lambda: s.update(pool_count=rng.choice((0, 1, 8, 9)))),
                             (0.03, lambda: s["light_map"].update(cap=rng.choice((0, 3, 8, 16)))),
                             (0.03, lambda: s.update(light=rng.choice((0, 301, 302, 0xffffffff)))),
                             (0.03, lambda: s.update(ep_bytes=u32(11, 22, 101, rng.choice((0, 401, 0xffffffff)), 301))),
                             (0.02, lambda: s.update(head=rng.choice((0, 5, 31, 32)), tail=rng.choice((0, 1, 31, 32)))),
                             (0.04, lambda: s["omit"].add(rng.choice(("flags", "rig", "wield_row", "wd", "weapon_entry", "ep",
                                                                       "controls", "local", "row0", "light_map_rows"))))):
            if rng.random() < chance:
                edit()
        out.append((f"fuzz{n}", s))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    source = args.retail / "src/first_person/backend.lua"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    scenes = hand_made() + fuzzed(600)
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
        script.write("return {\n")
        for name, s in scenes:
            memory = build(RETAIL, s)
            rows = ",".join('{%d,"%s"}' % (at, data.hex()) for at, data in sorted(memory.items()))
            script.write("{base=%d,context=%d,memory={%s}},\n" % (BASE, s["context"], rows))
        script.write("}\n")
    done = subprocess.run(["luajit", str(HERE / "run_snapshot.lua"), str(args.retail), script.name], capture_output=True, text=True)
    Path(script.name).unlink()
    if done.returncode:
        sys.exit("run_snapshot.lua failed:\n" + done.stderr)
    lines = done.stdout.splitlines()
    if len(lines) != len(scenes):
        sys.exit(f"{len(lines)} results for {len(scenes)} scenes")
    out = [f"/* Generated by tools/retail_ref/gen_snapshot.py. Do not edit.\n * Retail file: src/first_person/backend.lua at commit {commit}, "
           f"sha256 {digest}.\n * Each scene is one logical game state. The retail backend answered it on fake memory in retail's layout;\n"
           f" * the bytes below are the same state in build 25480438's layout. */",
           f'#define SNAPSHOT_VECTOR_COMMIT "{commit}"', f'#define SNAPSHOT_VECTOR_SHA256 "{digest}"',
           "typedef struct SnapshotRecord { unsigned long long at; unsigned size; const unsigned char *bytes; } SnapshotRecord;",
           "typedef struct SnapshotAnswer { int ok; const char *error; unsigned kind, head, tail; long long record_offset;",
           "  unsigned request_avatar, request_weapon, avatar, avatar_index, body, viewmodel;",
           "  int aiming, shoulder_aim, seated, reloading, controller_active, blocked, safe, weapon_transition, head_camera_transition;",
           "  unsigned weapon, weapon_unit; } SnapshotAnswer;",
           "typedef struct SnapshotVector { const char *name; unsigned long long base, context; const SnapshotRecord *records; unsigned count;",
           "  SnapshotAnswer full, player; } SnapshotVector;"]
    def answer(text):
        if text.startswith("E "):
            return '{0,"%s",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}' % text[2:].replace("\\", "\\\\").replace('"', '\\"')
        return "{1,\"\"," + ",".join(text.split(" ")[1:]) + "}"
    table = []
    passed = failed = 0
    for index, ((name, s), line) in enumerate(zip(scenes, lines)):
        memory = build(HF, s)
        rows = []
        for n, (at, data) in enumerate(sorted(memory.items())):
            out.append("static const unsigned char snapshot_%d_%d[] = {%s};" % (index, n, ",".join(map(str, data))))
            rows.append("{%dULL,%d,snapshot_%d_%d}" % (at, len(data), index, n))
        out.append("static const SnapshotRecord snapshot_%d[] = {%s};" % (index, ",".join(rows)))
        full, player = line.split("|")
        passed += full.startswith("S")
        failed += full.startswith("E")
        table.append('  {"%s",%dULL,%dULL,snapshot_%d,%d,%s,%s},' % (name, BASE, s["context"], index, len(rows), answer(full), answer(player)))
    out += ["static const SnapshotVector snapshot_vectors[] = {"] + table + ["};"]
    (args.out / "snapshot_vectors.h").write_text("\n".join(out) + "\n")
    print(f"snapshot: {len(scenes)} scenes ({passed} full snapshots, {failed} refusals)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
