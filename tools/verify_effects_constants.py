#!/usr/bin/env python3
"""The build constants of the effect modules (dof, aim markers, movement, native aim option),
checked against the images of build 25480438.

Input: the constant tables as the C units carry them, one per line, as printed by
`work/tests/wire_effects --constants` (tests/wire_effects_test.c):

    code <build> <module> <name> <game|exe> <rva hex> <bytes hex>
    data <build> <module> <name> <game|exe> <rva-or-offset hex>

Checks, each printed with its evidence; exit 1 when one fails, exit 0 with a "skipped" line when
an image file is not there:

  1. every code witness of the 25480438 tables is the bytes of the image at that address;
  2. where a witness differs from retail's, the difference lies only in the displacement /
     immediate bytes named in SHAPE (same instructions, another target);
  3. every RIP-relative displacement inside a witness resolves to the global the unit reads;
  4. every data global is loaded by code that then uses the field the unit reads off it;
  5. the two renderer-setting thunks jump into functions that read the renderer settings table, and
     the engine functions the binder calls are registered under their names at the census address.

The image files are read only. File offset = RVA in both.

    tools/verify_effects_constants.py <dump> [--game FILE] [--exe FILE] [--census FILE]
"""
import argparse
import os
import struct
import sys

GAME = os.path.expanduser("~/Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll")
EXE = os.path.expanduser("~/Applications/HellForge/mods-retired/localappdata-1019/LocalAppData/LinksLinkToMemory/r6_reload_exe_20261017.bin")
STAMPS = {"game": (0x6AB3B43F, 0x4744000), "exe": (0x6AB382E4, 0x39E8000)}  # src/profile.h

# Bytes that may differ between retail's witness and this build's: (first, last+1) spans.
SHAPE = {
    ("dof", "get_thunk"): [(4, 8)],            # jmp rel32
    ("dof", "set_thunk"): [(4, 8)],
    ("movement", "anchor0"): [(3, 7)],         # mov r11,[rip+d]
    ("movement", "anchor5"): [(7, 11), (18, 19)],  # call rel32; mov rax,[rbp+d8]
    ("native_aim", "witness1"): [(3, 7)],      # mov rax,[rip+d]
    ("aim_markers", "check3"): [(3, 7), (8, 12), (15, 19)],  # lea [r15+d]; call rel32; lea [r15+d]
}

failures = 0


def say(ok, text):
    global failures
    if not ok:
        failures += 1
    print(("ok    " if ok else "FAIL  ") + text)


def u32(image, at):
    return struct.unpack_from("<I", image, at)[0]


def rel(image, at, size):
    """The target of an instruction of `size` bytes at `at` whose last four bytes are a rel32."""
    return at + size + struct.unpack_from("<i", image, at + size - 4)[0]


def pe(image):
    header = u32(image, 0x3C)
    return u32(image, header + 8), u32(image, header + 0x50)


def references(image, target, low, high):
    """Offsets p in [low, high) where the four bytes at p are a rel32 that resolves to `target`."""
    out = []
    data = memoryview(image)
    for p in range(low, high):
        if p + 4 + struct.unpack_from("<i", data, p)[0] == target:
            out.append(p)
    return out


def all_references(image, target, low=0x1000, high=None):
    try:
        import numpy as np
    except ImportError:
        return references(image, target, low, high or len(image) - 4)
    high = high or len(image) - 4
    a = np.frombuffer(image, dtype=np.uint8)[low:high + 4].astype(np.int64)
    d = a[:-3] | (a[1:-2] << 8) | (a[2:-1] << 16) | (a[3:] << 24)
    d = np.where(d >= 2 ** 31, d - 2 ** 32, d)
    p = np.arange(low, low + len(d))
    return [int(x) for x in p[(p + 4 + d) == target]]


def field_uses(image, global_refs, field, window=0x30):
    """Loads of a global that are followed within `window` bytes by the 32-bit displacement `field`."""
    needle = struct.pack("<I", field)
    return [p for p in global_refs if image.find(needle, p + 4, p + 4 + window) != -1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dump")
    parser.add_argument("--game", default=os.environ.get("TFP_GAME_IMAGE", GAME))
    parser.add_argument("--exe", default=os.environ.get("TFP_EXE_IMAGE", EXE))
    parser.add_argument("--census", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "history", "research-0.5.34",
                                                         "live-sdk-census-g1-9472b065.tsv"))
    args = parser.parse_args()
    for path in (args.game, args.exe):
        if not os.path.isfile(path):
            print(f"skipped: image not found: {path}")
            return 0
    images = {"game": open(args.game, "rb").read(), "exe": open(args.exe, "rb").read()}
    for name, image in images.items():
        say(pe(image) == STAMPS[name], f"{name} image is build 25480438 (PE stamp 0x{pe(image)[0]:08x}, size 0x{pe(image)[1]:x})")
    if failures:
        return 1

    code, data = {}, {}
    for line in open(args.dump):
        f = line.split()
        if not f or f[0] not in ("code", "data"):
            continue
        key = (f[1], f[2], f[3])
        if f[0] == "code":
            code[key] = (f[4], int(f[5], 16), bytes.fromhex(f[6]))
        else:
            data[key] = (f[4], int(f[5], 16))
    here = {k[1:]: v for k, v in code.items() if k[0] == "25480438"}
    retail = {k[1:]: v for k, v in code.items() if k[0] == "retail"}
    values = {k[1:]: v for k, v in data.items() if k[0] == "25480438"}
    say(len(here) == 15 and len(retail) == 15 and len(values) == 8, f"{len(here)} code witnesses and {len(values)} data constants per build read from the units")

    print("\n-- 1. the witnesses are the image's bytes; 2. differences from retail are displacements only")
    for key in sorted(here):
        module, rva, expected = here[key]
        image = images[module]
        found = image[rva:rva + len(expected)]
        say(found == expected, f"{key[0]}.{key[1]}: {module}+0x{rva:x} holds {found.hex()}")
        _, retail_rva, retail_bytes = retail[key]
        differing = [i for i in range(len(expected)) if i >= len(retail_bytes) or expected[i] != retail_bytes[i]]
        allowed = SHAPE.get(key, [])
        inside = all(any(a <= i < b for a, b in allowed) for i in differing) and len(expected) == len(retail_bytes)
        if differing:
            say(inside, f"   retail {module}+0x{retail_rva:x} {retail_bytes.hex()}: differs at bytes {differing}, all displacement bytes {allowed}")
        else:
            say(True, f"   equal to retail's bytes at {module}+0x{retail_rva:x}")

    game, exe = images["game"], images["exe"]
    print("\n-- 3 / 4. displacements and data globals")
    # movement: anchor 0 is `mov r11,[rip+d]`: the pool global.
    at = here[("movement", "anchor0")][1]
    pool = values[("movement", "pool")][1]
    say(rel(game, at, 7) == pool, f"movement.pool game+0x{pool:x}: anchor 0 at game+0x{at:x} is mov r11,[rip+d] -> game+0x{rel(game, at, 7):x}")
    at5, at2 = here[("movement", "anchor5")][1], here[("movement", "anchor2")][1]
    callee = rel(game, at5 + 6, 5)
    r5, r2 = retail[("movement", "anchor5")], retail[("movement", "anchor2")]
    retail_callee = r5[1] + 6 + 5 + struct.unpack("<i", r5[2][7:11])[0]
    say(at2 - callee == r2[1] - retail_callee and 0 < at2 - callee < 0x1000,
        f"movement.anchor5: its call goes to game+0x{callee:x}; anchor 2 sits 0x{at2 - callee:x} into that function (retail: 0x{r2[1] - retail_callee:x} into game+0x{retail_callee:x})")
    # native aim: witness 1 loads the avatar pool global; the function it sits in loads the player and weapon globals.
    at = here[("native_aim", "witness1")][1]
    say(rel(game, at, 7) == 0x3326D20, f"native_aim.witness1 at game+0x{at:x} is mov rax,[rip+d] -> game+0x{rel(game, at, 7):x} (the avatar pool, src/profile.h FP_AVATAR; "
        "then [rax+0x40], test dword [rcx+0xf0]: the settings and the aim-memory mode the unit reads)")
    low = min(v[1] for k, v in here.items() if k[0] == "native_aim") - 0x40
    high = max(v[1] for k, v in here.items() if k[0] == "native_aim") + 0x20
    for name in ("player", "weapons"):
        target = values[("native_aim", name)][1]
        found = references(game, target, low, high)
        say(len(found) >= 1, f"native_aim.{name} game+0x{target:x}: loaded at {', '.join('game+0x%x' % (p - 3) for p in found)}, inside the function of the three witnesses "
            f"(game+0x{low:x}..0x{high:x})")
    # aim markers
    state, scene = values[("aim_markers", "state")][1], values[("aim_markers", "scene")][1]
    root, cross = values[("aim_markers", "root")][1], values[("aim_markers", "crosshair")][1]
    state_refs = all_references(game, state)
    uses = [p for p in field_uses(game, state_refs, scene, 0x10) if game[p + 4:p + 6] in (b"\x83\xb8", b"\x83\xb9", b"\x83\xba", b"\x83\xbb")
            and game[p + 10] == 4]
    say(len(uses) >= 1, f"aim_markers.state game+0x{state:x} and scene +0x{scene:x}: {len(uses)} sites load the global and compare dword [reg+0x{scene:x}] with 4 "
        f"(first: game+0x{uses[0] - 3 if uses else 0:x})")
    root_refs = all_references(game, root)
    built = field_uses(game, root_refs, 0x24E334, 0x30)
    say(len(built) >= 4, f"aim_markers.root game+0x{root:x}: {len(root_refs)} loads, {len(built)} of them followed by [reg+0x24e334] (the HUD built byte); "
        f"first: game+0x{built[0] - 3 if built else 0:x}")
    donor = all_references(game, 0x346D518)
    donor_built = field_uses(game, donor, 0x24E334, 0x30) + field_uses(game, donor, 0x24E340, 0x30)
    say(len(donor_built) == 0, f"   the donor's value game+0x346d518 (src/markers.c:8) is another object: {len(donor)} loads, {len(donor_built)} followed by +0x24e334 / +0x24e340")
    at = here[("aim_markers", "check3")][1]
    say(u32(game, at + 3) == cross and u32(game, at + 15) == cross, f"aim_markers.crosshair +0x{cross:x}: check 3 at game+0x{at:x} is lea rcx,[r15+0x{u32(game, at + 3):x}] / call / lea rdx,[r15+…]")
    callee = rel(game, at + 7, 5)
    k1, k2 = here[("aim_markers", "check1")][1], here[("aim_markers", "check2")][1]
    r3 = retail[("aim_markers", "check3")]
    retail_callee = r3[1] + 12 + struct.unpack("<i", r3[2][8:12])[0]
    rk1, rk2 = retail[("aim_markers", "check1")][1], retail[("aim_markers", "check2")][1]
    say((k1 - callee, k2 - callee) == (rk1 - retail_callee, rk2 - retail_callee),
        f"   its call goes to game+0x{callee:x}; checks 1 and 2 sit 0x{k1 - callee:x} and 0x{k2 - callee:x} into that function (retail: 0x{rk1 - retail_callee:x}, 0x{rk2 - retail_callee:x} "
        f"into game+0x{retail_callee:x})")
    # The caller of the function check 3 sits in passes HUD + 0x24e340 when the scene is 4.
    site = game.find(bytes.fromhex("83f8047519488d8f40e32400"), 0x12F0000, 0x12F1000)
    say(site != -1 and game[site - 0x23:site - 0x20] == b"\x41\x8b\x82" and u32(game, site - 0x20) == scene, f"   mission HUD +0x24e340: game+0x{site:x} is cmp eax,4 / jne / lea rcx,[rdi+0x24e340] after mov eax,[state+0x{scene:x}]")
    set_shown = values[("aim_markers", "set_shown")][1]
    say(here[("aim_markers", "check0")][1] == set_shown and game[set_shown + 0x17:set_shown + 0x1D] == bytes.fromhex("84d2440f44c8"),
        f"aim_markers.set_shown game+0x{set_shown:x}: check 0 is the function itself (and/or bit 0x10 of [rcx] by dl: void(widget, shown))")

    print("\n-- 5. the renderer-setting thunks and the engine functions")
    get, sett = here[("dof", "get_thunk")][1], here[("dof", "set_thunk")][1]
    get_body, set_body = rel(exe, get + 3, 5), rel(exe, sett + 3, 5)
    table = [p for p in references(exe, 0x1A102B8, get_body, get_body + 0x200)]
    say(len(table) >= 1, f"dof.get_thunk exe+0x{get:x}: mov rdx,rcx / jmp exe+0x{get_body:x}; that function loads the renderer settings table exe+0x1a102b8 at "
        f"{', '.join('exe+0x%x' % (p - 3) for p in table)} (src/backend_ops.c B_EXE_RENDER_LOAD)")
    say(exe[sett:sett + 4] == bytes.fromhex("488bd1e9") and set_body < sett and exe[set_body - 1] == 0xCC,
        f"dof.set_thunk exe+0x{sett:x}: mov rdx,rcx / jmp exe+0x{set_body:x} (a function start: int3 padding before it)")
    census = {}
    if os.path.isfile(args.census):
        for line in open(args.census):
            f = line.rstrip("\n").split("\t")
            if len(f) >= 4 and f[2] == "exe":
                census[f[0]] = int(f[3], 16)
    names = ["Application.render_setting", "Application.set_render_setting", "Application.main_world", "World.entities",
             "EntityManager.render_data_component", "EntityManager.alive", "RenderDataComponent.instances", "RenderDataComponent.get_property",
             "RenderDataComponent.set_property", "Keyboard.button_index", "Keyboard.button_id", "Keyboard.button", "Window.mouse_focus",
             "Window.show_cursor"]
    say(census.get("Application.render_setting") == get and census.get("Application.set_render_setting") == sett,
        "the live census of this build has Application.render_setting / set_render_setting at the two thunks")
    for name in names:
        if name not in census:
            say(False, f"{name}: not in the census")
            continue
        rva = census[name]
        short = name.split(".")[1].encode() + b"\0"
        strings, at = [], exe.find(b"\0" + short)
        while at != -1:
            strings.append(at + 1)
            at = exe.find(b"\0" + short, at + 1)
        pairs = []
        for p in all_references(exe, rva, 0x1000, 0x1400000):
            if exe[p - 3:p - 1] not in (b"\x48\x8d", b"\x4c\x8d"):
                continue
            for s in strings:
                near = [q for q in references(exe, s, max(p - 0x60, 0), p) if exe[q - 3:q - 1] in (b"\x48\x8d", b"\x4c\x8d")]
                if near:
                    pairs.append((near[-1] - 3, p - 3))
        say(len(pairs) >= 1, f"{name} = exe+0x{rva:x} (census); the image registers the name and the function together at "
            + ", ".join("exe+0x%x / +0x%x" % pair for pair in pairs[:2]))
    print(f"\n{'FAILED: %d check(s)' % failures if failures else 'all checks passed'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
