#!/usr/bin/env python3
"""Cut reference vectors from the retail model_shadow_source.lua (the read-only inventory of the
first-person rig, the arm pieces on it and the weapon tree), run unmodified under LuaJIT against a
scripted backend.

  gen_model_shadow_source.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import argparse
import random
import sys
from pathlib import Path

import visibility_world as vw
from visibility_world import Script, standard

HERE = Path(__file__).resolve().parent
sweep = vw.sweep


def changes(w, p):
    """Things that can change under an observation; each undoes cleanly."""
    v = w.var
    region = lambda unit: (unit.obj, unit.obj + 0x400)
    return [
        lambda: v("s_kind", 4), lambda: v("s_safe", 0), lambda: w.text("s_pid", b"player-B"),
        lambda: w.text("s_identity", b"player-A/other"), lambda: v("s_record", 0x1F0B2300), lambda: v("s_body", p.torso.ref),
        lambda: v("s_viewmodel", p.gun.ref), lambda: v("s_weapon_unit", p.optic.ref), lambda: v("full_fail", 1),
        lambda: v("full_fail", 9), lambda: v("foreign", 1), lambda: v("foreign", 2), lambda: v("exe_fail", 1),
        lambda: w.retire(p.rig), lambda: w.retire(p.gun), lambda: w.retire(p.lens), lambda: w.retire(p.arms[1]),
        lambda: w.retire(p.body),
        lambda: w.ptr(p.arms[0].obj + 0x1D0, p.body.obj), lambda: w.ptr(p.gun.obj + 0x1D0, p.body.obj),
        lambda: w.ptr(p.lens.obj + 0x1D0, p.gun.obj), lambda: w.ptr(p.gun.obj + 0x1D8, 0),
        lambda: w.ptr(p.magazine.obj + 0x1E0, 0), lambda: w.ptr(p.rig.obj + 0x1D8, p.optic.obj),
        lambda: w.ptr(p.lens.obj + 0x1D8, p.magazine.obj),
        lambda: w.u32(p.rig.obj + 0x70, 62), lambda: w.poke(p.rig.obj + 0x358, b"\x53\x85\xbd\xa2\xbd\xc9\x3e\x5f"),
        lambda: w.ptr(p.rig.obj + 0x18, w.world_native + 16), lambda: w.ptr(p.arms[1].obj + 0x18, w.world_native + 16),
        lambda: w.ptr(p.optic.obj + 0x18, w.world_native + 16),
        lambda: w.slot(0, 7, 0), lambda: w.slot(1, 9, p.arms[0].ref), lambda: w.u32(p.gun.obj + 8, p.gun.ref + 1),
        lambda: w.ptr(w.objects + p.optic.index * 8, p.lens.obj),
        lambda: w.unreadable(*region(p.rig)), lambda: w.unreadable(*region(p.lens)), lambda: w.unreadable(*region(p.body)),
        lambda: w.unreadable(w.custom(), w.custom() + 0x200), lambda: w.unreadable(p.desc_at, p.desc_at + 24),
        lambda: w.unreadable(p.component, p.component + 20), lambda: w.unreadable(w.generations, w.generations + w.capacity),
        lambda: w.poke(p.desc_at + 3, b"\x09"), lambda: w.poke(p.component + 3, b"\x09"), lambda: w.poke(p.desc_at + 20, b"\x00"),
        lambda: v("s_request_weapon", 78), lambda: v("s_world", w.world_native + 16), lambda: v("desc_at", 0),
        lambda: w.bind(vw.DESCRIPTOR, 0), lambda: w.bind(vw.FOREIGN, 0), lambda: w.bind(vw.FOREIGN, 2),
    ]


def basics():
    w, p = standard()
    s = Script(w, "basics", "model", full=1)
    s.op("has"); s.op("unchanged"); s.op("valid," + str(p.gun.ref))
    s.op("probe"); s.op("has"); s.op("unchanged"); s.op("unchanged,1")
    for unit in [p.gun, p.magazine, p.optic, p.lens, p.arms[0], p.arms[1], p.rig, p.body, p.head]:
        s.op(f"valid,{unit.ref}")
    for tweak in range(1, 10):
        s.op(f"valid,{p.lens.ref},{tweak}"); s.op(f"valid,{p.arms[0].ref},{tweak}")
    for mode in (1, 2):                                      # the caller's inspect answers another object, or is no function
        w.var("insp_mode", mode); s.op("unchanged")
    w.var("insp_mode", 0); s.op("unchanged")
    # the appearance: stale, cut short, changed
    w.slot(2, 6, p.limbs[0].ref); s.op("unchanged"); s.op("unchanged,1"); s.op("probe,1"); s.op("probe,2"); s.op("probe"); s.op("unchanged")
    # an arm piece worn in two layers, an arm slot holding the rig and the body, a kept limb that is not on the rig
    w.slot(1, 6, p.arms[0].ref); w.slot(2, 9, p.arms[1].ref); s.op("probe"); s.op("unchanged")
    w.slot(1, 8, p.rig.ref); s.op("probe"); w.slot(1, 8, p.body.ref); s.op("probe")
    w.ptr(p.body.obj + 0x1D0, p.rig.obj); s.op("probe"); w.ptr(p.body.obj + 0x1D0, 0)
    w.slot(1, 8, 4294967295); w.slot(1, 7, 0xFFFFFFFE); s.op("probe"); w.slot(1, 7, 0); w.slot(1, 8, 0); s.op("probe")
    # what the observation needs to be remembered
    for change, back in ((lambda: w.var("s_weapon", -1), lambda: w.var("s_weapon", 77)), (lambda: w.var("s_weapon", 0), lambda: w.var("s_weapon", 77)),
                         (lambda: w.var("s_weapon", 77.5), lambda: w.var("s_weapon", 77)),
                         (lambda: w.var("s_weapon", 4294967295), lambda: w.var("s_weapon", 77)),
                         (lambda: w.var("s_wcd", -1), lambda: w.var("s_wcd", p.component)),
                         (lambda: w.text("s_wdesc", None), lambda: w.text("s_wdesc", w.peek(p.desc_at, 20))),
                         (lambda: w.text("s_wdesc", w.peek(p.desc_at, 19)), lambda: w.text("s_wdesc", w.peek(p.desc_at, 20))),
                         (lambda: w.bind(vw.DESCRIPTOR, 0), lambda: w.bind(vw.DESCRIPTOR, 1))):
        change(); s.op("probe"); s.op("unchanged"); back(); s.op("probe"); s.op("unchanged")
    # the snapshot the caller holds differs from the backend's
    for name, value, back in (("s_viewmodel", -1, p.rig.ref), ("s_weapon_unit", -1, p.gun.ref), ("s_body", p.rig.ref, p.body.ref),
                              ("s_viewmodel", 0, p.rig.ref), ("s_viewmodel", p.rig.ref + 0.5, p.rig.ref),
                              ("s_avatar_index", 1, 0), ("s_kind", 4, 6), ("s_safe", 0, 1)):
        w.var(name, value); s.op("probe"); s.op("unchanged"); s.op(f"valid,{p.gun.ref}"); w.var(name, back); s.op("probe")
    w.text("s_pid", None); s.op("probe"); s.op("unchanged"); w.text("s_pid", b"player-A"); s.op("probe")
    w.text("s_identity", None); s.op(f"valid,{p.gun.ref}"); w.text("s_identity", b"player-A/gun")
    for mode in (1, 2, 3):
        w.var("foreign", mode); s.op("probe"); s.op(f"valid,{p.gun.ref}"); s.op("unchanged")
    w.var("foreign", 0); w.bind(vw.FOREIGN, 0); s.op("probe"); s.op(f"valid,{p.gun.ref}"); w.bind(vw.FOREIGN, 1)
    # the weapon tree: too deep, too many, a sibling ring, a child that is the rig, a child whose slot holds another object
    s.op("probe")
    chain = [p.lens]
    for _ in range(9):
        chain.append(w.unit([255], parent=chain[-1].obj)); w.children(chain[-2], [chain[-1]])
        s.op("probe")
    w.children(chain[3], []); s.op("probe"); s.op("unchanged")
    many = [w.unit([255], parent=p.magazine.obj) for _ in range(30)]
    for count in (20, 24, 25, 30):
        w.children(p.magazine, many[:count]); s.op("probe")
    w.children(p.magazine, many[:3]); w.ptr(many[2].obj + 0x1E0, many[0].obj); s.op("probe")
    w.ptr(many[2].obj + 0x1E0, 0x1000); s.op("probe"); w.ptr(many[2].obj + 0x1E0, 2 ** 47); s.op("probe")
    w.ptr(many[2].obj + 0x1E0, 65536); s.op("probe"); w.ptr(many[2].obj + 0x1E0, 65535); s.op("probe"); w.ptr(many[2].obj + 0x1E0, 2 ** 47 - 1); s.op("probe")
    w.ptr(many[2].obj + 0x1E0, p.rig.obj); s.op("probe"); w.ptr(many[2].obj + 0x1E0, p.optic.obj); s.op("probe")
    w.ptr(many[2].obj + 0x1E0, 0); w.u32(many[1].obj + 8, 0); s.op("probe"); w.u32(many[1].obj + 8, many[0].ref); s.op("probe")
    w.u32(many[1].obj + 8, many[1].ref); w.children(p.magazine, []); s.op("probe"); s.op("unchanged")
    # a leaf that was empty gets a child: only the exact link comparison sees it
    w.ptr(p.lens.obj + 0x1D8, many[5].obj); s.op("unchanged"); w.ptr(p.lens.obj + 0x1D8, 0); s.op("unchanged")
    w.ptr(p.arms[0].obj + 0x1D8, many[5].obj); s.op("unchanged"); w.ptr(p.arms[0].obj + 0x1D8, 0); s.op("unchanged")
    # a part that names the weapon itself as its child; a snapshot without its identity text
    w.ptr(p.lens.obj + 0x1D8, p.gun.obj); s.op("probe"); w.ptr(p.lens.obj + 0x1D8, p.optic.obj); s.op("probe"); w.ptr(p.lens.obj + 0x1D8, 0); s.op("probe")
    w.text("s_identity", None); s.op("probe"); s.op("unchanged"); w.text("s_identity", b"player-A/gun"); s.op("probe")
    for fail in (1, 9, 2):                                    # no snapshot at all, with and without a text
        w.var("full_fail", fail); s.op("probe"); s.op(f"valid,{p.gun.ref}"); s.op("unchanged")
    w.var("full_fail", 0); s.op("probe")
    w.text("s_pid", b""); s.op("probe"); s.at(1, lambda: w.text("s_pid", None)); s.op("probe")   # an empty identity is not a missing one
    w.text("s_pid", None); s.at(1, lambda: w.text("s_pid", b"")); s.op("probe"); w.text("s_pid", b"player-A"); s.op("probe")
    # the rig and the body are one object; another world; the registry
    w.var("s_body", p.rig.ref); s.op("probe"); w.var("s_body", p.body.ref)
    w.ptr(w.objects + p.body.index * 8, p.rig.obj); s.op("probe"); w.ptr(w.objects + p.body.index * 8, p.body.obj)
    w.ptr(p.body.obj + 0x18, w.world_native + 32); s.op("probe"); w.ptr(p.body.obj + 0x18, w.world_native)
    w.u32(w.registry + 0x98, 2); s.op("probe"); w.u32(w.registry + 0x98, w.capacity)
    w.ptr(w.registry + 0xA0, 0); s.op("probe"); w.ptr(w.registry + 0xA0, w.generations)
    w.ptr(vw.EXE + 0x1ab76f8, 5); s.op("probe"); w.ptr(vw.EXE + 0x1ab76f8, w.registry); s.op("probe"); s.op("unchanged")
    return s


def sweeps(which):
    w, p = standard()
    s = Script(w, "sweep_" + which, "model")
    s.op("probe")
    every = changes(w, p)
    if which == "probe":
        sweep(s, "probe", every)
    elif which == "unchanged":
        sweep(s, "unchanged", every)
    else:
        sweep(s, f"valid,{p.lens.ref}", every); sweep(s, f"valid,{p.arms[0].ref}", every[:20], stride=3)
    return s


def bare():
    """A weapon without parts, no arm pieces, no descriptor function: the smallest inventory."""
    w, p = standard(arms=False, tree=False, backpack=False, rig_first=True)
    s = Script(w, "bare", "model", full=1)
    w.bind(vw.DESCRIPTOR, 0)
    s.op("probe"); s.op("unchanged"); s.op(f"valid,{p.gun.ref}"); s.op(f"valid,{p.rig.ref}")
    w.bind(vw.DESCRIPTOR, 1); s.op("probe"); s.op("unchanged"); s.op(f"valid,{p.gun.ref}")
    w.bind(vw.DESCRIPTOR, 0); s.op("unchanged"); w.bind(vw.DESCRIPTOR, 1); s.op("unchanged")   # the descriptor function is gone after the observation
    sweep(s, "probe", changes_bare(w, p))
    return s


def changes_bare(w, p):
    return [lambda: w.var("s_kind", 4), lambda: w.retire(p.gun), lambda: w.retire(p.rig), lambda: w.var("full_fail", 1),
            lambda: w.unreadable(p.gun.obj, p.gun.obj + 0x400), lambda: w.ptr(p.gun.obj + 0x1D8, p.body.obj)]


def walk(seed, count):
    """Seeded: a session of observations with the player changing weapons and arm pieces, and now and then
    something moving in the middle of a call."""
    rng = random.Random(seed)
    w, p = standard(layers=2)
    s = Script(w, f"walk{seed}", "model")
    every = changes(w, p)
    spare = [w.unit([255], parent=p.gun.obj) for _ in range(6)]
    known = [p.gun, p.magazine, p.optic, p.lens] + p.arms + spare
    for _ in range(count):
        roll = rng.random()
        if roll < 0.05:                                       # another set of weapon parts
            kids = rng.sample([p.magazine, p.optic] + spare, rng.randint(0, 4))
            w.children(p.gun, kids)
            if p.optic in kids and rng.random() < 0.7: w.children(p.optic, [p.lens])
        elif roll < 0.08:
            w.slot(rng.randint(0, 2), rng.randint(6, 9), rng.choice([0, p.arms[0].ref, p.arms[1].ref, spare[0].ref, p.limbs[0].ref, 4294967295]))
        elif roll < 0.10:
            w.var("s_record", 0x1F0B2200 + rng.randint(0, 3) * 0xe8)
        elif roll < 0.12:
            w.var("tick", w.vars["tick"] + rng.randint(1, 50))
        glitch = rng.random() < 0.12
        call = rng.choices(["probe", "unchanged", "valid", "has", "probe,1", "unchanged,1"], [30, 40, 20, 2, 3, 5])[0]
        if call == "valid": call = f"valid,{rng.choice(known).ref}" + (f",{rng.randint(1, 9)}" if rng.random() < 0.15 else "")
        if glitch:
            w.record(); s.at(rng.randint(1, 140), rng.choice(every))
        s.op(call)
        if glitch: w.undo()
    return s


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--script", type=Path, help="also write the script text here (for the coverage review)")
    args = parser.parse_args()
    RUNNER = HERE / "run_visibility.lua"
    vw.configure(args.retail, RUNNER)
    commit = vw.frozen_commit(args.retail)
    scripts = [basics(), sweeps("probe"), sweeps("unchanged"), sweeps("valid"), bare(), walk(1, 2500), walk(2, 2500)]
    lines = vw.run_reference(args.retail, scripts, RUNNER)
    if args.script:
        args.script.write_text("".join(script.header() + "\n" + "".join(op + "\n" for op in script.ops) for script in scripts))
    for script in scripts:
        print(f"{script.name}: {len(script.ops)} operations")
    raised = [line for line in lines if " RAISED=" in line]
    if raised:
        sys.exit("the reference raised:\n" + "\n".join(raised[:5]))
    vw.write_header(args.out / "model_shadow_source_vectors.h", "gen_model_shadow_source.py", "model_shadow_source", args.retail,
                    ["model_shadow_source.lua"], scripts, lines, commit)
    print(f"model shadow source: {len(lines)} operations in {len(scripts)} runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
