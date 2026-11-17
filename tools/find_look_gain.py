#!/usr/bin/env python3
"""Re-derive the look-gain constants of build 25480438 from the images.

The look-gain normalisation of src/pose_helper.c has 27 names for 20 values
that the retail helper (26568081) knows and this build did not. This script
finds each of them again from four memory-layout images (file offset = RVA),
by matching the retail function's instructions in this build's image, and
prints PASS / FAIL / OPEN per name. docs/LOOK-GAIN-CONSTANTS-25480438.md is
the write-up of what it prints.

  find_look_gain.py                      derive, compare with the values below
  find_look_gain.py --check-c src/pose_helper.c
                                         also compare the constant block
  find_look_gain.py -v                   print the byte evidence

Needs python3 and binutils objdump. Reads the images, writes nothing.
"""
import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

HOME = Path.home()
DEFAULT = {
    "retail_game": HOME / "Projects/Linux HD2 Mods/builds/26568081/runtime/images/game-26568081.dll",
    "retail_exe": HOME / "Projects/Linux HD2 Mods/builds/26568081/runtime/images/exe-26568081.bin",
    "here_game": HOME / "Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll",
    "here_exe": HOME / "Applications/HellForge/mods-retired/localappdata-1019/LocalAppData/LinksLinkToMemory/r6_reload_exe_20261017.bin",
}
# PE time stamp and image size: retail 26568081, and src/profile.h FP_*_STAMP / FP_*_SIZE for 25480438.
IDENTITY = {"retail_game": (0x6af322c1, 0x3382000), "retail_exe": (0x6af2d5e6, 0x29fe000),
            "here_game": (0x6ab3b43f, 0x4744000), "here_exe": (0x6ab382e4, 0x39e8000)}

# Retail's 16 code pins (native_ads_editor.cpp INPUT_PINS): offset, length, FNV-1a 64, in the executable.
RETAIL_PINS = [
    ("EVALUATOR", 0x1118370, 9688, 0xac98356760db02c9, False), ("PIN_2", 0x111a948, 24, 0xded70c3fd0eba95c, False),
    ("PIN_3", 0x1114950, 1724, 0x3690803fb2e8bced, False), ("EXP", 0x1f8ff70, 1013, 0xe4a380a8807b6ec2, False),
    ("SOLVER", 0x1109110, 1328, 0xabb508e82158f3f1, False), ("PIN_6", 0x1109640, 0x1a0, 0xf17ad9ae58e57ab0, False),
    ("PIN_7", 0x110b830, 0x5b0, 0xd77b549041450f76, False), ("STEERING", 0x111f860, 0x124, 0x23ae2b71a5e0f871, False),
    ("PIN_9", 0xe69ca0, 0xdc, 0x91b780e54d5bb826, False), ("ENTER", 0x110fcf0, 0xbb0, 0x32dc74e3dc6a26e9, False),
    ("EXIT", 0x11108a0, 0x1dd0, 0x1a26ca7b7bbba077, False), ("PIN_12", 0x1f3050, 51, 0xdafbc85ae8dffa23, True),
    ("PIN_13", 0x1f3090, 76, 0x9f25c14a58784f1f, True), ("WORLD_MATRIX", 0x1f30e0, 47, 0xd34dcd5634fed114, True),
    ("LOCAL_POSITION", 0x1f2310, 51, 0x542e94a63b7ebae6, True), ("GRAPH_GETTER", 0x28d3c0, 5, 0x86a7f411c046a30c, True)]
# Already established in src/pose_helper.c before this work (docs/POSE-HELPER-TRANSLATION.md): the anchors.
ANCHOR = {"EVALUATOR": 0x1286330, "SOLVER": 0x1277250, "STEERING": 0x128d820, "ENTER": 0x127de20, "EXIT": 0x127e9d0,
          "EXP": 0x21087d0, "INPUT_OWNER": (0x2e38250, 0x347cf18), "WORLD_MATRIX": 0x1fd220}
RETAIL_CONST = {"B_CONST_ONE": (0x22465c4, "0000803f"), "B_CONST_MINUS_ONE": (0x2247894, "000080bf"),
                "B_CONST_RAD_TO_DEG": (0x2246fb0, "e02e6542"), "B_CONST_MINUS_HALF_PI": (0x22478c8, "db0fc9bf"),
                "B_CONST_HALF_PI": (0x2246774, "db0fc93f"), "B_CONST_MINUS_75": (0x2247ab0, "000096c2"),
                "B_CONST_DOUBLE_ONE": (0x2246900, "000000000000f03f")}
RETAIL_CELL = {"B_GAME_NO_AVATAR": 0x2e3fcb8, "B_GAME_HOST_AVATAR": 0x2e3fa9c, "B_GAME_IDLE_AVATAR": 0x2e3f0e4}

# What this script is expected to find. A value here is a claim; the run re-derives each one and compares.
ESTABLISHED = {
    "B_PIN_2": 0x1288908, "B_PIN_2_FNV": 0x6f23c15a305e84dc,
    "B_PIN_3": 0x1282a40, "B_PIN_3_FNV": 0x6d6b65a1d985444d,
    "B_PIN_EXP_FNV": 0xfa3d329f82edb886,
    "B_PIN_6": 0x1277780, "B_PIN_6_FNV": 0x36566b360cb60898,
    "B_PIN_7": 0x1279970, "B_PIN_7_FNV": 0xcb8c325895c1bf1d,
    "B_PIN_9": 0xfd9ba0, "B_PIN_9_FNV": 0x29608cd098aef135,
    "B_PIN_12": 0x1fd190, "B_PIN_12_FNV": 0x2d9df1a66a439fb6,
    "B_PIN_13": 0x1fd1d0, "B_PIN_13_FNV": 0xe99ec6ca69868d09,
    "B_CONST_ONE": 0x23c6d70, "B_CONST_MINUS_ONE": 0x23c800c, "B_CONST_RAD_TO_DEG": 0x23c7730,
    "B_CONST_MINUS_HALF_PI": 0x23c8040, "B_CONST_HALF_PI": 0x23c6f1c, "B_CONST_MINUS_75": 0x23c8228,
    "B_CONST_DOUBLE_ONE": 0x23c7098,
    "B_GAME_NO_AVATAR": 0x3484788, "B_GAME_HOST_AVATAR": 0x348456c, "B_GAME_IDLE_AVATAR": 0x3483c34,
    "B_INPUT_DEVICE": 0x324,
}
# The exit function is a recompiled body here, so it has no byte match. Its hash over the fixed 0x1dd0 bytes
# is accepted when the function's use of the preset row is retail's ("exit preset use" below).
OPEN = {"B_PIN_EXIT_FNV": 0xab98f35fc8fb95d5}

IMAGES = {}
VERBOSE = False


def image(key):
    return IMAGES[key][1]


def fnv(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return value


def sections(key):
    data = image(key)
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    first = pe + 0x18 + struct.unpack_from("<H", data, pe + 0x14)[0]
    return [struct.unpack_from("<II", data, first + 40 * i + 8)[::-1] for i in range(count)]  # (rva, size)


def read_only_data(key, address):
    """True when the address is in the image's read-only data section (the one after the code)."""
    index = 2 if key.endswith("exe") else 1
    start, size = sections(key)[index]
    return start <= address < start + size


def function_end(key, start):
    """End of the unwind-table entry that begins at start (the 4th section is the function table)."""
    data = image(key)
    table, size = sections(key)[3]
    for at in range(table, table + size - size % 12, 12):
        begin, end, _ = struct.unpack_from("<III", data, at)
        if begin == start:
            return end
    return None


LINE = re.compile(r"^\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*(?:\t(.*))?$")
CACHE = {}


def instructions(key, start, length):
    """[(address, bytes, text, wild, kind, target)]: wild = byte positions that hold a rip displacement or a
    relative branch target (kind 'rip' / 'branch'), loose = also 32-bit displacements and immediates."""
    if (key, start, length) in CACHE:
        return CACHE[key, start, length]
    out = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "intel", "-w",
                          f"--start-address={start}", f"--stop-address={start + length}", str(IMAGES[key][0])],
                         capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        match = LINE.match(line)
        if not match:
            continue
        address, raw, text = int(match.group(1), 16), bytes.fromhex(match.group(2).replace(" ", "")), (match.group(3) or "").strip()
        end, wild, kind, target, loose = address + len(raw), set(), None, None, set()
        comment = re.search(r"# 0x([0-9a-f]+)", text)
        if comment and "rip" in text:
            target = int(comment.group(1), 16)
            at = raw.find(struct.pack("<i", target - end), 1)
            if at >= 0:
                wild, kind = set(range(at, at + 4)), "rip"
        if kind is None and ((raw[0] in (0xe8, 0xe9) and len(raw) == 5) or (len(raw) == 6 and raw[0] == 0x0f and 0x80 <= raw[1] <= 0x8f)):
            wild, kind, target = set(range(len(raw) - 4, len(raw))), "branch", end + struct.unpack("<i", raw[-4:])[0]
        if kind is None:
            for number in re.finditer(r"0x([0-9a-f]+)", text):
                value = int(number.group(1), 16)
                if 0x80 <= value <= 0xffffffff:
                    at = raw.find(struct.pack("<I", value), 1)
                    if at >= 0:
                        loose |= set(range(at, at + 4))
        rows.append((address, raw, text, wild, kind, target, loose))
    CACHE[key, start, length] = rows
    return rows


def pattern(key, start, length, loose):
    parts = []
    for _, raw, _, wild, _, _, more in instructions(key, start, length):
        skip = wild | more if loose else wild
        parts.append(b"".join(b"." if i in skip else re.escape(raw[i:i + 1]) for i in range(len(raw))))
    return re.compile(b"".join(parts), re.S)


def find(retail_key, start, length, here_key, loose=False):
    return [m.start() for m in pattern(retail_key, start, length, loose).finditer(image(here_key))]


class Pair:
    """A retail span and its counterpart here, compared instruction by instruction."""

    def __init__(self, name, retail_key, retail, here_key, here, length):
        self.name, self.retail, self.here, self.length = name, retail, here, length
        self.retail_key, self.here_key = retail_key, here_key
        a, b = instructions(retail_key, retail, length), instructions(here_key, here, length)
        self.count = (len(a), len(b))
        self.references, self.fields, self.other = [], [], []
        self.aligned = len(a) == len(b)
        for x, y in zip(a, b):
            if x[0] - retail != y[0] - here or len(x[1]) != len(y[1]):
                self.aligned = False
                self.other.append((x, y))
                continue
            differing = {i for i in range(len(x[1])) if x[1][i] != y[1][i]}
            if x[4] and x[4] == y[4] and differing <= x[3]:
                self.references.append((x[0], y[0], x[4], x[5], y[5]))
            elif differing and differing <= x[6]:
                self.fields.append((x, y))
            elif differing:
                self.other.append((x, y))

    def equal(self):
        """Same instructions at the same offsets; only relocated targets and struct fields may differ."""
        return self.aligned and not self.other

    def describe(self):
        text = f"{self.count[0]} instructions against {self.count[1]}, {len(self.references)} relocated targets, {len(self.fields)} field changes, {len(self.other)} other differences"
        if VERBOSE:
            for x, y in self.fields + self.other:
                text += f"\n        retail {x[0]:#x} {x[1].hex()} {x[2]}\n        here   {y[0]:#x} {y[1].hex()} {y[2]}"
        return text


RESULT = {}
FAILED = []


def report(name, ok, value, note):
    expected = ESTABLISHED.get(name)
    good = bool(ok) and value is not None and value == expected
    RESULT[name] = value if ok else None
    if not good:
        FAILED.append(name)
    shown = "none" if value is None else f"{value:#x}"
    print(f"{'PASS' if good else 'FAIL'}  {name:<22} {shown:<20} {note}")


def evidence(text):
    if VERBOSE:
        print("        " + text)


def hexat(key, at, length):
    return image(key)[at:at + length].hex()


def main():
    global VERBOSE
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for key, path in DEFAULT.items():
        parser.add_argument("--" + key.replace("_", "-"), type=Path, default=path)
    parser.add_argument("--check-c", type=Path, help="src/pose_helper.c: compare its constant block with what was found")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    VERBOSE = args.verbose
    for key in DEFAULT:
        path = getattr(args, key)
        IMAGES[key] = (path, path.read_bytes())

    print("== images")
    for key, (stamp, size) in IDENTITY.items():
        data = image(key)
        pe = struct.unpack_from("<I", data, 0x3c)[0]
        found = (struct.unpack_from("<I", data, pe + 8)[0], struct.unpack_from("<I", data, pe + 0x50)[0])
        ok = found == (stamp, size)
        print(f"{'ok  ' if ok else 'BAD '}  {key:<12} stamp {found[0]:#x} size {found[1]:#x}  {IMAGES[key][0]}")
        if not ok:
            return 2
    bad = [name for name, at, length, want, exe in RETAIL_PINS
           if fnv(image("retail_exe" if exe else "retail_game")[at:at + length]) != want]
    print(f"{'ok  ' if not bad else 'BAD '}  retail's 16 pins hash to retail's own values in the retail images {bad or ''}")
    if bad:
        return 2
    retail_pin = {name: (at, length) for name, at, length, _, _ in RETAIL_PINS}

    print("== anchors (established before; re-checked here)")
    pairs = {}
    for name in ("EVALUATOR", "SOLVER", "STEERING", "ENTER"):
        at, length = retail_pin[name]
        pairs[name] = Pair(name, "retail_game", at, "here_game", ANCHOR[name], length)
        # Same instruction boundaries throughout is what the reference map below needs. ENTER has one
        # instruction with its two address registers swapped (lea r8d,[rdx+r10] / [r10+rdx]).
        print(f"{'ok  ' if pairs[name].aligned else 'BAD '}  {name:<12} retail {at:#x} here {ANCHOR[name]:#x}: {pairs[name].describe()}")
        if not pairs[name].aligned or len(pairs[name].other) > (1 if name == "ENTER" else 0):
            return 2
    delta = ANCHOR["EVALUATOR"] - retail_pin["EVALUATOR"][0]

    print("== code pins")
    # PIN_2: the evaluator's jump table, the 24 bytes that follow the evaluator span in both builds.
    table = [(x, y) for x, y in pairs["EVALUATOR"].fields if "0x111a948" in x[2]]
    here = None
    if len(table) == 1:
        here = int(re.search(r"\+0x([0-9a-f]+)\]", table[0][1][2]).group(1), 16)
    r_entries = struct.unpack("<6I", image("retail_game")[0x111a948:0x111a960])
    h_entries = struct.unpack("<6I", image("here_game")[here:here + 24]) if here else ()
    ok = here == ANCHOR["EVALUATOR"] + retail_pin["EVALUATOR"][1] and all(h - r == delta for r, h in zip(r_entries, h_entries)) and len(h_entries) == 6
    report("B_PIN_2", ok, here, f"jump table named by the evaluator's one table load; 6 entries each retail + {delta:#x}")
    evidence(f"retail {table[0][0][0]:#x} {table[0][0][2]} | here {table[0][1][0]:#x} {table[0][1][2]}" if table else "no table load")
    evidence(f"retail bytes {hexat('retail_game', 0x111a948, 24)}")
    evidence(f"here   bytes {hexat('here_game', here, 24) if here else ''}")
    report("B_PIN_2_FNV", ok, fnv(image("here_game")[here:here + 24]) if here else None, "FNV-1a 64 over the 24 bytes")

    def whole(name, pin, exe, loose, retail_start=None, search_length=None):
        """Search this build's image for the retail span; one hit required."""
        r_key, h_key = ("retail_exe", "here_exe") if exe else ("retail_game", "here_game")
        at, length = retail_pin[pin]
        start = at if retail_start is None else retail_start
        span = search_length or length - (start - at)
        hits = find(r_key, start, span, h_key, loose)
        strict = find(r_key, start, min(span, 64), h_key, False)
        if len(hits) != 1:
            report(name, False, None, f"{len(hits)} matches")
            return None, None
        pair = Pair(pin, r_key, start, h_key, hits[0], span)
        pairs[pin] = pair
        return hits[0] - (start - at), f"1 match in the image ({'fields wild' if loose else 'targets wild'}; first 64 bytes alone: {len(strict)}); {pair.describe()}"

    # PIN_3, EXP, PIN_12, PIN_13: every byte equal except rip displacements and branch targets.
    for name, pin, exe in (("B_PIN_3", "PIN_3", False), ("B_PIN_12", "PIN_12", True), ("B_PIN_13", "PIN_13", True)):
        here, note = whole(name, pin, exe, False)
        if here is None:
            report(name + "_FNV", False, None, "no address")
            continue
        length = retail_pin[pin][1]
        ok = pairs[pin].equal() and not pairs[pin].fields
        if exe:
            ok = ok and ANCHOR["WORLD_MATRIX"] - here == 0x1f30e0 - retail_pin[pin][0]
            note += f"; {ANCHOR['WORLD_MATRIX'] - here:#x} below the world-matrix function in both builds"
        report(name, ok, here, note)
        key = "here_exe" if exe else "here_game"
        evidence(f"retail first 24 bytes {hexat('retail_exe' if exe else 'retail_game', retail_pin[pin][0], 24)}")
        evidence(f"here   first 24 bytes {hexat(key, here, 24)}")
        report(name + "_FNV", ok, fnv(image(key)[here:here + length]), f"FNV-1a 64 over {length} bytes")

    here, note = whole("B_PIN_EXP_FNV", "EXP", False, False)
    if here is not None:
        end = function_end("here_game", here)
        tables = all(image("retail_game")[r:r + 0x200] == image("here_game")[h:h + 0x200]
                     for _, _, kind, r, h in pairs["EXP"].references if kind == "rip" and read_only_data("retail_game", r) and r >= 0x2250000)
        ok = here == ANCHOR["EXP"] and pairs["EXP"].equal() and not pairs["EXP"].fields and end == here + 1013 and tables
        report("B_PIN_EXP_FNV", ok, fnv(image("here_game")[here:here + 1013]),
               f"at B_GAME_EXP {here:#x}: {note}; this build's function table ends the function at {end:#x} = +1013; lookup tables equal")

    # PIN_6: the whole function (0x19f bytes) and one padding byte; struct fields may differ.
    here, note = whole("B_PIN_6", "PIN_6", False, True, search_length=0x19f)
    if here is not None:
        end = function_end("here_game", here)
        changes = sorted({(re.search(r"0x[0-9a-f]{4,}", x[2]).group(0), re.search(r"0x[0-9a-f]{4,}", y[2]).group(0)) for x, y in pairs["PIN_6"].fields})
        ok = pairs["PIN_6"].equal() and changes == [("0xac488", "0xac408")] and end == here + 0x19f
        report("B_PIN_6", ok, here, f"{note}; the one field change is B_SETTINGS_DEGREES {changes}; function ends at +0x19f in both, the 0x1a0th byte is padding (retail c3, here {hexat('here_game', here + 0x19f, 1)})")
        evidence(f"retail first 24 bytes {hexat('retail_game', 0x1109640, 24)}")
        evidence(f"here   first 24 bytes {hexat('here_game', here, 24)}")
        report("B_PIN_6_FNV", ok, fnv(image("here_game")[here:here + 0x1a0]), "FNV-1a 64 over 0x1a0 bytes")
    else:
        report("B_PIN_6_FNV", False, None, "no address")

    # PIN_7: retail's span starts 2 bytes before an instruction boundary (inside a rip displacement) and ends
    # 5 bytes into a 7-byte lea. Match the whole instructions in between, then check both ragged ends.
    here, note = whole("B_PIN_7", "PIN_7", False, True, retail_start=0x110b832, search_length=0x110bddb - 0x110b832)
    if here is not None:
        before_r = instructions("retail_game", 0x110b829, 9)[0]
        before_h = instructions("here_game", here + 2 - 9, 9)[0]
        after_r = instructions("retail_game", 0x110bddb, 7)[0]
        after_h = instructions("here_game", here + (0x110bddb - 0x110b830), 7)[0]
        head = (before_r[1][:5] == before_h[1][:5] and before_r[4] == before_h[4] == "rip"
                and image("retail_game")[before_r[5]:before_r[5] + 4] == image("here_game")[before_h[5]:before_h[5] + 4])
        tail = after_r[1][:3] == after_h[1][:3] and after_r[5] == after_h[5] == 0
        changes = sorted({(re.search(r"0x[0-9a-f]{4,}", x[2]).group(0), re.search(r"0x[0-9a-f]{4,}", y[2]).group(0)) for x, y in pairs["PIN_7"].fields})
        expected = [("0x110c2a8", f"{0x110c2a8 + (here - 0x110b830):#x}"), ("0x2cb21cc", "0x32f9a1c")]
        ok = pairs["PIN_7"].equal() and head and tail and changes == expected
        report("B_PIN_7", ok, here, f"{note}; field changes {changes} = the jump table moved with the code and B_GAME_PRESETS+0x8c; ragged ends: the load before it reads the same number, the lea after it is the image base in both")
        evidence(f"retail {before_r[0]:#x} {before_r[1].hex()} {before_r[2]} | here {before_h[0]:#x} {before_h[1].hex()} {before_h[2]}")
        evidence(f"retail {after_r[0]:#x} {after_r[1].hex()} {after_r[2]} | here {after_h[0]:#x} {after_h[1].hex()} {after_h[2]}")
        report("B_PIN_7_FNV", ok, fnv(image("here_game")[here:here + 0x5b0]), "FNV-1a 64 over 0x5b0 bytes")
    else:
        report("B_PIN_7_FNV", False, None, "no address")

    # PIN_9: the entity -> avatar lookup the steering function calls.
    here, note = whole("B_PIN_9", "PIN_9", False, True)
    if here is not None:
        callers = [h for _, _, kind, r, h in pairs["STEERING"].references if kind == "branch" and r == 0xe69ca0]
        changes = sorted({(re.search(r"0x[0-9a-f]{4,}", x[2]).group(0), re.search(r"0x[0-9a-f]{4,}", y[2]).group(0)) for x, y in pairs["PIN_9"].fields})
        expected = [("0x1e693d", "0x1e65e4"), ("0xf24990", "0xf22ec8"), ("0xf24998", "0xf22ed0"), ("0xf2499c", "0xf22ed4"), ("0xf249a0", "0xf22ed8")]
        ok = pairs["PIN_9"].equal() and callers == [here] and changes == expected
        report("B_PIN_9", ok, here, f"{note}; the steering pin's call goes to it; field changes are B_WORLD_ENTITY_MAP (0xf22ec8..) and B_WORLD_ENTITY_AVATAR (0x1e65e4*8 = 0xf32f20)")
        evidence(f"retail first 24 bytes {hexat('retail_game', 0xe69ca0, 24)}")
        evidence(f"here   first 24 bytes {hexat('here_game', here, 24)}")
        report("B_PIN_9_FNV", ok, fnv(image("here_game")[here:here + 0xdc]), "FNV-1a 64 over 0xdc bytes")
    else:
        report("B_PIN_9_FNV", False, None, "no address")

    print("== the seven numbers")
    targets = {}
    for pair in pairs.values():
        if pair.retail_key != "retail_game":
            continue
        for r_at, h_at, kind, r, h in pair.references:
            if kind == "rip":
                targets.setdefault(r, []).append((pair.name, r_at, h_at, h))
    for name, (retail, bits) in RETAIL_CONST.items():
        uses = targets.get(retail, [])
        values = {h for _, _, _, h in uses}
        here = values.pop() if len(values) == 1 else None
        size = len(bits) // 2
        ok = (here is not None and hexat("retail_game", retail, size) == bits and hexat("here_game", here, size) == bits
              and read_only_data("here_game", here) and hexat("retail_game", retail, 8) == hexat("here_game", here, 8))
        where = ", ".join(sorted({f"{n} x{sum(1 for u in uses if u[0] == n)}" for n, _, _, _ in uses}))
        report(name, ok, here, f"retail {retail:#x}; {len(uses)} matched loads ({where}) all name one address; value {bits} at both, read-only data")
        for n, r_at, h_at, h in uses:
            evidence(f"{n}: retail {r_at:#x} -> {retail:#x} | here {h_at:#x} -> {h:#x}")

    print("== steering cells and the device field")
    for name, retail in RETAIL_CELL.items():
        uses = targets.get(retail, [])
        values = {h for _, _, _, h in uses}
        here = values.pop() if len(values) == 1 else None
        report(name, here is not None, here, f"retail {retail:#x}; {len(uses)} matched reads ({', '.join(sorted({n for n, _, _, _ in uses}))})")
        for n, r_at, h_at, h in uses:
            evidence(f"{n}: retail {r_at:#x} | here {h_at:#x}; 16 bytes before the cell: retail {hexat('retail_game', retail - 16, 16)} here {hexat('here_game', h - 16, 16)}")
    r_eval, h_eval = retail_pin["EVALUATOR"][0], ANCHOR["EVALUATOR"]
    owner = [(r_at, h_at) for r_at, h_at, kind, r, h in pairs["EVALUATOR"].references if kind == "rip" and (r, h) == ANCHOR["INPUT_OWNER"]]
    reads = [(x[0], x[1], x[2]) for x in instructions("retail_game", r_eval, retail_pin["EVALUATOR"][1]) if x[2] == "mov    eax,DWORD PTR [rdi+0x324]"]
    same = [image("here_game")[at - r_eval + h_eval:at - r_eval + h_eval + len(raw)] == raw for at, raw, _ in reads]
    loads_rdi = any(image("here_game")[h_at:h_at + 3] == bytes.fromhex("488b3d") for _, h_at in owner)
    ok = len(reads) == 2 and all(same) and loads_rdi
    report("B_INPUT_DEVICE", ok, 0x324 if ok else None,
           f"the evaluator loads rdi from B_GAME_INPUT_OWNER and reads [rdi+0x324] at {', '.join(f'game+{at - r_eval + h_eval:#x}' for at, _, _ in reads)}, the same bytes as retail")
    for at, raw, text in reads:
        evidence(f"retail {at:#x} {raw.hex()} {text} | here {at - r_eval + h_eval:#x} {hexat('here_game', at - r_eval + h_eval, len(raw))}")

    print("== not established")
    r_at, r_len = retail_pin["EXIT"]
    h_at = ANCHOR["EXIT"]
    r_end = next(x[0] + 1 for x in instructions("retail_game", r_at, r_len) if x[2].startswith("ret") and image("retail_game")[x[0] + 1] != 0x48)
    h_end = None
    table_start, table_size = sections("here_game")[3]
    entries = sorted(struct.unpack_from("<II", image("here_game"), at) for at in range(table_start, table_start + table_size - table_size % 12, 12))
    cursor = h_at
    for begin, end in entries:
        if begin == cursor:
            cursor = end
            h_end = end
    a = instructions("retail_game", r_at, r_end - r_at)
    b = instructions("here_game", h_at, h_end - h_at)
    same_bytes = image("retail_game")[r_at:r_end] == image("here_game")[h_at:h_end]
    candidate = fnv(image("here_game")[h_at:h_at + r_len])
    print(f"OPEN  B_PIN_EXIT_FNV         candidate {candidate:#x} {'(as listed)' if candidate == OPEN['B_PIN_EXIT_FNV'] else '(CHANGED)'}"
          f"  retail's function is {r_end - r_at:#x} bytes / {len(a)} instructions, this build's {h_end - h_at:#x} bytes / {len(b)} instructions"
          f" ({'same' if same_bytes else 'different'} body: registers, stack slots, block order and one more call). The candidate hashes the 0x1dd0 bytes"
          f" the C reads from game+{h_at:#x}: the whole function, 8 padding bytes, the whole next function and its padding, ending at game+{h_at + r_len:#x}.")
    if candidate != OPEN["B_PIN_EXIT_FNV"]:
        FAILED.append("B_PIN_EXIT_FNV candidate")

    def preset_use(which, at, length, table):
        """How the function uses the preset row: the index load and scaling before the table lea, then every r13 operand."""
        rows = instructions(which, at, length)
        hit = next(i for i, x in enumerate(rows) if x[2].startswith("lea") and "rax,[rip" in x[2] and x[0] + len(x[1]) + struct.unpack_from("<i", x[1], 3)[0] == table)
        lead = [x[2] for x in rows[hit - 4:hit] if "0x1f0c" in x[2] or "r13" in x[2]]
        uses = sorted(re.sub(r"xmm\d+", "xmm", x[2]) for x in rows[hit + 1:] if "r13" in x[2])
        index_refs = sorted(x[2] for x in rows if "0x1f0c" in x[2])
        return lead, uses, index_refs
    r_use = preset_use("retail_game", r_at, r_end - r_at, 0x2cb2140)
    h_use = preset_use("here_game", h_at, h_end - h_at, 0x32f9990)
    same_use = r_use == h_use and len(r_use[1]) > 10
    print(f"{'PASS' if same_use else 'FAIL'}  exit preset use            index {r_use[0]} then {len(r_use[1])} r13 operands and {len(r_use[2])} index references:"
          f" {'the same in both builds' if same_use else 'DIFFERENT'}")
    if not same_use:
        FAILED.append("exit preset use")

    if args.check_c:
        print(f"== {args.check_c}")
        text = args.check_c.read_text()
        flag = re.search(r"#define B_INPUT_ESTABLISHED\s+(\S+)", text).group(1)
        print(f"{'ok  ' if flag == '1' else 'BAD '}  B_INPUT_ESTABLISHED is {flag} (1: all pins entered)")
        if flag != "1":
            FAILED.append("B_INPUT_ESTABLISHED")
        for name in list(ESTABLISHED) + list(OPEN):
            match = re.search(rf"#define {name}\s+(?:UINT(?:32|64)_C\()?(0x[0-9a-fA-F]+|\d+)", text)
            value = int(match.group(1), 0) if match else None
            if name in OPEN:
                ok, want = value == OPEN[name], f"{OPEN[name]:#x} (accepted on its preset use)"
            else:
                ok, want = value in (0, ESTABLISHED[name]) and (value == 0 or RESULT.get(name) == value), f"{ESTABLISHED[name]:#x} or 0"
            print(f"{'ok  ' if ok else 'BAD '}  {name:<22} in the C: {'missing' if value is None else hex(value)}; expected {want}")
            if not ok:
                FAILED.append("C:" + name)

    print(f"== {len(ESTABLISHED) - len([n for n in FAILED if n in ESTABLISHED])} of {len(ESTABLISHED)} names re-derived, {len(OPEN)} open" + (f"; FAILED: {', '.join(FAILED)}" if FAILED else ""))
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
