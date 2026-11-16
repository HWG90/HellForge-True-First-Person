#!/usr/bin/env python3
"""Cut reference vectors from the retail arm_pieces.lua (the sleeves of the first-person arms), run
unmodified under LuaJIT against the scripted engine of arms_common.lua.

  gen_arm_pieces.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import argparse
import math
import random
import sys
from pathlib import Path

from arms_gen import (HERE, address, IDENTITY, NAN, add, axis_angle, frozen_commit, link_word, pose_edit, qmul, qrot, random_rotation, run,
                      write_header)

CHEST, SPINE_A, SPINE_B, GROUP = 0x37EF0820, 0x58D886FB, 0x5A4BBAA2, 0x315CD161
COLLAR_L, COLLAR_R, SHOULDER_L, SHOULDER_R = 0xC4787B4E, 0xB2BCD839, 0x03752C98, 0x1620B2CE
RIG, BODY, LEFT, RIGHT, PAD, GUN, OTHER, GHOST, BARE, RIG2, BODY2, LEFT2, RIGHT2 = 1, 2, 3, 4, 5, 6, 7, 8, 9, 21, 22, 23, 24


def ref(unit): return 0x400000 * 3 + 100 + unit
def on(bone): return link_word(2, bone)


# zero-based bone tables
RIG_NAMES = [0x11, SHOULDER_L, SHOULDER_R, CHEST, 0x12, 0x13, 0x14]
BODY_NAMES = [0x21, CHEST, COLLAR_L, SHOULDER_L, COLLAR_R, SHOULDER_R, SPINE_A, SPINE_B, GROUP, 0x22]
# where the body's bones sit in the torso's own frame (x right, z up)
BODY_AT = {1: (0, 0, 1.3), 2: (-.05, 0, 1.35), 3: (-.2, 0, 1.4), 4: (.05, 0, 1.35), 5: (.2, 0, 1.4), 6: (0, 0, 1.15), 7: (0, 0, 1.05),
           8: (0, -.05, 1.38), 0: (0, 0, 1.0), 9: (0, .1, 1.6)}
UNITS = {
    RIG: dict(names=RIG_NAMES),
    BODY: dict(names=BODY_NAMES),
    LEFT: dict(names=[0x31, CHEST, COLLAR_L, SPINE_A, SHOULDER_L, SPINE_B, GROUP], links=[0, on(3), on(3), 0, on(1), on(3), on(4)], parent=RIG),
    RIGHT: dict(names=[0x41, CHEST, COLLAR_R, SHOULDER_R, GROUP], links=[0, on(3), on(3), on(2), on(5)], parent=RIG),
    PAD: dict(names=[CHEST, COLLAR_L, 0x51], links=[on(3), on(3), 0], parent=RIG),      # the chest is bone 0; link target 0 is a real home
    GUN: dict(names=[7, 8], links=[on(4), 0], parent=RIG),
    OTHER: dict(names=[COLLAR_L, CHEST], links=[on(1), on(1)], parent=None),             # carries the names, hangs elsewhere
    GHOST: dict(names=[CHEST], links=[0], parent=RIG),                                   # its object cannot be resolved
    BARE: dict(names=[0x91, COLLAR_R, SPINE_A], links=[0, on(3), on(3)], parent=RIG),    # a collar without a chest: never accepted
    RIG2: dict(names=[SHOULDER_R, 0x15, SHOULDER_L, 0x16]),
    BODY2: dict(names=[SPINE_B, SHOULDER_R, COLLAR_R, CHEST, SHOULDER_L, COLLAR_L, 0x23, GROUP]),
    LEFT2: dict(names=[COLLAR_L, 0x32, CHEST, GROUP], links=[on(1), 0, on(1), 0], parent=RIG2),
    RIGHT2: dict(names=[COLLAR_R, COLLAR_L, CHEST], links=[on(3), on(3), on(3)], parent=RIG2),   # both collars: the right side wins
}
BODY2_AT = {0: (0, 0, 1.0), 1: (.22, 0, 1.42), 2: (.06, 0, 1.36), 3: (0, .01, 1.31), 4: (-.22, 0, 1.42), 5: (-.06, 0, 1.36), 6: (0, 0, .9),
            7: (0, -.04, 1.4)}
SNAP = dict(body=ref(BODY), viewmodel=ref(RIG), avatar_owner=77, avatar_index=0)
SNAP2 = dict(body=ref(BODY2), viewmodel=ref(RIG2), avatar_owner=77, avatar_index=1)
FIRST = [RIG, BODY, LEFT, RIGHT, PAD, GUN, OTHER, GHOST, BARE]
SECOND = [RIG2, BODY2, LEFT2, RIGHT2, GUN]
ERRORS = ["engine refused", "Memory unavailable", "stack traceback: C function", "x"]


class Script:
    def __init__(self, start=100.0):
        self.ops, self.t, self.pending = [], start, []

    def edit(self, *e):
        self.pending.append(list(e))

    def engine(self, *missing):
        """From here on the engine lacks these functions (none: it is whole again)."""
        self.ops.append(dict(call="engine", missing=list(missing)))

    def op(self, call="update", dt=1 / 60, at=None, **kw):
        self.t = at if at is not None else self.t + dt
        o = dict(call=call, now=self.t)
        if call == "update":
            o.update(s=dict(SNAP), active=True, h=1)
        o.update(kw)
        if self.pending:
            o["edits"], self.pending = self.pending, []
        self.ops.append(o)
        return o

    # ---- the scene ----
    def build(self):
        for unit, spec in UNITS.items():
            parent = spec.get("parent")
            self.edit("unit", unit, dict(ref=ref(unit), address=address(unit), names=list(spec["names"]), links=list(spec.get("links", [])),
                                         parent=address(parent) if parent else address(99)))
            # the links the game made, as the engine has them
            for index, word in enumerate(spec.get("links", [])):
                if word % 65536 == 2 and parent:
                    self.edit("engine_link", unit, index + 1, parent, word // 65536 + 1)
        self.edit("bad", GHOST, "object", True)
        self.handles(1, FIRST, fresh=True)

    def handles(self, key, units, fresh=False):
        self.edit("handles", key, [[ref(u), u] for u in units], fresh)

    def body(self, q=IDENTITY, o=(0.0, 0.0, 0.0), unit=BODY, at=None, rng=None):
        for node, p in (at or BODY_AT).items():
            turn = random_rotation(rng) if rng else axis_angle((1, 2, 3), .3 * node)
            self.edit(*pose_edit(unit, node + 1, qmul(q, turn), add(o, qrot(q, p))))

    def rig(self, q=IDENTITY, o=(5.0, 0.0, 2.0), unit=RIG, rng=None, left=1, right=2, half=.15, nodes=None):
        for node, p in ((left, (-half, 0, 0)), (right, (half, 0, 0))):
            turn = random_rotation(rng) if rng else axis_angle((0, 1, 1), -.4 + node)
            self.edit(*pose_edit(unit, node + 1, turn, add(o, qrot(q, p))))
        for node in range(nodes or len(UNITS[unit]["names"])):
            if node not in (left, right):
                self.edit(*pose_edit(unit, node + 1, axis_angle((1, 0, 0), .2 * node), add(o, (9.0, 9.0 + node, 9.0))))

    def lay(self, rng=None):
        self.body(rng=rng)
        self.rig(q=axis_angle((0, 0, 1), math.pi / 2), rng=rng)


def started():
    s = Script()
    s.build()
    s.lay()
    s.op("new")
    return s


def tour(s):
    """Every path a healthy engine takes: used alone and under every missing engine function."""
    s.op(active=False)
    s.op()                                                    # found, hung, placed
    s.op("result"); s.op("clean")
    for _ in range(3):
        s.rig(q=axis_angle((0, 0, 1), 1.0 + .1 * len(s.ops)), o=(5.0, .1 * len(s.ops), 2.0)); s.op()
    s.op(dt=.6)                                               # the confirming lookup
    s.edit("engine_link", LEFT, 2, RIG, 4); s.op()            # the game hangs a chest back on its stand-in bone
    s.op()
    # aligned frames: no turn and no roll at all
    s.body(); s.rig(); s.op()
    # shoulder lines opposed: half a turn about world up
    s.rig(q=axis_angle((0, 0, 1), math.pi)); s.op()
    s.op(active=False)                                        # leaving the arm view clears the offsets
    s.op(active=False)
    s.lay(); s.op()
    s.edit("alive", RIGHT, False); s.op(dt=.3)                # a piece dies
    s.edit("alive", RIGHT, True); s.op(dt=.3); s.op(dt=.3)
    s.op("stop"); s.op("clean"); s.op("result")
    s.op(); s.op("stop"); s.op("stop")


def basic():
    s = started()
    s.op("result"); s.op("clean"); s.op("stop")
    tour(s)
    # the snapshot or the handle table missing
    s.op(); s.op(s=None); s.op(); s.op(h=None); s.op()
    for key in ("body", "viewmodel"):
        snap = dict(SNAP); del snap[key]
        s.op(s=snap); s.op()
        snap = dict(SNAP); snap[key] = 5
        s.op(s=snap); s.op()
    s.op(s=dict(body=ref(BODY), viewmodel=ref(RIG)))          # no avatar fields: still the same unit
    s.op(s=dict(SNAP, avatar_owner=78, avatar_index=3))
    # a tolerance either side of 2 cm: the chest hung on a bone that sits beside the shoulder
    for shift in (.019, .021, .0199, .0201):
        s.op()
        q = axis_angle((0, 1, 1), -.4 + 1)
        s.edit(*pose_edit(RIG, 2, q, (5.0, -.15, 2.0))); s.edit(*pose_edit(RIG, 6, q, (5.0 + shift, -.15, 2.0)))
        s.op()
        s.edit("engine_link", LEFT, 2, RIG, 6); s.op(); s.op()
    s.op("stop")
    return s.ops


def timing():
    s = started()
    s.handles(2, [RIG, BODY, GUN], fresh=True)                # no pieces at all
    for dt in (0, .1, .2, .2, .05, .05, .1, .1, .3, .01, .25, .2499, .2501):
        s.op(dt=dt, h=2)                                      # every update for 0.6 s, then every quarter second
    s.op(dt=-50, h=2); s.op(dt=.1, h=2); s.op(dt=.1, h=2)     # the clock went back
    s.op(dt=.7, h=2); s.op(dt=-.2, h=2); s.op(dt=-.1, h=2); s.op(dt=.05, h=2)
    s.op(active=False, h=2); s.op(dt=3, h=2); s.op(dt=.1, h=2); s.op(dt=.7, h=2); s.op(dt=.1, h=2); s.op(dt=.2, h=2)
    s.edit("handle", 2, ref(LEFT), LEFT)                      # the pieces arrive, in the table the module already knows
    s.op(dt=.1, h=2); s.op(dt=.2, h=2); s.op(dt=.1, h=2)
    s.edit("handle", 2, ref(RIGHT), RIGHT)                    # only the confirming lookup sees the second one
    for dt in (.1, .1, .1, .1, .1, .1, .1): s.op(dt=dt, h=2)
    s.edit("handle", 2, ref(PAD), PAD)                        # nothing looks again after that
    for dt in (.3, .3, 1, 5): s.op(dt=dt, h=2)
    s.op(dt=-100, h=2); s.op(h=2)
    s.op("stop")
    # the edges to the last bit: the end of the quick window, the confirming lookup, the clock going back by 0.6 s
    s.handles(6, [RIG, BODY, GUN], fresh=True); s.op(active=False, h=6)      # stop() does not end the quick window; leaving the view does
    t0 = math.floor(s.t) + 2.0
    s.op(at=t0, h=6); s.op(at=t0 + .1, h=6); s.op(at=t0 + .5, h=6); s.op(at=t0 + .6, h=6); s.op(at=t0 + .6 + 1e-9, h=6); s.op(at=t0 + .61, h=6)
    s.op(at=t0 + 1.0, h=6); s.op(at=t0 + 1.0, h=6); s.op(at=t0 + 1.25, h=6); s.op(at=t0 + 1.0, h=6)     # a lookup, and the same instant again
    s.op(at=t0 + .61, h=6)
    s.op(at=t0 - .65 + .61, h=6)                              # back by 0.65 s: the window starts again
    for i in range(1, 16): s.op(at=t0 - .02 + .05 * i, h=6)
    s.op(active=False, h=6)
    t0 = s.t + 1
    s.op(at=t0, h=6); s.op(at=t0 + .3, h=6); s.op(at=t0 - .3, h=6)  # back by exactly the window and a little less
    for i in range(1, 16): s.op(at=t0 - .3 + .05 * i, h=6)
    s.op(active=False, h=6)
    t0 = s.t + 1
    s.op(at=t0)                                               # found now: confirmed at exactly t0 + 0.5
    s.edit("handle", 1, ref(BARE), False)
    s.op(at=t0 + .5 - 1e-9); s.op(at=t0 + .5); s.op(at=t0 + .5 + 1e-9); s.op(at=t0 + 2)
    s.edit("handle", 1, ref(BARE), BARE); s.op("stop")
    for clock in (1, 0, 1, 1, 0):
        s.op(clock=clock); s.op(clock=clock, active=False)
    s.op(now=NAN); s.op(); s.op(now=NAN); s.op(now=float("inf")); s.op(now=float("-inf")); s.op()
    s.op("stop")
    return s.ops


def armour_change():
    s = started()
    s.op(); s.op()
    s.handles(1, FIRST); s.op(); s.op()                       # the same table refilled in place: nothing to do
    s.handles(1, FIRST, fresh=True); s.op(); s.op()           # another table with the same content: all over again
    s.handles(1, [RIG, BODY, LEFT, GUN]); s.op(dt=.6); s.op() # a piece leaves the table in place: not noticed while it lives
    s.handles(3, [RIG, BODY, RIGHT, PAD], fresh=True); s.op(h=3); s.op(h=3)
    # the body's handle changes under the same reference
    s.edit("unit", 30, dict(ref=ref(BODY), address=address(40), names=list(BODY_NAMES), links=[]))
    s.body(unit=30, q=axis_angle((1, 0, 0), .4))
    s.edit("handle", 3, ref(BODY), 30); s.op(h=3); s.op(h=3)
    s.edit("unit", 31, dict(ref=ref(RIG), address=address(RIG), names=list(RIG_NAMES), links=[]))   # and the rig's
    s.rig(unit=31, nodes=len(RIG_NAMES))
    s.edit("handle", 3, ref(RIG), 31); s.op(h=3); s.op(h=3)
    s.op("stop")
    # the body or the rig under a second reference, same unit: still another body
    s.handles(7, FIRST, fresh=True); s.edit("handle", 7, 999, BODY); s.edit("handle", 7, 998, RIG)
    s.edit("unit", 41, dict(ref=999, address=address(BODY), names=list(BODY_NAMES), links=[]))
    s.edit("unit", 42, dict(ref=998, address=address(RIG), names=list(RIG_NAMES), links=[]))
    s.edit("handle", 7, 999, BODY); s.edit("handle", 7, 998, RIG)
    s.op(h=7); s.op(h=7); s.op(h=7, s=dict(SNAP, body=999)); s.op(h=7, s=dict(SNAP, body=999)); s.op(h=7)
    s.op(h=7, s=dict(SNAP, viewmodel=998)); s.op(h=7, s=dict(SNAP, viewmodel=998)); s.op(h=7); s.op("stop")
    # another armour on another rig
    s.body(unit=BODY2, at=BODY2_AT, q=axis_angle((0, 1, 0), .2), o=(1.0, 2.0, 0.0))
    s.rig(unit=RIG2, left=2, right=0, half=.14, q=axis_angle((1, 1, 0), .5))
    s.handles(4, SECOND, fresh=True)
    s.op(s=dict(SNAP2), h=4); s.op(s=dict(SNAP2), h=4); s.op(s=dict(SNAP2), h=4, dt=.6)
    s.op(); s.op()                                            # back to the first set between two updates
    s.op(s=dict(SNAP2), h=4)
    s.op(s=dict(SNAP, viewmodel=ref(RIG2)), h=4)              # body of one, rig of the other: not in this table
    s.handles(5, FIRST + SECOND, fresh=True)
    s.op(s=dict(SNAP, viewmodel=ref(RIG2)), h=5); s.op(s=dict(SNAP, viewmodel=ref(RIG2)), h=5)
    s.op(s=dict(SNAP2, viewmodel=ref(RIG)), h=5); s.op(s=dict(SNAP2, viewmodel=ref(RIG)), h=5)
    s.op("stop"); s.op("clean")
    return s.ops


def vanish():
    s = started()
    s.op(); s.op()
    for unit in (LEFT, RIGHT, PAD):
        s.edit("alive", unit, False); s.op(); s.op(dt=.3); s.edit("alive", unit, True); s.op(dt=.3); s.op()
    s.edit("items", LEFT, 9); s.op(); s.op(dt=.3); s.edit("items", LEFT, 7); s.op(dt=.3); s.op()
    s.edit("odd_alive", RIGHT, True); s.op(); s.op(dt=.3); s.edit("odd_alive", RIGHT, False); s.op(dt=.3)
    for unit in (BODY, RIG):
        s.edit("alive", unit, False); s.op(); s.op(); s.op(active=False); s.op(); s.op("stop"); s.op()
        s.edit("alive", unit, True); s.op(); s.op()
        s.edit("items", unit, 3); s.op(); s.op("stop"); s.edit("items", unit, len(UNITS[unit]["names"])); s.op(); s.op()
    s.edit("alive", LEFT, False); s.edit("alive", RIGHT, False); s.edit("alive", PAD, False)
    s.op(); s.op(dt=.3); s.op("stop")
    for unit in (LEFT, RIGHT, PAD): s.edit("alive", unit, True)
    s.op(dt=.3); s.op()
    # everything gone while hung, then stop: nothing may be written
    s.edit("alive", LEFT, False); s.edit("alive", RIG, False); s.op("stop"); s.op("clean")
    s.edit("alive", LEFT, True); s.edit("alive", RIG, True); s.op(); s.op()
    s.edit("alive", RIG, False); s.op(active=False); s.op("stop")
    s.edit("alive", RIG, True); s.op(); s.edit("alive", RIGHT, False); s.op(active=False); s.edit("alive", RIGHT, True); s.op()
    # handles leave the table
    s.edit("handle", 1, ref(BODY), False); s.op(); s.op(); s.edit("handle", 1, ref(BODY), BODY); s.op()
    s.edit("handle", 1, ref(RIG), False); s.op(); s.edit("handle", 1, ref(RIG), RIG); s.op()
    s.edit("no_world", True); s.edit("engine_link", RIGHT, 2, RIG, 4); s.op(); s.op("stop"); s.edit("no_world", False); s.op(); s.op("stop")
    return s.ops


def missing_bones():
    s = started()
    def again(ops=2):
        s.handles(1, FIRST, fresh=True)
        for _ in range(ops): s.op(dt=.3)
    for unit, names in ((BODY, BODY_NAMES), (RIG, RIG_NAMES)):
        for hash_ in (SHOULDER_L, SHOULDER_R, CHEST, SPINE_B, SPINE_A, COLLAR_L, COLLAR_R, GROUP):
            if hash_ not in names: continue
            index = names.index(hash_) + 1
            s.edit("name", unit, index, 0x99); again(); s.op("stop")
            s.edit("name", unit, index, hash_)
    again()
    s.edit("name", BODY, 2, 0x99); s.edit("name", BODY, 3, 0x98); again(); s.op("stop")      # the pad: chest at bone 0, nothing to place
    s.edit("name", BODY, 2, CHEST); s.edit("name", BODY, 3, COLLAR_L)
    # the body gains bones between the first lookup and the confirming one: no home is known for them
    s.edit("name", BODY, 9, 0x97); s.edit("name", BODY, 8, 0x96); again(1)
    s.edit("name", BODY, 9, GROUP); s.edit("name", BODY, 8, SPINE_B); s.op(dt=.6); s.op(); s.op("stop")
    again(1); s.op(dt=.6); s.op(); s.op("stop")              # hung, looked up again, handed back: the homes are the game's
    # a shoulder goes missing after the pieces were found, and the lookup that notices fails half way
    s.edit("name", RIG, 2, 0x99); s.edit("count", RIGHT, 0); s.op(dt=.6); s.op(); s.op(active=False); s.op()
    s.edit("engine_link", LEFT, 2, RIG, 4); s.op(); s.op("stop")
    s.edit("name", RIG, 2, SHOULDER_L); s.edit("count", RIGHT, False); again()
    # the same name twice: the last one counts
    s.edit("names", BODY, BODY_NAMES + [CHEST, SHOULDER_L]); s.edit("items", BODY, 12)
    s.edit(*pose_edit(BODY, 11, IDENTITY, (.01, .02, 1.33))); s.edit(*pose_edit(BODY, 12, IDENTITY, (-.21, .01, 1.41))); again()
    s.edit("names", LEFT, [0x31, CHEST, COLLAR_L, SPINE_A, SHOULDER_L, SPINE_B, CHEST]); again(); s.op("stop")
    s.edit("names", LEFT, list(UNITS[LEFT]["names"])); s.edit("names", BODY, list(BODY_NAMES)); s.edit("items", BODY, 10)
    # bone counts
    for unit in (RIG, BODY, LEFT, GUN, OTHER):
        for count in (0, 256, 255, 1, 4294967295, len(UNITS[unit]["names"]) + 1, len(UNITS[unit]["names"]) - 1):
            s.edit("count", unit, count); again(); s.op("stop")
        s.edit("count", unit, False)
    # links: other modes, a home of bone 0, homes far away
    again()
    s.edit("links", LEFT, [0, link_word(1, 3), link_word(3, 3), link_word(2, 0), on(1), link_word(2 + 65536 * 4, 0), link_word(2, 65535)])
    again(); s.op("stop"); s.op()
    s.edit("links", LEFT, list(UNITS[LEFT]["links"])); again(); s.op("stop")
    # parents
    s.edit("parent", LEFT, address(RIG) + 1); again(); s.edit("parent", LEFT, address(RIG) + 2 ** 47); again()
    s.edit("parent", LEFT, address(RIG) + 2 ** 48); again(); s.op("stop")     # only six bytes are compared
    s.edit("parent", LEFT, address(RIG)); s.edit("parent", OTHER, address(RIG)); again(); s.op("stop")
    s.edit("parent", OTHER, address(99))
    # memory that cannot be read, in every place
    for unit in (RIG, BODY, LEFT, GHOST, OTHER):
        for what in ("object", "count", "ptr_names", "names", "parent", "ptr_links", "links"):
            s.edit("bad", unit, what, True); again(); s.op(active=False)
            s.edit("bad", unit, what, unit == GHOST and what == "object"); s.op(); s.op("stop")
    # found while healthy, unreadable at the confirming lookup
    again(1); s.edit("bad", LEFT, "names", True); s.op(dt=.6); s.op(); s.edit("bad", LEFT, "names", False); s.op(); s.op(dt=.6); s.op("stop")
    return s.ops


def geometry():
    s = started()
    quarter = axis_angle((0, 0, 1), math.pi / 2)
    def frame(*edits):
        s.body(); s.rig(q=quarter)
        for e in edits: s.edit(*e)
        s.op(); s.op()
    frame()
    frame(pose_edit(BODY, 4, IDENTITY, (.17, 0, 1.4)))                         # body shoulders 3 cm apart
    frame(pose_edit(BODY, 4, IDENTITY, (.15, 0, 1.4)), pose_edit(BODY, 6, IDENTITY, (.2, 0, 1.4)))
    frame(pose_edit(BODY, 4, IDENTITY, (.1501, 0, 1.4)))                       # just under / over 5 cm
    frame(pose_edit(BODY, 4, IDENTITY, (.1499, 0, 1.4)))
    frame(pose_edit(BODY, 4, IDENTITY, (.145, 0, 1.4)))                        # 5.5 cm
    frame(pose_edit(RIG, 3, IDENTITY, (5.0, -.095, 2.0)))                      # rig shoulders 5.5 cm apart
    frame(pose_edit(BODY, 8, IDENTITY, (0, 0, 1.275)))                         # spine 2.5 cm
    frame(pose_edit(BODY, 8, IDENTITY, (.3, 0, 1.3005)))                       # the body's up half a millimetre long
    frame(pose_edit(RIG, 2, IDENTITY, (5.0002, 0, 1.8)), pose_edit(RIG, 3, IDENTITY, (5.0, 0, 2.2)))   # the rig's nearly as short
    frame(pose_edit(RIG, 3, IDENTITY, (5.0, -.11, 2.0)))                       # rig shoulders 4 cm apart
    frame(pose_edit(BODY, 8, IDENTITY, (0, 0, 1.285)))                         # spine 1.5 cm
    frame(pose_edit(BODY, 8, IDENTITY, (0, 0, 1.2801)), pose_edit(BODY, 2, IDENTITY, (0, 0, 1.3)))
    frame(pose_edit(BODY, 8, IDENTITY, (.3, 0, 1.3)))                          # spine along the shoulder line: no up
    frame(pose_edit(RIG, 2, IDENTITY, (5.0, 0, 1.8)), pose_edit(RIG, 3, IDENTITY, (5.0, 0, 2.2)))   # rig shoulders along world up
    frame(pose_edit(RIG, 2, IDENTITY, (5.0, 0, 2.2)), pose_edit(RIG, 3, IDENTITY, (5.0, 0, 1.8)))
    # no turn (lines parallel) with and without a roll; a half turn; both degenerate
    s.body(); s.rig(); s.op(); s.op()
    s.body(q=axis_angle((1, 0, 0), .6)); s.rig(); s.op()
    s.body(); s.rig(q=axis_angle((0, 0, 1), math.pi)); s.op()
    s.body(q=axis_angle((1, 0, 0), math.pi)); s.rig(); s.op()                  # the up vectors opposed: half a turn about the line
    s.body(q=axis_angle((1, 0, 0), math.pi)); s.rig(q=axis_angle((0, 0, 1), math.pi)); s.op()
    s.body(); s.rig()
    s.edit(*pose_edit(BODY, 8, IDENTITY, (.3, 0, 1.3))); s.op()                # no turn, no up: nothing to place by
    s.body(q=axis_angle((0, 0, 1), .5)); s.rig()
    s.edit(*pose_edit(BODY, 8, IDENTITY, add((0, 0, 1.3), qrot(axis_angle((0, 0, 1), .5), (.3, 0, 0))))); s.op()   # a turn, no up
    for q in (axis_angle((0, 1, 0), 1.2), axis_angle((1, 1, 1), 2.5), axis_angle((0, 0, 1), 3.1), axis_angle((1, 0, 0), -1.5)):
        s.body(q=q, o=(3.0, -2.0, .5)); s.rig(q=qmul(quarter, q)); s.op(); s.op()
    # numbers that are not numbers
    for unit, node in ((BODY, 4), (BODY, 2), (RIG, 2), (BODY, 8), (BODY, 3)):
        s.lay(); s.edit(*pose_edit(unit, node, IDENTITY, (NAN, 0, 1.4))); s.op(); s.op()
        s.lay(); s.edit(*pose_edit(unit, node, (NAN, 0, 0, 1), (.2, 0, 1.4))); s.op(); s.op()
        s.lay(); s.edit(*pose_edit(unit, node, IDENTITY, (float("inf"), 0, 1.4))); s.op(); s.op()
    s.lay(); s.op(); s.op("stop")
    return s.ops


CALLS = ["obj", "u32", "ptr", "rd", "mw", "lk", "al", "ns", "wp", "wo", "wr", "sp", "clk"]


def errors():
    """Every engine and backend function failing at every call of a lookup, a placement, a release and a hand-back."""
    s = started()
    def sweep(setup, action, limit):
        for name in CALLS:
            for nth in range(1, limit(name) + 1):
                setup()
                action(fail=[name, nth, ERRORS[(nth + len(name)) % len(ERRORS)]])
                s.op(); s.op(active=False); s.op("stop")
    quiet = dict(obj=0, u32=0, ptr=0, rd=0, mw=0, lk=0, clk=1)
    # the first update: lookup, hang, place
    sweep(lambda: s.handles(1, FIRST, fresh=True), lambda **kw: s.op(**kw),
          lambda name: dict(obj=9, u32=7, ptr=10, rd=16, mw=1, lk=11, al=7, ns=7, wp=3, wo=18, wr=11, sp=11, clk=1)[name])
    # a steady update
    def steady(): s.handles(1, FIRST, fresh=True); s.op(); s.op()
    sweep(steady, lambda **kw: s.op(**kw), lambda name: dict(quiet, al=6, ns=6, wp=9, wo=18, wr=11, sp=11)[name])
    # the game has re-linked a piece: hang again
    def relinked(): steady(); s.edit("engine_link", RIGHT, 2, RIG, 4)
    sweep(relinked, lambda **kw: s.op(**kw), lambda name: dict(quiet, mw=1, lk=11, sp=1, wp=1, al=1, ns=1, wo=1, wr=1)[name])
    # leaving the view, and stop while hung
    sweep(steady, lambda **kw: s.op(active=False, **kw), lambda name: dict(quiet, al=4, ns=4, sp=11, clk=0, wp=0, wo=0, wr=0)[name])
    sweep(steady, lambda **kw: s.op("stop", **kw), lambda name: dict(quiet, al=10, ns=10, sp=11, mw=1, lk=8, clk=0, wp=0, wo=0, wr=0)[name])
    # the confirming lookup, half a second on
    def due(): steady(); s.t += .6
    sweep(due, lambda **kw: s.op(**kw), lambda name: dict(obj=9, u32=7, ptr=10, rd=16, mw=0, lk=0, al=5, ns=5, wp=0, wo=0, wr=0, sp=0, clk=0)[name])
    # the value maths failing at every call of a placing update, of a first update and of a release
    for setup, kw, count in ((steady, {}, 260), (lambda: s.handles(1, FIRST, fresh=True), {}, 260), (steady, dict(active=False), 14),
                             (steady, dict(call="stop"), 14)):
        for nth in range(1, count + 1):
            setup()
            s.op(mfail=[nth, ERRORS[nth % len(ERRORS)]], **kw)
            s.op(); s.op("stop")
    return s.ops


def shared():
    """Two instances: the module's SIDES table belongs to both."""
    s = started()
    s.op("new", inst=2)
    s.body(unit=BODY2, at=BODY2_AT); s.rig(unit=RIG2, left=2, right=0)
    s.handles(4, SECOND, fresh=True)
    s.op(); s.op()
    s.op(inst=2, s=dict(SNAP2), h=4)                          # the second lookup moves the shoulders of the first
    s.op(); s.op(inst=2, s=dict(SNAP2), h=4); s.op()
    s.edit("engine_link", LEFT, 2, RIG, 4); s.op()
    s.op(active=False); s.op(inst=2, s=dict(SNAP2), h=4)
    s.edit("name", RIG2, 1, 0x98)                             # the second rig loses a shoulder: the first has none either
    s.handles(4, SECOND, fresh=True); s.op(inst=2, s=dict(SNAP2), h=4)
    s.op(); s.edit("engine_link", LEFT, 2, RIG, 4); s.op(); s.op()
    s.op("stop"); s.op("stop", inst=2)
    s.handles(1, FIRST, fresh=True); s.op(); s.op(inst=2, s=dict(SNAP2), h=4); s.op(); s.op("stop")
    s.edit("name", RIG2, 1, SHOULDER_R); s.handles(4, SECOND, fresh=True)
    s.op(); s.op(inst=2, s=dict(SNAP2), h=4, dt=.3); s.op(); s.op(inst=2, s=dict(SNAP2), h=4); s.op("stop"); s.op("stop", inst=2)
    return s.ops


def walk(seed, count):
    rng = random.Random(seed)
    s = started()
    table, key, units, snap, active = 1, 1, list(FIRST), dict(SNAP), True
    broken = []
    for _ in range(count):
        dt = rng.choice((1 / 60, 1 / 60, 1 / 60, 1 / 30, 1 / 144, .1, .26, .6))
        if rng.random() < .01: dt = rng.choice((-.3, -5, 30))
        body_q = axis_angle((rng.uniform(-.3, .3), rng.uniform(-.3, .3), 1), rng.uniform(-3.2, 3.2))
        s.body(q=body_q, o=(rng.uniform(-9, 9), rng.uniform(-9, 9), rng.uniform(0, 2)), rng=rng if rng.random() < .5 else None)
        s.rig(q=random_rotation(rng, .9) if rng.random() < .8 else body_q, o=(rng.uniform(-9, 9), rng.uniform(-9, 9), 2.0), rng=rng)
        kw = {}
        e = rng.random()
        if e < .03: active = not active
        elif e < .05:
            unit = rng.choice(units[2:] + [BODY, RIG]); s.edit("alive", unit, False); broken.append(["alive", unit, True])
        elif e < .06:
            unit = rng.choice(units); s.edit("items", unit, rng.choice((1, 3, 12))); broken.append(["items", unit, len(UNITS[unit]["names"])])
        elif e < .10:
            unit = rng.choice((LEFT, RIGHT, PAD)); s.edit("engine_link", unit, rng.choice((1, 2, 3)), RIG, rng.choice((1, 4, 5)))
        elif e < .12:
            key += 1; table = key; rng.shuffle(units); s.handles(table, units, fresh=True)
        elif e < .14:
            units = [RIG, BODY] + rng.sample(FIRST[2:], rng.randint(0, 7)); rng.shuffle(units); s.handles(table, units)
        elif e < .15:
            unit = rng.choice((RIG, BODY, LEFT)); s.edit("handle", table, ref(unit), False); broken.append(["handle", table, ref(unit), unit])
        elif e < .18:
            unit = rng.choice(units); what = rng.choice(("object", "count", "ptr_names", "names", "parent", "ptr_links", "links"))
            s.edit("bad", unit, what, True); broken.append(["bad", unit, what, unit == GHOST and what == "object"])
        elif e < .20:
            unit = rng.choice((RIG, BODY, LEFT, RIGHT)); names = UNITS[unit]["names"]; index = rng.randrange(len(names))
            s.edit("name", unit, index + 1, rng.choice((0x99, CHEST, COLLAR_L, SHOULDER_R))); broken.append(["name", unit, index + 1, names[index]])
        elif e < .21:
            unit = rng.choice(units); s.edit("count", unit, rng.choice((0, 1, 2, 300))); broken.append(["count", unit, False])
        elif e < .24: kw["fail"] = [rng.choice(CALLS), rng.randint(1, 12), rng.choice(ERRORS)]
        elif e < .27: kw["mfail"] = [rng.randint(1, 250), rng.choice(ERRORS)]
        elif e < .275: kw["clock"] = 1
        elif e < .285: kw["s"] = rng.choice((None, dict(SNAP, body=None), dict(SNAP, viewmodel=ref(GUN)), dict(SNAP, body=ref(LEFT))))
        elif e < .29: kw["h"] = None
        elif e < .30:
            s.edit(*pose_edit(rng.choice((RIG, BODY)), rng.randint(1, 7), IDENTITY, (NAN, 0, 0)))
        elif e < .31:                                         # a degenerate torso
            s.edit(*pose_edit(BODY, 4, IDENTITY, (0, 0, 0))); s.edit(*pose_edit(BODY, 6, IDENTITY, (rng.choice((0, .01, .2)), 0, 0)))
        if broken and rng.random() < .25:
            s.edit(*broken.pop(rng.randrange(len(broken))))
        c = rng.random()
        if c < .015: s.op("stop", dt=dt, **{k: v for k, v in kw.items() if k in ("fail", "mfail")})
        elif c < .03: s.op(rng.choice(("clean", "result")), dt=dt)
        else:
            kw.setdefault("s", dict(snap)); kw.setdefault("h", table)
            s.op(dt=dt, active=active, **kw)
    s.op("stop")
    return s.ops


MISSING = ["Application_main_world", "World_link_unit", "Unit_alive", "Unit_num_scene_graph_items", "Unit_world_pose", "Unit_world_position",
           "Unit_world_rotation", "Unit_set_local_pose", "Vector3_call", "Vector3_length", "Vector3_dot", "Vector3_cross", "Vector3_normalize",
           "Quaternion_axis_angle", "Quaternion_rotate", "Quaternion_multiply", "Quaternion_identity", "Matrix4x4_identity",
           "Matrix4x4_from_quaternion_position", "Matrix4x4_multiply", "Matrix4x4_inverse", "Matrix4x4_translation",
           "Unit", "World", "Matrix4x4", "Vector3", "Quaternion"]


def without():
    s = started()
    tour(s)
    return s.ops


def vanishing():
    """Engine functions that go missing in the middle of a session, and come back."""
    s = started()
    for name in MISSING + ["Unit World", "Vector3 Quaternion Matrix4x4"]:
        for action in ("update", "leave", "stop"):
            s.engine(); s.lay(); s.handles(1, FIRST, fresh=True); s.op(); s.op()
            s.engine(*name.split())
            if action == "update": s.op(); s.op(active=False); s.op("stop")
            elif action == "leave": s.op(active=False); s.op("stop")
            else: s.op("stop"); s.op("clean"); s.op()
            s.engine(); s.op(); s.op("stop")
    return s.ops


def build_runs():
    runs = [dict(name="basic", ops=basic()), dict(name="timing", ops=timing()), dict(name="armour_change", ops=armour_change()),
            dict(name="vanish", ops=vanish()), dict(name="missing_bones", ops=missing_bones()), dict(name="geometry", ops=geometry()),
            dict(name="errors", ops=errors()), dict(name="shared", ops=shared())]
    runs.append(dict(name="vanishing", ops=vanishing()))
    runs += [dict(name="without_" + name, ops=without(), missing=[name]) for name in MISSING]
    vector = ["Vector3_" + name for name in ("call", "length", "dot", "cross", "normalize", "lerp", "x", "y", "z")]
    runs += [dict(name="only_" + name, ops=without()[:8], missing=[other for other in vector if other != name]) for name in vector]
    runs += [dict(name=f"walk{seed}", ops=walk(seed, 2500)) for seed in (1, 2, 3, 4)]
    return runs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runner", type=Path, default=HERE / "run_arm_pieces.lua", help="for the coverage review: a wrapper of the runner")
    args = parser.parse_args()
    commit = frozen_commit(args.retail)
    runs = build_runs()
    lines = run(args.runner, args.retail, runs)
    write_header(args.out, "arm_pieces", "Each run starts a fresh world; the module (and its SIDES table) is loaded once for all of them.",
                 commit, args.retail / "src/first_person/arm_pieces.lua", runs, lines)
    for r in runs:
        if not r["name"].startswith(("without_", "only_")): print(f"{r['name']}: {len(r['ops'])} operations")
    print(f"arm pieces: {sum(len(r['ops']) for r in runs)} operations in {len(runs)} runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
