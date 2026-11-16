#!/usr/bin/env python3
"""Cut reference vectors from the retail head_visibility.lua (with the retail model_shadow_source.lua
behind it), run unmodified under LuaJIT against a scripted engine, backend, options and clock.

  gen_head_visibility.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import argparse
import random
import sys
from pathlib import Path

import visibility_world as vw
from visibility_world import Script, standard

HERE = Path(__file__).resolve().parent
OTHER_RESOURCE = b"\x53\x85\xbd\xa2\xbd\xc9\x3e\x5f"


class Head(Script):
    """A head-visibility run: `go()` is one frame later and an update."""

    def __init__(self, world, name, **options):
        super().__init__(world, name, "head", **options)

    def tick(self, ms=16): self.w.var("tick", self.w.vars["tick"] + ms)

    def go(self, count=1, ms=16):
        for _ in range(count):
            self.tick(ms); self.op("update")

    def stop(self): self.tick(); self.op("stop")

    def toggle(self, name, value, back, frames=2):
        """A value changes for some updates and comes back."""
        self.w.var(name, value); self.go(frames); self.w.var(name, back); self.go(frames)


def session(name="session", **options):
    w, p = standard(layers=2)
    s = Head(w, name, full=1, **options)
    s.op("stop"); s.go(4); s.stop(); s.stop(); s.go(3)
    w.var("s_aiming", 1); s.go(2); w.var("s_aiming", 0); s.go(2)          # a new epoch: full observation, nothing to write
    s.op("shutdown"); s.go(2); s.op("shutdown")
    return s


def gates():
    """Everything that decides whether the mode shows first person at all."""
    w, p = standard()
    s = Head(w, "gates", full=1)
    s.go(2)
    for name, value, back in (("enabled", 0, 1), ("mode", 0, 1), ("foreign", 1, 0), ("foreign", 2, 0), ("foreign", 3, 0), ("s_kind", 7, 6),
                              ("s_kind", 5, 6), ("s_seated", 1, 0), ("s_safe", 0, 1), ("pin_fail", 1, 0), ("pin_fail", 9, 0),
                              ("player_fail", 1, 0), ("pinned", 1, 0)):
        s.toggle(name, value, back)
    w.var("s_safe", 0); s.toggle("s_wt", 1, 0); s.toggle("s_hct", 1, 0); w.var("s_safe", 1); s.go(2)
    w.text("s_pid", b""); s.go(2); w.text("s_pid", None); s.go(2); w.text("s_pid", b"player-A"); s.go(2)
    for flags in (bytes(23), bytes(25), None, bytes(23) + b"\x04"):                       # the steady path needs the 24 flag bytes
        w.text("s_flags", flags); s.go(3)
    w.text("s_flags", bytes(24)); s.go(3)
    w.var("full_body", 1); s.go(3); w.var("full_body", 0); s.go(3)                        # the observation of the frame before is still held
    w.var("pin_fail", 1); s.toggle("player_fail", 1, 0); s.toggle("player_fail", 9, 0)
    w.bind(vw.PLAYER_SNAPSHOT, 0); s.go(2); w.bind(vw.PLAYER_SNAPSHOT, 1); w.var("pin_fail", 0); s.go(2)
    for code in (vw.PIN_SNAPSHOT, vw.PINNED, vw.FOREIGN, vw.BACKPACK, vw.FALLBACK, vw.SEATED_HEAD, vw.FULL_BODY, vw.HIDE_TORSO, vw.SHOW_TORSO):
        w.bind(code, 0); s.go(2); w.bind(code, 2 if code in (vw.PIN_SNAPSHOT, vw.FOREIGN, vw.BACKPACK) else 1); s.go(2); w.bind(code, 1); s.go(2)
    w.var("pinned", 1); s.toggle("s_kind", 7, 6); s.toggle("s_safe", 0, 1); w.var("pinned", 0); s.go(2)
    w.bind(vw.PINNED, 0); s.toggle("s_kind", 7, 6); w.var("s_kind", 4); s.toggle("fallback", 0, 1); w.var("s_kind", 6); w.bind(vw.PINNED, 1); s.go(2)
    # the head-camera gap view (kind 4): needs the fallback, the action state and the on-foot test
    w.var("s_kind", 4); s.go(3)
    for name, value, back in (("fallback", 0, 1), ("fallback", 2, 1), ("act_mode", 0, 1), ("act_mode", 2, 1), ("act_mode", 3, 1), ("act_mode", 4, 1),
                              ("on_foot", 0, 1), ("on_foot", 2, 1), ("s_safe", 0, 1)):
        s.toggle(name, value, back)
    w.bind(vw.FALLBACK, 0); s.go(2); w.bind(vw.FALLBACK, 1); s.go(2)
    w.var("s_safe", 0); s.toggle("s_wt", 1, 0); s.toggle("s_hct", 1, 0); w.var("s_safe", 1)
    w.var("s_seated", 1); s.go(2); s.toggle("seated_head", 0, 1); w.bind(vw.SEATED_HEAD, 0); s.go(2); w.bind(vw.SEATED_HEAD, 1); s.go(2)
    s.toggle("on_foot", 0, 1); w.var("s_seated", 0); s.go(2)
    w.var("s_kind", 6); s.go(3); w.var("s_kind", 4); s.go(2); w.var("s_kind", 6); s.go(2)
    # another player takes the seat
    w.text("s_pid", b"player-B"); w.text("s_identity", b"player-B/gun"); s.go(3)
    w.var("fail_fn", vw.SET); w.var("fail_mode", 1); w.text("s_pid", b"player-A"); w.text("s_identity", b"player-A/gun"); s.go(2)
    w.var("fail_fn", 0); s.go(3); s.stop()
    return s


def armour():
    """Clothing changes in the middle of a session; SDK handles that are missing, late, doubled or tagged."""
    w, p = standard(layers=2)
    s = Head(w, "armour")
    s.go(3)
    helmet = w.unit([255, 255, 2], parent=p.body.obj, listed=False)
    w.slot(0, 0, helmet.ref); s.go(2); s.go(1, 100); s.go(1, 300); s.go(1, 300)          # no handle yet: one enumeration per half second
    w.var("tick", 200000); s.op("update"); w.var("tick", 200499); s.op("update"); w.var("tick", 200500); s.op("update")   # exactly half a second
    w.set_units(w.units_list + [helmet.handle]); s.go(1, 100); s.go(1, 600); s.go(3)
    w.var("tick", w.vars["tick"] - 5000); s.go(2)                                        # the clock steps back
    hood = w.unit([255], parent=p.body.obj, listed=False); w.slot(1, 0, hood.ref); s.go(1); w.var("tick", w.vars["tick"] - 100); s.go(2)
    w.set_units(w.units_list + [hood.handle]); s.go(1, 600); s.go(2)
    for bad in (float("nan"), float("inf"), float("-inf")):                               # the clock is asked when a handle is missing
        cap = w.unit([255], parent=p.body.obj, listed=False); w.slot(2, 0, cap.ref)
        good = w.vars["tick"]; w.var("tick", bad); s.op("update"); s.op("update"); w.var("tick", good)
        w.set_units(w.units_list + [cap.handle]); s.go(1, 600); s.go(1)
    w.slot(2, 0, 0); s.go(2)
    # a handle listed twice (fatal for clothing, tolerated for the rig), a list that fails, an answer that is no list
    vest = w.unit([255, 255], parent=p.body.obj, listed=False); w.slot(1, 2, vest.ref)
    w.set_units(w.units_list + [vest.handle, vest.handle]); s.go(1, 600); s.go(1, 600)
    w.set_units(w.units_list[:-1]); s.go(1, 600); s.go(2)
    sash = w.unit([255], parent=p.body.obj, listed=False); w.slot(2, 2, sash.ref)
    w.set_units(w.units_list + [p.rig.handle]); s.go(1, 600); s.go(1, 600)
    w.set_units(w.units_list + [sash.handle, p.rig.handle]); s.go(1, 600); s.go(2); s.go(1, 600); s.go(2)
    w.set_units([h for h in w.units_list if h != p.rig.handle] + [p.rig.handle]); glove = w.unit([255], parent=p.body.obj, listed=False)
    w.slot(2, 8, glove.ref); w.set_units(w.units_list + [glove.handle]); s.go(1, 600); s.go(3)
    for mode in (1, 3):
        scarf = w.unit([255], parent=p.body.obj); w.slot(2, 9, scarf.ref)
        w.var("fail_fn", vw.UNITS); w.var("fail_mode", mode); s.go(1, 600); s.go(1, 600); w.var("fail_fn", 0); s.go(1, 600); s.go(2)
    tagged = w.unit([255, 255], parent=p.body.obj, tag=3); w.slot(2, 9, tagged.ref); s.go(1, 600); s.go(2)
    # meshes: none, too many, reordered, replaced, the same one twice, a new array
    empty = w.unit([], parent=p.body.obj); w.slot(0, 8, empty.ref); s.go(1, 600); s.go(2)
    w.ptr(empty.obj + 0xD0, 0); s.go(2); s.stop(); s.go(2)                               # no meshes and no array
    huge = w.unit([255] * 129, parent=p.body.obj); w.slot(0, 8, huge.ref); s.go(1, 600); s.go(2)
    most = w.unit([255] * 128, parent=p.body.obj); w.slot(0, 8, most.ref); s.go(1, 600); s.go(2)
    wide = w.unit([255] * 40 + [2, 0, 254, 253, 3, 1], parent=p.body.obj); w.slot(0, 8, wide.ref); s.go(1, 600); s.go(2)
    a, b = wide.meshes[0], wide.meshes[1]
    w.ptr(wide.array, b); w.ptr(wide.array + 16, a); s.go(3)                              # two meshes trade places
    fresh = w.alloc(0x100); w.u8(fresh + 0xDC, 255); w.canon(fresh + 0xDC, 255); w.ptr(wide.array + 32, fresh); s.go(3)
    w.ptr(wide.array + 48, fresh); s.go(2); w.ptr(wide.array + 48, wide.meshes[3]); s.go(2)
    moved = w.alloc(46 * 16); w.poke(moved, w.peek(wide.array, 46 * 16)); w.ptr(wide.obj + 0xD0, moved); s.go(3)
    w.u32(wide.obj + 0xC8, 45); s.go(2); w.u32(wide.obj + 0xC8, 46); s.go(2); w.u32(wide.obj + 0xC8, 0); s.go(2); w.u32(wide.obj + 0xC8, 46); s.go(2)
    # the registry: retired and reused generations, a self reference that lies, another renderer type, an index past the end
    w.retire(wide); s.go(2); w.revive(wide); s.go(2)
    w.u32(wide.obj + 8, wide.ref + 1); s.go(2); w.u32(wide.obj + 8, wide.ref); s.go(2)
    other = w.new_vt(good=False); w.ptr(wide.obj, other); s.go(2); w.ptr(wide.obj, w.new_vt()); s.go(2); w.ptr(wide.obj, 12); s.go(2); w.ptr(wide.obj, w.vt); s.go(2)
    w.u32(w.registry + 0x98, 3); s.go(2); w.u32(w.registry + 0x98, 0); s.go(2); w.u32(w.registry + 0x98, 0x400001); s.go(2)
    w.u32(w.registry + 0x98, 1); s.go(2); w.u32(w.registry + 0x98, 0x400000); s.go(2)
    w.u32(w.registry + 0x98, w.capacity); s.go(2)
    w.retire(p.body); s.go(2); w.revive(p.body); s.go(2)
    # clothing leaves; the wearer takes everything off
    w.slot(0, 8, 0); s.go(2)
    for layer in range(3):
        for slot in (0, 2, 8, 9): w.slot(layer, slot, 0)
    s.go(3); s.stop()
    return s


def refusals():
    """Appearance tables the module refuses, and what it keeps hidden meanwhile."""
    w, p = standard()
    s = Head(w, "refusals")
    s.go(2)
    def trial(change, back, frames=2):
        change(); s.go(frames); back(); s.go(frames)
    trial(lambda: w.slot(1, 0, p.body.ref), lambda: w.slot(1, 0, 0))                     # clothing that is the body, the rig
    trial(lambda: w.slot(1, 2, p.rig.ref), lambda: w.slot(1, 2, 0))
    trial(lambda: w.slot(1, 2, p.head.ref), lambda: w.slot(1, 2, 0))                     # one renderer as head and torso
    trial(lambda: w.slot(1, 8, p.torso.ref), lambda: w.slot(1, 8, 0))
    trial(lambda: w.slot(1, 3, p.torso.ref), lambda: w.slot(1, 3, 0))                    # a hidden renderer that is also a kept limb
    trial(lambda: w.slot(2, 7, p.head.ref), lambda: w.slot(2, 7, 0))
    trial(lambda: w.var("pack_ref", p.head.ref), lambda: w.var("pack_ref", p.pack.ref))  # the backpack is a piece of clothing, the body, the rig
    trial(lambda: (w.var("pack_ref", p.torso.ref), w.text("pack_id", b"7:torso")), lambda: (w.var("pack_ref", p.pack.ref), w.text("pack_id", b"7:pack")))
    trial(lambda: w.var("pack_ref", p.body.ref), lambda: w.var("pack_ref", p.pack.ref))
    trial(lambda: w.var("pack_ref", p.rig.ref), lambda: w.var("pack_ref", p.pack.ref))
    trial(lambda: w.var("pack_ref", p.limbs[0].ref), lambda: w.var("pack_ref", p.pack.ref))
    trial(lambda: w.var("pack_mode", 2), lambda: w.var("pack_mode", 1))
    trial(lambda: w.var("pack_mode", 0), lambda: w.var("pack_mode", 1))
    trial(lambda: w.text("pack_id", b"8:other"), lambda: w.text("pack_id", b"7:pack"))
    trial(lambda: w.text("pack_id", None), lambda: w.text("pack_id", b"7:pack"))
    trial(lambda: w.slot(1, 8, p.arms[0].ref), lambda: w.slot(1, 8, 0))                  # an arm piece on the rig in a shoulder slot
    trial(lambda: w.slot(1, 9, p.gun.ref), lambda: w.slot(1, 9, 0))
    trial(lambda: w.ptr(p.head.obj + 0x1D0, p.rig.obj), lambda: w.ptr(p.head.obj + 0x1D0, p.body.obj))   # attachments
    trial(lambda: w.ptr(p.pack.obj + 0x1D0, 0), lambda: w.ptr(p.pack.obj + 0x1D0, p.body.obj))
    trial(lambda: w.ptr(p.limbs[1].obj + 0x1D0, p.gun.obj), lambda: w.ptr(p.limbs[1].obj + 0x1D0, p.body.obj))
    trial(lambda: w.ptr(p.limbs[1].obj + 0x1D0, p.rig.obj), lambda: w.ptr(p.limbs[1].obj + 0x1D0, p.body.obj))
    trial(lambda: w.ptr(p.limbs[2].obj + 0x18, w.world_native + 64), lambda: w.ptr(p.limbs[2].obj + 0x18, w.world_native))
    trial(lambda: w.ptr(p.torso.obj + 0x18, w.world_native + 64), lambda: w.ptr(p.torso.obj + 0x18, w.world_native))
    trial(lambda: w.retire(p.limbs[0]), lambda: w.revive(p.limbs[0]))
    trial(lambda: w.slot(2, 4, 0xFFFFFFFE), lambda: w.slot(2, 4, 0))
    trial(lambda: w.slot(2, 4, 4294967295), lambda: w.slot(2, 4, 0))
    trial(lambda: w.slot(2, 0, 4294967295), lambda: w.slot(2, 0, 0))
    trial(lambda: w.ptr(p.limbs[0].array, p.torso.meshes[0]), lambda: w.ptr(p.limbs[0].array, p.limbs[0].meshes[0]))   # a mesh in two renderers
    trial(lambda: w.ptr(p.arms[0].array, p.gun.meshes[0]), lambda: w.ptr(p.arms[0].array, p.arms[0].meshes[0]))
    trial(lambda: w.ptr(p.limbs[0].array, p.gun.meshes[0]), lambda: w.ptr(p.limbs[0].array, p.limbs[0].meshes[0]))
    trial(lambda: w.ptr(p.limbs[0].array, p.limbs[0].ref), lambda: w.ptr(p.limbs[0].array, p.limbs[0].meshes[0]))    # a mesh address equal to a unit reference
    trial(lambda: w.ptr(p.limbs[0].array, p.body.meshes[0]), lambda: w.ptr(p.limbs[0].array, p.limbs[0].meshes[0]))  # a body mesh in a limb
    for value in (65536, 65535, 2 ** 47, 2 ** 47 - 1):                                   # the edges of what counts as a pointer
        trial(lambda: w.ptr(p.head.obj + 0x1D0, value), lambda: w.ptr(p.head.obj + 0x1D0, p.body.obj))
    for value in (2 ** 47 - 16, 2 ** 47 - 33, 65536):                                    # an object too close to the end of memory to read
        trial(lambda: w.ptr(w.objects + p.head.index * 8, value), lambda: w.ptr(w.objects + p.head.index * 8, p.head.obj))
    for owner in (float("nan"), w.owner + 0.5):
        trial(lambda: w.var("s_avatar_owner", owner), lambda: w.var("s_avatar_owner", w.owner))
    for index in (8, -1, 1, 7.5):                                                        # the avatar row
        trial(lambda: w.var("s_avatar_index", index), lambda: w.var("s_avatar_index", 0))
    # the world: no world, another native world, another SDK world, a call that fails
    trial(lambda: w.var("world_handle", 0), lambda: w.var("world_handle", w.world_handle))
    other = w.alloc(16); w.ptr(other, w.world_native + 64)
    trial(lambda: w.var("world_handle", other), lambda: w.var("world_handle", w.world_handle))
    twin = w.alloc(16); w.ptr(twin, w.world_native)
    trial(lambda: w.var("world_handle", twin), lambda: w.var("world_handle", w.world_handle), 3)
    trial(lambda: w.ptr(w.world_handle, 7), lambda: w.ptr(w.world_handle, w.world_native))
    trial(lambda: w.move_world(w.world_native + 64), lambda: w.move_world(w.world_native), 3)   # the same SDK world, another native one
    trial(lambda: w.unreadable(w.world_handle, w.world_handle + 8), w.readable)
    for mode in (1, 3):
        w.var("fail_fn", vw.MAIN_WORLD); w.var("fail_mode", mode); s.go(2); w.var("fail_fn", 0); s.go(2)
    # the executable: each pin, the binding's home, a new game base
    for at in (0x3f4ca0, 0x51fe80, 0x3f4c23, 0x51feaa, 0x2875ac0, 0x2875ac8, 0xa06d0):
        keep = w.peek(vw.EXE + at, 1)
        w.poke(vw.EXE + at, bytes([keep[0] ^ 1])); w.var("s_base", w.vars["s_base"] + 0x10000); s.go(2)
        w.poke(vw.EXE + at, keep); s.go(2); s.stop()
    trial(lambda: (w.var("exe_fail", 1), w.var("s_base", w.vars["s_base"] + 0x10000)), lambda: w.var("exe_fail", 0))
    trial(lambda: w.var("s_base", w.vars["s_base"] + 0x10000), lambda: None, 3)
    trial(lambda: w.ptr(vw.EXE + 0x1ab76f8, 9), lambda: w.ptr(vw.EXE + 0x1ab76f8, w.registry))
    trial(lambda: w.unreadable(w.custom(), w.custom() + 0x200), w.readable)
    trial(lambda: w.unreadable(p.head.meshes[0], p.head.meshes[0] + 0x100), w.readable)
    trial(lambda: w.unreadable(p.rig.obj, p.rig.obj + 0x400), w.readable)
    trial(lambda: w.unreadable(p.body.obj, p.body.obj + 0x400), w.readable)
    s.stop()
    return s


def edges():
    """The avatar owner at the ends of readable memory (retail layout only: the rows of another build start elsewhere)."""
    w, p = standard()
    s = Head(w, "edges", full=1)
    s.go(2)
    for owner in (-0x600000, 2 ** 47, 2 ** 47 - 0x546b8c - 100, 2 ** 47 - 0x546b8c - 120, 2 ** 47 - 0x546b8c - 121, 65536 - 0x546b8c, 65535 - 0x546b8c):
        w.var("s_avatar_owner", owner); s.go(2); w.var("s_avatar_owner", w.owner); s.go(2)
    s.stop()
    return s


def rig():
    """The first-person rig: its two shoulder pads by name, layouts that are not the known rig, queries that fail."""
    w, p = standard()
    s = Head(w, "rig")
    s.go(3)
    left, right = vw.PADS[3], vw.PADS[4]
    def trial(change, back, frames=2, wait=16):
        change(); s.go(frames, wait); back(); s.go(1, 600); s.go(frames)
        change(); s.stop(); s.go(frames, wait); s.go(1, 600); back(); s.stop(); s.go(1, 600); s.go(frames)   # the same, leaving in between
        s.stop(); w.reset_masks(); s.go(2)                   # a pad that could not be identified on the way out stays hidden: show it again
    trial(lambda: w.no_rig_mesh(left[0]), lambda: w.rig_mesh(left[0], p.rig.meshes[3], left[1], left[2]))
    for hash_, node, materials, material in ((left[1] + 1, left[2], 1, 1), (left[1], left[2] + 1, 1, 1), (left[1], left[2], 2, 1),
                                             (left[1], left[2], 1, 0), (left[1] + 2 ** 32, left[2], 1, 1), (left[1] - 2 ** 32, left[2], 1, 1),
                                             (float("nan"), left[2], 1, 1), (float("inf"), left[2], 1, 1), (left[1], float("nan"), 1, 1)):
        trial(lambda: w.rig_mesh(left[0], p.rig.meshes[3], hash_, node, materials, material),
              lambda: w.rig_mesh(left[0], p.rig.meshes[3], left[1], left[2]), 2, 600)
    trial(lambda: w.rig_mesh(right[0], p.rig.meshes[3], right[1], right[2]), lambda: w.rig_mesh(right[0], p.rig.meshes[4], right[1], right[2]))
    trial(lambda: w.rig_mesh(right[0], p.gun.meshes[0], right[1], right[2]), lambda: w.rig_mesh(right[0], p.rig.meshes[4], right[1], right[2]))
    for at in (4096, 65535, 65536, 2 ** 47 - 1, 2 ** 47):       # a mesh handle whose address is no mesh address
        trial(lambda: w.rig_mesh(right[0], at, right[1], right[2]), lambda: w.rig_mesh(right[0], p.rig.meshes[4], right[1], right[2]))
    trial(lambda: w.rig_mesh(right[0], p.rig.meshes[4], right[1], right[2], unit=p.gun.handle), lambda: w.rig_mesh(right[0], p.rig.meshes[4], right[1], right[2]))
    for code in (vw.HAS_MESH, vw.MESH, vw.MESH_NAME, vw.MESH_NODE, vw.MESH_MATERIALS, vw.MESH_HAS_MATERIAL):
        for mode in (1, 3):
            trial(lambda: (w.var("fail_fn", code), w.var("fail_mode", mode)), lambda: w.var("fail_fn", 0), 2, 300)
        trial(lambda: w.bind(code, 0), lambda: w.bind(code, 1), 2, 300)
        trial(lambda: w.bind(code, 2), lambda: w.bind(code, 1))
        # the function disappears while pads are owned: the stop cannot identify them
        s.stop(); s.go(2); w.bind(code, 0); s.stop(); s.stop(); w.bind(code, 1); s.stop(); s.go(2)
    trial(lambda: w.u32(p.rig.obj + 0xC8, 4), lambda: w.u32(p.rig.obj + 0xC8, 5), 2, 300)          # not five meshes
    trial(lambda: w.poke(p.rig.obj + 0x358, OTHER_RESOURCE), lambda: w.poke(p.rig.obj + 0x358, vw.RIG_RESOURCE), 2, 300)   # another armor's rig
    trial(lambda: w.retire(p.rig), lambda: w.revive(p.rig), 2, 300)
    trial(lambda: w.var("s_viewmodel", -1), lambda: w.var("s_viewmodel", p.rig.ref))
    trial(lambda: w.var("s_viewmodel", 0), lambda: w.var("s_viewmodel", p.rig.ref))
    trial(lambda: w.var("s_viewmodel", p.rig.ref + 0.5), lambda: w.var("s_viewmodel", p.rig.ref))
    trial(lambda: w.var("s_viewmodel", p.body.ref), lambda: w.var("s_viewmodel", p.rig.ref))
    trial(lambda: w.var("s_viewmodel", 4294967295), lambda: w.var("s_viewmodel", p.rig.ref))
    trial(lambda: w.unlist(p.rig), lambda: w.set_units(w.units_list + [p.rig.handle]), 2, 300)
    # a failed rig is looked at again half a second later, not sooner
    w.var("fail_fn", vw.MESH_NODE); w.var("fail_mode", 1)
    for wait in (16, 100, 100, 100, 100, 100, 100, 16, 600, 16): s.go(1, wait)
    w.var("fail_fn", 0)
    for wait in (16, 100, 100, 100, 100, 100, 16): s.go(1, wait)
    s.stop(); w.var("fail_fn", vw.MESH_NODE); s.go(1); s.go(1, 0); s.go(1, 0); s.go(1, 499); s.go(1, 1); s.go(1, 0)   # the edges of that half second
    w.var("fail_fn", 0); s.stop(); w.var("fail_fn", vw.MESH_NODE); s.go(1); w.var("tick", w.vars["tick"] - 700); s.go(1, 0); s.go(1, 100)   # and a clock that steps back
    w.var("fail_fn", 0); s.go(1, 600); s.go(2)
    # the same on round ticks, where the half second is exact: 400.0 fails, 400.499 waits, 400.5 looks again; and 399.7 after 400.0
    s.stop(); w.var("fail_fn", vw.MESH_NODE)
    for tick in (400000, 400000, 400499, 400500, 400500, 400999, 401000, 402000, 401700, 401700, 402300):
        w.var("tick", tick); s.op("update")
    w.var("fail_fn", 0); s.go(1, 600); s.go(2)
    # the gap view keeps the pads hidden with their shadow; back in the rig view they lose it again
    w.var("s_kind", 4); s.go(3); w.var("s_kind", 6); s.go(3); s.stop()
    return s


def torso():
    """The gap view (kind 4) and the show-torso option: only after 0.6 s of a rig-less, undisturbed state."""
    w, p = standard(kind=4, layers=2)
    s = Head(w, "torso")
    s.go(2); w.var("show_torso", 1); s.go(3, 100); s.go(4, 200); s.go(2)
    w.text("s_cbytes", bytes([0, 0, 0, 0, 1, 0])); s.go(2, 400)                           # byte 5 does not count
    for index in (0, 1, 2, 3, 5):
        raw = bytearray(6); raw[index] = 2
        w.text("s_cbytes", raw); s.go(2); w.text("s_cbytes", bytes(6)); s.go(2, 250); s.go(2, 250)
    w.text("s_cbytes", bytes(5)); s.go(2); w.text("s_cbytes", None); s.go(2); w.text("s_cbytes", bytes(7)); s.go(1, 700); s.go(2)
    w.text("s_cbytes", bytes(6)); s.go(1, 700); s.go(2)
    w.var("tick", w.vars["tick"] - 3000); s.go(1); s.go(1, 500); s.go(1, 200); s.go(2)   # the clock steps back
    w.var("show_torso", 0); s.go(2); w.var("show_torso", 1); s.go(2)
    w.text("s_cbytes", bytes([0, 0, 2, 0, 0, 0])); s.go(1); w.text("s_cbytes", bytes(6))   # exactly 0.6 s: tick 0, then tick 600
    w.var("tick", 0); s.op("update"); w.var("tick", 599); s.op("update"); w.var("tick", 600); s.op("update"); w.var("tick", 100000); s.go(2)
    w.var("s_kind", 6); s.go(3); w.var("s_kind", 4); s.go(2); s.go(1, 700); s.go(2)       # the rig view always hides the torso
    w.bind(vw.SHOW_TORSO, 0); s.go(2); w.bind(vw.SHOW_TORSO, 1); s.go(2)
    # full body (the parked third-person arms): only the head; with hide_torso the torso too, unless it shares a renderer with a limb
    w.var("full_body", 1); s.go(3); w.var("hide_torso", 1); s.go(3)
    w.slot(2, 8, p.torso.ref); s.go(2); w.slot(2, 8, 0); s.go(2)
    w.slot(2, 9, p.extra[1].ref); s.go(2); w.slot(2, 9, 0); s.go(2)
    w.var("pack_ref", p.torso.ref); w.text("pack_id", b"7:vest"); s.go(2); w.var("pack_ref", p.pack.ref); w.text("pack_id", b"7:pack"); s.go(2)
    # the kept backpack holds a torso mesh (that row is dropped, the rest stays), then a head mesh (refused)
    w.ptr(p.pack.array, p.extra[1].meshes[0]); w.text("pack_id", b"7:pack2"); s.go(3)
    w.ptr(p.pack.array, p.pack.meshes[0]); w.text("pack_id", b"7:pack"); s.go(2)
    w.ptr(p.pack.array, p.head.meshes[0]); w.text("pack_id", b"7:pack2"); s.go(3)
    w.ptr(p.pack.array, p.pack.meshes[0]); w.text("pack_id", b"7:pack"); s.go(2)
    w.var("s_kind", 6); s.go(3); w.var("hide_torso", 0); s.go(3); w.var("full_body", 0); s.go(3); w.var("hide_torso", 1); s.go(3)
    w.var("full_body", 1); s.go(2); w.var("s_kind", 4); s.go(2); w.var("full_body", 0); w.var("hide_torso", 0); s.go(3); s.stop()
    return s


def masks():
    """Pass bits changed by the game or by someone else while they are owned or wanted."""
    w, p = standard()
    s = Head(w, "masks")
    head, pad, gun, body = p.head.meshes[0], p.rig.meshes[3], p.gun.meshes[0], p.body.meshes[0]
    s.go(3)
    for value in (255, 0, 1, 254, 253, 2, 3, 252):
        for mesh in (head, pad, gun, body):
            w.u8(mesh + 0xDC, value); s.go(1, 400); s.go(2)                               # long after the epoch began: someone else
            w.var("s_aiming", 1 - w.vars["s_aiming"]); s.go(2)                            # a new epoch forgets the grudge
    for value in (255, 0, 1, 254):
        for mesh in (head, pad, gun, body):
            w.var("s_record", w.vars["s_record"] + 0xe8); s.go(1)                         # a new epoch: the game republishes inside 0.25 s
            w.u8(mesh + 0xDC, value); s.go(1, 100); w.u8(mesh + 0xDC, value); s.go(1, 100); s.go(1, 100); s.go(2)
            w.u8(mesh + 0xDC, value); s.go(1)
    # every epoch field
    for name, value in (("s_head", 5), ("s_tail", 9), ("s_request_weapon", 78), ("s_active", 1), ("s_aiming", 1), ("s_record", 0x1F0B9900)):
        w.u8(head + 0xDC, 255); s.go(2, 400); w.var(name, value); s.go(3)
    w.var("s_request_weapon", 77); s.go(2)
    # A mesh the game hides itself (mask 2) is remembered while it owns no bit: when the game shows it in the very
    # frame the weapon tree, the clothing slot or the SDK handle changes, it is not taken for a fresh baseline.
    extra = p.head.meshes[1]
    s.stop(); w.reset_masks(); s.go(3, 200)
    w.children(p.gun, [p.magazine]); w.u8(extra + 0xDC, 255); s.go(1, 400); s.go(2)
    w.children(p.gun, [p.magazine, p.optic]); s.go(2); w.var("s_aiming", 1 - w.vars["s_aiming"]); s.go(2)
    s.stop(); w.reset_masks(); s.go(3, 200)
    w.slot(0, 0, 0); w.slot(1, 2, p.head.ref); w.u8(extra + 0xDC, 255); s.go(1, 400); s.go(2)       # the same renderer is now torso
    w.slot(1, 2, 0); w.slot(0, 0, p.head.ref); s.go(2)
    s.stop(); w.reset_masks(); s.go(3, 200)
    patch = w.unit([255], parent=p.body.obj, listed=False); w.slot(2, 9, patch.ref)                # a new unit: handles are listed again,
    w.set_units([h + 2 if h == p.head.handle else h for h in w.units_list] + [patch.handle])       # and the head's handle is another one
    w.u8(extra + 0xDC, 255); s.go(1, 600); s.go(2); s.stop(); w.reset_masks()
    w.set_units([h - 2 if h == p.head.handle + 2 else h for h in w.units_list]); w.slot(2, 9, 0); s.go(1, 600); s.go(2)
    # the body's shadow was off: it is switched on and given back
    w.set_mask(p.body, 0, 253); w.set_mask(p.body, 1, 1); s.go(3); w.u8(body + 0xDC, 253); s.go(2, 400); s.stop(); s.go(2); s.stop()
    # meshes the game already hides, shows only in shadow, or shows not at all
    for i, value in enumerate((2, 0, 1, 254, 253, 3, 252, 255)):
        w.set_mask(p.head, 0, value); w.set_mask(p.rig, 3, value); w.set_mask(p.rig, 0, value); w.set_mask(p.gun, 1, value)
        s.go(3); s.stop(); s.go(2)
        w.var("s_kind", 4); s.go(2); s.stop(); w.var("s_kind", 6); s.go(2); s.stop()
    return s


def restores():
    """Leaving: what is put back, what the game has already put back, and what cannot be put back yet."""
    w, p = standard()
    s = Head(w, "restores")
    head, pad, gun = p.head.meshes[0], p.rig.meshes[3], p.gun.meshes[0]
    def owned(): s.go(3)
    owned(); w.var("s_kind", 4); s.stop(); w.var("s_kind", 6)                              # leaving through kind 4
    for value in (255, 3, 1, 253, 254, 0, 2):                                              # the game published its own masks on exit
        for kind, pid in ((6, b"player-A"), (4, b"player-A"), (4, b"player-B"), (7, b"player-A")):
            owned()
            for mesh in (head, pad, gun): w.u8(mesh + 0xDC, value)
            w.var("s_kind", kind); w.text("s_pid", pid); s.stop(); s.stop(); w.var("s_kind", 6); w.text("s_pid", b"player-A"); w.reset_masks()
    owned(); w.var("s_body", p.torso.ref); w.var("s_kind", 4); w.u8(gun + 0xDC, 3); s.stop(); w.var("s_body", p.body.ref); w.var("s_kind", 6); w.reset_masks()
    owned(); w.var("pin_fail", 1); w.var("player_fail", 1); w.u8(gun + 0xDC, 3); s.stop(); w.var("pin_fail", 0); w.var("player_fail", 0); w.reset_masks()
    # units and meshes that are gone
    for gone in (p.head, p.rig, p.gun, p.body, p.pack):
        owned(); w.retire(gone); s.stop(); s.stop(); w.revive(gone); s.stop(); w.reset_masks()
    owned(); w.u32(p.gun.obj + 0xC8, 1); s.stop(); w.u32(p.gun.obj + 0xC8, 2); s.stop(); w.reset_masks()
    owned(); w.ptr(p.head.obj + 0x18, w.world_native + 64); s.stop(); w.ptr(p.head.obj + 0x18, w.world_native); s.stop(); w.reset_masks()
    owned(); w.poke(p.rig.obj + 0x358, OTHER_RESOURCE); s.stop(); w.poke(p.rig.obj + 0x358, vw.RIG_RESOURCE); s.stop(); w.reset_masks()
    owned(); w.no_rig_mesh("g_l_shoulderpad"); s.stop(); w.rig_mesh("g_l_shoulderpad", p.rig.meshes[3], vw.PADS[3][1], vw.PADS[3][2]); s.stop(); w.reset_masks()
    owned(); w.set_units([p.head.handle + 4]); w.ptr(p.head.array, p.head.meshes[1]); w.ptr(p.head.array + 16, p.head.meshes[0]); s.stop()
    w.ptr(p.head.array, p.head.meshes[0]); w.ptr(p.head.array + 16, p.head.meshes[1]); w.reset_masks()
    # the setter fails, does nothing, or is another function now: restoration stays pending and is tried again
    for mode in (1, 2, 3):
        for nth in (0, 1, 2, 7, 23):
            owned(); w.var("fail_fn", vw.SET); w.var("fail_mode", mode); w.var("fail_nth", nth)
            s.stop(); s.go(1); s.stop(); w.var("fail_fn", 0); s.go(1); s.go(1); s.stop(); w.reset_masks()
    w.var("fail_nth", 0)
    for mode in (0, 2):
        owned(); w.bind(vw.SET, mode); s.stop(); s.go(2); s.op("shutdown"); w.bind(vw.SET, 1); s.go(2); s.stop(); w.reset_masks()
    owned(); w.unreadable(gun, gun + 0x100); s.stop(); s.go(1); w.readable(); s.go(1); s.stop(); w.reset_masks()
    owned(); w.unreadable(w.generations, w.generations + w.capacity); s.stop(); s.go(1); w.readable(); s.go(2); s.stop(); w.reset_masks()
    owned(); w.var("exe_fail", 1); s.stop(); w.var("exe_fail", 0)
    # hiding fails half way: everything written so far is given back in the same update
    for mode in (1, 2, 3):
        for nth in (1, 2, 5, 12, 24):
            w.var("fail_fn", vw.SET); w.var("fail_mode", mode); w.var("fail_nth", nth); s.go(1); w.var("fail_fn", 0); s.go(2); s.stop(); w.reset_masks()
    w.var("fail_nth", 0)
    for mode in (0, 2):
        w.bind(vw.SET, mode); s.go(2); w.bind(vw.SET, 1); s.go(2); w.bind(vw.SET, mode); s.go(2); s.stop(); w.bind(vw.SET, 1); s.go(1); s.stop(); w.reset_masks()
    # the first-person models stop being provable between observation and write
    owned(); s.stop(); w.var("full_fail", 1); s.go(2); w.var("full_fail", 0); s.go(2); s.stop()
    # closing: nothing is hidden again
    owned(); w.var("fail_fn", vw.SET); w.var("fail_mode", 1); s.op("shutdown"); s.go(1); w.var("fail_fn", 0); s.go(1); s.go(1); s.op("shutdown")
    return s


def bindings():
    """The steady path proves that nothing was rebound; any change sends the update down the full path."""
    w, p = standard()
    s = Head(w, "bindings")
    s.go(3)
    for code in range(1, 14):
        w.bind(code, 2); s.go(3); w.bind(code, 1); s.go(3)
    for code in (vw.MESH_NAME, vw.HAS_MESH, vw.BACKPACK, vw.PIN_SNAPSHOT):
        w.bind(code, 0); s.go(3); w.bind(code, 1); s.go(1, 600); s.go(3)
    s.stop(); w.bind(vw.SET, 2); s.go(3); s.stop(); w.bind(vw.SET, 1); s.go(3); s.stop()
    # The setter is another function while bits are owned, and the game has shown every owned mesh again: nothing is
    # left to give back, and the refusal itself is what gets reported.
    w.var("s_kind", 4); s.go(2); w.bind(vw.SET, 2)
    for unit in (p.head, p.torso, p.pack, p.rig) + tuple(p.shoulders):
        for mesh in unit.meshes: w.u8(mesh + 0xDC, 255)
    s.go(2); w.bind(vw.SET, 1); s.go(2); s.stop(); w.var("s_kind", 6); w.reset_masks(); s.go(2)
    for code in (vw.MAIN_WORLD, vw.UNITS, vw.SET):
        w.bind(code, 0); s.go(2); w.bind(code, 1); s.go(2)
    s.stop()
    return s


def handles():
    """The host's address of an engine handle moves (a handle that is no longer the unit, mesh or world it was)."""
    w, p = standard()
    s = Head(w, "handles")
    s.go(3)
    for bias in (4, 1, 3, 400, 2 ** 46, 2 ** 47, 2 ** 52):
        w.var("addr_bias", bias); s.go(2); s.go(1, 600); s.stop(); s.go(2); w.var("addr_bias", 0); s.stop(); s.go(1, 600); s.go(2)
    return s


def targeted():
    """Changes that arrive with one particular call: the SDK setter itself, a snapshot, a handle address, each
    other engine and option call."""
    w, p = standard()
    s = Head(w, "targeted")
    v = w.var
    sets = lambda text: text.startswith("set ")
    setter = [lambda: w.bind(vw.SET, 2), lambda: w.bind(vw.SET, 0), lambda: (v("fail_fn", vw.SET), v("fail_mode", 1)),
              lambda: (v("fail_fn", vw.SET), v("fail_mode", 2)), lambda: (v("fail_fn", vw.SET), v("fail_mode", 3)),
              lambda: w.u8(p.head.meshes[0] + 0xDC, 77), lambda: w.u8(p.rig.meshes[3] + 0xDC, 0), lambda: w.u8(p.gun.meshes[0] + 0xDC, 255),
              lambda: w.retire(p.rig), lambda: w.retire(p.body), lambda: v("foreign", 1), lambda: v("full_fail", 1), lambda: v("pack_mode", 0),
              lambda: w.slot(0, 0, 0), lambda: v("addr_bias", 4), lambda: w.no_rig_mesh("g_l_shoulderpad")]
    vw.at_each(s, "update", sets, setter, before=("stop",), reset=True)
    vw.at_each(s, "stop", sets, setter, before=("stop", "update"), reset=True)
    snapshots = lambda text: text in ("pin", "psnap", "snap")
    owner = [lambda: v("pin_fail", 1), lambda: (v("pin_fail", 1), w.bind(vw.PLAYER_SNAPSHOT, 0)), lambda: (v("pin_fail", 1), v("player_fail", 1)),
             lambda: (v("pin_fail", 1), v("player_fail", 9)), lambda: w.text("s_pid", b"player-B"), lambda: w.text("s_pid", b""),
             lambda: v("s_kind", 4), lambda: v("s_kind", 7), lambda: v("s_safe", 0), lambda: v("s_seated", 1), lambda: v("s_aiming", 1),
             lambda: w.text("s_flags", bytes(23) + b"\x02"), lambda: v("s_viewmodel", -1), lambda: v("s_body", p.limbs[0].ref),
             lambda: v("s_record", 0x1F0B2300), lambda: v("full_fail", 1), lambda: v("full_fail", 9), lambda: w.text("s_identity", b"x"),
             lambda: v("s_weapon_unit", p.optic.ref), lambda: v("foreign", 1), lambda: (v("s_kind", 4), v("fallback", 0))]
    vw.at_each(s, "update", snapshots, owner, before=("stop",), reset=True)
    vw.at_each(s, "update", snapshots, owner, before=("stop", "update"), reset=True)
    vw.at_each(s, "stop", snapshots, owner, before=("stop", "update"), reset=True)
    addresses = lambda text: text.startswith("A(")
    moved = [lambda: v("addr_bias", 4), lambda: v("addr_bias", 2 ** 47), lambda: v("addr_bias", 1)]
    vw.at_each(s, "update", addresses, moved, before=("stop",), reset=True, limit=70)
    vw.at_each(s, "update", addresses, moved, before=("stop", "update"), reset=True)
    vw.at_each(s, "stop", addresses, moved, before=("stop", "update"), reset=True, limit=30)
    calls = lambda text: text.split(" ")[0] in ("mw", "un", "hm", "me", "m6", "m7", "m8", "m9", "exe", "pack", "clk", "fc", "fbd", "ht", "pr", "cr", "ce")
    world = [lambda: v("tick", w.vars["tick"] + 1000), lambda: v("tick", w.vars["tick"] - 1000), lambda: w.set_units([]), lambda: v("world_handle", 0),
             lambda: w.retire(p.head), lambda: w.retire(p.gun), lambda: v("pack_mode", 2), lambda: v("foreign", 2), lambda: v("exe_fail", 1),
             lambda: w.u8(p.head.meshes[0] + 0xDC, 255), lambda: w.rig_mesh("g_r_shoulderpad", p.rig.meshes[4], 5, 58),
             lambda: w.bind(vw.MESH_NODE, 0), lambda: w.bind(vw.HAS_MESH, 2), lambda: v("enabled", 0), lambda: v("full_body", 1)]
    vw.at_each(s, "update", calls, world, before=("stop",), reset=True, limit=45)
    vw.at_each(s, "update", calls, world, before=("stop", "update"), reset=True)
    vw.at_each(s, "stop", calls, world, before=("stop", "update"), reset=True, limit=30)
    # An arm piece is both a shadow target and a kept limb. Its mesh array moves between the two looks at it, and
    # nothing has to be written to it: the frame is fine, but it is not remembered as steady.
    arm = p.arms[0]
    elsewhere = w.alloc(64); w.poke(elsewhere, w.peek(arm.array, 32)); w.set_mask(arm, 0, 253); w.set_mask(arm, 1, 253)
    vw.at_each(s, "update", lambda text: text == f"r({arm.obj + 0x1D0},8)", [lambda: w.ptr(arm.obj + 0xD0, elsewhere)], before=("stop",),
               settle=("update", "update"), reset=True)
    w.var("s_kind", 4)                                       # the gap view
    vw.at_each(s, "update", lambda text: snapshots(text) or text in ("fb", "act", "foot", "st", "sh"), owner + world[:8], before=("stop",), reset=True)
    return s


def steady_breaks(w, p):
    """Changes that must end the steady path (or pass it), one at a time."""
    v = w.var
    region = lambda unit: (unit.obj, unit.obj + 0x400)
    return [
        lambda: v("s_kind", 4), lambda: v("s_kind", 7), lambda: v("s_safe", 0), lambda: v("s_seated", 1), lambda: w.text("s_pid", b"player-B"),
        lambda: v("s_aiming", 1), lambda: v("s_active", 1), lambda: v("s_record", 0x1F0B2300), lambda: v("s_head", 9), lambda: v("s_tail", 9),
        lambda: v("s_request_weapon", 78), lambda: v("s_viewmodel", -1), lambda: v("s_viewmodel", p.gun.ref), lambda: v("s_body", p.torso.ref),
        lambda: w.text("s_flags", bytes(23) + b"\x01"), lambda: w.text("s_flags", bytes(23)), lambda: w.text("s_flags", None),
        lambda: v("s_avatar_index", 1), lambda: v("s_avatar_owner", w.owner + 0x1B8), lambda: v("s_base", w.vars["s_base"] + 0x10000),
        lambda: v("foreign", 1), lambda: v("foreign", 2), lambda: v("full_body", 1), lambda: v("hide_torso", 1), lambda: v("show_torso", 1),
        lambda: v("enabled", 0), lambda: v("mode", 0), lambda: v("pinned", 1), lambda: v("fallback", 0),
        lambda: v("pack_mode", 0), lambda: v("pack_mode", 2), lambda: v("pack_ref", p.head.ref), lambda: w.text("pack_id", b"9:x"),
        lambda: v("full_fail", 1), lambda: v("pin_fail", 1), lambda: (v("pin_fail", 1), v("player_fail", 1)), lambda: v("exe_fail", 1),
        lambda: v("tick", w.vars["tick"] + 1000), lambda: v("s_world", w.world_native + 64), lambda: v("desc_at", 0),
        lambda: v("world_handle", 0), lambda: w.ptr(w.world_handle, w.world_native + 64),
        lambda: w.retire(p.body), lambda: w.retire(p.head), lambda: w.retire(p.rig), lambda: w.retire(p.gun), lambda: w.retire(p.arms[0]),
        lambda: w.retire(p.pack), lambda: w.retire(p.limbs[0]), lambda: w.retire(p.lens),
        lambda: w.u8(p.head.meshes[0] + 0xDC, 255), lambda: w.u8(p.head.meshes[0] + 0xDC, 0), lambda: w.u8(p.head.meshes[1] + 0xDC, 3),
        lambda: w.u8(p.rig.meshes[3] + 0xDC, 255), lambda: w.u8(p.rig.meshes[0] + 0xDC, 255), lambda: w.u8(p.gun.meshes[0] + 0xDC, 255),
        lambda: w.u8(p.body.meshes[0] + 0xDC, 253), lambda: w.u8(p.lens.meshes[0] + 0xDC, 3), lambda: w.u8(p.pack.meshes[0] + 0xDC, 255),
        lambda: w.ptr(p.head.obj + 0x1D0, 0), lambda: w.ptr(p.arms[0].obj + 0x1D0, p.body.obj), lambda: w.ptr(p.gun.obj + 0x1D0, p.body.obj),
        lambda: w.ptr(p.limbs[0].obj + 0x1D0, p.rig.obj), lambda: w.ptr(p.lens.obj + 0x1D8, p.magazine.obj),
        lambda: w.ptr(p.head.obj + 0x18, w.world_native + 64), lambda: w.ptr(p.gun.obj + 0x18, w.world_native + 64),
        lambda: w.ptr(p.body.obj + 0x18, w.world_native + 64), lambda: w.ptr(w.objects + p.head.index * 8, p.pack.obj),
        lambda: w.ptr(w.objects + p.optic.index * 8, p.lens.obj), lambda: w.ptr(w.objects + p.body.index * 8, p.rig.obj),
        lambda: w.u32(p.head.obj + 0xC8, 1), lambda: w.u32(p.head.obj + 0xC8, 0), lambda: w.u32(p.limbs[0].obj + 0xC8, 0),
        lambda: w.u32(p.arms[0].obj + 0xC8, 1), lambda: w.ptr(p.arms[1].array, p.arms[1].meshes[1]),
        lambda: (w.ptr(p.head.array, p.head.meshes[1]), w.ptr(p.head.array + 16, p.head.meshes[0])),
        lambda: w.ptr(p.limbs[0].array, p.head.meshes[0]), lambda: w.ptr(p.gun.obj + 0xD0, p.arms[0].array),
        lambda: w.slot(0, 0, 0), lambda: w.slot(1, 0, p.limbs[0].ref), lambda: w.slot(0, 3, p.head.ref), lambda: w.slot(2, 9, p.torso.ref),
        lambda: w.slot(0, 7, 0), lambda: w.poke(p.rig.obj + 0x358, OTHER_RESOURCE), lambda: w.poke(p.head.obj + 0x358, b"x"),
        lambda: w.u32(p.rig.obj + 0x70, 62), lambda: w.u32(w.registry + 0x98, 3),
        lambda: w.ptr(w.registry + 0x88, w.objects + 8), lambda: w.ptr(vw.EXE + 0x1ab76f8, w.registry + 16),
        lambda: w.set_units([]), lambda: w.set_units(w.units_list + [p.body.handle]), lambda: w.set_units(w.units_list + [p.rig.handle]),
        lambda: (v("fail_fn", vw.SET), v("fail_mode", 1)), lambda: (v("fail_fn", vw.SET), v("fail_mode", 2)),
        lambda: (v("fail_fn", vw.SET), v("fail_mode", 3)), lambda: (v("fail_fn", vw.HAS_MESH), v("fail_mode", 1)),
        lambda: (v("fail_fn", vw.MAIN_WORLD), v("fail_mode", 3)), lambda: (v("fail_fn", vw.MAIN_WORLD), v("fail_mode", 1)),
        lambda: (v("fail_fn", vw.UNITS), v("fail_mode", 1)), lambda: (v("fail_fn", vw.MESH), v("fail_mode", 3)),
        lambda: w.bind(vw.SET, 2), lambda: w.bind(vw.SET, 0), lambda: w.bind(vw.HAS_MESH, 0), lambda: w.bind(vw.MESH_NAME, 0),
        lambda: w.bind(vw.MESH_NODE, 2), lambda: w.bind(vw.BACKPACK, 0), lambda: w.bind(vw.PIN_SNAPSHOT, 0), lambda: w.bind(vw.FOREIGN, 2),
        lambda: w.bind(vw.PLAYER_SNAPSHOT, 2), lambda: w.bind(vw.DESCRIPTOR, 0),
        lambda: w.unreadable(*region(p.head)), lambda: w.unreadable(*region(p.rig)), lambda: w.unreadable(*region(p.gun)),
        lambda: w.unreadable(w.custom(), w.custom() + 0x200), lambda: w.unreadable(w.generations, w.generations + w.capacity),
        lambda: w.unreadable(p.head.meshes[0], p.head.meshes[0] + 0x100), lambda: w.unreadable(vw.EXE, vw.EXE + 0x3000000),
        lambda: w.unreadable(w.registry, w.registry + 0x100), lambda: w.unreadable(p.head.array, p.head.array + 32),
        lambda: w.no_rig_mesh("g_r_shoulderpad"), lambda: w.rig_mesh("g_l_shoulderpad", p.rig.meshes[3], 1, 57),
    ]


def sweeps(which, stride, part=None):
    """One operation, repeated with every change arriving at every `stride`-th call it makes."""
    w, p = standard()
    s = Head(w, f"sweep_{which}" + (f"_{part}" if part is not None else ""))
    every = steady_breaks(w, p)
    if part is not None: every = every[part::4]
    frame = lambda: s.tick()
    if which == "apply":                                     # from clean: observe everything and hide it
        vw.sweep(s, "update", every, stride, before=("stop",), reset=True)
    elif which == "steady":                                  # the read-only frame
        vw.sweep(s, "update", every, stride, before=("stop", "update"), reset=True)
    elif which == "stop":                                    # giving everything back
        vw.sweep(s, "stop", every, stride, before=("stop", "update"), reset=True)
    elif which == "gap":                                     # kind 4: apply after the rig view (shadow rows are restored)
        w.var("s_kind", 4)
        vw.sweep(s, "update", every, stride, before=("stop",), reset=True)
    frame()
    return s


def walk(seed, count, **options):
    """Seeded sessions: the player aims, swaps, changes clothing and weapons parts, sits, dies and comes back,
    the mode is switched, and now and then something moves in the middle of a call."""
    rng = random.Random(seed)
    w, p = standard(layers=rng.choice((1, 2)))
    s = Head(w, f"walk{seed}", **options)
    glitches = steady_breaks(w, p)
    spare = [w.unit([rng.choice((255, 255, 255, 2, 254, 3))] * rng.randint(1, 3), parent=p.body.obj) for _ in range(8)]
    parts = [w.unit([255], parent=p.gun.obj) for _ in range(4)]
    for _ in range(count):
        roll = rng.random()
        if roll < 0.03: w.var("s_aiming", 1 - w.vars["s_aiming"])
        elif roll < 0.05: w.var("s_record", 0x1F0B2200 + rng.randint(0, 31) * 0xe8)
        elif roll < 0.07:
            kind = rng.choice((4, 4, 6, 6, 6, 7))
            w.var("s_kind", kind)
            for arm in p.arms: w.ptr(arm.obj + 0x1D0, p.rig.obj if kind == 6 else rng.choice((p.rig.obj, p.body.obj)))
        elif roll < 0.09: w.var("s_safe", rng.choice((0, 1, 1))); w.var("s_wt", rng.choice((0, 1)))
        elif roll < 0.11:
            w.slot(rng.randint(0, 2), rng.choice((0, 2, 8, 9)), rng.choice([0, 0] + [u.ref for u in spare]))
        elif roll < 0.12:
            w.slot(rng.randint(0, 2), rng.choice((3, 4, 5)), rng.choice([0] + [u.ref for u in spare[:2]]))
        elif roll < 0.14:
            kids = rng.sample([p.magazine, p.optic] + parts, rng.randint(0, 4)); w.children(p.gun, kids)
            if p.optic in kids: w.children(p.optic, [p.lens])
        elif roll < 0.15: w.var("pack_mode", rng.choice((0, 1, 1)))
        elif roll < 0.16: w.var("enabled", rng.choice((0, 1, 1))); w.var("mode", rng.choice((0, 1, 1, 1)))
        elif roll < 0.17: w.var("foreign", rng.choice((0, 0, 0, 1)))
        elif roll < 0.18: w.var("show_torso", rng.choice((0, 1))); w.text("s_cbytes", bytes([0, 0, rng.choice((0, 0, 2)), 0, 0, 0]))
        elif roll < 0.19:
            unit, mesh = rng.choice([(p.head, 0), (p.rig, 3), (p.rig, 1), (p.gun, 0), (p.body, 0), (p.torso, 0), (p.pack, 0), (p.arms[0], 1)])
            w.u8(unit.meshes[mesh] + 0xDC, rng.choice((255, 255, 0, 1, 2, 3, 253, 254)))
        elif roll < 0.195: w.reset_masks()
        elif roll < 0.20: w.var("s_seated", rng.choice((0, 0, 1))); w.var("fallback", rng.choice((0, 1, 1)))
        elif roll < 0.205:
            pid = rng.choice((b"player-A", b"player-A", b"player-B")); w.text("s_pid", pid); w.text("s_identity", pid + b"/gun")
        glitch = rng.random() < 0.06
        call = rng.choices(["update", "stop", "shutdown"], [94, 5.7, 0.3])[0]
        s.tick(rng.choice((16, 16, 16, 33, 120, 300, 700)))
        if glitch:
            w.record(); s.at(rng.randint(1, 1500 if rng.random() < 0.5 else 300), rng.choice(glitches))
        s.op(call)
        if glitch: w.undo()
    return s


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--script", type=Path, help="also write the script text here (for the coverage review)")
    parser.add_argument("--only", help="comma-separated run names (while working on one)")
    parser.add_argument("--quick", action="store_true", help="the hand-written runs only")
    args = parser.parse_args()
    runner = HERE / "run_visibility.lua"
    vw.configure(args.retail, runner)
    commit = vw.frozen_commit(args.retail)
    scripts = [session(), session("session_plain", nolog=1, models=0), session("session_no_sdk", sr=0),
               session("session_bridge", bridge=1), session("session_bridge_differs", bridge=2), gates(), armour(), refusals(), rig(),
               torso(), masks(), restores(), bindings(), handles(), edges()]
    if not args.quick:
        scripts += [targeted()] + [sweeps("apply", 61, part) for part in range(4)] + [sweeps("steady", 23), sweeps("stop", 67), sweeps("gap", 97)]
        scripts += [walk(1, 2500), walk(2, 2500), walk(3, 1000, nolog=1, models=0)]
    if args.only:
        scripts = [script for script in scripts if script.name in args.only.split(",")]
    lines = vw.run_reference(args.retail, scripts, runner)
    if args.script:
        args.script.write_text("".join(script.header() + "\n" + "".join(op + "\n" for op in script.ops) for script in scripts))
    for script in scripts:
        print(f"{script.name}: {len(script.ops)} operations")
    raised = [line for line in lines if " RAISED=" in line]
    if raised:
        sys.exit("the reference raised:\n" + "\n".join(line[:600] for line in raised[:5]))
    vw.write_header(args.out / "head_visibility_vectors.h", "gen_head_visibility.py", "head_visibility", args.retail,
                    ["head_visibility.lua", "model_shadow_source.lua"], scripts, lines, commit)
    print(f"head visibility: {len(lines)} operations in {len(scripts)} runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
