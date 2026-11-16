#!/usr/bin/env python3
"""Check the build profile's code pins against a game image (file offset = RVA).

Reads every {"name", [true|false,] UINT32_C(0x...), "hex"} row in the given
headers. A row marked true belongs to the executable image; the others to
game.dll. '??' in the hex is a wildcard byte. Exit 0 only when every pin
matches; 1 on any mismatch or unchecked pin; 2 when no pins were found."""
import argparse
import re
import sys
from pathlib import Path

ROW = re.compile(r'\{\s*"(\w+)"\s*,\s*(?:(true|false)\s*,\s*)?UINT32_C\((0x[0-9a-fA-F]+)\)\s*,\s*"([0-9a-fA-F?]+)"\s*\}')


def check(data, rva, pattern):
    width = len(pattern) // 2
    if rva + width > len(data):
        return "OUTSIDE", None
    for index in range(width):
        want = pattern[2 * index:2 * index + 2]
        if want != "??" and data[rva + index] != int(want, 16):
            return "MISMATCH", index
    return "ok", None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True, help="game.dll image, file offset = RVA")
    parser.add_argument("--exe-image", type=Path, help="helldivers2.exe image, file offset = RVA")
    parser.add_argument("--header", type=Path, action="append", required=True)
    args = parser.parse_args()
    game = args.image.read_bytes()
    exe = args.exe_image.read_bytes() if args.exe_image else None
    rows = [row for header in args.header for row in ROW.findall(header.read_text())]
    if not rows:
        print("No pins found in the given headers", file=sys.stderr)
        return 2
    failed = False
    for name, in_exe, rva, pattern in rows:
        rva = int(rva, 16)
        if in_exe == "true" and exe is None:
            print(f"UNCHECKED {name} {rva:#x} (no executable image given)")
            failed = True
            continue
        result, index = check(exe if in_exe == "true" else game, rva, pattern)
        if result == "ok":
            print(f"ok {name} {rva:#x}")
        elif result == "OUTSIDE":
            print(f"OUTSIDE {name} {rva:#x}")
            failed = True
        else:
            print(f"MISMATCH {name} {rva:#x} at byte {index}")
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
