#!/usr/bin/env python3
"""Cut reference vectors from the retail grip_hand.lua (hands of the first-person arms on the weapon), run
unmodified under LuaJIT against the scripted engine of arms_common.lua.

  gen_grip_hand.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import argparse
import math
import random
import sys
from pathlib import Path

from arms_gen import (HERE, address, IDENTITY, INF, NAN, add, axis_angle, frozen_commit, link_word, pose_edit, qinv, qmul, qrot, random_rotation, run,
                      scale, sub, write_header)

GRIP, HAND_NAME, RIGHT_NAME, ATTACH = 0x67C94E4B, 0x5C0C68D3, 0x248004F5, 0x4FDD3337
COLLAR_L, CHEST = 0xC4787B4E, 0x37EF0820
RIG, BODY, ARM_L, ARMOUR_L, ARM_R, ARMOUR_R, GUN, FOREGRIP, BACK, OTHER, GHOST, GUN2, RIG2, BODY2, SLEEVES = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15


def ref(unit): return 0x400000 * 3 + 100 + unit
def on(bone): return link_word(2, bone)
def child(bone): return link_word(1, bone)
def rig_name(bone): return 0x1000 + bone


def rig_tables():
    names = [rig_name(i) for i in range(63)]
    names[8] = ATTACH
    links = [0] * 63
    for bone, parent in ((1, 0), (7, 1), (8, 7), (10, 7), (11, 10), (12, 10), (13, 12), (14, 13), (15, 13), (16, 14), (30, 7), (31, 30), (32, 30),
                         (33, 32), (34, 32), (35, 34), (36, 35), (40, 40), (41, 42), (42, 41), (50, 16), (51, 50), (52, 36)):
        links[bone] = child(bone if bone == 40 else parent)
    links[45] = on(13)                                      # a rig bone in link mode 2: not a parent
    return names, links


RIG_NAMES, RIG_LINKS = rig_tables()
# body bones, zero-based: 1 left hand, 2-4 its fingers, 5 right hand, 6-7 its fingers, 8 weapon bone, 9 right upper arm, 10 what it hangs on
BODY_NAMES = [0x21, HAND_NAME, rig_name(14), rig_name(15), rig_name(16), RIGHT_NAME, rig_name(35), rig_name(36), ATTACH, rig_name(30), 0x25, 0x26]
BODY_LINKS = [0, child(11), child(1), child(1), child(2), child(11), child(5), child(6), child(5), child(10), child(11), 0]
UNITS = {
    RIG: dict(names=RIG_NAMES, links=RIG_LINKS, meshes=[dict(visible=True, materials=2), dict(visible=True, materials=1),
                                                         dict(visible=False, materials=2), dict(visible=True, materials=2)]),
    BODY: dict(names=BODY_NAMES, links=BODY_LINKS),
    # the arm with its hand: upper arm, twist, forearm, hand, three fingers, a collar the sleeve module owns, a loose bone, a bone under a finger
    ARM_L: dict(names=[0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, COLLAR_L, 0x38, 0x39],
                links=[on(10), on(11), on(12), on(13), on(14), on(15), on(16), on(10), 0, on(50)], parent=RIG),
    ARMOUR_L: dict(names=[0x41, 0x42], links=[on(12), 0], parent=RIG),
    ARM_R: dict(names=[0x51, 0x52, 0x53, 0x54, 0x55, 0x56, CHEST], links=[on(30), on(32), on(33), on(34), on(35), on(36), on(34)], parent=RIG),
    ARMOUR_R: dict(names=[0x61, 0x62, 0x63], links=[on(32), on(31), on(8)], parent=RIG),
    GUN: dict(names=[0x71, GRIP, 0x72, GRIP], links=[on(8), child(0), 0, 0], parent=RIG),     # the first grip bone counts
    FOREGRIP: dict(names=[GRIP, 0x81], links=[0, 0], parent=GUN),
    BACK: dict(names=[0x91, 0x92, GRIP], links=[on(3), 0, 0], parent=BODY),
    OTHER: dict(names=[5, 6], links=[0, 0], parent=None),
    GHOST: dict(names=[GRIP], links=[on(8)], parent=RIG),
    GUN2: dict(names=[GRIP, 0xA1], links=[on(8), 0], parent=RIG),
    # every bone name the sleeve module owns, hung on arm bones, and one ordinary bone
    SLEEVES: dict(names=[0x37EF0820, 0xC4787B4E, 0xB2BCD839, 0x58D886FB, 0x5A4BBAA2, 0x315CD161, 0xC1],
                  links=[on(12), on(13), on(32), on(34), on(10), on(30), on(12)], parent=RIG),
}
SNAP = dict(body=ref(BODY), viewmodel=ref(RIG), avatar_owner=77, avatar_index=0, aiming=False, shoulder_aim=False, weapon=5, request_weapon=5,
            weapon_transition=False)
ALL = [RIG, BODY, ARM_L, ARMOUR_L, ARM_R, ARMOUR_R, GUN, FOREGRIP, BACK, OTHER, GHOST, SLEEVES]
ERRORS = ["engine refused", "Memory unavailable", "stack traceback: C function", "x"]
CALLS = ["obj", "u32", "ptr", "rd", "al", "ns", "wp", "wo", "wr", "sp", "nm", "ms", "sv", "mv", "mm", "clk"]


def pmul(a, b):                                             # pose a, then pose b (the engine's row-matrix order)
    return (qmul(b[0], a[0]), add(qrot(b[0], a[1]), b[1]))


def pinv(a):
    i = qinv(a[0])
    return (i, qrot(i, scale(a[1], -1)))


class Script:
    def __init__(self, start=100.0):
        self.ops, self.t, self.pending = [], start, []
        # the scene's dials
        self.o, self.yaw = (5.0, 3.0, 1.6), .4               # where the rig is and how it is turned
        self.wob = 0.0                                       # arm animation
        self.hand_off = (0.0, 0.0, 0.0)                      # the body's left hand from the grip bone it holds
        self.body_on = GUN                                   # whose grip bone that is; None: hanging at its side
        self.rig_hand_off = (0.0, 0.0, 0.0)                  # the rig's left hand from its usual place
        self.grip_off = (.02, .02, 0.0)                      # the grip bone from the rig's usual left hand place
        self.right_off = (.012, .008, .004)                  # the body's right hand from where the rig's is, on the weapon bone
        self.right_far = (0.0, 0.0, 0.0)                     # the rig's right hand from its usual place
        self.spin = 0.0                                      # the body's weapon turned in the body's hand
        self.attach_shift = 0.0                              # the rig's weapon bone turned in the rig's hand
        self.ref_shift = (0.0, 0.0, 0.0)                     # the body's arm root moved against its right hand
        self.ref_turn = 0.0                                  # ... and turned
        self.attach_slide = (0.0, 0.0, 0.0)                  # the rig's weapon bone moved in the rig's hand
        self.flip = False                                    # the body's right hand rotation as the negated quaternion
        self.grip_q = None                                   # the gun's grip bone rotation, when not the usual one
        self.straight = False                                # the rig's elbows on the line from shoulder to hand
        self.curl = .3
        self.snap = dict(SNAP)
        self.table = 1

    def edit(self, *e):
        self.pending.append(list(e))

    def engine(self, *missing):
        """From here on the engine lacks these functions (none: it is whole again)."""
        self.ops.append(dict(call="engine", missing=list(missing)))

    def op(self, call="update", dt=1 / 60, at=None, **kw):
        self.t = at if at is not None else self.t + dt
        o = dict(call=call, now=self.t)
        if call == "update":
            o.update(s=dict(self.snap), active=True, h=self.table)
        o.update(kw)
        if self.pending:
            o["edits"], self.pending = self.pending, []
        self.ops.append(o)
        return o

    def build(self):
        for unit, spec in UNITS.items():
            parent = spec.get("parent")
            self.edit("unit", unit, dict(ref=ref(unit), address=address(unit), names=list(spec["names"]), links=list(spec.get("links", [])),
                                         parent=address(parent) if parent else address(99), meshes=[dict(m) for m in spec.get("meshes", [])]))
        self.edit("bad", GHOST, "object", True)
        self.handles(1, ALL, fresh=True)

    def handles(self, key, units, fresh=False):
        self.edit("handles", key, [[ref(u), u] for u in units], fresh)

    def pose(self, unit, node, pose):
        self.edit(*pose_edit(unit, node, pose[0], pose[1]))

    def lay(self, rig=RIG, body=BODY, gun=GUN):
        """Write every pose the module can ask for, from the dials."""
        R = axis_angle((0, 0, 1), self.yaw)
        w = self.wob
        def at(local): return add(self.o, qrot(R, local))
        def turned(axis, angle): return qmul(R, axis_angle(axis, angle))
        # left arm of the rig
        s_l = (-.18, .05, -.1)
        e_l = add(s_l, (.02 + .01 * math.sin(w), .27, -.12))
        h_l = add(e_l, (.10, .24, .08 + .01 * math.cos(w)))
        if self.straight: e_l = scale(add(s_l, h_l), .5)
        hand_l = (turned((1, .2, 0), .5 + .1 * math.sin(w)), at(add(h_l, self.rig_hand_off)))
        self.last_hand_l, self.last_forearm = hand_l, qrot(R, sub(h_l, e_l))
        self.pose(rig, 11, (turned((0, 1, 0), .3 + .05 * w), at(s_l)))
        self.pose(rig, 12, (turned((0, 1, .1), .35), at(scale(add(s_l, e_l), .5))))
        self.pose(rig, 13, (turned((1, 0, 0), -.4 + .05 * w), at(e_l)))
        self.pose(rig, 14, hand_l)
        for i, node in enumerate((15, 16, 17, 51, 52)):
            self.pose(rig, node, pmul((axis_angle((1, 0, 0), .2 + .1 * i), (.02 * i, .05, .01)), hand_l))
        # right arm
        s_r = (.18, .05, -.1)
        e_r = add(s_r, (0.0, .2, -.15))
        h_r = add(e_r, (-.08, .22, .06))
        if self.straight: e_r = scale(add(s_r, h_r), .5)
        hand_r = (turned((0, 1, .3), -.3 + .05 * math.sin(w)), at(add(h_r, self.right_far)))
        self.pose(rig, 31, (turned((0, 1, 0), -.3), at(s_r)))
        self.pose(rig, 32, (turned((0, 1, 0), -.2), at(scale(add(s_r, e_r), .5))))
        self.pose(rig, 33, (turned((1, 0, 0), -.5), at(e_r)))
        self.pose(rig, 34, (turned((1, 0, 0), -.45), at(scale(add(e_r, h_r), .5))))
        self.pose(rig, 35, hand_r)
        for i, node in enumerate((36, 37, 53)):
            self.pose(rig, node, pmul((axis_angle((0, 0, 1), .3 + .1 * i), (.01, .04 + .02 * i, 0.0)), hand_r))
        attach_rest = (turned((0, 0, 1), .2), at(add(h_r, (0.0, .03, .02))))
        attach = pmul(pmul((axis_angle((0, 1, 0), self.attach_shift), self.attach_slide), pmul(attach_rest, pinv(hand_r))), hand_r)
        self.pose(rig, 9, attach)
        self.pose(rig, 46, pmul((IDENTITY, (.01, 0.0, 0.0)), hand_l))
        # the weapon's grip bones
        grip = (self.grip_q or turned((1, 0, .3), .9 + .1 * math.cos(w)), at(add(h_l, self.grip_off)))
        fore = (turned((1, 0, .2), 1.0), at(add(h_l, add(self.grip_off, (0.0, .17, 0.0)))))
        self.last_grip = grip
        self.pose(gun, 1 if gun == GUN2 else 2, grip)
        if gun == GUN:
            self.pose(gun, 4, (IDENTITY, at((0.0, 0.0, 3.0))))
        self.pose(FOREGRIP, 1, fore)
        # the body, somewhere else, leaning as it likes
        B = axis_angle((.2, .1, 1), self.yaw + .7)
        b_o = add(self.o, (-.3, -.4, -.2))
        self.pose(BACK, 3, (B, add(b_o, (0.0, -.2, .1))))
        held = {GUN: grip, FOREGRIP: fore, BACK: (B, add(b_o, (0.0, -.2, .1)))}.get(self.body_on)
        if held is None:
            hand = (qmul(B, axis_angle((0, 1, 0), 1.2)), add(b_o, (-.3, .05, -.5)))        # at the hip
        else:
            hand = (qmul(held[0], axis_angle((0, 0, 1), .05)) if self.hand_off != (0.0, 0.0, 0.0) else held[0], add(held[1], self.hand_off))
        self.pose(body, 2, hand)
        for i, node in enumerate((3, 4, 5)):
            self.pose(body, node, pmul((axis_angle((1, 0, 0), self.curl + .2 * i), (.015 * i, .06, -.01)), hand))
        body_attach_rest = (qmul(B, axis_angle((1, 0, 0), .3)), add(b_o, (.25, .3, 0.0)))
        rel = pmul((hand_r[0], add(at(h_r), qrot(R, self.right_off))), pinv(attach_rest))  # the hold: right hand as the weapon bone sees it
        body_right = pmul(rel, body_attach_rest)
        body_attach = pmul((axis_angle((0, .3, 1), self.spin), (0.0, 0.0, 0.0)), body_attach_rest)
        if self.flip: body_right = (tuple(-c for c in body_right[0]), body_right[1])
        self.last_hand_r, self.last_ref_base = body_right[1], add(b_o, (.1, 0.0, .35))
        self.pose(body, 6, body_right)
        for i, node in enumerate((7, 8)):
            self.pose(body, node, pmul((axis_angle((0, 0, 1), .25 + .15 * i), (.01, .05 + .02 * i, 0.0)), body_right))
        self.pose(body, 9, body_attach)
        self.pose(body, 10, (qmul(B, axis_angle((0, 1, 0), -.3)), add(b_o, (.2, 0.0, .3))))
        self.pose(body, 11, (qmul(B, axis_angle((1, 0, .5), self.ref_turn)), add(add(b_o, (.1, 0.0, .35)), self.ref_shift)))
        self.pose(body, 12, (B, add(b_o, (0.0, 0.0, .3))))

    def frame(self, dt=1 / 60, wob=True, **kw):
        if wob: self.wob += dt * 2
        self.lay()
        return self.op(dt=dt, **kw)


def started(start=100.0):
    s = Script(start)
    s.build()
    s.lay()
    s.op("new")
    return s


def tour(s):
    """Every path a healthy engine takes: used alone and under every missing engine function."""
    s.op(active=False)
    s.frame(); s.op("result"); s.op("clean")
    for _ in range(3): s.frame()
    s.hand_off = (.0, .25, .0); s.frame(); s.frame()         # the body's hand wanders: the grip stays
    s.rig_hand_off = (.0, -.6, .0); s.frame(); s.frame()     # a reload takes the rig's hand away
    s.rig_hand_off = (.0, -.24, .0); s.frame()               # half way
    s.rig_hand_off = (0.0, 0.0, 0.0); s.hand_off = (0.0, 0.0, 0.0); s.frame()
    s.body_on = FOREGRIP; s.frame(); s.frame(dt=.05); s.frame(dt=.2)
    s.snap["aiming"] = True; s.frame(); s.snap["aiming"] = False; s.frame()
    s.snap["weapon"] = 6; s.frame(); s.frame(dt=.11); s.frame(dt=.2); s.frame(dt=.3)
    # one-handed: the left hand at the hip, then the body spins the weapon in its hand
    s.body_on = None; s.frame(); s.frame(dt=.2); s.frame()
    for _ in range(8):
        s.spin += .45; s.frame()
    for _ in range(4): s.frame()
    s.spin = 0.0
    for _ in range(3): s.frame(dt=.1)
    s.frame(dt=.3); s.frame(dt=.3)
    s.body_on = GUN; s.frame()
    s.op(active=False); s.op(active=False); s.frame()
    s.edit("alive", ARM_R, False); s.frame(); s.edit("alive", ARM_R, True); s.frame()
    s.frame(dt=1.1)
    s.op("stop"); s.op("clean"); s.op("result"); s.frame(); s.op("stop"); s.op("stop")


def module_functions():
    ops = []
    def solve(S, E, H, T, **kw): ops.append(dict(call="solve", S=list(map(float, S)), E=list(map(float, E)), H=list(map(float, H)),
                                                  T=list(map(float, T)), **kw))
    S, E, H = (0, 0, 0), (.3, 0, .14), (.55, 0, -.05)
    for T in ((.5, .02, -.08), (2, 0, 0), (0, 0, 0), (1e-6, 0, 0), (.3, 0, .14), (.01, .01, .01), (-.4, .1, .2), (.6, 0, .28), (NAN, 0, 0),
              (INF, 0, 0), (.55, 0, -.05), (0, 0, .7), (.5786, 0, 0), (.58, 0, 0), (.02, 0, 0), (.019, 0, 0)):
        solve(S, E, H, T)
    solve(S, (.3, 0, .03), (.62, 0, 0), (2, 0, 0))                       # already past the stretch limit: keeps its own reach
    solve(S, (.3, 0, 0), (.6, 0, 0), (.5, 0, 0))                         # a straight arm has no bend side
    solve(S, (.3, 0, 0), (.6, 0, 0), (.5, .1, 0))
    solve(S, (0, 0, 0), H, (.3, .1, 0)); solve(S, E, E, (.3, .1, 0)); solve(S, E, S, (.3, .1, 0)); solve(S, S, S, (.3, .1, 0))
    solve(S, (.3, 0, .14), (.31, 0, .14), (.5, 0, 0)); solve(S, (.01, 0, 0), (.5, 0, .2), (.1, .1, 0))    # very unequal bones
    solve(S, E, H, (.5, 1e-7, -.08)); solve(S, (.3, 1e-6, 0), (.6, 0, 0), (.5, 0, 0))
    solve((NAN, 0, 0), E, H, (.5, 0, 0)); solve(S, (.3, NAN, 0), H, (.5, 0, 0)); solve(S, E, (INF, 0, 0), (.5, 0, 0))
    solve(S, E, H, (5e-5, 0, 0)); solve(S, E, H, (0, 3e-5, 4e-5)); solve(S, (.3, 1e-4, 0), (.6, 0, 0), (.5, 0, 0))   # lengths of 0.05 mm
    solve(S, (.3, 0, 1.5e-5), (.6, 0, 0), (.5, 0, 0)); solve(S, (.3, 0, 2.5e-5), (.6, 0, 0), (.5, 0, 0))
    rng = random.Random(7)
    for _ in range(300):
        p = [tuple(rng.uniform(-1, 1) * rng.choice((.01, .3, 1, 30)) for _ in range(3)) for _ in range(4)]
        solve(*p)
    for nth in range(1, 34):
        solve(S, E, H, (.5, .02, -.08), mfail=[nth, ERRORS[nth % 4]])
    for d in (0, .2, .32, 1, .26, .1999, .2001, .3199, .3201, -1, NAN, INF, -INF, .25, .3):
        ops.append(dict(call="weight", distance=float(d)))
    for _ in range(100): ops.append(dict(call="weight", distance=rng.uniform(.15, .35)))
    return ops


def basic():
    s = started()
    s.op("result"); s.op("clean"); s.op("stop")
    tour(s)
    s.frame(); s.op(s=None); s.frame(); s.op(h=None); s.frame()
    snap = dict(SNAP); del snap["viewmodel"]; s.op(s=snap); s.frame()
    snap = dict(SNAP); del snap["body"]; s.op(s=snap); s.frame(); s.frame()
    s.op(s=dict(SNAP, viewmodel=5)); s.frame(); s.op(s=dict(SNAP, body=5)); s.frame()
    s.op(s=dict(body=ref(BODY), viewmodel=ref(RIG))); s.frame()      # no avatar, weapon or aim fields at all
    s.op(s=dict(SNAP, avatar_owner=78, avatar_index=3)); s.frame()
    s.op("stop")
    return s.ops


def grip_logic():
    s = started()
    # easing in: a bone the body's hand has never been on, found while the hand is on it
    for dt in (.01, .05, .05, .05, .05): s.frame(dt=dt)
    # the hand off the bone from the start of another weapon: nothing until it has been there
    s.snap["weapon"] = 7; s.hand_off = (.0, .1, .0)
    for dt in (.02, .02, .11, .2, .3, .1): s.frame(dt=dt)
    s.hand_off = (0.0, 0.0, 0.0)
    for dt in (.03, .03, .03, .03, .03, .03, .03): s.frame(dt=dt)     # eases in over 0.15 s
    s.hand_off = (.0, .3, .0)
    for dt in (.03, .03, .03): s.frame(dt=dt)                         # stays once it has been there
    # exactly at the edges of "on the bone"
    for off in (.02, .0199, .0201, .021, .019):
        s.snap["weapon"] += 1; s.hand_off = (off, 0.0, 0.0); s.frame(); s.frame(dt=.2); s.frame()
    s.hand_off = (0.0, 0.0, 0.0); s.frame()
    # the blend by the rig hand's distance, at its edges
    for far in (.17, .18, .19, .2, .23, .26, .29, .3, .31, .34, .6, 0):
        s.rig_hand_off = (0.0, -far, 0.0); s.frame()
    s.rig_hand_off = (0.0, 0.0, 0.0)
    # two grip bones: the body's hand moves to the foregrip and back; then is on neither
    s.body_on = FOREGRIP
    for dt in (.02, .02, .05, .1): s.frame(dt=dt)
    s.body_on = GUN; s.frame(); s.frame()
    s.body_on = FOREGRIP; s.frame(); s.body_on = None; s.frame(); s.frame(); s.body_on = GUN; s.frame()
    s.body_on = BACK; s.frame(); s.frame(); s.body_on = GUN; s.frame()
    # a remembered grip: out of the arm view and back while the body's hand is elsewhere
    s.op(active=False); s.hand_off = (.0, .2, .1); s.frame(); s.frame(); s.hand_off = (0.0, 0.0, 0.0); s.frame()
    # after a swap the old weapon's bone is dropped; the remembered one comes back at once
    s.snap["weapon"] = 20; s.hand_off = (.0, .2, .1); s.frame(); s.frame(dt=.11); s.frame(dt=.2); s.frame(dt=.3); s.frame()
    s.hand_off = (0.0, 0.0, 0.0); s.frame()
    # the hand trails the bone by a frame of travel: compared with where the bone was
    for i in range(4):
        before = s.grip_off
        s.grip_off = add(s.grip_off, (.0, .05, .0)); s.hand_off = sub(before, s.grip_off); s.frame()
    s.grip_off = (.02, .02, 0.0); s.hand_off = (0.0, 0.0, 0.0); s.frame()
    # the body unusable or without a left hand
    s.edit("alive", BODY, False); s.frame(); s.frame(); s.edit("alive", BODY, True); s.frame()
    s.edit("items", BODY, 9); s.frame(); s.edit("items", BODY, 12); s.frame()
    s.edit("name", BODY, 2, 0x99); s.handles(2, ALL, fresh=True); s.table = 2; s.frame(); s.frame()
    s.snap["weapon"] = 30; s.frame(); s.frame(dt=.6)
    s.edit("name", BODY, 2, HAND_NAME); s.frame(dt=1.1); s.frame()
    # the arm cannot be bent: the grip in line with the arm, or at the shoulder
    s.grip_off = (-.12, -.51, -.04); s.frame(); s.grip_off = (.02, .02, 0.0); s.frame()
    s.op("stop")
    s.frame(); s.frame()
    # a clock that stands still or runs back
    for dt in (0, 0, -.01, -1, .5, -100, .1, .1, 0): s.frame(dt=dt)
    s.op("stop")
    return s.ops


def weapon_swap():
    s = started()
    s.frame(); s.frame()
    s.snap["weapon"] = 6
    for dt in (.016, .05, .05, .1, .1, .1, .2, .2): s.frame(dt=dt)   # at once, then 0.1, 0.25 and 0.5 s on
    s.snap["weapon_transition"] = True; s.frame(); s.frame(dt=.3)
    s.snap["weapon_transition"] = False; s.frame(); s.frame(dt=.7)
    s.snap["weapon"] = None; s.frame(); s.snap["request_weapon"] = 9; s.frame(); s.snap["request_weapon"] = None; s.frame()
    s.snap["weapon"] = 5.5; s.frame(); s.snap["weapon"] = 5; s.snap["request_weapon"] = 5; s.frame()
    # the clock jumps back between the delayed lookups
    s.snap["weapon"] = 8; s.frame(); s.frame(dt=-5); s.frame(dt=.1); s.frame(dt=.1); s.frame(dt=6); s.frame(); s.frame()
    # another weapon unit arrives a few frames after the swap, the old one goes to the back
    s.snap["weapon"] = 9; s.frame()
    s.edit("parent", GUN, address(BODY)); s.edit("link", GUN, 1, on(3)); s.frame(); s.frame()
    s.edit("handle", 1, ref(GUN2), GUN2)
    s.lay(gun=GUN2); s.op(dt=.11); s.lay(gun=GUN2); s.op(dt=.2); s.lay(gun=GUN2); s.op(dt=.3)
    for _ in range(3):
        s.lay(gun=GUN2); s.op()
    # dropped: nothing in the hands, then no weapon anywhere
    s.edit("handle", 1, ref(GUN2), False); s.frame(dt=1.1); s.frame()
    s.handles(1, [RIG, BODY, ARM_L, ARMOUR_L, ARM_R, ARMOUR_R, OTHER]); s.frame(); s.frame(dt=.3); s.frame(dt=1.2)
    s.handles(3, [RIG, BODY, ARM_L, ARMOUR_L, ARM_R, ARMOUR_R, OTHER], fresh=True); s.table = 3; s.frame(); s.frame(dt=.3); s.frame(dt=.3)
    s.frame(dt=.7); s.frame(dt=.26); s.frame(dt=.2); s.frame(dt=.1)
    # picked up again
    s.edit("parent", GUN, address(RIG)); s.edit("link", GUN, 1, on(8))
    s.handles(1, ALL); s.table = 1; s.frame(); s.frame()
    s.op("stop"); s.frame(); s.snap["weapon"] = 10; s.frame(); s.op(active=False); s.snap["weapon"] = 11; s.frame(); s.frame()
    s.op("stop")
    return s.ops


def one_handed():
    s = started()
    s.body_on = None
    def spin(frames, rate=.45, dt=1 / 60, **kw):
        for _ in range(frames):
            s.spin += rate; s.frame(dt=dt, **kw)
    def still(frames, dt=1 / 60, **kw):
        for _ in range(frames): s.frame(dt=dt, **kw)
    still(4)
    spin(12); still(6); spin(6, -.45); still(3)             # two turns with a pause, hand still out of place
    s.spin = 0.0; still(20, dt=.03)                          # back as it was: eases back
    # never comes back: ends three seconds after the last movement
    spin(5); still(10, dt=.2); still(10, dt=.2); still(3, dt=.1)
    s.spin = 0.0; still(3)
    # the hand moves against the arm root during the spin
    spin(4)
    for i in range(6):
        s.ref_shift = (.01 * i, 0.0, .02 * i); s.spin += .3; s.frame()
    still(5); s.ref_shift = (0.0, 0.0, 0.0); s.spin = 0.0; still(25, dt=.02)
    # slides instead of turning
    for i in range(5):
        s.right_off = (.012 + .04 * i, .008, .004); s.frame()
    s.right_off = (.012, .008, .004); still(25, dt=.02)
    # just under and over the limits: 400 degrees a second
    for rate in (.11, .12, .116, .117):
        spin(3, rate); s.spin = 0.0; still(30, dt=.02)
    # aiming ends it; aiming over the shoulder does not
    spin(5); still(2, s=dict(s.snap, aiming=True)); still(3); spin(3, s=dict(s.snap, aiming=True)); s.spin = 0.0; still(20, dt=.02)
    spin(5); still(2, s=dict(s.snap, shoulder_aim=True)); s.spin = 0.0; still(25, dt=.02)
    # the rig playing an animation of its own (a reload) keeps the spin handling out
    for i in range(6):
        s.attach_shift = .2 * i; s.spin += .45; s.frame()
    still(3); spin(3); s.attach_shift = 0.0; s.spin = 0.0; still(40, dt=.02)
    for i in range(4):
        s.rig_hand_off = (0.0, 0.0, .01 * i); s.spin += .45; s.frame()     # only the rig's left hand moving
    s.rig_hand_off = (0.0, 0.0, 0.0); s.spin = 0.0; still(40, dt=.02)
    # long gaps between updates: nothing is compared across them
    spin(3, dt=.3); spin(3, dt=.25); spin(3, dt=.2499); s.spin = 0.0; still(30, dt=.02)
    spin(4); s.frame(dt=-2); spin(3); s.frame(dt=.5); spin(2); s.frame(dt=-.2); s.spin = 0.0; still(30, dt=.02)
    # the weapon unit goes away, changes or dies in the middle of a spin
    spin(5); s.edit("alive", GUN, False); spin(2); s.edit("alive", GUN, True); spin(3); s.spin = 0.0; still(25, dt=.02)
    spin(5); s.edit("items", GUN, 9); spin(2); s.edit("items", GUN, 4); s.spin = 0.0; still(25, dt=.02)
    spin(5); s.edit("link", GUN, 1, on(7)); s.frame(dt=1.1); spin(3); s.edit("link", GUN, 1, on(8)); s.frame(dt=1.1); s.spin = 0.0; still(25, dt=.02)
    spin(5); s.edit("handle", 1, ref(GUN), False); s.edit("handle", 1, ref(GUN2), GUN2); spin(1, dt=1.1); spin(4)
    s.edit("handle", 1, ref(GUN), GUN); s.edit("handle", 1, ref(GUN2), False); s.frame(dt=1.1); s.spin = 0.0; still(25, dt=.02)
    # leaving the view, a swap and stop in the middle of a spin
    spin(5); s.op(active=False); s.op("clean"); still(2); spin(5); s.snap["weapon"] = 6; spin(3); s.spin = 0.0; still(25, dt=.02)
    spin(5); s.op("stop"); s.op("clean"); still(2); spin(5); s.op("stop"); s.spin = 0.0; still(5)
    # two hands on the weapon: the body's moving weapon is the rig's own business
    s.body_on = GUN; still(3); spin(6); s.spin = 0.0; still(10)
    # the arm root unknown: no remembered hand place, and no moving aside
    s.body_on = None
    s.edit("name", BODY, 10, 0x98); s.handles(4, ALL, fresh=True); s.table = 4; still(3); spin(8); s.spin = 0.0; still(25, dt=.02)
    s.edit("name", BODY, 10, rig_name(30)); s.edit("link", BODY, 10, 0); s.handles(5, ALL, fresh=True); s.table = 5; still(3); spin(8)
    s.spin = 0.0; still(25, dt=.02)
    s.edit("link", BODY, 10, child(200)); s.handles(6, ALL, fresh=True); s.table = 6; still(3); spin(4); s.spin = 0.0; still(25, dt=.02)
    s.edit("link", BODY, 10, child(10)); s.handles(7, ALL, fresh=True); s.table = 7; still(2)
    # no right hand or no weapon bone on the body; none on the rig
    for unit, index, name in ((BODY, 6, RIGHT_NAME), (BODY, 9, ATTACH), (RIG, 9, ATTACH)):
        s.edit("name", unit, index, 0x97); s.frame(dt=1.1); spin(3); s.edit("name", unit, index, name); s.frame(dt=1.1); s.spin = 0.0; still(3)
    s.op("stop")
    return s.ops


def pieces():
    s = started()
    armour_only = [RIG, BODY, ARMOUR_L, ARMOUR_R, GUN, FOREGRIP]
    # the armour hangs on the rig before the arm does
    s.handles(2, armour_only, fresh=True); s.table = 2
    for dt in (.016, .1, .2, .2, .05, .1, .2, .3, .3): s.frame(dt=dt)
    s.edit("handle", 2, ref(ARM_L), ARM_L); s.frame(dt=.2); s.frame(dt=.1); s.frame()
    s.edit("handle", 2, ref(ARM_R), ARM_R); s.frame(dt=.2); s.frame(dt=.1); s.frame()
    # nothing on the rig at all; weapons only
    s.handles(3, [RIG, BODY, GUN], fresh=True); s.table = 3; s.frame(); s.frame(dt=.3); s.frame(dt=.7); s.frame(dt=.3)
    s.handles(4, [RIG, BODY], fresh=True); s.table = 4; s.frame(); s.frame(dt=.3)
    s.handles(5, ALL, fresh=True); s.table = 5; s.frame(); s.frame()
    # the lookups of a steady view: half a second after it begins, then once a second; on every aim edge
    for dt in (.2, .2, .2, .3, .3, .3, .3, .3, .3): s.frame(dt=dt)
    for aim in ("aiming", "shoulder_aim"):
        s.snap[aim] = True; s.frame(); s.frame(dt=.3); s.frame(dt=.3); s.snap[aim] = False; s.frame(); s.frame(dt=.6)
    s.snap["aiming"] = True; s.snap["shoulder_aim"] = True; s.frame(); s.snap["aiming"] = False; s.frame(); s.snap["shoulder_aim"] = False; s.frame()
    # pieces and weapons that die, change size or lose their place
    for unit in (ARM_L, ARMOUR_L, ARM_R, ARMOUR_R, GUN, FOREGRIP, BACK):
        s.edit("alive", unit, False); s.frame(); s.frame(dt=.3); s.edit("alive", unit, True); s.frame(dt=.3); s.frame()
        s.edit("items", unit, 77); s.frame(); s.frame(dt=.3); s.edit("items", unit, len(UNITS[unit]["names"])); s.frame(dt=.3)
        s.edit("odd_alive", unit, True); s.frame(); s.edit("odd_alive", unit, False); s.frame()
    for unit in (ARM_L, ARM_R, GUN):
        s.edit("parent", unit, address(BODY)); s.frame(dt=1.1); s.frame(); s.edit("parent", unit, address(RIG)); s.frame(dt=1.1)
    s.edit("parent", ARM_L, address(RIG) + 2 ** 48); s.frame(dt=1.1); s.edit("parent", ARM_L, address(RIG))
    # the left hand's own bone missing from the arm; link modes other than 2; bones hung outside the arms
    s.edit("link", ARM_L, 4, 0); s.frame(dt=1.1); s.frame(); s.edit("link", ARM_L, 4, child(13)); s.frame(dt=1.1)
    s.edit("link", ARM_L, 4, on(45)); s.frame(dt=1.1); s.edit("link", ARM_L, 4, on(62)); s.frame(dt=1.1); s.edit("link", ARM_L, 4, on(63)); s.frame(dt=1.1)
    s.edit("link", ARM_L, 4, on(65535)); s.frame(dt=1.1); s.edit("link", ARM_L, 4, on(13)); s.frame(dt=1.1)
    s.edit("link", ARM_R, 4, on(41)); s.frame(dt=1.1); s.edit("link", ARM_R, 4, on(40)); s.frame(dt=1.1); s.edit("link", ARM_R, 4, on(34)); s.frame(dt=1.1)
    # fingers the body has no bone for, or two for
    s.edit("name", BODY, 3, 0x96); s.frame(dt=1.1); s.frame(); s.edit("name", BODY, 3, rig_name(14)); s.edit("name", BODY, 12, rig_name(15)); s.frame(dt=1.1)
    s.edit("name", BODY, 12, 0x26); s.frame(dt=1.1)
    # the rig's own parents: garbage, loops, an arm bone under the other arm
    s.edit("link", RIG, 21, child(63)); s.frame(dt=1.1); s.frame(); s.edit("link", RIG, 21, child(65535)); s.frame(dt=1.1)
    s.edit("link", RIG, 21, child(62)); s.frame(dt=1.1); s.edit("link", RIG, 21, 0)
    s.edit("link", RIG, 31, child(13)); s.frame(dt=1.1); s.frame(); s.edit("link", RIG, 31, child(7)); s.frame(dt=1.1)
    s.edit("link", RIG, 14, child(34)); s.frame(dt=1.1); s.edit("link", RIG, 14, child(12)); s.frame(dt=1.1)
    # the rig's weapon bone named twice, and the body's bones
    s.edit("name", RIG, 21, ATTACH); s.frame(dt=1.1); s.frame(); s.edit("name", RIG, 21, rig_name(20)); s.frame(dt=1.1)
    s.edit("name", BODY, 12, HAND_NAME); s.frame(dt=1.1); s.frame(); s.edit("name", BODY, 12, 0x26); s.frame(dt=1.1)
    # bone counts and unreadable memory, everywhere
    for unit in (RIG, BODY, ARM_L, GUN, FOREGRIP, GHOST):
        for count in (0, 256, 255, 1, len(UNITS[unit]["names"]) - 1):
            s.edit("count", unit, count); s.frame(dt=1.1); s.frame()
        s.edit("count", unit, False)
        for what in ("object", "count", "ptr_names", "names", "parent", "ptr_links", "links"):
            s.edit("bad", unit, what, True); s.frame(dt=1.1); s.frame(); s.op(active=False)
            s.edit("bad", unit, what, unit == GHOST and what == "object"); s.frame(); s.frame(dt=1.1)
    s.op("stop")
    return s.ops


def precision():
    """The spin's limits from both sides, and its clocks to the last bit. One-handed, the arms' own animation frozen."""
    s = started(1000.0)
    s.body_on = None
    D = math.radians
    def still(frames, dt=1 / 60, **kw):
        for _ in range(frames): s.frame(dt=dt, wob=False, **kw)
    def calm():                                              # out of the view (which forgets every sample and the spin), everything back
        s.op(active=False)
        s.spin = s.attach_shift = s.ref_turn = 0.0; s.flip = False
        s.right_off = (.012, .008, .004); s.attach_slide = s.rig_hand_off = s.ref_shift = s.right_far = (0.0, 0.0, 0.0)
        still(2)
    lean = [RIG, BODY, ARM_R, GUN, FOREGRIP, BACK]           # no left arm on the rig: shorter updates where the left hand has no part
    s.handles(9, lean, fresh=True); s.table = 9
    def turn(degrees, frames=1, dt=1 / 60, **kw):
        for _ in range(frames):
            s.spin += D(degrees); s.frame(dt=dt, wob=False, **kw)
    def begin(): still(3); turn(7)                           # 420 degrees a second for one frame: the spin begins
    def near(): s.ref_shift = sub(add(s.last_hand_r, (.05, 0.0, 0.0)), s.last_ref_base); still(2)   # the arm root 5 cm from the hand
    still(4)
    # the weapon turning or sliding in the body's hand: 400 degrees and 1.5 m a second
    for speed in (399.5, 400.5):
        still(3); turn(speed / 60, 3); calm()
    for speed in (1.45, 1.55):
        still(3)
        for i in range(1, 4):
            s.right_off = (.012 + speed / 60 * i, .008, .004); s.frame(wob=False)
        calm()
    # the rig's own animation: 45 degrees and 0.15 m a second, weapon bone or left hand, for half a second
    for speed in (44.5, 45.5):
        still(3)
        for _ in range(3):
            s.attach_shift += D(speed / 60); turn(7.5)
        calm()
    for speed in (.145, .155):
        for mover in ("attach_slide", "rig_hand_off"):
            still(3)
            for i in range(1, 4):
                setattr(s, mover, (0.0, speed / 60 * i * .6, speed / 60 * i * .8)); turn(7.5)
            calm()
    for mover in ("attach_slide", "rig_hand_off"):           # along each axis alone
        for axis in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
            still(3)
            for i in range(1, 3):
                setattr(s, mover, scale(axis, .155 / 60 * i)); turn(7.5)
            calm()
    for wait in (.45, .5, .55):
        still(3); s.attach_shift = .3; s.frame(wob=False); t0 = s.t
        for i in range(1, 9): s.lay(); s.op(at=t0 + .05 * i)
        s.lay(); s.op(at=t0 + wait - 1 / 60); s.spin += D(7.5); s.lay(); s.op(at=t0 + wait); turn(7.5, 2); calm()
    # ... forgotten when the clock goes back past it
    for back in (.005, .015, .025):
        still(3); s.attach_shift = .3; s.frame(wob=False); t0 = s.t
        s.lay(); s.op(at=t0 - back); turn(7.5, 3); calm()
    still(3); s.attach_shift = .3; s.frame(wob=False); t0 = s.t
    s.lay(); s.op(at=t0 + .5 - .5 - .01); turn(7.5, 3); calm()
    # samples a quarter second apart or at the same instant are not compared
    for gap in (.2499, .25, .27, 0.0):
        still(3); t0 = s.t; s.spin += D(150); s.lay(); s.op(at=t0 + gap); still(2); calm()                  # the body's weapon
        still(3); t0 = s.t; s.attach_shift += .4; s.lay(); s.op(at=t0 + gap); turn(7.5, 3); calm()          # the rig's
    # the same rotation as the negated quaternion is no turn
    still(3); s.flip = True; turn(2); still(1); s.flip = False; turn(2); still(1); calm()
    # what was last seen is forgotten with the view
    still(3); s.op(active=False); s.spin += D(30); still(2); calm()
    # away from where it began: 12 degrees or 3 cm, of the weapon in the hand or of the hand against the arm root
    for total in (11.5, 12.5):
        begin(); turn(total - 7); still(15, dt=.05); calm()
    for gap in (.025, .035):
        begin(); s.right_off = (.012, .008 + gap / 2, .004); turn(-3.5); s.right_off = (.012, .008 + gap, .004); turn(-3.5)
        still(15, dt=.05); calm()
    for gap in (.025, .035):
        begin(); s.ref_shift = (gap / 2, 0.0, 0.0); turn(-3.5); s.ref_shift = (gap, 0.0, 0.0); turn(-3.5); still(15, dt=.05); calm()
    for total in (11.5, 12.5):
        near(); begin(); s.ref_turn = D(total / 2); turn(-3.5); s.ref_turn = D(total); turn(-3.5); still(15, dt=.05); calm()
    # kept going by movement: 30 degrees or 0.1 m a second, until three seconds after the last of it
    for speed in (29.5, 30.5):
        begin(); turn(8.5)
        for i in range(38): turn(speed / 10 * (1 if i % 2 else -1), dt=.1)
        calm()
        near(); begin(); turn(8.5)
        for i in range(38):
            s.ref_turn = D(speed / 10) * (i % 2); s.frame(dt=.1, wob=False)
        calm()
    for speed in (.095, .105):
        begin(); turn(8.5)
        for i in range(38):
            s.right_off = (.012 + speed / 10 * (i % 2), .008, .004); s.frame(dt=.1, wob=False)
        calm()
        begin(); turn(8.5)
        for i in range(38):
            s.ref_shift = (0.0, speed / 10 * (i % 2), 0.0); s.frame(dt=.1, wob=False)
        calm()
    for last in (2.999, 3.0, 3.001):                         # exactly three seconds
        still(3); s.lay(); s.op(at=math.floor(s.t) + 2.0); t0 = s.t
        s.spin += D(30); s.lay(); s.op(at=t0 + .0625); s.spin += D(8.5); s.lay(); s.op(at=t0 + .125)
        for i in range(1, 24): s.lay(); s.op(at=t0 + .125 + .125 * i)
        s.lay(); s.op(at=t0 + .125 + last)
        for i in range(1, 8): s.lay(); s.op(at=t0 + .125 + last + .0625 * i)
        calm()
    # the clock a little back and on again while held away: the three seconds count from there
    still(3); s.lay(); s.op(at=math.floor(s.t) + 2.0); t0 = s.t
    s.spin += D(30); s.lay(); s.op(at=t0 + .0625); s.spin += D(8.5); s.lay(); s.op(at=t0 + .125)
    s.lay(); s.op(at=t0 + .12); s.lay(); s.op(at=t0 + .123)
    for i in range(1, 24): s.lay(); s.op(at=t0 + .123 + .125 * i)
    s.lay(); s.op(at=t0 + 3.124)
    for i in range(1, 6): s.lay(); s.op(at=t0 + 3.124 + .0625 * i)
    calm()
    # the arm root found while the weapon is held away and still: that is no movement
    s.edit("name", BODY, 10, 0x98); s.snap["shoulder_aim"] = True; still(3); begin(); turn(8.5); still(5, dt=.1)
    s.edit("name", BODY, 10, rig_name(30)); s.snap["shoulder_aim"] = False; still(33, dt=.1); calm()
    # the quarter second after the last turn, to the bit; the clock going back past it; two updates at one instant
    for after in (.2499, .25, .2501):
        begin(); t0 = s.t; s.lay(); s.op(at=t0 + after); still(3); calm()
    for back in (.005, .015, .025):
        begin(); t0 = s.t; s.lay(); s.op(at=t0 - back); still(3); calm()
    begin(); t0 = s.t; s.lay(); s.op(at=t0 + .25 - .25 - .01); still(3); calm()
    begin(); t0 = s.t; s.lay(); s.op(at=t0 + .24); s.lay(); s.op(at=t0 + .26); s.lay(); s.op(at=t0 + .26); still(3); calm()
    # the left hand aside: the end of the spin to the bit; the clock a little back; every weapon gone for one update
    s.table = 1; still(3)
    for after in (.2499, .25, .2501):
        begin(); turn(7.5, 6); still(3); t0 = s.t - 3 / 60; s.lay(); s.op(at=t0 + .2); s.lay(); s.op(at=t0 + after); still(3); calm()
    begin(); turn(7.5, 5); s.frame(dt=-.005, wob=False); turn(7.5, 3); calm()
    begin(); turn(7.5, 8); still(18)
    for unit in (GUN, FOREGRIP, BACK): s.edit("alive", unit, False)
    still(1)
    for unit in (GUN, FOREGRIP, BACK): s.edit("alive", unit, True)
    turn(7.5, 4); calm()
    # the rig's animation is forgotten with the view and at stop
    for leave in ("update", "stop"):
        still(3); s.attach_shift = .3; s.frame(wob=False)
        s.op(leave, active=False) if leave == "update" else s.op("stop")
        still(1); turn(7.5, 3); calm()
    # the right arm's correction ends in the middle of a spin: hand away, body gone, arm straight
    begin(); turn(7.5, 3); s.ref_shift = (.4, 0.0, 0.0); still(2); calm()
    begin(); turn(7.5, 3); s.edit("alive", BODY, False); still(1); s.edit("alive", BODY, True); still(2); calm()
    begin(); turn(7.5, 3); s.right_off = (.08, -.10, -.15); turn(7.5, 2); calm()      # the target on the line of the upper arm
    # another weapon unit on the weapon bone in the middle of a spin, seen by a lookup in the same update
    begin(); turn(7.5, 3)
    s.edit("handle", 1, ref(GUN), False); s.edit("handle", 1, ref(GUN2), GUN2); s.snap["shoulder_aim"] = True
    s.spin += D(7.5); s.lay(gun=GUN2); s.op()
    for _ in range(3):
        s.spin += D(7.5); s.lay(gun=GUN2); s.op()
    s.edit("handle", 1, ref(GUN), GUN); s.edit("handle", 1, ref(GUN2), False); s.snap["shoulder_aim"] = False; calm()
    # the arm root found in the very update the spin begins, and lost while it lasts
    s.edit("name", BODY, 10, 0x98); s.snap["shoulder_aim"] = True; still(3)
    s.edit("name", BODY, 10, rig_name(30)); s.snap["shoulder_aim"] = False; turn(7.5, 4); calm()
    begin(); turn(7.5, 3); s.edit("name", BODY, 10, 0x98); s.snap["shoulder_aim"] = True; turn(7.5, 3)
    s.edit("name", BODY, 10, rig_name(30)); s.snap["shoulder_aim"] = False; calm()
    s.op("stop")
    return s.ops


def edges():
    """Lookup times to the last bit, with a clock that starts at zero."""
    s = started(-5.0)
    def at(time, **kw): s.lay(); return s.op(at=time, **kw)
    armour_only = [RIG, BODY, ARMOUR_L, ARMOUR_R, GUN, FOREGRIP]
    # something missing: every update for 0.6 s, then every quarter second
    s.handles(2, armour_only, fresh=True); s.table = 2
    at(0.0); at(0.0); at(.5); at(.6); at(.7); at(.75); at(.75); at(.8); at(1.0); at(1.0); at(1.2); at(1.25)
    s.op(active=False)
    at(2.0); at(2.59); at(2.61); at(1.99); at(2.0); at(2.1)   # before the view began: no quick window
    s.op(active=False)
    # nothing missing: half a second after the view begins, then once a second
    s.table = 1
    at(10.0); at(10.5 - 1e-9); at(10.5); at(11.5 - 1e-9); at(11.5); at(12.5); at(12.5)
    at(13.5 - 1 - .015); at(13.0); at(14.485 - 1e-9); at(14.485); at(15.485 - 1 - .005); at(15.485)
    at(15.485 + 1 - 1 - .01); at(17.0)
    s.op(active=False)
    at(20.0); at(19.985); at(20.5); at(20.9); at(20.985)       # the clock back past the beginning: confirmed at once, not again
    s.op(active=False)
    at(30.0); at(29.995); at(30.5); at(31.0)
    s.op(active=False)
    at(40.0); at(40.0 + .5 - .5 - .01); at(40.5); at(41.0)
    # aiming begins: again half a second later
    s.snap["shoulder_aim"] = True; at(50.0); at(50.5 - 1e-9); at(50.5); s.snap["shoulder_aim"] = False; at(50.6); at(51.1 - 1e-9); at(51.1)
    # another weapon: at once, then 0.1, 0.25 and 0.5 s later, each once; all at once when the clock goes back a second
    for times in ((60.1 - 1e-9, 60.1, 60.2, 60.25, 60.4, 60.5, 60.6), (60.1 - 1.5, 58.61, 58.62, 58.63), (60.1 - 1, 59.2, 59.5, 59.6, 59.7),
                  (60.1 - .5, 60.1 - 1 - 1e-9, 59.0, 59.3, 59.4), (60.7, 60.7, 60.7, 60.7)):
        s.snap["weapon"] += 1; at(60.0)
        for time in times: at(time)
        s.op(active=False); at(55.0); at(56.0)
    s.snap["weapon"] += 1; at(70.0); s.op(active=False); at(70.02); at(70.11); at(70.26); at(70.51); at(71.03)
    s.op("stop")
    return s.ops


def memory():
    """What is remembered about a weapon, and bone tables that put the first bone to work."""
    s = started()
    def frames(count=2, **kw):
        for _ in range(count): s.frame(wob=False, **kw)
    frames()
    # the weapon grows a bone while held: what was remembered was for the old count
    s.handles(7, [RIG, BODY, ARM_L, ARMOUR_L, ARM_R, ARMOUR_R, GUN], fresh=True); s.table = 7; frames()
    s.edit("names", GUN, [0x71, GRIP, 0x72, GRIP, 0x73]); s.edit("items", GUN, 5); frames(3)
    s.op(active=False); s.hand_off = (0.0, .2, .1); s.snap["weapon"] = 40; frames(3); s.hand_off = (0.0, 0.0, 0.0); frames()
    s.edit("names", GUN, list(UNITS[GUN]["names"])); s.edit("items", GUN, 4); frames(3); s.table = 1; frames()
    # two grip bones both held before; the hand leaves: the bone in use stays, whichever is first in the table
    for first, second in ((GUN, FOREGRIP), (FOREGRIP, GUN)):
        s.body_on = first; frames(); s.body_on = second; frames(); s.body_on = first; frames()
        s.hand_off = (0.0, .25, 0.0); frames(3); s.hand_off = (0.0, 0.0, 0.0); frames()
    s.body_on = GUN; frames()
    # every weapon gone for one update while the hand is off the bone: the finger shape goes with it
    s.hand_off = (0.0, .25, 0.0); frames()
    for unit in (GUN, FOREGRIP, BACK): s.edit("alive", unit, False)
    frames(1)
    for unit in (GUN, FOREGRIP, BACK): s.edit("alive", unit, True)
    frames(3); s.hand_off = (0.0, 0.0, 0.0); frames()
    # sixteen weapons held one after the other, each with its own finger shape; then each again with the hand elsewhere
    guns = list(range(50, 66))
    for unit in guns:
        s.edit("unit", unit, dict(ref=ref(unit), address=address(unit), names=[0x71, GRIP], links=[0, 0], parent=address(BODY)))
    s.handles(8, [RIG, BODY, ARM_L, ARMOUR_L, ARM_R, ARMOUR_R] + guns, fresh=True); s.table = 8
    def hold(held):
        s.lay()
        for unit in guns:
            s.pose(unit, 2, s.last_grip if unit == held else (IDENTITY, (float(unit), 0.0, 0.0)))
        s.op()
    for index, unit in enumerate(guns):
        s.curl = .1 + .03 * index; hold(unit); hold(unit)
    s.hand_off = (0.0, .2, .1)
    for index, unit in enumerate(guns + guns[:3]):
        s.snap["weapon"] = 100 + index; hold(unit); hold(unit)
    s.hand_off = (0.0, 0.0, 0.0); s.handles(1, ALL); s.table = 1; frames()
    # the hand's own bone first in the arm piece, a finger first of all, the armour alone on the right
    s.edit("links", ARM_L, [on(13), on(11), on(12), on(10), on(11), on(11), on(11), on(10), 0, on(11)]); s.frame(dt=1.1, wob=False); frames()
    s.edit("links", ARM_L, [on(14), on(11), on(12), on(13)]); s.frame(dt=1.1, wob=False); frames()
    s.edit("links", ARM_L, list(UNITS[ARM_L]["links"]))
    s.edit("links", ARM_R, [on(34), on(31), on(31), on(31), on(31), on(31), on(31)]); s.frame(dt=1.1, wob=False); frames()
    s.edit("links", ARM_R, [0, 0, 0, 0, 0, 0, 0]); s.edit("links", SLEEVES, [0, 0, 0, 0, 0, 0, 0]); s.frame(dt=1.1, wob=False); frames()
    s.edit("links", ARM_R, list(UNITS[ARM_R]["links"])); s.edit("links", SLEEVES, list(UNITS[SLEEVES]["links"])); s.frame(dt=1.1, wob=False)
    # rig bone 0 under the left hand, a long chain of parents, arm bones under the other arm
    s.edit("link", RIG, 1, child(13)); s.edit("link", ARM_L, 9, on(0)); s.frame(dt=1.1, wob=False); frames()
    s.edit("link", RIG, 1, 0); s.edit("link", ARM_L, 9, 0)
    chain = list(range(62, 36, -1)) + list(range(29, 19, -1))
    for bone, parent in zip(chain, chain[1:] + [13]): s.edit("link", RIG, bone + 1, child(parent))
    s.edit("link", ARM_L, 9, on(62)); s.frame(dt=1.1, wob=False); frames()
    for bone in chain: s.edit("link", RIG, bone + 1, RIG_LINKS[bone])
    s.edit("link", ARM_L, 9, 0)
    for bone, parent in ((10, 34), (12, 34), (13, 34), (30, 13), (32, 13), (34, 13)):
        s.edit("link", RIG, bone + 1, child(parent)); s.frame(dt=1.1, wob=False); frames(1); s.edit("link", RIG, bone + 1, RIG_LINKS[bone])
    s.frame(dt=1.1, wob=False)
    # the weapon bone as the rig's first bone; a body whose first bone is a finger; the arm root's parent just past the table
    s.edit("name", RIG, 1, ATTACH); s.edit("name", RIG, 9, 0x94); s.edit("link", GUN, 1, on(0)); s.frame(dt=1.1, wob=False); frames()
    s.edit("name", RIG, 1, rig_name(0)); s.edit("name", RIG, 9, ATTACH); s.edit("link", GUN, 1, on(8))
    s.edit("name", BODY, 1, rig_name(16)); s.edit("name", BODY, 5, 0x95); s.frame(dt=1.1, wob=False); frames()
    s.edit("name", BODY, 1, 0x21); s.edit("name", BODY, 5, rig_name(16))
    for parent in (11, 12, 13):
        s.edit("link", BODY, 10, child(parent)); s.frame(dt=1.1, wob=False); frames(1)
    s.edit("link", BODY, 10, child(10)); s.frame(dt=1.1, wob=False)
    # an arm that cannot bend: the grip on the line of the upper arm (left), the weapon's hold on it (right); the grip at the hand itself
    e, f = (.02 + .01 * math.sin(s.wob), .27, -.12), (.10, .24, .08 + .01 * math.cos(s.wob))
    frames(); s.straight = True; s.grip_off = scale(add(e, f), .04); s.right_off = (-.0032, .0168, -.0036); frames(3)
    s.straight = False; s.grip_off = (.02, .02, 0.0); s.right_off = (.012, .008, .004); frames()
    s.grip_off = (0.0, 0.0, 0.0); frames(3)
    # the hand turned half round about an axis across the forearm, give or take five millionths: no roll to give the forearm
    for w in (5e-6, -5e-6, 0.0, 2e-5):
        axis = scale(s.last_forearm, 1 / math.sqrt(sum(c * c for c in s.last_forearm)))
        across = (axis[1], -axis[0], 0.0)
        n = math.sqrt(sum(c * c for c in across)); k = math.sqrt(1 - w * w) / n
        s.grip_q = qmul((across[0] * k, across[1] * k, 0.0, w), s.last_hand_l[0]); frames()
    s.grip_q = None; s.grip_off = (.02, .02, 0.0); frames()
    # a very small first step: the bone newly held, a few microseconds after the last update
    for dt in (2e-6, 5e-6, 1e-5, 2e-5, 4e-5, 8e-5, 1.6e-4, 3e-4):
        s.snap["weapon"] += 1; s.hand_off = (0.0, .1, 0.0); frames(); s.hand_off = (0.0, 0.0, 0.0); s.frame(dt=dt, wob=False); frames(1)
    s.op("stop")
    return s.ops


def vanishing():
    """Engine functions that go missing in the middle of a session, and come back."""
    s = started()
    def spin():
        s.body_on = None; s.frame(); s.frame()
        for _ in range(4):
            s.spin += .45; s.frame()
    for name in ("Unit", "Vector3", "Quaternion", "Matrix4x4", "Mesh", "Quaternion_lerp", "Matrix4x4_rotation", "Matrix4x4_identity",
                 "Unit_set_local_pose", "Unit_alive", "Unit_num_scene_graph_items", "Unit_set_mesh_visibility", "Unit Mesh",
                 "Vector3 Quaternion Matrix4x4"):
        for state in ("held", "spinning"):
            for action in ("update", "leave", "stop"):
                s.engine(); s.spin = 0.0; s.body_on = GUN; s.handles(1, ALL, fresh=True); s.frame(); s.frame()
                if state == "spinning": spin()
                s.engine(*name.split())
                if action == "update": s.frame(); s.op(active=False); s.op("stop")
                elif action == "leave": s.op(active=False); s.op("clean"); s.op("stop")
                else: s.op("stop"); s.op("clean"); s.frame()
                s.engine(); s.frame(); s.op("stop")
    return s.ops


def vanish():
    s = started()
    s.frame(); s.frame()
    for unit in (RIG, BODY):
        s.edit("alive", unit, False); s.frame(); s.frame(); s.op(active=False); s.frame(); s.op("stop"); s.frame()
        s.edit("alive", unit, True); s.frame(); s.frame()
        s.edit("items", unit, 62); s.frame(); s.frame(); s.op("stop"); s.edit("items", unit, len(UNITS[unit]["names"])); s.frame(); s.frame()
    # another rig under the same reference, with its own meshes: the screen moves with it
    s.edit("unit", RIG2, dict(ref=ref(RIG), address=address(RIG), names=list(RIG_NAMES), links=list(RIG_LINKS),
                              meshes=[dict(visible=True, materials=2), dict(visible=True, materials=2)]))
    s.edit("handle", 1, ref(RIG), RIG2); s.lay(rig=RIG2); s.op(); s.lay(rig=RIG2); s.op(); s.lay(rig=RIG2); s.op(dt=1.1)
    s.edit("alive", RIG, False); s.edit("handle", 1, ref(RIG), RIG); s.frame(); s.edit("alive", RIG, True); s.frame(); s.frame()
    s.edit("handle", 1, ref(RIG), RIG2); s.edit("alive", RIG, False); s.lay(rig=RIG2); s.op(); s.edit("alive", RIG, True)
    s.edit("handle", 1, ref(RIG), RIG); s.frame(); s.frame()
    # the body's handle changes in place: nobody looks until the next lookup
    s.edit("unit", BODY2, dict(ref=ref(BODY), address=address(BODY), names=list(BODY_NAMES), links=list(BODY_LINKS)))
    s.edit("handle", 1, ref(BODY), BODY2); s.frame(); s.lay(body=BODY2); s.op(); s.lay(body=BODY2); s.op(dt=1.1); s.lay(body=BODY2); s.op()
    s.edit("handle", 1, ref(BODY), BODY); s.frame(dt=1.1); s.frame()
    s.edit("handle", 1, ref(BODY), False); s.frame(dt=1.1); s.frame(); s.edit("handle", 1, ref(BODY), BODY); s.frame(dt=1.1)
    s.edit("handle", 1, ref(RIG), False); s.frame(); s.frame(); s.edit("handle", 1, ref(RIG), RIG); s.frame()
    # everything gone while corrected, then stop: nothing may be written
    for unit in (ARM_L, ARMOUR_L, ARM_R, ARMOUR_R, GUN): s.edit("alive", unit, False)
    s.op("stop"); s.op("clean")
    for unit in (ARM_L, ARMOUR_L, ARM_R, ARMOUR_R, GUN): s.edit("alive", unit, True)
    s.frame(); s.frame(); s.edit("alive", RIG, False); s.op("stop"); s.op("clean"); s.edit("alive", RIG, True); s.frame()
    s.edit("alive", RIG, False); s.op(active=False); s.edit("alive", RIG, True); s.frame(); s.op("stop")
    return s.ops


def rig_screen():
    s = started()
    s.frame(); s.frame()
    # the left arm's correction ends and starts: the mesh comes back and goes
    s.rig_hand_off = (0.0, -.6, 0.0); s.frame(); s.frame(); s.rig_hand_off = (0.0, 0.0, 0.0); s.frame()
    # meshes change while hidden: only what was hidden is shown again
    s.edit("mesh", RIG, 5, True, 2); s.edit("mesh", RIG, 2, True, 2); s.op(active=False); s.frame(); s.op(active=False)
    for count in (0, 1, 2.5, 3, -1, NAN, 6):
        s.edit("mesh_count", RIG, count); s.frame(); s.frame(); s.op(active=False)
    s.edit("mesh_count", RIG, False)
    for visible, materials in ((False, 2), (True, 3), (True, 2.0), (1, 2), (True, NAN)):
        s.edit("mesh", RIG, 1, visible, materials); s.frame(); s.op(active=False)
    s.edit("mesh", RIG, 1, True, 2)
    # the rig goes while its mesh is hidden: nothing to show again
    s.frame(); s.edit("alive", RIG, False); s.frame(); s.edit("alive", RIG, True); s.frame(); s.frame()
    s.frame(); s.edit("items", RIG, 60); s.op(active=False); s.edit("items", RIG, 63); s.frame()
    s.frame(); s.edit("alive", RIG, False); s.op("stop"); s.op("clean"); s.edit("alive", RIG, True); s.frame(); s.op("stop"); s.op("clean")
    # 70 meshes: more than anyone has seen
    for index in range(1, 71): s.edit("mesh", RIG, index, True, 2)
    s.frame(); s.frame(); s.op(active=False); s.frame(); s.op("stop")
    return s.ops


def errors():
    """Every engine and backend function failing at every call of a lookup, a correcting update, a release and a stop."""
    s = started()
    def settle(): s.frame(); s.op("stop")
    def fresh(): s.handles(1, ALL, fresh=True)
    def steady(): fresh(); s.frame()
    def sweep(setup, action, limits, stride=1):
        for name, limit in limits.items():
            for nth in range(1, limit + 1, stride):
                setup()
                s.wob += .03; s.lay()
                action(fail=[name, nth, ERRORS[(nth + len(name)) % len(ERRORS)]])
                settle()
    sweep(fresh, lambda **kw: s.op(**kw), dict(obj=12, u32=11, ptr=20, rd=27, al=17, ns=9, wp=40, wo=14, wr=9, sp=20, nm=1, ms=4, sv=2, mv=4, mm=3,
                                               clk=1))
    sweep(steady, lambda **kw: s.op(**kw), dict(al=16, ns=16, wp=40, wo=14, wr=9, sp=20, clk=1))
    sweep(steady, lambda **kw: s.op(active=False, **kw), dict(al=7, ns=7, sp=20, sv=2))
    sweep(steady, lambda **kw: s.op("stop", **kw), dict(al=7, ns=7, sp=20, sv=2))
    def aimed(): steady(); s.snap["aiming"] = not s.snap["aiming"]
    sweep(aimed, lambda **kw: s.op(**kw), dict(obj=12, u32=11, ptr=20, rd=27, al=22, ns=14, sp=38), stride=3)
    s.snap["aiming"] = False
    # while the weapon is turned in the hand
    def spinning():
        steady(); s.body_on = None; s.frame()
        for _ in range(2):
            s.spin += .45; s.frame()
        s.spin += .45
    def calm(): s.spin = 0.0; s.body_on = GUN; settle()
    for name, limit in dict(al=16, ns=16, wp=44, wo=14, wr=9, sp=22, clk=1).items():
        for nth in range(1, limit + 1, 3):
            spinning(); s.lay(); s.op(fail=[name, nth, ERRORS[nth % 4]]); s.frame(); calm()
    for call, kw in (("update", dict(active=False)), ("stop", {})):
        for name, limit in dict(al=8, ns=8, sp=22, sv=2).items():
            for nth in range(1, limit + 1, 2):
                spinning(); s.lay(); s.op(call, fail=[name, nth, ERRORS[nth % 4]], **kw); s.frame(); calm()
    # the value maths failing: every call of a lookup-and-correct update in turn, then of a steady one, a release and a stop
    for setup, kw, count, stride in ((fresh, {}, 1150, 11), (steady, {}, 1150, 11), (steady, dict(active=False), 20, 1), (steady, dict(call="stop"), 20, 1)):
        for nth in range(1, count + 1, stride):
            setup()
            s.wob += .03; s.lay()
            s.op(mfail=[nth, ERRORS[nth % len(ERRORS)]], **kw)
            settle()
    for nth in range(1, 1400, 37):
        spinning(); s.lay(); s.op(mfail=[nth, ERRORS[nth % 4]]); s.frame(); calm()
    return s.ops


def walk(seed, count):
    rng = random.Random(seed)
    s = started()
    key, units, active = 1, list(ALL), True
    broken = []
    mode, left = "hold", 0
    for _ in range(count):
        dt = rng.choice((1 / 60, 1 / 60, 1 / 60, 1 / 30, 1 / 144, .05, .12))
        if rng.random() < .02: dt = rng.choice((.26, .6, 1.2, -.3, -5, 30))
        s.yaw += rng.uniform(-.05, .05)
        s.o = add(s.o, (rng.uniform(-.03, .03), rng.uniform(-.03, .03), rng.uniform(-.01, .01)))
        s.curl = rng.uniform(.1, .6)
        # what the player is doing
        if left <= 0:
            mode = rng.choice(("hold", "hold", "hold", "wander", "reload", "foregrip", "one", "spin", "spin", "slide", "rig_anim"))
            left = rng.randint(3, 40)
            s.hand_off = s.rig_hand_off = s.right_far = (0.0, 0.0, 0.0); s.body_on = GUN; s.attach_shift = 0.0
            s.right_off = (.012, .008, .004)
            if mode == "wander": s.hand_off = (rng.uniform(-.3, .3), rng.uniform(-.3, .3), rng.uniform(-.02, .02))
            elif mode == "reload": s.rig_hand_off = (0.0, -rng.uniform(.1, .7), 0.0); s.right_far = (0.0, 0.0, rng.choice((0.0, 0.0, .25, .5)))
            elif mode == "foregrip": s.body_on = FOREGRIP
            elif mode in ("one", "spin", "slide", "rig_anim"): s.body_on = None
        left -= 1
        if mode == "spin": s.spin += rng.choice((.45, .45, .3, .1, 0.0, -.45))
        elif mode == "slide": s.right_off = (.012 + rng.uniform(0, .1), .008, .004)
        elif mode == "rig_anim": s.attach_shift += rng.uniform(0, .3); s.spin += .45
        elif rng.random() < .3: s.spin = 0.0
        if mode == "wander" and rng.random() < .3: s.hand_off = (0.0, 0.0, 0.0)
        kw = {}
        e = rng.random()
        if e < .03: active = not active
        elif e < .06: s.snap["aiming"] = not s.snap["aiming"]
        elif e < .07: s.snap["shoulder_aim"] = not s.snap["shoulder_aim"]
        elif e < .10: s.snap["weapon"] = rng.choice((5, 6, 7, None))
        elif e < .12: s.snap["weapon_transition"] = not s.snap["weapon_transition"]
        elif e < .14:
            unit = rng.choice(units[1:]); s.edit("alive", unit, False); broken.append(["alive", unit, True])
        elif e < .15:
            unit = rng.choice(units); s.edit("items", unit, rng.choice((1, 3, 70))); broken.append(["items", unit, len(UNITS[unit]["names"])])
        elif e < .17:
            key += 1; s.table = key; rng.shuffle(units); s.handles(key, units, fresh=True)
        elif e < .20:
            units = [RIG, BODY] + rng.sample(ALL[2:], rng.randint(0, 9)); rng.shuffle(units); s.handles(s.table, units)
        elif e < .21:
            unit = rng.choice((RIG, BODY, ARM_L, GUN)); s.edit("handle", s.table, ref(unit), False); broken.append(["handle", s.table, ref(unit), unit])
        elif e < .24:
            unit = rng.choice(ALL); what = rng.choice(("object", "count", "ptr_names", "names", "parent", "ptr_links", "links"))
            s.edit("bad", unit, what, True); broken.append(["bad", unit, what, unit == GHOST and what == "object"])
        elif e < .26:
            unit = rng.choice((ARM_L, ARM_R, ARMOUR_L, GUN)); links = UNITS[unit]["links"]; index = rng.randrange(len(links))
            s.edit("link", unit, index + 1, rng.choice((0, on(13), on(34), on(8), on(12), child(13), on(70)))); broken.append(["link", unit, index + 1, links[index]])
        elif e < .27:
            index = rng.randrange(63); s.edit("link", RIG, index + 1, rng.choice((child(13), child(34), child(63), child(index), 0)))
            broken.append(["link", RIG, index + 1, RIG_LINKS[index]])
        elif e < .28:
            unit = rng.choice((BODY, GUN, RIG)); names = UNITS[unit]["names"]; index = rng.randrange(len(names))
            s.edit("name", unit, index + 1, rng.choice((0x99, GRIP, HAND_NAME, ATTACH))); broken.append(["name", unit, index + 1, names[index]])
        elif e < .29:
            unit = rng.choice(units); s.edit("count", unit, rng.choice((0, 1, 2, 300))); broken.append(["count", unit, False])
        elif e < .31: kw["fail"] = [rng.choice(CALLS), rng.randint(1, 40), rng.choice(ERRORS)]
        elif e < .33: kw["mfail"] = [rng.randint(1, 1300), rng.choice(ERRORS)]
        elif e < .335: kw["clock"] = 1
        elif e < .345: kw["s"] = rng.choice((None, dict(s.snap, body=None), dict(s.snap, viewmodel=ref(GUN)), dict(s.snap, viewmodel=None)))
        elif e < .35: kw["h"] = None
        elif e < .36:
            s.edit("mesh", RIG, rng.randint(1, 5), rng.random() < .7, rng.choice((1, 2, 2)))
        if broken and rng.random() < .3:
            s.edit(*broken.pop(rng.randrange(len(broken))))
        s.wob += dt * 2
        s.lay()
        if rng.random() < .01: s.edit(*pose_edit(rng.choice((RIG, BODY, GUN)), rng.randint(1, 14), IDENTITY, (NAN, 0, 0)))
        if rng.random() < .01: s.edit(*pose_edit(RIG, rng.choice((11, 13, 14, 31, 33, 35)), random_rotation(rng), s.o))   # an arm folded to a point
        c = rng.random()
        if c < .012: s.op("stop", dt=dt, **{k: v for k, v in kw.items() if k in ("fail", "mfail")})
        elif c < .025: s.op(rng.choice(("clean", "result")), dt=dt)
        else: s.op(dt=dt, active=active, **kw)
    s.op("stop")
    return s.ops


MISSING = ["Unit_alive", "Unit_num_scene_graph_items", "Unit_world_pose", "Unit_world_position", "Unit_world_rotation", "Unit_set_local_pose",
           "Unit_num_meshes", "Unit_mesh", "Unit_set_mesh_visibility", "Mesh_visibility", "Mesh_num_materials", "Mesh",
           "Vector3_call", "Vector3_length", "Vector3_dot", "Vector3_cross", "Vector3_normalize", "Vector3_lerp", "Vector3_x", "Vector3_y",
           "Vector3_z", "Quaternion_identity", "Quaternion_axis_angle", "Quaternion_multiply", "Quaternion_rotate", "Quaternion_lerp",
           "Quaternion_inverse", "Quaternion_to_elements", "Quaternion_from_elements", "Matrix4x4_identity",
           "Matrix4x4_from_quaternion_position", "Matrix4x4_multiply", "Matrix4x4_inverse", "Matrix4x4_translation", "Matrix4x4_rotation",
           "Unit", "Matrix4x4", "Vector3", "Quaternion", "World", "Application_main_world"]


def without():
    s = started()
    s.ops += [dict(call="solve", S=[0.0, 0.0, 0.0], E=[.3, 0.0, .14], H=[.55, 0.0, -.05], T=list(map(float, T)))
              for T in ((.5, .02, -.08), (2, 0, 0), (0, 0, 0), (-.4, .1, .2))]
    tour(s)
    return s.ops


def build_runs():
    runs = [dict(name="module_functions", ops=module_functions()), dict(name="basic", ops=basic()), dict(name="grip_logic", ops=grip_logic()),
            dict(name="weapon_swap", ops=weapon_swap()), dict(name="one_handed", ops=one_handed()), dict(name="pieces", ops=pieces()),
            dict(name="vanish", ops=vanish()), dict(name="rig_screen", ops=rig_screen()), dict(name="errors", ops=errors())]
    runs += [dict(name="precision", ops=precision()), dict(name="edges", ops=edges()), dict(name="memory", ops=memory()),
             dict(name="vanishing", ops=vanishing())]
    runs += [dict(name="without_" + name, ops=without(), missing=[name]) for name in MISSING]
    for table in ("Vector3", "Unit"):
        names = [name for name in MISSING if name.startswith(table + "_")]
        runs += [dict(name="only_" + name, ops=without()[:14], missing=[other for other in names if other != name]) for name in names]
    runs += [dict(name=f"walk{seed}", ops=walk(seed, 900)) for seed in (1, 2, 3)]
    return runs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runner", type=Path, default=HERE / "run_grip_hand.lua", help="for the coverage review: a wrapper of the runner")
    args = parser.parse_args()
    commit = frozen_commit(args.retail)
    runs = build_runs()
    lines = run(args.runner, args.retail, runs)
    write_header(args.out, "grip_hand", "Each run starts a fresh world.", commit, args.retail / "src/first_person/grip_hand.lua", runs, lines)
    for r in runs:
        if not r["name"].startswith(("without_", "only_")): print(f"{r['name']}: {len(r['ops'])} operations")
    print(f"grip hand: {sum(len(r['ops']) for r in runs)} operations in {len(runs)} runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
