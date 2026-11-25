#!/usr/bin/env python3
"""Check src/entity_fields.h against build 25480438's game.dll image (file offset = RVA).

Each of the fifteen instructions the mod reads a movable field's displacement from must have, in the image, the
bytes the header says before and after its displacement, and a displacement equal to the game's own offset of
that field (HellForge 0.4 and the unmodded game). Also prints the masked code-pin hashes of src/pose_helper.c's
B_PIN_9 and B_PIN_STEERING. Reads the image and the header; writes nothing.

    tools/verify_entity_sites.py [--image game.dll] [--header src/entity_fields.h]
"""
import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = Path.home() / "Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll"
STAMP, SIZE = 0x6AB3B43F, 0x4744000  # src/profile.h FP_GAME_STAMP / FP_GAME_SIZE

# The displacement each site holds in the game: the field's own offset (records: / 8; their +8: / 8 + 1).
EXPECTED = {
    "TFP_SITE_RECORDS": 0xF32F18 // 8, "TFP_SITE_RECORDS_NETWORK": 0xF32F18 // 8 + 1,
    "TFP_SITE_NETWORK_BUCKETS": 0xF22EC8, "TFP_SITE_NETWORK_CAPACITY": 0xF22ED0, "TFP_SITE_NETWORK_EMPTY": 0xF22ED4,
    "TFP_SITE_NETWORK_MULTIPLIER": 0xF22ED8,
    "TFP_SITE_ENTITY_BUCKETS": 0xF1AEB0, "TFP_SITE_ENTITY_CAPACITY": 0xF1AEB8, "TFP_SITE_ENTITY_EMPTY": 0xF1AEBC,
    "TFP_SITE_ENTITY_MULTIPLIER": 0xF1AEC0,
    "TFP_SITE_PLAYER_COUNT": 0x84, "TFP_SITE_PLAYER_LIVE": 0x88, "TFP_SITE_PLAYER_DESCRIPTORS": 0xE8,
    "TFP_SITE_PLAYER_NETWORK": 0x3A8, "TFP_SITE_PLAYER_NETWORK_STEERING": 0x3A8,
}
PINS = {"B_PIN_9": (0xFD9BA0, 0xDC, (0x2C, 0x3A, 0x5C, 0x63, 0xB3), 0x4F44ABD28DFD84FA),
        "B_PIN_STEERING": (0x128D820, 0x124, (0x16, 0x1F), 0xF4122F2D563E703F)}


def fnv(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--image", type=Path, default=IMAGE)
    parser.add_argument("--header", type=Path, default=ROOT / "src/entity_fields.h")
    args = parser.parse_args()
    if not args.image.exists():
        print(f"skipped: {args.image} not found")
        return 0
    image = args.image.read_bytes()
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if struct.unpack_from("<I", image, pe + 8)[0] != STAMP or struct.unpack_from("<I", image, pe + 80)[0] != SIZE:
        print("FAIL  the image is not build 25480438's game.dll")
        return 1
    text = args.header.read_text()
    names = re.search(r"enum \{\s*(TFP_SITE_RECORDS,.*?)TFP_FIELD_SITES", text, re.S).group(1)
    order = [n.strip() for n in names.replace("\n", " ").split(",") if n.strip()]
    table = re.search(r"tfp_field_sites\[TFP_FIELD_SITES\] = \{(.*?)\n\};", text, re.S).group(1)
    rows = re.findall(r'\{"([^"]+)", UINT32_C\((0x[0-9A-Fa-f]+)\), (\d+), (\d+), \{([^}]*)\}, \{([^}]*)\}\}', table)
    failed = 0
    if len(rows) != len(order):
        print(f"FAIL  {len(rows)} rows for {len(order)} names")
        return 1
    for name, (label, rva, nb, na, before, after) in zip(order, rows):
        rva, nb, na = int(rva, 16), int(nb), int(na)
        before = bytes(int(x, 16) for x in before.split(",") if x.strip())
        after = bytes(int(x, 16) for x in after.split(",") if x.strip())
        disp = struct.unpack_from("<i", image, rva)[0]
        ok = (len(before) == nb and len(after) == na and image[rva - nb:rva] == before and image[rva + 4:rva + 4 + na] == after
              and disp == EXPECTED[name])
        failed += not ok
        print(f"{'PASS' if ok else 'FAIL'}  game+0x{rva:x} {label:<34} {image[rva - nb:rva].hex()} [{disp:#x}] {image[rva + 4:rva + 4 + na].hex()}")
    for name, (at, length, masks, want) in PINS.items():
        data = bytearray(image[at:at + length])
        for m in masks:
            data[m:m + 4] = bytes(4)
        got = fnv(bytes(data))
        ok = got == want and f"{want:#x}" in open(ROOT / "src/pose_helper.c").read().lower()
        failed += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {name} masked FNV-1a 64 {got:#x} (src/pose_helper.c {want:#x})")
    print(f"== {len(rows) + len(PINS) - failed} of {len(rows) + len(PINS)} as the header and the pose helper say")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
