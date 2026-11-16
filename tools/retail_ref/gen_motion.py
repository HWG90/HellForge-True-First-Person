#!/usr/bin/env python3
"""Cut reference vectors for src/motion_math.c from the retail motion.lua, run
unmodified under LuaJIT by run_motion.lua.

Writes tests/vectors/motion_vectors.h: every operation and what the retail
module answered (numbers as exact doubles), plus the retail commit and the
file's SHA-256.

  gen_motion.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
  gen_motion.py --retail ... --script-only FILE   (write the Lua script only; used by the coverage check)
"""
import argparse
import hashlib
import math
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
FROZEN = "1d0d453"
NAN, INF = float("nan"), float("inf")
SETTINGS = ["bob_strength", "bob_speed", "sway_strength", "sway_speed", "sprint_strength", "camera_bob_strength",
            "camera_bob_speed", "camera_sway_strength", "camera_sway_speed"]


class Other:
    """A Lua value of a type the field does not expect (a string where a number or boolean belongs)."""
    def __repr__(self):
        return "OTHER"


class Truthy:
    """The number 1 where retail tests '== true': true-ish in Lua, yet not true."""
    def __repr__(self):
        return "TRUTHY"


OTHER, TRUTHY = Other(), Truthy()


def lua(value):
    if value is None:
        return "nil"
    if value is OTHER:
        return "'x'"
    if value is TRUTHY:
        return "1"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        value = float(value)
        if value != value:
            return "(0/0)"
        if value in (INF, -INF):
            return "math.huge" if value > 0 else "(-math.huge)"
        return repr(value)
    if isinstance(value, str):
        return '"%s"' % value
    if isinstance(value, dict):
        return "{" + ",".join(f"{k}={lua(v)}" for k, v in value.items() if v is not None) + "}"
    raise TypeError(value)


def c_double(value):
    if value != value:
        return "NAN"
    if value in (INF, -INF):
        return "INFINITY" if value > 0 else "-INFINITY"
    return float(value).hex()


# ------------------------------------------------------------------ building
class Script:
    def __init__(self):
        self.ops = []
        self.owner_keys = {}

    def new(self, options=None):
        self.ops.append(dict(op="new", options=options))

    def configure(self, value):
        self.ops.append(dict(op="configure", options=value))

    def update(self, sample, dt):
        self.ops.append(dict(op="update", sample=None if sample is None else dict(sample), dt=dt))

    def reset(self):
        self.ops.append(dict(op="reset"))

    def clear_ads(self):
        self.ops.append(dict(op="clear_ads"))

    def state(self):
        self.ops.append(dict(op="state"))


def sample(x=0.0, y=0.0, yaw=0.0, owner="local", z=0.0, pitch=0.0, **more):
    return dict(owner=owner, x=x, y=y, z=z, yaw=yaw, pitch=pitch, **more)


def fixtures(s):
    """The input sequences of the retail fixture tests/first_person/motion.lua, test by test."""
    aim = dict(shoulder_aim_enabled=True)
    # idle has no fabricated bob and first sample never jumps
    s.new(aim); s.update(sample(), 1 / 60)
    for _ in range(180):
        s.update(sample(), 1 / 60)
    # gait phase follows horizontal distance at different frame rates
    for rate in (30, 60, 120):
        s.new(aim); s.update(sample(), 1 / rate)
        for i in range(1, rate * 2 + 1):
            s.update(sample(0, 3 * i / rate), 1 / rate)
    # turn inertia is bounded and yaw wrapping does not create a kick
    s.new(aim); s.update(sample(0, 0, math.pi - .01), 1 / 60)
    for _ in range(121):
        s.update(sample(0, 0, -math.pi + .01), 1 / 60)
    # walking and strafing keep native weapon rotation unchanged
    s.new(aim); s.update(sample(), 1 / 60)
    for i in range(1, 61):
        s.update(sample(i * .05, i * .025, i * .001), 1 / 60)
    # sprint has no built-in placement or rotation
    s.new(aim); s.update(sample(), 1 / 60)
    for i in range(1, 121):
        s.update(sample(0, 5 * i / 60, sprint=True), 1 / 60)
    for _ in range(180):
        s.update(sample(0, 10), 1 / 60)
    # sprint release hold bridges brief false samples then eases out without doubled bob
    s.new(aim); s.update(sample(), 1 / 60)
    for i in range(1, 241):
        s.update(sample(0, 6 * i / 60, sprint=True), 1 / 60)
    for i in range(1, 21):
        s.update(sample(0, 24 + .1 * i), 1 / 60)
    # shoulder aim eases sprint rotation to neutral while full ADS is exact immediately
    s.new(aim); s.update(sample(), 1 / 60)
    for _ in range(180):
        s.update(sample(sprint=True), 1 / 60)
    for _ in range(121):
        s.update(sample(sprint=True, shoulder_aim=True), 1 / 60)
    s.update(sample(sprint=True, shoulder_aim=False, ads=True), 1 / 60)
    # normal shoulder aim smoothly moves left and toward the camera with no extra height
    s.new(aim); s.update(sample(), 1 / 60)
    for _ in range(121):
        s.update(sample(shoulder_aim=True), 1 / 60)
    for _ in range(120):
        s.update(sample(shoulder_aim=False), 1 / 60)
    # full ADS clears shoulder placement immediately and leaves no residual on release
    s.new(aim); s.update(sample(), 1 / 60)
    for _ in range(120):
        s.update(sample(shoulder_aim=True), 1 / 60)
    for _ in range(31):
        s.update(sample(shoulder_aim=True, ads=True), 1 / 60)
    s.update(sample(shoulder_aim=False, ads=False), 1 / 60)
    # shoulder attack and release match elapsed time at 30 and 120 FPS independently of motion scale
    for rate in (30, 120):
        s.new(aim); s.update(sample(), 1 / rate)
        for _ in range(rate * 2):
            s.update(sample(shoulder_aim=True), 1 / rate)
        for _ in range(rate * 2):
            s.update(sample(shoulder_aim=False), 1 / rate)
    s.new(dict(scale=.5, shoulder_aim_enabled=True)); s.update(sample(), 1 / 60)
    for _ in range(120):
        s.update(sample(shoulder_aim=True), 1 / 60)
    s.configure(dict(enabled=False)); s.update(sample(shoulder_aim=True), 1 / 60)
    # aim stance survives disabled sway and zero strength while sprint bob and ADS stay neutral
    # (the fixture passes the second option through a metatable; the plain table is the live form)
    for extra in (dict(enabled=False), dict(scale=0)):
        s.new(dict(shoulder_aim_enabled=True, **extra)); s.update(sample(), 1 / 60)
        for i in range(1, 121):
            s.update(sample(0, i * .05, sprint=True), 1 / 60)
        for i in range(1, 121):
            s.update(sample(0, 6 + i * .05, shoulder_aim=True), 1 / 60)
        s.update(sample(0, 12, ads=True, shoulder_aim=True), 1 / 60)
        s.update(sample(0, 12, ads=False, shoulder_aim=False), 1 / 60)
        for _ in range(120):
            s.update(sample(0, 12, ads=False, shoulder_aim=True), 1 / 60)
        s.update(sample(0, 12, ads=False, shoulder_aim=True, editor_frozen=True), 1 / 60)
        s.state()
    # ADS removes every procedural output while retaining continuous movement history
    s.new(aim); s.update(sample(), 1 / 60)
    for i in range(1, 61):
        s.update(sample(0, i / 20, sprint=True, ads=1), 1 / 60)
    s.update(sample(0, 3.05), 1 / 60)
    s.new(aim); s.update(sample(), 1 / 60); s.update(sample(0, .05), 1 / 60)
    s.new(aim); s.update(sample(), 1 / 60); s.update(sample(0, .05, ads=.5), 1 / 60)
    # owner changes teleports and long gaps reset motion instead of carrying inertia
    s.new(aim); s.update(sample(), 1 / 60)
    s.update(sample(30, 0), 1 / 60)
    s.update(sample(30, 0, owner="replacement"), 1 / 60)
    s.update(sample(30, 0, owner="replacement"), 1)
    s.update(sample(30, 0, owner="replacement"), 2)
    s.update(sample(30, 0, owner="replacement"), 0)
    s.update(dict(owner="x", x=NAN, y=0, z=0, yaw=0, pitch=0), 1 / 60)
    # hitches teleports and launches keep the aim stance and ease the remaining bob
    for fault in ("time_gap", "teleport", "spin"):
        s.new(aim); s.update(sample(), 1 / 60)
        for i in range(1, 121):
            s.update(sample(0, i * .05, shoulder_aim=True), 1 / 60)
        y = 120 * .05
        if fault == "time_gap":
            s.update(sample(0, y, shoulder_aim=True), .5)
            s.update(sample(0, y + .05, shoulder_aim=True), 1 / 60)
        elif fault == "teleport":
            s.update(sample(400, y, shoulder_aim=True), 1 / 60)
            s.update(sample(400, y + .05, shoulder_aim=True), 1 / 60)
        else:
            s.update(sample(0, y, 3, shoulder_aim=True), 1 / 60)
            s.update(sample(0, y + .05, 3, shoulder_aim=True), 1 / 60)
    s.new(); s.update(sample(), 1 / 60)
    for i in range(1, 121):
        s.update(sample(0, i * .05), 1 / 60)
    s.update(sample(0, 6), .5)
    for _ in range(240):
        s.update(sample(0, 6), 1 / 60)
    # pressing and releasing aim while walking fades the gait offset instead of switching it
    for placement in (True, False):
        s.new(dict(shoulder_aim_enabled=placement, bob_strength=3, sway_strength=3)); s.update(sample(), 1 / 144)
        y = 0.0
        for frames, aiming in ((288, False), (1, True), (287, True), (1, False), (287, False)):
            for _ in range(frames):
                y = y + 4 / 144
                s.update(sample(0, y, shoulder_aim=aiming), 1 / 144)
    # slow frames do not inflate speed and vertical travel cannot advance walking phase
    s.new(aim); s.update(sample(), .1)
    s.update(sample(0, .3), .1)
    s.update(sample(0, .3, z=.2), .1)
    # editor freeze motion off and scale zero return neutral without a catch-up pulse
    s.new(aim); s.update(sample(), 1 / 60)
    s.update(sample(0, .05, editor_frozen=True), 1 / 60)
    s.update(sample(0, 20), 1 / 60)
    s.configure(dict(enabled=False)); s.update(sample(), 1 / 60)
    s.configure(dict(enabled=True, scale=0)); s.update(sample(), 1 / 60)
    s.configure(dict(scale=1, editor_frozen=True)); s.update(sample(), 1 / 60)
    s.configure(dict(editor_frozen=False)); s.update(sample(), 1 / 60)
    s.configure(dict(scale=INF)); s.state()
    s.configure(dict(scale=-1)); s.configure(dict(enabled=OTHER))
    # imperceptible translation tails settle exactly while meaningful movement remains live
    s.new(aim); s.update(sample(), 1 / 60)
    for _ in range(241):
        s.update(sample(0, .05, .2), 1 / 60)
    for _ in range(240):
        s.update(sample(0, .05, .2, shoulder_aim=True), 1 / 60)
    # default normal aim does not add placement movement
    s.new(); s.update(sample(), 1 / 60)
    for _ in range(180):
        s.update(sample(shoulder_aim=True), 1 / 60)
    # weapon and camera motion are independently scaled and sprint moves the gun
    s.new(dict(bob_strength=0, sway_strength=0, camera_bob_strength=1, sprint_strength=0)); s.update(sample(), 1 / 60)
    for i in range(1, 61):
        s.update(sample(0, i * .03), 1 / 60)
    s.new(dict(camera_bob_strength=0, sprint_strength=1)); s.update(sample(), 1 / 60)
    for i in range(1, 121):
        s.update(sample(0, i * .08, sprint=True), 1 / 60)
    s.configure(dict(bob_speed=0)); s.configure(dict(camera_sway_strength=NAN))


def edges(s):
    """Hand-made: every refusal, every threshold on both sides, Lua-specific values."""
    # Motion.new: no options, options, a refused option (retail raises the text).
    s.new(); s.state()
    s.new(dict()); s.state()
    s.new(dict(scale=3)); s.state()
    s.new(dict(enabled=OTHER))
    s.new(dict(bob_speed=.2))
    full = dict(enabled=True, scale=1.5, editor_frozen=False, shoulder_aim_enabled=True, bob_strength=2.5, bob_speed=.25,
                sway_strength=3, sway_speed=2, sprint_strength=1, camera_bob_strength=.5, camera_bob_speed=.75,
                camera_sway_strength=2, camera_sway_speed=1.25)
    s.new(full); s.state()
    # configure: not a table, each refusal, and the order when several are wrong.
    s.new()
    s.configure(None); s.configure(OTHER); s.configure(False); s.configure(3)
    for key in ("enabled", "editor_frozen", "shoulder_aim_enabled"):
        s.configure({key: OTHER}); s.configure({key: TRUTHY}); s.configure({key: True}); s.state(); s.configure({key: False}); s.state()
    for bad in (OTHER, NAN, INF, -INF, -.0001, 2.0001, -1e-300, True):
        s.configure(dict(scale=bad))
    for good in (0, -0.0, 2, 1e-300, 1):
        s.configure(dict(scale=good)); s.state()
    for key in SETTINGS:
        speed = "speed" in key
        for bad in (OTHER, NAN, INF, -INF, -.0001, .2499 if speed else -1, 2.0001 if speed else 3.0001, False):
            s.configure({key: bad})
        for good in (.25 if speed else 0, 2 if speed else 3, 1):
            s.configure({key: good}); s.state()
    # Order of the checks: the first one in retail's order names the refusal.
    s.configure(dict(enabled=OTHER, scale=9, editor_frozen=OTHER, shoulder_aim_enabled=OTHER, bob_speed=9))
    s.configure(dict(scale=9, editor_frozen=OTHER, shoulder_aim_enabled=OTHER, bob_speed=9))
    s.configure(dict(editor_frozen=OTHER, shoulder_aim_enabled=OTHER, bob_speed=9))
    s.configure(dict(shoulder_aim_enabled=OTHER, bob_speed=9))
    s.configure(dict(camera_sway_speed=9, sway_speed=9, bob_speed=9)); s.state()
    s.configure(dict(camera_sway_speed=9, camera_bob_speed=9)); s.state()
    s.configure(dict(bob_strength=2, camera_sway_speed=9)); s.state()       # nothing is taken before all nine pass
    # A refusal for a setting comes after shoulder_aim_enabled has been taken (motion.lua:40).
    s.new(); s.configure(dict(shoulder_aim_enabled=True, enabled=False, scale=.5, bob_speed=9)); s.state()
    s.configure(dict(shoulder_aim_enabled=False, sway_strength=-1)); s.state()
    # configure keeps the previous sample, shoulder and aim; the editor freeze drops them.
    s.new(dict(shoulder_aim_enabled=True)); s.update(sample(), 1 / 60)
    for i in range(1, 31):
        s.update(sample(0, i * .05, shoulder_aim=True, sprint=(i < 4)), 1 / 60)
    s.state(); s.configure(dict(sway_speed=2)); s.state()
    s.update(sample(0, 31 * .05, shoulder_aim=True), 1 / 60)
    s.configure(dict()); s.state(); s.update(sample(0, 32 * .05, shoulder_aim=True), 1 / 60)
    s.configure(dict(editor_frozen=True)); s.state(); s.update(sample(0, 33 * .05), 1 / 60); s.state()
    s.configure(dict(scale=2)); s.state()                                   # still frozen: everything stays dropped
    s.configure(dict(editor_frozen=False)); s.state(); s.update(sample(0, 34 * .05), 1 / 60); s.update(sample(0, 35 * .05), 1 / 60)
    # reset and clear_ads called from outside (motion_runtime calls clear_ads on a held clock).
    for i in range(36, 50):
        s.update(sample(0, i * .05, sprint=True), 1 / 60)
    s.state(); s.clear_ads(); s.state(); s.update(sample(0, 50 * .05, sprint=True), 1 / 60)
    s.reset(); s.state(); s.update(sample(0, 51 * .05), 1 / 60); s.update(sample(0, 52 * .05), 1 / 60)
    # Invalid samples of every kind; each also resets the model.
    def walk(n=6):
        s.new(dict(shoulder_aim_enabled=True, sprint_strength=1)); s.update(sample(), 1 / 60)
        for i in range(1, n + 1):
            s.update(sample(i * .03, i * .04, i * .01, pitch=i * .005), 1 / 60)
    for bad in (None, dict(sample(), owner=None), dict(sample(), owner=""), dict(sample(), owner=NAN),
                dict(sample(), owner=INF), dict(sample(), owner=-INF), dict(sample(), owner=True),
                dict(sample(), x=NAN), dict(sample(), y=INF), dict(sample(), z=-INF), dict(sample(), yaw=OTHER),
                dict(sample(), pitch=None), dict(sample(), x=None), dict(sample(), y=OTHER), dict(sample(), z=NAN),
                dict(sample(), yaw=INF), dict(sample(), pitch=NAN)):
        walk(); s.update(bad, 1 / 60); s.state(); s.update(sample(.2, .3), 1 / 60); s.update(sample(.22, .33), 1 / 60)
    # An invalid sample is named before an invalid dt, and the dt before the editor freeze.
    walk(); s.update(None, 0); s.update(dict(sample(), x=NAN, editor_frozen=True), NAN)
    s.update(sample(editor_frozen=True), 0); s.update(sample(frozen=True), -1)
    for bad in (0, -0.0, -1 / 60, NAN, INF, -INF, None, OTHER, -1e-300):
        walk(); s.update(sample(.2, .3), bad); s.state(); s.update(sample(.2, .3), 1 / 60)
    # editor_frozen / frozen must be exactly true.
    for key in ("editor_frozen", "frozen"):
        for value in (True, TRUTHY, False, OTHER):
            walk(); s.update(sample(.2, .3, **{key: value}), 1 / 60); s.state()
    # Owners: strings and numbers, a change, the same again; a number and a string never equal.
    s.new(); s.update(sample(owner="a"), 1 / 60); s.update(sample(owner="a"), 1 / 60); s.update(sample(owner="b"), 1 / 60)
    s.update(sample(owner=5), 1 / 60); s.update(sample(owner=5.0), 1 / 60); s.update(sample(owner=7.25), 1 / 60)
    s.update(sample(owner=0), 1 / 60); s.update(sample(owner=-0.0), 1 / 60); s.update(sample(owner="0"), 1 / 60); s.state()
    # dt thresholds: > 1 resets, > .25 rebases, the .05 integration clamp.
    for dt in (1, 1.0000000000000002, .25, .2500000000000001, .05, .050000000000000003, .0500001, .26, .9, 2, 1e-300, 5e-324, 1e-9, 1e300):
        walk(); s.update(sample(.2, .28, .07, pitch=.03, sprint=True), dt); s.state(); s.update(sample(.21, .29, .07, pitch=.03), 1 / 60)
    # Discontinuity thresholds: distance 2, speed 20, turn and pitch turn pi/2 (each side, each alone).
    def step_from_rest(dx, dy, dz, dyaw, dpitch, dt):
        s.new(dict(sprint_strength=1)); s.update(sample(), 1 / 60); s.update(sample(0, .03), 1 / 60)
        s.update(sample(dx, .03 + dy, dyaw, z=dz, pitch=dpitch), dt); s.state(); s.update(sample(dx, .06 + dy, dyaw, z=dz, pitch=dpitch), 1 / 60)
    for d in (2.0, 2.0000000000000004, 1.9999999999999998):
        step_from_rest(d, 0, 0, 0, 0, .2); step_from_rest(0, d - .03, 0, 0, 0, .2); step_from_rest(0, -.03, d, 0, 0, .2)
    step_from_rest(1.2, 1.57, .3, 0, 0, .2); step_from_rest(1.2, 1.6, .31, 0, 0, .2)
    for d in (.2, .20000000000000004, .19999999999999998, .25, .1):
        step_from_rest(d, 0, 0, 0, 0, .01); step_from_rest(0, -.03, d, 0, 0, .01)
    half = math.pi / 2
    for a in (half, math.nextafter(half, 4), math.nextafter(half, 0), -half, -math.nextafter(half, 4), 1.5, -1.5, 3, -3):
        step_from_rest(0, 0, 0, a, 0, 1 / 60); step_from_rest(0, 0, 0, 0, a, 1 / 60)
    # Yaw wrapping: the short way round, exact half turns, many turns, large angles.
    for a, b in ((3.1, -3.1), (-3.1, 3.1), (0, math.pi), (0, -math.pi), (math.pi, -math.pi), (0, 2 * math.pi), (0, 7 * math.pi + .2),
                 (100.0, 100.3), (-100.0, -100.3), (1e6, 1e6 + .25), (0, 1e15), (0, -6.5), (6.2, .1), (-6.2, -.1)):
        s.new(); s.update(sample(yaw=a), 1 / 60); s.update(sample(yaw=b), 1 / 60); s.update(sample(yaw=b), 1 / 60); s.state()
    # A yaw step too large for a double: the turn is NaN, passes every '>' test, and stays in the sway (math.min /
    # math.max let a NaN through as LuaJIT computes them). The pitch step alone is infinite and rebases.
    for a, b in ((1e308, -1e308), (-1e308, 1e308)):
        s.new(dict(camera_sway_strength=1)); s.update(sample(yaw=a), 1 / 60); s.update(sample(yaw=b), 1 / 60); s.state()
        s.update(sample(yaw=b), 1 / 60); s.update(sample(.01, yaw=b, shoulder_aim=True), 1 / 60); s.state()
        s.new(); s.update(sample(pitch=a), 1 / 60); s.update(sample(pitch=b), 1 / 60); s.update(sample(pitch=b), 1 / 60); s.state()
    # Large and tiny coordinates.
    for x0, x1 in ((1e7, 1e7 + .05), (-1e12, -1e12 + .0625), (1e300, 1e300), (1e-300, 3e-300), (0, 1e-200), (1e308, -1e308),
                   (0, 1e155), (0, 1e200)):
        s.new(); s.update(sample(x0, x0), 1 / 60); s.update(sample(x1, x1), 1 / 60); s.update(sample(x1, x1), 1 / 60); s.state()
    # ads: true, numbers (clamped, non-finite count as none), everything else.
    for value in (True, False, None, OTHER, 0, -0.0, .5, .999999, 1, 1.0000001, 2, -1, NAN, INF, -INF, 1e-320, .25):
        walk(8)
        for i in range(9, 14):
            s.update(sample(i * .03, i * .04, i * .01, pitch=i * .005, ads=value, sprint=(i % 2 == 0)), 1 / 60)
        s.state()
        for i in range(14, 17):
            s.update(sample(i * .03, i * .04, i * .01, pitch=i * .005, shoulder_aim=True, ads=value), 1 / 60)
        s.state()
    # shoulder_aim and sprint must be exactly true.
    for key in ("shoulder_aim", "sprint"):
        for value in (True, TRUTHY, False, None, OTHER):
            walk(8)
            for i in range(9, 14):
                s.update(sample(i * .03, i * .04, i * .01, **{key: value}), 1 / 60)
            s.state()
    # Sprint: hold .10 s after release at several frame times, motion off, scale 0, with aim, with ads.
    for dt in (1 / 30, 1 / 60, 1 / 120, 1 / 144, .1, .05, .0999999, .025):
        s.new(dict(sprint_strength=1.5)); s.update(sample(), dt)
        y = 0.0
        for i in range(12):
            y += 6 * dt; s.update(sample(0, y, sprint=True), dt)
        for i in range(14):
            y += 3 * dt; s.update(sample(0, y), dt)
        s.state()
    for options in (dict(enabled=False), dict(scale=0), dict(scale=-0.0), dict(scale=1e-300), dict(sprint_strength=3, scale=2)):
        s.new(options); s.update(sample(), 1 / 60)
        for i in range(1, 30):
            s.update(sample(0, i * .1, sprint=True), 1 / 60)
        s.state()
        for i in range(30, 40):
            s.update(sample(0, i * .1, sprint=(i < 33), shoulder_aim=(i >= 33)), 1 / 60)
        s.state()
        for i in range(40, 50):
            s.update(sample(0, i * .1, sprint=True, ads=(i < 45)), 1 / 60)
        s.state()
    # Long settle: every tail reaches its exact endpoint (aim 0/1, shoulder 0/1, sprint 0/1, the 10 micrometre settle).
    for options in (dict(shoulder_aim_enabled=True, sprint_strength=1), dict(sprint_strength=.4, sway_speed=.25, bob_speed=2)):
        s.new(options); s.update(sample(), 1 / 144)
        y = 0.0
        for phase in range(6):
            for i in range(220):
                moving = phase in (0, 2, 4)
                y += (4 / 144 if moving else 0)
                s.update(sample(.3 * math.sin(y), y, .002 * i if phase == 2 else 0, pitch=.001 * i if phase == 2 else 0,
                                shoulder_aim=(phase in (1, 2)), sprint=(phase == 4)), 1 / 144)
            s.state()
    # Camera outputs with every camera setting on.
    s.new(dict(camera_bob_strength=3, camera_bob_speed=2, camera_sway_strength=3, camera_sway_speed=.25, sprint_strength=2))
    s.update(sample(), 1 / 60)
    for i in range(1, 200):
        s.update(sample(.02 * i, .05 * i, .01 * i, pitch=.2 * math.sin(i / 9), sprint=(60 < i < 120), ads=(.5 if 150 < i < 170 else None)), 1 / 60)
    s.state()


def damp(value, target, dt, time):
    return value + (target - value) * (1 - math.exp(-dt / time))


def last_step(value, target, time, goal):
    """A frame time below .05 s for which damp(value, target, dt, time) is exactly the double `goal`."""
    low, high, rising = 1e-4, .0499, target > value
    for _ in range(400):
        middle = (low + high) / 2
        result = damp(value, target, middle, time)
        if result == goal:
            return middle
        if (result < goal) == rising: low = middle
        else: high = middle
    sys.exit("no frame time reaches the threshold exactly")


def exact_limits(s):
    """The .00001 thresholds met exactly (retail compares with < and >, so the exact value is NOT snapped), and
    situations that reach the output clamps."""
    small, nearly = .00001, 1 - .00001
    dt = 1 / 60
    # aim and shoulder falling to exactly .00001, then on.
    s.new(dict(shoulder_aim_enabled=True)); s.update(sample(), dt)
    for _ in range(200):
        s.update(sample(shoulder_aim=True), dt)
    s.state()                                              # both are exactly 1 here
    value = 1.0
    while value > 1.45e-5:
        value = damp(value, 0, dt, .12); s.update(sample(), dt)
    s.update(sample(), last_step(value, 0, .12, small)); s.state(); s.update(sample(), dt); s.state()
    # aim and shoulder rising to exactly 1 - .00001.
    s.new(dict(shoulder_aim_enabled=True)); s.update(sample(), dt)
    value = 0.0
    while 1 - value > 1.5e-5:
        value = damp(value, 1, dt, .10); s.update(sample(shoulder_aim=True), dt)
    s.update(sample(shoulder_aim=True), last_step(value, 1, .10, nearly)); s.state(); s.update(sample(shoulder_aim=True), dt); s.state()
    # sprint rising to exactly 1 - .00001, then (after the .10 s hold) falling to exactly .00001.
    s.new(dict(sprint_strength=1)); s.update(sample(), dt)
    value = 0.0
    while 1 - value > 1.4e-5:
        value = damp(value, 1, dt, .12); s.update(sample(sprint=True), dt)
    s.update(sample(sprint=True), last_step(value, 1, .12, nearly)); s.state()
    for _ in range(60):
        s.update(sample(sprint=True), dt)
    s.state()
    for _ in range(7):                                     # the hold: .10 s of frames keep the target
        s.update(sample(), dt)
    s.state()
    value = damp(1.0, 0, dt, .22)                          # the seventh frame was the first without a target
    while value > 1.2e-5:
        value = damp(value, 0, dt, .22); s.update(sample(), dt)
    s.update(sample(), last_step(value, 0, .22, small)); s.state(); s.update(sample(), dt); s.state()
    # settle(): a displacement of exactly .00001 m stays, the next double below is zero.
    look = damp(0, .008, dt, .065 / 1)
    scale = small / look
    for _ in range(4000):
        if (0 + look * 1 * 1) * scale == small: break
        scale = math.nextafter(scale, 2 if (0 + look * 1 * 1) * scale < small else 0)
    else:
        sys.exit("no scale reaches the settle threshold exactly")
    for value in (scale, math.nextafter(scale, 0), math.nextafter(math.nextafter(scale, 0), 0), math.nextafter(scale, 2)):
        s.new(); s.update(sample(), dt); s.configure(dict(scale=value)); s.update(sample(pitch=-1.0), dt)
        s.new(); s.update(sample(), dt); s.configure(dict(scale=value)); s.update(sample(pitch=1.0), dt)
    # Output clamps +-.15: the strongest sprint bob at scale 2 (dx both ways, dy downward with the aim offset).
    s.new(dict(scale=2, bob_strength=3, sprint_strength=3, shoulder_aim_enabled=True)); s.update(sample(), dt)
    y = 0.0
    for i in range(240):
        y += 7 * dt; s.update(sample(0, y, sprint=True), dt)
    s.state()
    for i in range(120):
        y += 7 * dt; s.update(sample(0, y, shoulder_aim=(i > 20), sprint=(i <= 20)), dt)
    s.state()
    # The strafe and forward lag clamps (.008 and .006): above 8 and 10 m/s, each way.
    for vx, vy in ((15, 0), (-15, 0), (0, 15), (0, -15), (9, 11), (-12.5, -12.5)):
        s.new(dict(sway_strength=3, bob_strength=0)); s.update(sample(), dt)
        x = y = 0.0
        for i in range(90):
            x += vx * dt; y += vy * dt; s.update(sample(x, y), dt)
        s.state()
        for i in range(30):
            s.update(sample(x, y), dt)
    # A hitch inside the sprint hold: the rebased frame takes only .05 s off the hold (motion.lua:79, 113).
    for hitch, jump in ((.3, 0), (.2, 50), (.06, 0), (.26, 0), (.9, 0)):
        s.new(dict(sprint_strength=1)); s.update(sample(), dt)
        for i in range(1, 40):
            s.update(sample(0, i * .1, sprint=True), dt)
        s.update(sample(jump, 40 * .1), hitch); s.state(); s.update(sample(jump, 41 * .1), dt); s.state()
        for i in range(42, 48):
            s.update(sample(jump, i * .1), dt)
        s.state()
    # Motion.new starts from the defaults whatever the model before it held.
    s.new(dict(enabled=False, scale=.5, editor_frozen=True, shoulder_aim_enabled=True, bob_strength=2, bob_speed=2, sway_strength=2,
               sway_speed=2, sprint_strength=2, camera_bob_strength=2, camera_bob_speed=2, camera_sway_strength=2, camera_sway_speed=2))
    s.state(); s.new(); s.state()


def random_run(s, rate, seed, count):
    """A seeded player: walks, strafes, turns, sprints, aims, with hitches, teleports, respawns and option changes."""
    rng = random.Random(seed)
    dt = 1 / rate
    s.new(rng.choice((None, dict(sprint_strength=1), dict(shoulder_aim_enabled=True, sprint_strength=.6))))
    x = y = z = yaw = pitch = 0.0
    owner, speed, turn_rate, mode, ads, made = 1, 0.0, 0.0, "walk", None, 0
    while made < count:
        roll = rng.random()
        if roll < .012:
            mode = rng.choice(("walk", "walk", "sprint", "aim", "ads", "idle", "strafe", "ads_half"))
            ads = dict(ads=True, ads_half=rng.choice((.25, .5, .999, 1, 1.5)), aim=None).get(mode)
        if roll > .9:
            speed = rng.choice((0, 0, 1.5, 3, 4.2, 6.5, 7.5))
            turn_rate = rng.choice((0, 0, .4, -.4, 2.5, -2.5, 9, -9))
        frame = dt
        hitch = rng.random()
        if hitch < .004: frame = rng.choice((.2, .26, .5, .99, 1.5, 3))
        elif hitch < .02: frame = dt * rng.choice((.5, 1.5, 2, 3, .999, 1.001))
        elif hitch < .022: frame = rng.choice((0, -dt, NAN))
        yaw += turn_rate * frame if frame == frame and frame > 0 else 0
        if rng.random() < .01: yaw = rng.uniform(-9, 9)
        pitch = max(-1.4, min(1.4, pitch + rng.uniform(-1, 1) * .8 * dt))
        step = speed * (frame if frame == frame and frame > 0 else 0)
        heading = yaw + (math.pi / 2 if mode == "strafe" else 0)
        x += -math.sin(heading) * step; y += math.cos(heading) * step
        if rng.random() < .01: z += rng.choice((.2, -.2, 1.5, -3))
        if rng.random() < .003: x += rng.choice((2.5, -40, 400)); y += rng.choice((0, 3))
        if rng.random() < .002: owner += 1
        fields = dict(sprint=(mode == "sprint") or None, shoulder_aim=(mode == "aim") or None, ads=ads)
        if rng.random() < .01: fields["sprint"] = rng.choice((True, False, TRUTHY))
        if rng.random() < .003: fields[rng.choice(("editor_frozen", "frozen"))] = rng.choice((True, TRUTHY))
        value = sample(x, y, yaw, owner=f"p{owner}", z=z, pitch=pitch, **fields)
        if rng.random() < .002: value = rng.choice((None, dict(value, x=NAN), dict(value, owner=None), dict(value, yaw=INF)))
        s.update(value, frame); made += 1
        extra = rng.random()
        if extra < .004:
            options = {}
            for key in rng.sample(SETTINGS, rng.randint(1, 4)):
                options[key] = rng.choice((.25, .5, 1, 1.75, 2) if "speed" in key else (0, .3, 1, 2.2, 3))
            if rng.random() < .3: options["scale"] = rng.choice((0, .5, 1, 2))
            if rng.random() < .3: options["enabled"] = rng.random() < .7
            if rng.random() < .2: options["shoulder_aim_enabled"] = rng.random() < .5
            if rng.random() < .08: options["editor_frozen"] = True
            if rng.random() < .1: options[rng.choice(SETTINGS)] = rng.choice((9, NAN, OTHER))
            s.configure(options); made += 1
            if options.get("editor_frozen"):
                s.update(value, dt); s.configure(dict(editor_frozen=False)); made += 2
        elif extra < .008: s.clear_ads(); made += 1
        elif extra < .009: s.reset(); made += 1
        elif extra < .05: s.state(); made += 1


def build():
    s = Script()
    fixtures(s)
    edges(s)
    exact_limits(s)
    for rate, seed in ((30, 3001), (60, 6001), (120, 12001), (144, 14401)):
        random_run(s, rate, seed, 5000)
    return s.ops


# -------------------------------------------------------------- Lua and back
def lua_script(ops):
    rows = []
    for op in ops:
        kind = op["op"]
        if kind in ("new", "configure"):
            field = "options" if kind == "new" else "value"
            rows.append("{%s,%s=%s}" % (lua(kind), field, lua(op["options"])))
        elif kind == "update":
            rows.append("{%s,sample=%s,dt=%s}" % (lua(kind), lua(op["sample"]), lua(op["dt"])))
        else:
            rows.append("{%s}" % lua(kind))
    # One Lua function holds at most 65536 constants: the script is a run of small functions.
    out = ["local ops = {}", "local function add(op) ops[#ops + 1] = op end"]
    for start in range(0, len(rows), 500):
        out += ["do local function part()"] + ["add" + row for row in rows[start:start + 500]] + ["end part() end"]
    return "\n".join(out) + "\nreturn ops\n"


def run(interpreter, retail, ops):
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
        script.write(lua_script(ops))
    done = subprocess.run(interpreter + [str(HERE / "run_motion.lua"), str(retail), script.name], capture_output=True, text=True)
    Path(script.name).unlink()
    if done.returncode:
        sys.exit("run_motion.lua failed:\n" + done.stderr)
    lines = done.stdout.splitlines()
    if len(lines) != len(ops):
        sys.exit(f"run_motion.lua: {len(lines)} results for {len(ops)} operations")
    return lines


def number(text):
    return float(text)  # "nan", "-nan", "inf", "-inf" and 17 digits all read back exactly


FLAG = {None: 0, False: 1, True: 2}          # TfpMotionFlag: absent, false, true, other
RESETS = ["-", "invalid_sample", "invalid_dt", "editor_frozen", "initial", "owner_changed", "time_gap"]
REBASES = ["-", "time_gap", "discontinuity"]
TEXTS = ["", "Motion options require a table", "Motion enabled must be boolean",
         "Motion scale must be finite and between zero and two", "Editor freeze must be boolean",
         "Aim movement must be boolean"] + ["Invalid motion setting: " + key for key in SETTINGS]


def flag_code(value):
    return FLAG[value] if value is None or isinstance(value, bool) else 3


def number_code(value):
    """(TfpMotionKind, double): nil, a number, anything else."""
    if value is None:
        return 0, 0.0
    if isinstance(value, bool) or value is OTHER:
        return 2, 0.0
    if value is TRUTHY:
        return 1, 1.0
    return 1, float(value)


def exactly_true(value):
    return int(value is True)


def header(ops, lines, stamp):
    out = [stamp, "#include <math.h>",
           "typedef struct MotionOptionsVector { int table; int enabled, editor_frozen, shoulder_aim_enabled;",
           "  int scale_kind; double scale; int kind[9]; double value[9]; } MotionOptionsVector;",
           "typedef struct MotionStateVector { int enabled, editor_frozen, shoulder_aim_enabled, has_previous;",
           "  double scale, setting[9]; unsigned long long owner; double previous[5], dynamic[13]; } MotionStateVector;",
           "typedef struct MotionVector { int op, index; /* index: into motion_options or motion_states */",
           "  int has_sample, has_owner; unsigned long long owner; double x, y, z, yaw, pitch; int ads_kind; double ads;",
           "  int shoulder_aim, sprint, editor_frozen, frozen; double dt;",
           "  int ok, text; /* text: index into motion_texts */",
           "  int reset, rebased, has_camera, sprint_target, sprint_held; double out[18]; } MotionVector;",
           "enum { MV_NEW, MV_CONFIGURE, MV_UPDATE, MV_RESET, MV_CLEAR_ADS, MV_STATE };",
           "static const char *const motion_texts[] = {%s};" % ",".join('"%s"' % t for t in TEXTS),
           "/* retail's reset and rebased texts, in the order of the reset and rebased numbers below (\"\" is false) */",
           "static const char *const motion_reset_texts[] = {%s};" % ",".join('"%s"' % t.replace("-", "") for t in RESETS),
           "static const char *const motion_rebase_texts[] = {%s};" % ",".join('"%s"' % t.replace("-", "") for t in REBASES)]
    owners = {}

    def owner_key(owner):
        """Retail's owner (a non-empty string or a finite number) as a key: equal owners, equal keys."""
        if isinstance(owner, str):
            name = "string:" + owner
        else:
            name = "number:%.14g" % (owner + 0.0)       # -0 and 0 are one Lua value
        return owners.setdefault(name, len(owners) + 1)

    options_rows, options_index, states, rows = [], {}, [], []
    for op, line in zip(ops, lines):
        kind = op["op"]
        row = dict(op="MV_" + kind.upper(), index=-1, has_sample=0, has_owner=0, owner=0, x=0.0, y=0.0, z=0.0, yaw=0.0, pitch=0.0,
                   ads_kind=0, ads=0.0, shoulder_aim=0, sprint=0, editor_frozen=0, frozen=0, dt=0.0, ok=1, text=0,
                   reset=0, rebased=0, has_camera=0, sprint_target=0, sprint_held=0, out=[0.0] * 18)
        parts = line.split(" ")
        if kind in ("new", "configure"):
            o = op["options"]
            if isinstance(o, dict):
                scale_kind, scale = number_code(o.get("scale"))
                codes = [number_code(o.get(key)) for key in SETTINGS]
                text = "{1,%d,%d,%d,%d,%s,{%s},{%s}}" % (
                    flag_code(o.get("enabled")), flag_code(o.get("editor_frozen")), flag_code(o.get("shoulder_aim_enabled")),
                    scale_kind, c_double(scale), ",".join(str(k) for k, _ in codes), ",".join(c_double(v) for _, v in codes))
            else:
                if kind == "new" and o is not None:
                    sys.exit("Motion.new is only scripted with a table or nothing")
                text = "{0,0,0,0,0,0,{0},{0}}"
            if text not in options_index:
                options_index[text] = len(options_rows)
                options_rows.append(text)
            row["index"] = options_index[text]
            assert parts[0] == ("N" if kind == "new" else "C")
            row["ok"] = int(parts[1] == "T")
            if not row["ok"]:
                row["text"] = TEXTS.index(line[4:])
        elif kind == "update":
            v = op["sample"]
            if v is not None:
                row["has_sample"] = 1
                owner = v.get("owner")
                good = isinstance(owner, str) and owner != "" or (isinstance(owner, (int, float)) and not isinstance(owner, bool)
                                                                   and owner == owner and abs(owner) != INF)
                if good:
                    row["has_owner"], row["owner"] = 1, owner_key(owner)
                for key in ("x", "y", "z", "yaw", "pitch"):
                    n = v.get(key)
                    row[key] = float(n) if isinstance(n, (int, float)) and not isinstance(n, bool) else NAN
                ads = v.get("ads")
                if ads is True: row["ads_kind"] = 1
                elif isinstance(ads, (int, float)) and not isinstance(ads, bool): row["ads_kind"], row["ads"] = 2, float(ads)
                for key in ("shoulder_aim", "sprint", "editor_frozen", "frozen"):
                    row[key] = exactly_true(v.get(key))
            dt = op["dt"]
            row["dt"] = float(dt) if isinstance(dt, (int, float)) and not isinstance(dt, bool) else NAN
            assert parts[0] == "U" and len(parts) == 24
            row["reset"], row["rebased"], row["has_camera"] = RESETS.index(parts[1]), REBASES.index(parts[2]), int(parts[3])
            row["out"] = [number(p) for p in parts[4:22]]
            row["sprint_target"], row["sprint_held"] = int(parts[22]), int(parts[23])
        elif kind == "state":
            assert parts[0] == "S" and len(parts) == 34
            if parts[4] == "1":
                parts[15] = parts[15].replace("number:-0", "number:0") if parts[15] == "number:-0" else parts[15]
                if parts[15] not in owners:
                    sys.exit("state names an owner no sample carried: " + parts[15])
                key = owners[parts[15]]
            else:
                key = 0
            states.append("{%s,%s,%s,%s,%s,{%s},%dULL,{%s},{%s}}" % (
                parts[1], parts[2], parts[3], parts[4], c_double(number(parts[5])),
                ",".join(c_double(number(p)) for p in parts[6:15]), key,
                ",".join(c_double(number(p)) for p in parts[16:21]), ",".join(c_double(number(p)) for p in parts[21:34])))
            row["index"] = len(states) - 1
        rows.append("  {%s,%d,%d,%d,%dULL,%s,%s,%s,%s,%s,%d,%s,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,{%s}}," % (
            row["op"], row["index"], row["has_sample"], row["has_owner"], row["owner"], c_double(row["x"]), c_double(row["y"]),
            c_double(row["z"]), c_double(row["yaw"]), c_double(row["pitch"]), row["ads_kind"], c_double(row["ads"]),
            row["shoulder_aim"], row["sprint"], row["editor_frozen"], row["frozen"], c_double(row["dt"]), row["ok"], row["text"],
            row["reset"], row["rebased"], row["has_camera"], row["sprint_target"], row["sprint_held"],
            ",".join(c_double(v) for v in row["out"])))
    out.append("static const MotionOptionsVector motion_options[] = {\n  %s\n};" % ",\n  ".join(options_rows))
    out.append("static const MotionStateVector motion_states[] = {\n  %s\n};" % ",\n  ".join(states))
    out.append("static const MotionVector motion_vectors[] = {")
    out += rows
    out.append("};")
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--script-only", type=Path)
    parser.add_argument("--lua", default="luajit", help="interpreter and arguments, e.g. 'luajit -joff' or 'lua5.1'")
    parser.add_argument("--lines", type=Path, help="also write the raw answers here (to compare interpreters)")
    args = parser.parse_args()
    ops = build()
    if args.script_only:
        args.script_only.write_text(lua_script(ops))
        return 0
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    source = args.retail / "src/first_person/motion.lua"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    lines = run(args.lua.split(), args.retail, ops)
    if args.lines:
        args.lines.write_text("\n".join(lines) + "\n")
    if not args.out:
        return 0
    stamp = (f"/* Generated by tools/retail_ref/gen_motion.py. Do not edit.\n * Retail file: src/first_person/motion.lua at commit "
             f"{commit}, sha256 {digest}.\n * {len(ops)} operations replayed against that file under LuaJIT; doubles are exact. */\n"
             f'#define MOTION_VECTOR_COMMIT "{commit}"\n#define MOTION_VECTOR_SHA256 "{digest}"')
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "motion_vectors.h").write_text(header(ops, lines, stamp))
    updates = sum(op["op"] == "update" for op in ops)
    print(f"motion: {len(ops)} operations ({updates} updates)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
