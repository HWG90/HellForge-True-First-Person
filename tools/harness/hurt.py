#!/usr/bin/env python3
"""Test tool: list the game's health records, or set one entity's health, from outside the process.

  hurt.py list                 every record: index, entity, unit, health / maximum, life
  hurt.py set ENTITY HEALTH    write the health (i32) of that entity's record
  hurt.py kill                 the local player dies: health 0 and life 1 (downed) on its own record; the game then
                               runs its death (ELIMINATED, the body falls, a replacement pod). The player is the
                               avatar of the mod's last "Ids:" log line (written once per lease: the view must be on (U))

Layout from HellForge's SDK (hellforge_globals.h, HfHealthManager): game.dll+0x3326688 points to the manager;
+0x1020 count, +0x1048 descriptor pointers, +0x1058 records (0x1B8 each: health i32 at +0x14, life u32 at +0x19C),
+0x1060 ext records (0x1C each: maximum i32 at +0x14); a descriptor has the entity (u32) at +8, the unit at +12 and
flags at +20. For producing a death in a test session of one's own offline game copy; nothing else.
"""
import struct
import sys

import peek

MANAGER = 0x3326688


def table(mem, base):
    def rd(at, n):
        mem.seek(at); return mem.read(n)
    manager = struct.unpack("<Q", rd(base + MANAGER, 8))[0]
    count = struct.unpack("<I", rd(manager + 0x1020, 4))[0]
    descriptors, = struct.unpack("<Q", rd(manager + 0x1048, 8))
    records, = struct.unpack("<Q", rd(manager + 0x1058, 8))
    ext, = struct.unpack("<Q", rd(manager + 0x1060, 8))
    rows = []
    for i in range(min(count, 4096)):
        d, = struct.unpack("<Q", rd(descriptors + i * 8, 8))
        if not d:
            continue
        try:
            entity, unit = struct.unpack("<II", rd(d + 8, 8)); flags, = struct.unpack("<I", rd(d + 20, 4))
            health, = struct.unpack("<i", rd(records + i * 0x1B8 + 0x14, 4)); life, = struct.unpack("<I", rd(records + i * 0x1B8 + 0x19C, 4))
            most, = struct.unpack("<i", rd(ext + i * 0x1C + 0x14, 4))
        except OSError:
            continue
        rows.append((i, entity, unit, health, most, life, flags, records + i * 0x1B8 + 0x14))
    return rows


def main(argv):
    if len(argv) < 2 or argv[1] not in ("list", "set", "kill"):
        print(__doc__); return 2
    pid = peek.game_pid()
    base, _ = peek.regions(pid, "game.dll")
    with open(f"/proc/{pid}/mem", "rb" if argv[1] == "list" else "r+b", buffering=0) as mem:
        rows = table(mem, base)
        if argv[1] == "list":
            for i, entity, unit, health, most, life, flags, _ in rows:
                print(f"{i:4} entity {entity:#010x} unit {unit:#010x} health {health:6} / {most:6} life {life} flags {flags:#x}")
            print(len(rows), "records")
            return 0
        if argv[1] == "kill":
            import re
            import matrix
            ids = re.findall(r"Ids: avatar=(0x[0-9a-f]+)", matrix.log_path().read_text(errors="replace"))
            if not ids:
                print("no Ids line in the mod's log: switch the view on first"); return 1
            entity = int(ids[-1], 16)
            for row in rows:
                if row[1] == entity and row[5] == 0:
                    mem.seek(row[7]); mem.write(struct.pack("<i", 0))
                    mem.seek(row[7] + 0x19C - 0x14); mem.write(struct.pack("<I", 1))
                    print(f"entity {entity:#x}: health {row[3]} -> 0, life 0 -> 1"); return 0
            print(f"entity {entity:#x} has no live record"); return 1
        entity, health = int(argv[2], 0), int(argv[3])
        for row in rows:
            if row[1] == entity:
                mem.seek(row[7]); mem.write(struct.pack("<i", health))
                print(f"entity {entity:#x}: health {row[3]} -> {health}")
                return 0
        print("no such entity"); return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
