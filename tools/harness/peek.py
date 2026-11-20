#!/usr/bin/env python3
"""Read the running game's memory from Linux (read-only), by module offset.

  peek.py read exe|game OFFSET [BYTES]          hex dump
  peek.py find exe|game HEX32 [HEX32 ...]       every place a 32-bit value sits inside the module's image
"""
import os
import struct
import sys
from pathlib import Path

MODULES = {"exe": "helldivers2.exe", "game": "game.dll"}


def game_pid():
    for proc in Path("/proc").glob("[0-9]*"):
        try:
            if proc.stat().st_uid == os.getuid() and (proc / "cmdline").read_bytes().split(b"\0", 1)[0].lower().endswith(b"game\\bin\\helldivers2.exe"):
                return int(proc.name)
        except OSError:
            continue
    raise SystemExit("no running game")


def regions(pid, module):
    rows = []
    for line in (Path("/proc") / str(pid) / "maps").read_text().splitlines():
        parts = line.split(None, 5)
        if len(parts) == 6 and parts[5].lower().endswith("/" + module):
            start, end = (int(x, 16) for x in parts[0].split("-"))
            rows.append((start, end))
    if not rows:
        raise SystemExit(f"{module} is not mapped")
    base = min(start for start, _ in rows)
    # Wine maps the image's sections as file mappings and anonymous memory in between: take the whole span.
    return base, max(end for _, end in rows)


def span(pid, base, end):
    out = []
    for line in (Path("/proc") / str(pid) / "maps").read_text().splitlines():
        start, stop = (int(x, 16) for x in line.split()[0].split("-"))
        if stop > base and start < end and "r" in line.split()[1]:
            out.append((max(start, base), min(stop, end)))
    return out


def main(argv):
    if len(argv) < 4 or argv[2] not in MODULES:
        print(__doc__)
        return 2
    pid = game_pid()
    base, end = regions(pid, MODULES[argv[2]])
    with open(f"/proc/{pid}/mem", "rb", buffering=0) as mem:
        if argv[1] == "read":
            offset, count = int(argv[3], 16), int(argv[4]) if len(argv) > 4 else 16
            mem.seek(base + offset)
            data = mem.read(count)
            print(f"{MODULES[argv[2]]}+{offset:#x} (base {base:#x}): {data.hex(' ')}")
        elif argv[1] == "find":
            needles = {struct.pack("<I", int(x, 16)): x for x in argv[3:]}
            for start, stop in span(pid, base, end):
                try:
                    mem.seek(start)
                    data = mem.read(stop - start)
                except OSError:
                    continue
                for needle, name in needles.items():
                    at = data.find(needle)
                    while at >= 0:
                        print(f"{name} at {MODULES[argv[2]]}+{start + at - base:#x}")
                        at = data.find(needle, at + 1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
