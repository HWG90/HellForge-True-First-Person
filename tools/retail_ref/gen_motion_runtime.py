#!/usr/bin/env python3
"""Cut reference vectors for src/motion_runtime.c from the retail motion_runtime.lua, run
unmodified under LuaJIT by run_motion_runtime.lua (with the unmodified retail motion.lua,
locomotion.lua and action_placement.lua beneath it).

Writes tests/vectors/motion_runtime_vectors.h: every operation, the knobs of the scripted
fakes it was run with, and what the retail module answered: the call's answer, the ordered
trace of every call into a fake with its arguments (log lines among them) and the state
table. Numbers in the answers are exact doubles.

  gen_motion_runtime.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
  gen_motion_runtime.py --retail ... --coverage      (line coverage of motion_runtime.lua, nothing written)
  gen_motion_runtime.py --retail ... --lines FILE    (the raw answers, to compare interpreters)
"""
import argparse
import hashlib
import math
import os
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
FROZEN = "1d0d453"
NAN, INF = float("nan"), float("inf")
ACTIONS = ["idle", "aim", "ads", "sprint", "reload", "crouch", "prone", "slide", "dive"]
PLACE = ["x", "y", "z", "yaw", "pitch", "roll"]
AXES = ["dx", "dy", "dz", "qx", "qy", "qz", "qw"]
RESOURCE = "4D1C334D294DFA97"
LEASES = 6

KNOBS = ["ctor_log", "ctor_options", "ctor_editor", "ctor_model", "ctor_tp", "ctor_bad", "cap_native",
         "time", "clock_step", "clock_mode1", "clock_mode2", "clock_mode3", "clock_mode4",
         "c_enabled", "c_result", "c_mode", "c_phase", "c_lease", "c_get"] + ["c_" + k for k in PLACE] + [
         "c_present", "c_set", "c_set_why",
         "b_alignment", "b_has_ap", "b_key", "b_revision", "b_revision_nil", "b_has_mapping", "b_mapping_mode", "b_mapping_set",
         "b_ads_set", "b_ads_why", "b_pose", "b_pose_why", "b_hint", "b_hint_active", "b_hint_shoulder", "b_pt", "b_pt_epoch",
         "b_pt_progress", "b_pt_ads", "b_pa", "b_pr"]
for _a in ACTIONS:
    KNOBS += [f"ap_{_a}_mode"] + [f"ap_{_a}_{k}" for k in PLACE] + [f"ap_{_a}_present"]
KNOBS += ["e_active", "e_why", "e_has_preview", "e_preview_mode", "e_yaw", "e_pitch", "e_roll", "e_present",
          "s_mode", "s_why", "s_owner", "s_x", "s_y", "s_z", "s_yaw", "s_pitch", "s_ads", "s_shoulder", "s_sprint", "s_reload"]
KNOBS += [f"s_f{i}" for i in range(1, 25)]
KNOBS += ["s_has_action", "s_resource", "s_verified", "s_l1", "s_l3", "s_l14", "s_has_l"]
KNOBS += [f"s_cam{i}" for i in range(1, 17)] + [f"s_par{i}" for i in range(1, 17)]
KNOBS += ["tp_mode", "tp_why"] + ["tp_" + a for a in AXES]
for _p in ("o_", "m_"):
    KNOBS += ([_p + "mode"] if _p == "o_" else []) + [_p + "enabled", _p + "scale_kind", _p + "scale"] + [
        f"{_p}k{i}" for i in range(1, 10)] + [f"{_p}v{i}" for i in range(1, 10)] + [_p + "frozen", _p + "shoulder"]
for _i in range(1, LEASES + 1):
    KNOBS += [f"l{_i}_{k}" for k in ("done", "cleanup", "blend", "origin", "identity", "player")]
KNOB_ID = {name: i + 1 for i, name in enumerate(KNOBS)}
# Knobs a fake changes by itself (the clock advances the time, an accepted placement is kept):
# the generator's own idea of their value can be stale, so a set of these is always written.
VOLATILE = {"time", "c_present"} | {"c_" + k for k in PLACE}
SETTINGS = ["bob_strength", "bob_speed", "sway_strength", "sway_speed", "sprint_strength", "camera_bob_strength",
            "camera_bob_speed", "camera_sway_strength", "camera_sway_speed"]
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


class Script:
    def __init__(self):
        self.ops = []          # (kind, [(knob id, value)], name)
        self.strings = []
        self.index = {}

    def text(self, value):
        if value is None:
            return 0
        if value not in self.index:
            self.strings.append(value)
            self.index[value] = len(self.strings)
        return self.index[value]


def basis(yaw=0.0, pitch=0.0, roll=0.0, scale=1.0, as_float=False, position=(0.0, 0.0, 0.0)):
    """Sixteen numbers in the engine's order: three axis rows and a position row."""
    cy, sy, cp, sp, cr, sr = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch), math.cos(roll), math.sin(roll)
    rz = [[cy, sy, 0], [-sy, cy, 0], [0, 0, 1]]
    rx = [[1, 0, 0], [0, cp, sp], [0, -sp, cp]]
    ry = [[cr, 0, -sr], [0, 1, 0], [sr, 0, cr]]

    def mul(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    m = mul(mul(ry, rx), rz)
    out = []
    for row in m:
        out += [v * scale for v in row] + [0.0]
    out += list(position) + [1.0]
    if as_float:
        import struct
        out = [struct.unpack("f", struct.pack("f", v))[0] for v in out]
    return out


class Session:
    """One runtime with its fakes. Knobs are set, then an operation runs."""
    DEFAULTS = dict(c_enabled=1, c_lease=1, b_alignment=1, c_present=63, c_x=.1, c_z=-.05, e_present=7, o_enabled=2, o_scale_kind=1,
                    o_scale=1, ctor_options=2, ctor_editor=1, time=10, s_has_l=7, b_mapping_set=1, tp_qw=1)

    def __init__(self, script, name, **knobs):
        self.script = script
        self.w = {name: 0 for name in KNOBS}
        self.pending = []
        start = dict(self.DEFAULTS)
        start.update(s_owner="body-A", s_resource=RESOURCE, b_key="1234567890abcdef")
        for action in ACTIONS:
            start[f"ap_{action}_present"] = 63
        for i in range(16):
            start[f"s_cam{i + 1}"] = IDENTITY[i]
            start[f"s_par{i + 1}"] = IDENTITY[i]
        start.update(knobs)
        self.set(**start)
        self.op("begin", name)

    def set(self, **knobs):
        for name, value in knobs.items():
            if isinstance(value, str) or (value is None and name in STRING_KNOBS):
                value = self.script.text(value)
            if isinstance(value, bool):
                value = 1 if value else 0
            value = float(value)
            old = self.w[name]
            same = old == value and math.copysign(1, old) == math.copysign(1, value)
            if same and name not in VOLATILE:
                continue
            self.w[name] = value
            self.pending.append((KNOB_ID[name], value))
        return self

    def op(self, kind, name=None):
        self.script.ops.append((kind, self.pending, name))
        self.pending = []

    def new(self, **knobs):
        self.set(**knobs)
        self.op("new")
        return self

    def update(self, **knobs):
        self.set(**knobs)
        self.op("update")
        return self

    def step(self, dt=1 / 60, **knobs):
        return self.update(time=self.w["time"] + dt, **knobs)

    def steps(self, count, dt=1 / 60, **knobs):
        self.set(**knobs)
        for _ in range(count):
            self.step(dt)
        return self

    def capture(self, native=None, **knobs):
        self.set(cap_native={None: 0, True: 1, False: 2}[native], **knobs)
        self.op("capture")
        return self

    def reset(self, **knobs):
        self.set(**knobs)
        self.op("reset")
        return self

    def bases(self, camera=None, parent=None):
        for prefix, values in (("s_cam", camera), ("s_par", parent)):
            if values is not None:
                self.set(**{f"{prefix}{i + 1}": values[i] for i in range(16)})
        return self

    def lease(self, index, identity=None, player=None, origin=None, blend=False, done=False, cleanup=False, both=True):
        if origin is None:
            origin = identity is not None or player is not None
        self.set(**{f"l{index}_origin": origin, f"l{index}_identity": self.script.text(identity),
                    f"l{index}_player": self.script.text(player), f"l{index}_blend": blend, f"l{index}_done": done,
                    f"l{index}_cleanup": cleanup}, c_lease=index)
        if both:
            self.set(b_alignment=index)
        return self

    def place(self, prefix, x=0.0, y=0.0, z=0.0, yaw=0.0, pitch=0.0, roll=0.0, present=63, mode=0):
        self.set(**{prefix + "x": x, prefix + "y": y, prefix + "z": z, prefix + "yaw": yaw, prefix + "pitch": pitch,
                    prefix + "roll": roll, prefix + "present": present})
        if prefix != "c_":
            self.set(**{prefix + "mode": mode})
        return self

    def vector(self, dx=0.0, dy=0.0, dz=0.0, qx=0.0, qy=0.0, qz=0.0, qw=1.0, mode=2):
        return self.set(tp_mode=mode, tp_dx=dx, tp_dy=dy, tp_dz=dz, tp_qx=qx, tp_qy=qy, tp_qz=qz, tp_qw=qw)

    def rotation(self, radians):
        """The fixture's rotation_only: no translation, a rotation about x."""
        if radians is None:
            return self.set(tp_mode=0)
        return self.vector(qx=math.sin(radians / 2), qw=math.cos(radians / 2))


STRING_KNOBS = {"s_owner", "s_resource", "b_key", "s_why", "c_set_why", "b_ads_why", "b_pose_why", "e_why", "tp_why"}


# ------------------------------------------------------------------ the retail fixture's own situations
def fixture(script, name, **knobs):
    """tests/first_person/motion_runtime.lua fixture(): no log, an editor that is closed, options from a
    function, a model with aim movement on, the conversion seam, no action placements."""
    start = dict(ctor_log=0, ctor_editor=1, ctor_options=2, ctor_model=2, m_shoulder=2, ctor_tp=1, b_has_ap=0)
    start.update(knobs)
    s = Session(script, "fixture: " + name, **start)
    s.new()
    return s


def walk(s, y, dt=1 / 60, **knobs):
    return s.step(dt, s_y=y, **knobs)


def profile(script, name, **knobs):
    s = fixture(script, name, b_has_ap=1, **knobs)
    for action in ACTIONS:
        aim = action == "aim"
        s.place(f"ap_{action}_", x=0 if aim else .12, z=0 if aim else -.03)
    s.place("c_", x=.12, z=-.03)
    return s


def fixtures(script):
    s = fixture(script, "only enabled owned running pose leases")
    for phase in (2, 1, 3, 4):
        s.step(c_phase=phase)
    s.step(c_phase=0, c_mode=1)
    s.step(c_mode=0, b_alignment=2)

    s = fixture(script, "repeated clock ticks retain the last pose")
    s.step()
    s.set(s_sprint=1)
    for i in range(1, 91):
        walk(s, .1 * i)
    s.update(s_y=s.w["s_y"] + .02)
    walk(s, s.w["s_y"] + .08)
    s.update(time=s.w["time"] - .01)

    s = fixture(script, "sprint quaternion and held-tick ADS edge")
    s.step()
    s.set(s_sprint=1)
    for i in range(1, 91):
        walk(s, .1 * i)
    walk(s, s.w["s_y"] + .02, s_shoulder=1)
    s.update(s_ads=1)
    s.steps(10)
    s.vector(qw=NAN)
    walk(s, s.w["s_y"] + .02, s_ads=0, s_shoulder=0)

    s = fixture(script, "real movement reaches the native delta")
    s.step()
    walk(s, .05)

    s = fixture(script, "editor and disabled idle send neutral")
    s.step()
    walk(s, .05)
    walk(s, 20, e_active=1)
    walk(s, 20, e_active=0)
    walk(s, 30, o_enabled=1)
    walk(s, 30, o_enabled=2)

    for options in (dict(o_enabled=1, o_scale=1), dict(o_enabled=2, o_scale=0)):
        s = fixture(script, "shoulder stance stays visible with sway off", **options)
        s.step()
        s.steps(30)
        s.step(s_shoulder=1)
        s.steps(120)
        s.update(s_ads=1)
        s.steps(10)
        s.step(s_ads=0, s_shoulder=0)
        s.steps(120, s_shoulder=1)
        s.steps(120, s_shoulder=0)
        s.steps(30)
        s.steps(120, s_shoulder=1)
        s.step(e_active=1)
        s.steps(8)

    s = fixture(script, "changing optional motion while aim is held", s_shoulder=1)
    s.steps(120)
    for enabled, scale in ((1, 1), (2, 0), (2, .5), (2, 1)):
        s.step(o_enabled=enabled, o_scale=scale)

    s = fixture(script, "disabled idle reads the separate shoulder signal", o_enabled=1)
    s.step()
    s.steps(120, s_shoulder=1, b_hint_shoulder=1)
    s.step(s_ads=1, b_hint_active=1)
    s.steps(10)
    s.step(s_ads=0, s_shoulder=0, b_hint_active=0, b_hint_shoulder=0)

    s = fixture(script, "read conversion and write failures stay neutral")
    s.step()
    walk(s, .05)
    walk(s, .1, s_mode=1, s_why="fixture sample unavailable")
    walk(s, .15, s_mode=0, tp_mode=1, tp_why="fixture bad basis")
    s.vector(dx=.151, qx=NAN, qy=NAN, qz=NAN, qw=NAN)
    walk(s, .2)
    walk(s, .25, tp_mode=0, b_pose=2, b_pose_why="fixture changed pose owner")

    s = fixture(script, "lease changes long gaps and explicit reset")
    s.step()
    walk(s, .05)
    s.lease(2)
    walk(s, 30)
    walk(s, 30.05)
    walk(s, 40, .5)
    walk(s, 50, 2)
    s.reset()
    walk(s, 50)

    s = fixture(script, "unchanged output skips only submissions")
    s.step()
    s.steps(60)
    walk(s, .05)
    s.steps(240)
    s.steps(60)
    s.rotation(.01)
    s.step()
    s.step()
    s.rotation(.02)
    s.step()

    s = fixture(script, "same-valued ADS editor reset and lease boundaries")
    s.step()
    s.step(s_ads=1)
    s.steps(10)
    s.step(s_ads=0)
    s.step(e_active=1)
    s.step()
    s.step(e_active=0)
    s.reset()
    s.step()
    s.lease(2)
    s.step()

    s = fixture(script, "a refused submission cannot populate the coalescing cache")
    s.step()
    s.reset()
    s.step(b_pose=2, b_pose_why="fixture changed pose owner")
    s.step(b_pose=0)
    s.step()

    s = fixture(script, "ADS settles to exact neutral")
    s.step()
    walk(s, .05)
    walk(s, .1, s_ads=1, s_sprint=1)
    s.steps(10)
    walk(s, .15, s_ads=0)

    s = fixture(script, "action profiles settle without writes", b_has_ap=1)
    for action in ACTIONS:
        s.place(f"ap_{action}_", x=.3 if action == "sprint" else .1, present=7)
    s.place("c_", x=.1, present=7)
    s.steps(30)
    s.steps(90, s_sprint=1)
    s.steps(30)
    s.step(b_revision=1)

    s = fixture(script, "editor yaw pitch preview coalesces", e_active=1, e_has_preview=1, e_yaw=12, e_pitch=-8, e_present=3)
    s.step()
    s.step()
    s.step(e_yaw=16)
    s.step(e_active=0)
    s.steps(8)

    s = fixture(script, "automatic capture retains saved idle transform")
    s.step()
    s.set(s_sprint=1)
    for i in range(1, 21):
        walk(s, i * .1)
    s.set(b_has_ap=1, c_set=1)
    for action in ACTIONS:
        s.place(f"ap_{action}_", x=.12, y=.03, z=-.08, yaw=14, pitch=-7, roll=3)
    s.place("c_", x=.12, y=.03, z=-.08, yaw=14, pitch=-7, roll=3)
    s.capture()
    s.capture(True)
    s.capture(b_pose=2, b_pose_why="fixture changed pose owner")

    s = profile(script, "ADS keeps the reached aim placement")
    s.step()
    s.steps(120, s_shoulder=1)
    s.steps(30, s_ads=1)
    s.steps(120, s_ads=0, s_shoulder=0)

    s = fixture(script, "custom rotation is eligible before ADS")
    s.rotation(.4)
    s.step()
    s.rotation(None)
    s.update(s_ads=1)
    s.step(.075)
    s.step(.076)
    s.step(.05)

    s = fixture(script, "rapid ADS reversal")
    s.rotation(.6)
    s.step()
    s.rotation(None)
    s.step(s_ads=1)
    s.step(.06)
    s.rotation(.6)
    s.update(s_ads=0)
    s.step(.075)
    s.step(.076)
    s.step(.05)

    s = fixture(script, "ADS return blends rotation from identity")
    s.rotation(.4)
    s.lease(2, blend=True)
    s.step()
    s.step(.075)
    s.step(.076)
    s.step(.05)
    s.lease(3)
    s.rotation(-.3)
    s.step()

    for fault in ("read_failure", "hitch", "long_gap"):
        s = fixture(script, "an interrupted ADS fade eases out: " + fault)
        s.rotation(.4)
        s.step()
        s.rotation(None)
        s.step(s_ads=1)
        s.step(.04)
        if fault == "read_failure":
            s.step(s_mode=1, s_why="fixture sample unavailable")
            s.set(s_mode=0)
        elif fault == "hitch":
            s.step(.4)
        else:
            s.step(2)
        s.steps(12)

    s = fixture(script, "shared phase controls presentation handoff")
    s.rotation(.4)
    s.step()
    s.rotation(None)
    s.step(s_ads=1, b_pt=1, b_pt_epoch=1, b_pt_progress=0, b_pt_ads=1)
    s.step(.05, b_pt_progress=.5)
    s.step(.01)
    s.step(.05, b_pt_progress=1)

    for same_owner in (True, False):
        s = fixture(script, "same-identity cleanup carries presentation scalars: " + str(same_owner))
        s.lease(1, identity="body/weapon1", player="body")
        s.rotation(.4)
        s.step()
        s.step(.01, c_phase=1)
        s.lease(2, identity="body/weapon1" if same_owner else "body/weapon2", player="body")
        s.rotation(-.2)
        s.step(.01, c_phase=0)
        s.steps(3, .06)

    for rate in (30, 60, 144):
        s = fixture(script, f"ordinary walking sprinting aiming and ADS at {rate}", o_k5=1, o_v5=1, o_k1=1, o_v1=2, o_k3=1, o_v3=2)
        dt = 1 / rate
        y = [0.0]

        def run(seconds, speed, s=s, dt=dt, y=y, rate=rate, **knobs):
            s.set(**knobs)
            for _ in range(math.ceil(seconds * rate)):
                y[0] += speed * dt
                walk(s, y[0], dt)
        run(.5, 0)
        run(1, 3)
        run(1, 7, s_sprint=1)
        run(.6, 2, s_sprint=0, s_shoulder=1)
        run(.5, 1, s_ads=1)
        run(.4, 2, s_ads=0)
        run(.6, 7, s_shoulder=0, s_sprint=1)
        run(.3, 0, s_sprint=0, s_ads=1)
        run(.5, 0, s_ads=0)

    for fault in ("read_failure", "convert_failure", "bad_rotation"):
        s = fixture(script, "a failed sample eases a held sprint pose: " + fault, o_k5=1, o_v5=1)
        s.step()
        s.set(s_sprint=1)
        for i in range(1, 121):
            walk(s, i * .1)
        if fault == "read_failure":
            s.set(s_mode=1, s_why="fixture sample unavailable")
        elif fault == "convert_failure":
            s.set(tp_mode=1, tp_why="fixture bad basis")
        else:
            s.vector(qw=NAN)
        for _ in range(10):
            walk(s, s.w["s_y"] + .1)
        s.set(s_mode=0, tp_mode=0)
        for _ in range(4):
            walk(s, s.w["s_y"] + .1)

    s = fixture(script, "a refused write or a new lease is never softened", o_k5=1, o_v5=1)
    s.step()
    s.set(s_sprint=1)
    for i in range(1, 121):
        walk(s, i * .1)
    s.lease(3)
    walk(s, s.w["s_y"] + .1)
    walk(s, s.w["s_y"] + .1, b_pose=2, b_pose_why="fixture changed pose owner")
    walk(s, s.w["s_y"] + .1, b_pose=0)

    s = fixture(script, "a slow frame stretches an unshared ADS fade")
    s.rotation(.4)
    s.step()
    s.rotation(None)
    s.update(s_ads=1)
    s.step(.2)
    s.step(.2)
    s.step(.2)

    s = profile(script, "a placement target that moves after its phase finished", b_pt=1, b_pt_epoch=1, b_pt_progress=1, b_pt_ads=0)
    s.place("c_")
    s.step()
    s.step(s_shoulder=1)
    s.steps(120)
    s.step(s_shoulder=0, b_pt_epoch=2, b_pt_progress=0)
    s.step(b_pt_progress=.5)
    s.step(b_pt_progress=1)

    for rate in (60, 100, 144):
        s = fixture(script, f"a large authored rotation blend at {rate}")
        s.rotation(.9)
        s.step(1 / rate)
        s.rotation(None)
        s.step(1 / rate, s_ads=1, b_pt=1, b_pt_epoch=1, b_pt_progress=0, b_pt_ads=1)
        progress = 0.0
        while progress < 1:
            progress = min(1, progress + (1 / rate) / .18)
            s.step(1 / rate, b_pt_progress=progress)
        s.rotation(.9)
        s.set(b_pt=0, s_ads=0)
        s.steps(math.ceil(.2 * rate) + 2, 1 / rate)


# ------------------------------------------------------------------ hand-made situations
def live(script, name, **knobs):
    """The shipped shape: log, editor, options function, the default model, real conversion,
    action placements with the three presentation hooks."""
    start = dict(ctor_log=1, ctor_editor=1, ctor_options=2, ctor_model=0, ctor_tp=0, b_has_ap=1, b_has_mapping=1, b_pa=1, b_pr=1,
                 s_has_action=1, s_verified=1, s_l1=1, s_l3=1, s_l14=36, o_k5=1, o_v5=1)
    start.update(knobs)
    s = Session(script, name, **start)
    for action in ACTIONS:
        s.place(f"ap_{action}_", x=.1, z=-.05)
    s.place("ap_aim_", x=.02, y=.03, z=-.02, yaw=2)
    s.place("ap_ads_", x=.01, y=.02, z=-.01, yaw=4, pitch=-3, roll=1)
    s.place("ap_sprint_", x=.14, y=-.02, z=-.09, yaw=-6, pitch=5, roll=8)
    s.place("ap_reload_", x=.08, z=-.07, pitch=9)
    s.place("ap_crouch_", x=.09, z=-.04, roll=-4)
    s.place("ap_prone_", x=.07, y=.01, z=-.03, yaw=7)
    s.place("ap_slide_", x=.12, z=-.1, roll=12)
    s.place("ap_dive_", x=.11, z=-.02, pitch=-11)
    s.lease(1, identity="body/rifle", player="body")
    s.bases(basis(.3, .1, 0, as_float=True), basis(.35, .05, .02, as_float=True))
    s.new()
    return s


def posture(s, name):
    """The stance flags of action_placement.lua:50-65."""
    flags = {10: 0, 11: 0, 13: 0}
    if name in ("crouch", "slide"):
        flags[10] = 0x20
    if name in ("prone", "dive"):
        flags[10] = 0x40
    if name == "slide":
        flags[11] = 2
    if name == "dive":
        flags[13] = 32
    s.set(**{f"s_f{i}": v for i, v in flags.items()})


def moving(s, count, speed=3.0, dt=1 / 60, turn=0.0, **knobs):
    s.set(**knobs)
    for _ in range(count):
        yaw = s.w["s_yaw"] + turn * dt
        s.step(dt, s_x=s.w["s_x"] - math.sin(yaw) * speed * dt, s_y=s.w["s_y"] + math.cos(yaw) * speed * dt, s_yaw=yaw)


def hand(script):
    # Construction.
    for bad in (1, 2, 3):
        Session(script, f"construction: dependency {bad} missing", ctor_bad=bad).new()
    for knobs in (dict(m_enabled=3), dict(m_scale_kind=1, m_scale=3), dict(m_scale_kind=2), dict(m_frozen=3), dict(m_shoulder=3),
                  dict(m_k4=1, m_v4=.1), dict(m_k9=2)):
        Session(script, f"construction: model refuses {knobs}", ctor_model=2, **knobs).new()
    for log in (0, 1):
        for options in (0, 1, 2):
            for editor in (0, 1):
                for model in (0, 1, 2):
                    s = Session(script, f"construction {log}{options}{editor}{model}", ctor_log=log, ctor_options=options,
                                ctor_editor=editor, ctor_model=model, m_shoulder=2, m_scale_kind=1, m_scale=1.5, o_scale=.5)
                    s.new()
                    s.step()
                    moving(s, 6)
                    s.step(o_scale=1, o_k1=1, o_v1=2)
                    moving(s, 4, s_shoulder=1)
                    s.step(e_active=1)
                    s.reset()
                    s.step()

    # The gate, every condition, from a running state each time.
    gates = [dict(c_enabled=0), dict(c_enabled=2), dict(c_enabled=4), dict(c_enabled=3), dict(c_result=1), dict(c_result=2),
             dict(c_mode=1), dict(c_mode=2), dict(c_phase=1), dict(c_phase=2), dict(c_lease=0), dict(c_lease=0, b_alignment=0),
             dict(b_alignment=2), dict(b_alignment=0), dict(l1_done=1), dict(l1_cleanup=1), dict(c_lease=2)]
    for gate in gates:
        for clock in (0, 1, 3):
            s = live(script, f"gate {gate} clock {clock}")
            moving(s, 8, s_sprint=1)
            keep = {k: s.w[k] for k in gate}
            s.step(**gate, clock_mode2=clock)
            s.step()
            s.step(**keep, clock_mode2=0)
            moving(s, 4)

    # Continuity across a lease change.
    cases = [("same", "body/rifle", "body", .01, .01), ("other weapon", "body/pistol", "body", .01, .01),
             ("other player", "body/rifle", "body2", .01, .01), ("late", "body/rifle", "body", .01, .26),
             ("at the limit", "body/rifle", "body", .125, .125), ("long wait", "body/rifle", "body", .3, .01),
             ("clock back", "body/rifle", "body", .01, -.005), ("no identity", None, "body", .01, .01),
             ("no player", "body/rifle", None, .01, .01)]
    for label, identity, player, wait, gap in cases:
        for resume in (0, 1, 2):
            for ads in (0, 1):
                s = live(script, f"continuity {label} resume {resume} ads {ads}", b_pr=resume)
                moving(s, 30, s_sprint=1, speed=6)
                if ads:
                    s.step(s_ads=1, s_sprint=0)
                    s.step()
                s.step(wait, c_phase=1)
                s.step(wait, c_phase=1)
                s.lease(2, identity=identity, player=player)
                s.step(gap, c_phase=0)
                moving(s, 14, s_sprint=1, speed=6)
    for label, knobs in (("old lease without origin", dict(l1_origin=0)), ("old identity not a text", dict(l1_identity=0)),
                         ("old player not a text", dict(l1_player=0)), ("clock fails while waiting", dict(clock_mode2=1)),
                         ("clock not finite while waiting", dict(clock_mode2=4)), ("mode left", dict(c_mode=1)),
                         ("controller off", dict(c_enabled=0)), ("clock fails on return", dict())):
        s = live(script, "continuity: " + label)
        moving(s, 30, s_sprint=1, speed=6)
        s.step(.01, c_phase=1, **knobs)
        s.lease(2, identity="body/rifle", player="body")
        back = dict(c_phase=0, c_mode=0, c_enabled=1, clock_mode2=1 if label == "clock fails on return" else 0)
        s.step(.01, **back)
        s.set(clock_mode2=0)
        moving(s, 10, s_sprint=1, speed=6)
    s = live(script, "continuity: nothing was sent before the wait")
    s.step(.01, c_phase=1)
    s.lease(2, identity="body/rifle", player="body")
    s.step(.01, c_phase=0)
    moving(s, 5)
    s = live(script, "continuity: the lease left, then another one of the same weapon")
    moving(s, 30, s_sprint=1, speed=6)
    s.step(.01, c_lease=0)
    s.lease(2, identity="body/rifle", player="body")
    s.step(.01)
    moving(s, 10)
    s = live(script, "continuity: a different lease while waiting keeps the old one's modifier")
    moving(s, 30, s_sprint=1, speed=6)
    s.lease(3, identity="body/other", player="body")
    s.step(.01, c_phase=1)
    s.lease(2, identity="body/rifle", player="body")
    s.step(.01, c_phase=0)
    moving(s, 10)

    # The clock, each of its four calls.
    for call in (1, 2, 3, 4):
        for mode in (1, 2, 3, 4, 5):
            s = live(script, f"clock call {call} mode {mode}")
            moving(s, 6)
            s.step(**{f"clock_mode{call}": mode})
            s.step()
            s.lease(2, identity="body/rifle", player="body")
            s.step(**{f"clock_mode{call}": mode})
            s.step(c_phase=1)
            s.step(**{f"clock_mode{call}": 0}, c_phase=0)
            moving(s, 4)
    s = live(script, "clock: dt extremes")
    for dt in (1 / 60, 0, 0, 1e-9, 1e-300, 1 / 240, 1 / 239, 1 / 241, .05, .0500001, .0499999, .1, .1000001, .25, .2500001, 1, 1.0000001,
               5, 1e6, 0, -1e-9, 1 / 60, -5, 1 / 60, 1e15, 1 / 60):
        moving(s, 1, dt=dt, speed=2)
        moving(s, 2)
    s = live(script, "clock: advancing between its calls", clock_step=.001)
    moving(s, 10)
    s.step(c_phase=1)
    s.lease(2, identity="body/rifle", player="body")
    s.step(c_phase=0)
    moving(s, 6, clock_step=-.004)
    moving(s, 6, clock_step=.3)
    s = live(script, "clock: large times", time=1e9)
    moving(s, 10)
    s.update(time=1e17)
    moving(s, 4, dt=16)

    # Options.
    option_cases = [dict(o_mode=1), dict(o_mode=2), dict(o_scale_kind=2), dict(o_scale=NAN), dict(o_scale=INF), dict(o_scale=-.0001),
                    dict(o_scale=2.0000001), dict(o_scale=2), dict(o_scale=0), dict(o_scale=-0.0), dict(o_scale_kind=0),
                    dict(o_enabled=0), dict(o_enabled=1), dict(o_enabled=3), dict(o_frozen=3), dict(o_frozen=2), dict(o_frozen=1),
                    dict(o_shoulder=3), dict(o_shoulder=2), dict(o_shoulder=1)]
    for i in range(1, 10):
        speed = i in (2, 4, 7, 9)
        option_cases += [{f"o_k{i}": 1, f"o_v{i}": 1.5}, {f"o_k{i}": 1, f"o_v{i}": .2 if speed else -.1},
                         {f"o_k{i}": 1, f"o_v{i}": 2.1 if speed else 3.1}, {f"o_k{i}": 2}, {f"o_k{i}": 1, f"o_v{i}": NAN}]
    for case in option_cases:
        for ap in (0, 1):
            s = live(script, f"options {case} ap {ap}", b_has_ap=ap)
            moving(s, 8, s_shoulder=1)
            back = {k: s.w[k] for k in case}
            moving(s, 3, **case)
            moving(s, 3, **back)
            moving(s, 2, **case)
            s.step(s_shoulder=0, **back)
            moving(s, 3, s_sprint=1)
    for mode in (0, 1):
        s = live(script, f"options: given as mode {mode}", ctor_options=mode, o_scale=.7)
        moving(s, 6, s_sprint=1)
        moving(s, 4, o_scale=1.2, o_enabled=1)
        moving(s, 4, o_enabled=2, o_k3=1, o_v3=2.5)

    # The placement editor.
    for case in (dict(e_active=2), dict(e_active=2, e_why="editor is busy"), dict(e_active=3), dict(e_active=3, e_why="editor broke"),
                 dict(e_active=4), dict(e_active=1), dict(e_active=1, e_has_preview=1, e_preview_mode=1),
                 dict(e_active=1, e_has_preview=1, e_preview_mode=2), dict(e_active=1, e_has_preview=1, e_present=0),
                 dict(e_active=1, e_has_preview=1, e_yaw=10), dict(e_active=1, e_has_preview=1, e_pitch=-5),
                 dict(e_active=1, e_has_preview=1, e_roll=3), dict(e_active=1, e_has_preview=1, e_yaw=10, e_present=6),
                 dict(e_active=1, e_has_preview=1, e_yaw=91), dict(e_active=1, e_has_preview=1, e_pitch=NAN),
                 dict(e_active=1, e_has_preview=1, e_yaw=20, e_pitch=10, e_roll=-30),
                 dict(e_active=1, e_has_preview=1, e_yaw=-0.0, e_pitch=0, e_roll=0),
                 dict(e_active=1, e_has_preview=1, e_yaw=10, s_mode=1, s_why="no sample"), dict(e_active=1, e_has_preview=1, e_yaw=10, s_mode=2),
                 dict(e_active=1, e_has_preview=1, e_yaw=10, s_mode=3), dict(e_active=1, e_has_preview=1, e_yaw=10, s_par1=NAN),
                 dict(e_active=1, e_has_preview=1, e_yaw=10, s_cam6=3)):
        s = live(script, f"editor {case}")
        moving(s, 12, s_sprint=1, speed=6)
        back = {k: s.w[k] for k in case}
        s.step(**case)
        s.step()
        if "e_yaw" in case:
            s.step(e_yaw=case["e_yaw"] + 1)
        s.step()
        s.step(**back)
        moving(s, 8)
    s = live(script, "editor: the seam's answer is checked", ctor_tp=1, e_active=1, e_has_preview=1, e_yaw=10)
    s.step()
    for bad in (dict(dx=.151), dict(dy=-.151), dict(dz=NAN), dict(qx=NAN), dict(qw=.4, qx=math.sqrt(1 - .16)), dict(qw=1.0002),
                dict(qx=.6, qw=.8), dict(dx=.15, dy=-.15, dz=.15, qy=math.sin(.5), qw=math.cos(.5))):
        s.vector(**bad)
        s.step()
        s.step()
    s.step(tp_mode=1)
    s.step(tp_mode=1, tp_why="basis text")
    s.step(tp_mode=0)

    # No action placements: the disabled-idle shortcut.
    for hint in (0, 1, 2, 3, 4):
        for off in (dict(o_enabled=1), dict(o_scale=0), dict(o_scale=-0.0), dict()):
            for model in (0, 2):
                s = live(script, f"shortcut hint {hint} {off} model {model}", b_has_ap=0, b_hint=hint, ctor_model=model, m_shoulder=2, **off)
                moving(s, 4)
                s.step(b_hint_shoulder=1, s_shoulder=1)
                moving(s, 20)
                s.step(b_hint_active=1, s_ads=1)
                moving(s, 12)
                s.step(b_hint_active=0, s_ads=0)
                s.step(b_hint_shoulder=0, s_shoulder=0)
                moving(s, 30)
                s.step(s_mode=1)
                s.step(s_mode=0, clock_mode2=1)
                moving(s, 3, clock_mode2=0)

    # Sampler and clock refusals on the main path.
    for case in (dict(s_mode=1, s_why="Motion owner changed during observation"), dict(s_mode=2), dict(s_mode=3),
                 dict(s_mode=3, s_why="sampler broke"), dict(clock_mode2=1), dict(clock_mode2=3), dict(clock_mode2=2)):
        s = live(script, f"main path refusal {case}")
        moving(s, 20, s_sprint=1, speed=6)
        for _ in range(3):
            s.step(**case)
        moving(s, 12, s_mode=0, clock_mode2=0, s_sprint=1, speed=6)
        s.step(**case)
        moving(s, 3, s_mode=0, clock_mode2=0)

    # Actions: each one, the mapping, the revision, every answer of every placement call.
    s = live(script, "actions: each in turn")
    moving(s, 10)
    for change in (dict(s_shoulder=1), dict(s_shoulder=0, s_sprint=1), dict(s_sprint=0, s_reload=1), dict(s_reload=0, s_ads=1),
                   dict(s_ads=0), "crouch", "prone", "slide", "dive", "stand"):
        if isinstance(change, str):
            posture(s, change)
            moving(s, 14)
        else:
            moving(s, 14, **change)
    for verified, has_action, resource in ((0, 1, RESOURCE), (1, 0, RESOURCE), (1, 1, "0000000000000000")):
        posture(s, "crouch")
        moving(s, 5, s_verified=verified, s_has_action=has_action, s_resource=resource)
    posture(s, "stand")
    moving(s, 3, s_verified=1, s_has_action=1, s_resource=RESOURCE)
    for mapping in (dict(b_mapping_mode=1, s_l1=75, s_l3=71), dict(s_l1=5, s_l3=6), dict(s_has_l=0), dict(s_has_l=7, s_l1=1, s_l3=2),
                    dict(b_mapping_set=2, s_l1=7, s_has_l=5), dict(s_has_l=6, s_l3=9), dict(s_resource="0000000000000000", s_l1=5, s_l3=6, s_has_l=7),
                    dict(b_mapping_mode=0), dict(b_has_mapping=0), dict(b_has_mapping=1, b_mapping_mode=2), dict(b_mapping_mode=1)):
        moving(s, 5, **mapping)
    for action in ACTIONS:
        for mode in (1, 2):
            for site in ("steady", "revision", "lease", "enter"):
                s = live(script, f"placement call: {action} mode {mode} at {site}")
                moving(s, 6)
                enter = dict(aim=dict(s_shoulder=1), ads=dict(s_ads=1), sprint=dict(s_sprint=1), reload=dict(s_reload=1)).get(action, {})
                if action in ("crouch", "prone", "slide", "dive"):
                    posture(s, action)
                if site != "enter":
                    moving(s, 6, **enter)
                s.set(**{f"ap_{action}_mode": mode}, **enter)
                if site == "revision":
                    s.set(b_revision=3)
                if site == "lease":
                    s.lease(2, identity="body/rifle", player="body")
                moving(s, 3)
                moving(s, 3, **{f"ap_{action}_mode": 0})
                posture(s, "stand")
                moving(s, 3, s_shoulder=0, s_ads=0, s_sprint=0, s_reload=0)
    for get in (1, 2):
        for site in ("steady", "enter aim", "enter ads", "revision", "capture"):
            for ads_nil in (0, 1):
                s = live(script, f"controller placement: mode {get} at {site} ads nil {ads_nil}", ap_ads_mode=ads_nil)
                moving(s, 6)
                if site == "steady":
                    moving(s, 3, c_get=get)
                elif site == "enter aim":
                    moving(s, 3, c_get=get, s_shoulder=1)
                elif site == "enter ads":
                    moving(s, 3, c_get=get, s_ads=1)
                elif site == "revision":
                    moving(s, 3, c_get=get, b_revision=2)
                else:
                    s.capture(c_get=get)
                    s.capture(c_get=get, b_has_ap=0)
                    s.capture(True, c_get=get, b_has_ap=0)
                    s.set(b_has_ap=1)
                moving(s, 3, c_get=0)
                moving(s, 3, s_shoulder=0, s_ads=0, ap_ads_mode=0)
    for case in (dict(c_set=1), dict(c_set=2), dict(c_set=2, c_set_why="Placement must be finite XYZ within 0.5 metres"), dict(c_set=3),
                 dict(c_set=3, c_set_why="placement broke"), dict(c_set=4), dict(b_ads_set=2), dict(b_ads_set=2, b_ads_why="Pose owner changed"),
                 dict(b_ads_set=3), dict(b_ads_set=3, b_ads_why="ads broke"), dict(b_ads_set=4), dict(b_revision_nil=1), dict(b_revision=NAN),
                 dict(b_key=None), dict(b_pa=0), dict(b_pa=2)):
        s = live(script, f"placement writes {case}")
        moving(s, 6)
        moving(s, 4, **{**dict(s_shoulder=1, b_revision=5), **case})
        moving(s, 4, s_shoulder=0)
        moving(s, 6, c_set=0, b_ads_set=0)
    s = live(script, "placement: fields that are missing, snapping, values that are not numbers")
    moving(s, 4)
    s.place("ap_aim_", x=.02, present=1)
    s.place("c_", x=.1, y=.01, present=3)
    moving(s, 30, s_shoulder=1)
    s.place("ap_idle_", x=.02 + .00009, present=7)
    moving(s, 3, s_shoulder=0)
    s.place("ap_aim_", x=.02 + .00009 + .0001, y=.01, z=0)
    moving(s, 3, s_shoulder=1)
    s.place("ap_idle_", x=NAN, y=.2, z=INF, yaw=-INF)
    moving(s, 3, s_shoulder=0)
    s.place("ap_aim_", x=.4, y=-.4, z=.4, yaw=80, pitch=-80, roll=80)
    moving(s, 40, s_shoulder=1, c_set=0)
    s.place("c_", x=.1, z=-.05)
    s.place("ap_ads_", x=.01, yaw=120, pitch=-3, roll=1)
    moving(s, 5, s_shoulder=0, s_ads=1)
    s.place("ap_ads_", x=.01, present=1)
    moving(s, 5, b_revision=9)
    s.place("ap_idle_", x=.1, z=-.05)
    moving(s, 5, s_ads=0)

    # The shared phase.
    for case in (dict(b_pt=2), dict(b_pt=3), dict(b_pt_progress=NAN), dict(b_pt_progress=-.01), dict(b_pt_progress=1.01),
                 dict(b_pt_progress=INF), dict(b_pt_ads=1), dict(b_pt_epoch=2 ** 40)):
        s = live(script, f"shared phase {case}", b_pt=1, b_pt_epoch=1, b_pt_progress=1)
        moving(s, 6)
        s.set(b_pt_epoch=2, b_pt_progress=0)
        for progress in (0, .2, .5):
            moving(s, 1, s_shoulder=1, b_pt_progress=progress)
        keep = {k: s.w[k] for k in case}
        moving(s, 3, **case)
        moving(s, 1, **keep)
        for progress in (.8, 1, 1, 1):
            moving(s, 1, b_pt_progress=progress)
        moving(s, 3, s_shoulder=0)
        moving(s, 1, s_ads=1, b_pt_epoch=7, b_pt_progress=0, b_pt_ads=1)
        for progress in (.1, .4, .4, .9, 1, 1):
            moving(s, 1, b_pt_progress=progress)
        moving(s, 1, s_ads=0, b_pt_epoch=8, b_pt_progress=.3, b_pt_ads=0)
        moving(s, 2, b_pt_progress=.2)
        moving(s, 2, b_pt_progress=1)
        s.lease(2, identity="body/rifle", player="body")
        moving(s, 3)
        s.lease(3, identity="body/rifle", player="body", blend=True)
        moving(s, 2, b_pt_epoch=9, b_pt_progress=0)
        moving(s, 2, b_pt_progress=.5)
        moving(s, 2, b_pt_progress=1)

    # The ADS handoff without a shared phase.
    for dt in (1 / 144, 1 / 60, 1 / 30, .05, .075, .2):
        for blend in (0, 1):
            s = live(script, f"handoff dt {dt} entry blend {blend}", l1_blend=blend)
            moving(s, 12, dt=dt, s_sprint=1, speed=6)
            moving(s, 2, dt=dt, s_sprint=0)
            moving(s, 10, dt=dt, s_ads=1)
            s.update()
            moving(s, 3, dt=dt, s_ads=0)
            s.update(s_ads=1)
            s.update()
            s.update(s_ads=0)
            moving(s, 10, dt=dt)
            s.lease(2, identity="body/gun2", player="body", blend=bool(blend))
            moving(s, 8, dt=dt, s_ads=1)
            s.lease(3, identity="body/gun3", player="body", blend=bool(blend))
            moving(s, 8, dt=dt, s_ads=0)
    s = live(script, "handoff: a held clock at the ADS edge, both ways", ctor_tp=1)
    s.rotation(.5)
    moving(s, 3)
    s.update(s_ads=1)
    s.update()
    s.update(s_ads=0)
    s.update(s_ads=1)
    s.rotation(None)
    moving(s, 12)
    s.rotation(.5)
    s.update(s_ads=0)
    moving(s, 12)

    # The conversion: the real one's refusals, and the seam's.
    bad_bases = [("s_cam1", NAN), ("s_cam6", INF), ("s_par11", NAN), ("s_cam1", 3.0), ("s_par6", .1), ("s_cam2", .9), ("s_par9", 2e8)]
    for knob, value in bad_bases:
        s = live(script, f"conversion: basis {knob}={value}")
        moving(s, 10, s_sprint=1, speed=6)
        keep = s.w[knob]
        moving(s, 3, **{knob: value})
        moving(s, 6, **{knob: keep})
    s = live(script, "conversion: mirrored and out-of-range angles")
    moving(s, 10, s_sprint=1, speed=6)
    mirrored = basis(.3, .1, 0)
    mirrored[0:3] = [-v for v in mirrored[0:3]]
    s.bases(mirrored)
    moving(s, 3)
    s.bases(basis(.3, .1, 0, as_float=True))
    s.place("ap_sprint_", x=.14, yaw=85)
    moving(s, 20, b_revision=1)
    s.place("ap_sprint_", x=.14, yaw=-6, pitch=5, roll=8)
    moving(s, 20, b_revision=2)
    s = live(script, "conversion: the seam's answers", ctor_tp=1)
    moving(s, 6)
    cases = [dict(dx=.15), dict(dx=.15000001), dict(dy=-.15), dict(dy=-.1500001), dict(dz=.151), dict(dx=NAN), dict(dy=INF), dict(dz=-INF),
             dict(qx=NAN), dict(qy=NAN), dict(qz=NAN), dict(qw=NAN), dict(qw=INF), dict(qw=1.00004), dict(qw=1.00006), dict(qw=.99996),
             dict(qw=.99994), dict(qx=math.sin(math.radians(59.9)), qw=math.cos(math.radians(59.9))),
             dict(qx=math.sin(math.radians(60.1)), qw=math.cos(math.radians(60.1))), dict(qy=math.sin(math.radians(60)), qw=math.cos(math.radians(60))),
             dict(qz=-1, qw=0), dict(qx=-math.sin(.3), qw=-math.cos(.3)), dict(qx=1e-200, qw=1), dict(qw=1, dx=-0.0)]
    for case in cases:
        s.vector(**case)
        moving(s, 2)
        s.vector()
        moving(s, 2)
    for case in (dict(tp_mode=1), dict(tp_mode=1, tp_why="Transform scale changed"), dict(tp_mode=0)):
        moving(s, 2, **case)

    # The pop guard.
    for rate in (30, 60, 144, 240, 500):
        s = live(script, f"pop guard at {rate}", ctor_tp=1)
        dt = 1 / rate
        moving(s, 4, dt=dt)
        jumps = [dict(dx=.1), dict(dx=.1, dy=.1), dict(), dict(qx=math.sin(.5), qw=math.cos(.5)), dict(qy=-math.sin(.5), qw=math.cos(.5)),
                 dict(dx=.0019), dict(dx=.0021 + 1 / rate), dict(dx=.0021 + 1 / rate, dz=1 / rate), dict(),
                 dict(qz=math.sin(math.radians(.49)), qw=math.cos(math.radians(.49))),
                 dict(qz=math.sin(math.radians(3.1 * 60 / rate + .6)), qw=math.cos(math.radians(3.1 * 60 / rate + .6))), dict()]
        for jump in jumps:
            s.vector(**jump)
            moving(s, max(3, int(.2 * rate)), dt=dt)
        # A second jump inside a running guard, a slow frame inside it, a held clock inside it.
        s.vector(dx=.12)
        moving(s, 2, dt=dt)
        s.vector(dx=-.12, qx=math.sin(.4), qw=math.cos(.4))
        moving(s, 2, dt=dt)
        moving(s, 1, dt=.3)
        s.update()
        moving(s, 12, dt=dt)
        # A guard and an ADS handoff together; a guard ended by the editor, a refusal, a reset.
        s.vector()
        moving(s, 12, dt=dt)
        s.vector(dy=.14)
        moving(s, 2, dt=dt)
        moving(s, 12, dt=dt, s_ads=1)
        s.vector(dy=-.14)
        moving(s, 2, dt=dt, s_ads=0)
        s.vector(dz=.14)
        moving(s, 3, dt=dt)
        s.step(dt, e_active=1)
        s.step(dt, e_active=0)
        s.vector(dz=-.14)
        moving(s, 2, dt=dt)
        s.step(dt, b_pose=2)
        s.step(dt, b_pose=0)
        s.vector(dx=.14)
        moving(s, 2, dt=dt)
        s.reset()
        moving(s, 3, dt=dt)

    # Submissions: every refusal, coalescing, the log.
    for case in (dict(b_pose=2), dict(b_pose=2, b_pose_why="Pose owner changed"), dict(b_pose=3), dict(b_pose=3, b_pose_why="pose broke"),
                 dict(b_pose=4)):
        for log in (0, 1):
            s = live(script, f"submission {case} log {log}", ctor_log=log)
            moving(s, 10, s_sprint=1, speed=6)
            for _ in range(3):
                moving(s, 1, **case)
            moving(s, 6, b_pose=0)
            moving(s, 2, **case)
            s.step(e_active=1)
            s.step(b_pose=0)
            s.step(e_active=0, s_mode=1, s_why="first")
            s.step()
            s.step(s_why="second")
            s.step(s_mode=2)
            s.step(s_mode=1, s_why="first")
            moving(s, 3, s_mode=0)
            s.step(s_mode=1, s_why="first")
            s.reset()
            s.step()
    s = live(script, "submission: nothing new is not sent again", ctor_tp=1)
    s.vector()
    moving(s, 5)
    s.vector(dx=-0.0)
    moving(s, 3)
    s.vector(dx=.001)
    moving(s, 3)
    moving(s, 2, s_ads=1)
    moving(s, 12)
    moving(s, 2, s_ads=0)

    # Automatic capture, every way out.
    captures = [dict(), dict(c_enabled=0), dict(c_enabled=2), dict(c_enabled=4), dict(c_enabled=3), dict(c_result=1), dict(c_result=2),
                dict(c_result=2, c_enabled=0), dict(c_mode=1), dict(c_phase=1), dict(c_lease=0), dict(b_alignment=2), dict(l1_done=1),
                dict(l1_cleanup=1), dict(s_mode=1, s_why="no capture sample"), dict(s_mode=2), dict(s_mode=3), dict(b_has_ap=0),
                dict(ap_idle_mode=1), dict(ap_idle_mode=2), dict(ap_idle_mode=1, c_get=1), dict(ap_idle_mode=1, c_get=2),
                dict(c_set=2), dict(c_set=2, c_set_why="Placement was not accepted"), dict(c_set=3), dict(c_set=4), dict(c_set=1),
                dict(ap_idle_yaw=95), dict(ap_idle_present=7), dict(ap_idle_yaw=14, ap_idle_pitch=-7, ap_idle_roll=3),
                dict(s_cam1=NAN, ap_idle_yaw=14), dict(s_cam1=NAN), dict(b_pose=2), dict(b_pose=2, b_pose_why="Pose owner changed"),
                dict(b_pose=3), dict(b_pose=4)]
    for case in captures:
        for native in (None, True, False):
            s = live(script, f"capture {case} native {native}")
            moving(s, 12, s_sprint=1, speed=6)
            keep = {k: s.w[k] for k in case}
            s.capture(native, **case)
            s.capture(native)
            s.step(**keep)
            s.capture(native)
            s.capture(native)
            moving(s, 6, s_sprint=1, speed=6)
    s = live(script, "capture: the seam's answer is checked", ctor_tp=1)
    moving(s, 4)
    for bad in (dict(dx=.2), dict(qw=NAN), dict(qx=.9, qw=math.sqrt(1 - .81)), dict(qx=math.sin(.3), qw=math.cos(.3))):
        s.vector(**bad)
        s.capture()
    s.capture(tp_mode=1)

    # The model's own answers as the runtime passes them on.
    s = live(script, "model: owner, teleport, values that are not finite")
    moving(s, 10, s_sprint=1, speed=6)
    s.step(s_owner="body-B")
    moving(s, 3)
    s.step(s_owner="body-A")
    s.step(s_x=500)
    moving(s, 3)
    s.step(s_x=NAN)
    s.step(s_x=0)
    s.step(s_yaw=INF)
    s.step(s_yaw=0, s_pitch=NAN)
    s.step(s_pitch=0, s_z=1e9)
    moving(s, 3)
    s.step(.3)
    s.step(1.5)
    moving(s, 3)
    s = live(script, "model: frozen by its options", o_frozen=2)
    moving(s, 5, s_sprint=1)
    moving(s, 5, o_frozen=1)
    s = live(script, "result and reset before anything ran")
    s.reset()
    s.capture()
    s.reset()
    s.step()


def gaps(script):
    """Situations added after the first mutation pass: each one tells a surviving change apart."""
    # The pop guard's turn threshold at sixty frames a second (six degrees).
    s = live(script, "gap: guard turn threshold", ctor_tp=1)
    s.vector()
    moving(s, 6)
    for degrees in (5.99, 6.01, 5.9, 6.1, 2.9, 12):
        s.vector(qz=math.sin(math.radians(degrees) / 2), qw=math.cos(math.radians(degrees) / 2))
        moving(s, 12)
        s.vector()
        moving(s, 12)
    # A shared epoch that is still running after the state was cleared starts a new handoff.
    for clear in ("lease", "sample", "refusal", "gate", "reset"):
        s = live(script, "gap: shared epoch after " + clear, b_pt=1, b_pt_epoch=5, b_pt_progress=.5, b_pt_ads=0)
        moving(s, 30, s_sprint=1, speed=6)
        if clear == "lease":
            s.lease(2, identity="body/other", player="body")
        elif clear == "sample":
            s.step(s_mode=2)
            s.set(s_mode=0)
        elif clear == "refusal":
            s.step(b_pose=2)
            s.set(b_pose=0)
        elif clear == "gate":
            s.step(.3, c_phase=1)
            s.step(.3)
            s.set(c_phase=0)
        else:
            s.reset()
        moving(s, 6, s_sprint=1, speed=6)
        moving(s, 3, b_pt_progress=1)
    # Coming back to the same lease starts from nothing, not from what was sent before.
    for clear in ("gate", "refusal", "reset"):
        for knobs in (dict(l1_blend=1), dict(s_ads=1)):
            s = live(script, f"gap: same lease again after {clear} {knobs}")
            moving(s, 30, s_sprint=1, speed=6)
            if clear == "gate":
                s.step(.3, c_phase=1)
                s.step(.3)
                s.set(c_phase=0)
            elif clear == "refusal":
                s.step(b_pose=2)
                s.set(b_pose=0)
            else:
                s.reset()
            moving(s, 8, **knobs)
    # Only the fourth quaternion number changes; only one small number makes a rotation.
    s = live(script, "gap: one number of the modifier", ctor_tp=1)
    s.vector()
    moving(s, 4)
    for case in (dict(qw=1.00002), dict(qw=1.00004), dict(qw=1.00002), dict(qx=.005), dict(qy=.005), dict(qz=.005), dict(qx=-.005, qw=1.00001)):
        s.vector(**case)
        moving(s, 3)
    for axis in ("qx", "qy", "qz"):
        s.vector(**{axis: 1e-9})
        moving(s, 4, s_ads=0)
        s.vector()
        moving(s, 14, s_ads=1)
        s.vector(**{axis: -1e-9})
        moving(s, 14, s_ads=0)
    # Capture, and a resumed lease, each end the carried modifier.
    s = live(script, "gap: capture ends the carried modifier")
    moving(s, 30, s_sprint=1, speed=6)
    s.step(.01, c_phase=1)
    s.capture(c_phase=0)
    s.lease(2, identity="body/rifle", player="body")
    s.step(.01)
    moving(s, 4)
    s = live(script, "gap: a resumed lease does not carry twice")
    moving(s, 30, s_sprint=1, speed=6)
    s.step(.01, c_phase=1)
    s.lease(2, identity="body/rifle", player="body")
    s.step(.01, c_phase=0)
    s.lease(3, identity="body/rifle", player="body")
    s.step(.01)
    moving(s, 4)
    # The carried modifier's own time window, on the frames spent waiting.
    waits = [("clock back while waiting", [(10.0, {}), (9.99, {})], 10.02), ("too long while waiting, clock back after", [(10.0, {}), (10.3, {})], 10.1),
             ("exactly a quarter second", [(10.0, {}), (10.25, {})], 10.25), ("just over a quarter second", [(10.0, {}), (10.2500001, {})], 10.25),
             ("return at the same time", [(10.0, {})], 10.0), ("controller off on the second wait", [(10.0, {}), (10.01, dict(c_enabled=0))], 10.02),
             ("clock fails on the second wait", [(10.0, {}), (10.01, dict(clock_mode2=1))], 10.02),
             ("mode left on the second wait", [(10.0, {}), (10.01, dict(c_mode=1))], 10.02), ("plain", [(10.0, {}), (10.01, {})], 10.02)]
    for label, frames, back in waits:
        s = live(script, "gap: carried window: " + label, time=9)
        moving(s, 30, s_sprint=1, speed=6)
        for at, knobs in frames:
            s.update(time=at, c_phase=1, **knobs)
        s.lease(2, identity="body/rifle", player="body")
        s.update(time=back, c_phase=0, c_enabled=1, c_mode=0, clock_mode2=0)
        moving(s, 4)
    # An identity that is a number is no identity, even when the next lease's text has the same key.
    for which in ("identity", "player"):
        s = live(script, "gap: lease " + which + " is a number")
        index = script.text("body/rifle" if which == "identity" else "body")
        s.set(**{f"l1_{which}": -index})
        moving(s, 30, s_sprint=1, speed=6)
        s.step(.01, c_phase=1)
        s.lease(2, identity="body/rifle", player="body")
        s.step(.01, c_phase=0)
        moving(s, 4)
        s.step(.01, c_phase=1)
        s.lease(3, identity="body/rifle", player="body")
        s.set(**{f"l3_{which}": -index})
        s.step(.01, c_phase=0)
        moving(s, 4)
    # The shared phase step where almost all of it is spent.
    s = profile(script, "gap: phase step close to its end", b_pt=1, b_pt_epoch=1, b_pt_progress=1, b_pt_ads=0)
    s.place("c_")
    s.step()
    s.set(s_shoulder=1, b_pt_epoch=2)
    for progress in (0, .5, .99, .999, .99982, .9999, 1, 1):
        s.step(b_pt_progress=progress)
    s.set(s_shoulder=0, b_pt_epoch=3)
    for progress in (.2, .99982, 1, .5, 1):
        s.step(b_pt_progress=progress)
    # The target moves away while the phase has a millionth, then nothing, left to spend.
    s.set(s_shoulder=1, b_pt_epoch=4)
    for progress in (.5, .999):
        s.step(b_pt_progress=progress)
    s.step(s_shoulder=0, b_pt_progress=.9999)
    s.step(s_shoulder=1, b_pt_progress=.99995)
    s.set(s_shoulder=1, b_pt_epoch=5)
    for progress in (.5, .99982):
        s.step(b_pt_progress=progress)
    s.step(s_shoulder=0, b_pt_progress=1)
    s.step(s_shoulder=1)
    s.steps(4)
    # The model's vertical bob is not clamped: at the largest settings the conversion refuses it.
    s = live(script, "gap: bob beyond the conversion's bound", o_scale=2, o_k1=1, o_v1=3, o_k5=1, o_v5=3)
    moving(s, 200, s_sprint=1, speed=7)
    moving(s, 30, s_sprint=0, speed=2)
    # Construction: the first setting refused; no options given, over a model built with its own.
    Session(script, "gap: construction refuses the first setting", ctor_model=2, m_k1=2).new()
    Session(script, "gap: construction refuses the first setting's value", ctor_model=2, m_k1=1, m_v1=3.5).new()
    s = live(script, "gap: no options over a model that was off", ctor_options=0, ctor_model=2, m_enabled=1, m_scale_kind=1, m_scale=.5,
             m_k5=1, m_v5=1)
    moving(s, 40, s_sprint=1, speed=6)
    # The step of the first frame, kept while the first clock call fails.
    for first in (0, 1):
        s = live(script, f"gap: first frame step, good frames before {first}", ctor_tp=1, clock_mode1=1 - first)
        s.vector()
        s.step()
        s.set(clock_mode1=1)
        moving(s, 3)
        for dx in (.0165, 0, .02, 0, .003, 0, .1, 0):
            s.vector(dx=dx)
            moving(s, 10)
    # Placement fields that are missing: the three angles one by one.
    for present in (55, 47, 31, 7):
        s = live(script, f"gap: capture, idle placement fields {present}")
        s.place("ap_idle_", x=.1, z=-.05, yaw=14, pitch=-7, roll=3, present=present)
        moving(s, 6)
        s.capture()
        moving(s, 3)
        s = live(script, f"gap: ADS placement fields {present}")
        s.place("ap_ads_", x=.01, y=.02, z=-.01, yaw=4, pitch=-3, roll=1, present=present)
        s.place("c_", x=.1, z=-.05, present=7)
        moving(s, 4, c_set=1)
        moving(s, 4, s_ads=1)
        moving(s, 4, s_ads=0)
        s = live(script, f"gap: reached placement fields {present}")
        s.place("ap_idle_", x=.1, z=-.05, yaw=10, pitch=5, roll=-7, present=present)
        s.place("c_", x=.1, z=-.05, yaw=10, pitch=5, roll=-7, present=present)
        moving(s, 8, s_sprint=1, speed=6)
    # A held clock at the ADS edge: its answer has no weights; the model's sprint is cleared.
    s = live(script, "gap: held clock, then the disabled shortcut", b_has_ap=0)
    moving(s, 10, s_sprint=1, speed=6)
    s.update(s_ads=1, s_sprint=0)
    s.step(s_ads=0, o_enabled=1)
    moving(s, 4)
    for ap in (0, 1):
        s = live(script, f"gap: held clock clears the sprint, ap {ap}", b_has_ap=ap)
        moving(s, 60, s_sprint=1, speed=7)
        s.update(s_ads=1)
        moving(s, 8, s_ads=0, s_sprint=1, speed=7)


# ------------------------------------------------------------------ seeded random sessions
def random_rotation(r, limit_degrees=110.0):
    """A unit quaternion within the limit, as the seam's answer."""
    axis = [r.gauss(0, 1) for _ in range(3)]
    length = math.sqrt(sum(a * a for a in axis)) or 1.0
    angle = math.radians(r.uniform(0, limit_degrees))
    sine = math.sin(angle / 2) / length
    return dict(qx=axis[0] * sine, qy=axis[1] * sine, qz=axis[2] * sine, qw=math.cos(angle / 2))


def random_session(script, seed, count, ap=True, seam=False, rate=60.0, faults=1.0):
    r = random.Random(seed)
    name = f"random seed {seed} ap {int(ap)} seam {int(seam)} rate {rate:g}"
    start = dict(ctor_log=1, ctor_options=r.choice([2, 2, 2, 1]), ctor_editor=r.choice([0, 1, 1]), ctor_model=r.choice([0, 1, 2]),
                 m_shoulder=r.choice([0, 1, 2]), ctor_tp=int(seam), b_has_ap=int(ap), b_has_mapping=r.choice([0, 1]), b_mapping_mode=1,
                 b_pa=r.choice([0, 1, 1, 2]), b_pr=r.choice([0, 1, 1, 2]), s_has_action=1, s_verified=1, s_l1=1, s_l3=1, s_l14=36,
                 o_k5=1, o_v5=r.choice([0, 1, 1, 2]), b_hint=0)
    s = Session(script, name, **start)

    def placements():
        for action in ACTIONS:
            s.place(f"ap_{action}_", x=r.uniform(-.3, .3), y=r.uniform(-.2, .2), z=r.uniform(-.3, .3),
                    yaw=r.choice([0, 0, r.uniform(-30, 30)]), pitch=r.choice([0, 0, r.uniform(-30, 30)]),
                    roll=r.choice([0, 0, r.uniform(-30, 30)]), present=r.choice([63, 63, 63, 7, 15, 55]))
    placements()
    s.place("c_", x=.1, z=-.05)
    weapons = ["body/rifle", "body/pistol", "body/support"]
    lease, weapon = 1, "body/rifle"
    s.lease(1, identity="body/rifle", player="body")
    camera_angles = [r.uniform(-3, 3), r.uniform(-.5, .5)]
    parent_angles = [r.uniform(-3, 3), r.uniform(-.5, .5), r.uniform(-.3, .3)]
    s.bases(basis(*camera_angles, 0, as_float=True), basis(*parent_angles, as_float=True))
    s.new()
    restore = []  # (operation number, knobs)
    speed, turn = 0.0, 0.0
    packet = None   # [epoch, progress]
    epoch = 0

    def fault(i, length, **knobs):
        keep = {k: s.w[k] for k in knobs}
        s.set(**knobs)
        restore.append((i + length, keep))

    for i in range(count):
        for item in [x for x in restore if x[0] <= i]:
            restore.remove(item)
            s.set(**item[1])
        dt = (1 / rate) * r.uniform(.8, 1.25)
        roll = r.random()
        if roll < .02:
            dt = 0.0
        elif roll < .025:
            dt = -r.uniform(0, .02)
        elif roll < .035:
            dt = r.uniform(.05, .3)
        elif roll < .04:
            dt = r.uniform(.3, 1.2)
        elif roll < .043:
            dt = r.choice([2, 100, 1e-7, 1e-12])
        event = False
        if r.random() < .02:
            speed = r.choice([0, 0, 1.5, 3, 4.5, 7, 7])
        if r.random() < .03:
            turn = r.choice([0, 0, .5, -.5, 2, -3])
        yaw = s.w["s_yaw"] + turn * max(dt, 0)
        if yaw > math.pi:
            yaw -= 2 * math.pi
        if yaw < -math.pi:
            yaw += 2 * math.pi
        s.set(s_x=s.w["s_x"] - math.sin(yaw) * speed * max(dt, 0), s_y=s.w["s_y"] + math.cos(yaw) * speed * max(dt, 0), s_yaw=yaw)
        if r.random() < .05:
            s.set(s_pitch=max(-1.4, min(1.4, s.w["s_pitch"] + r.uniform(-.05, .05))))
        if r.random() < .003:
            s.set(s_x=s.w["s_x"] + r.choice([1, 3, 50, 1e6]))
        if r.random() < .002 * faults:
            fault(i, 1, **{r.choice(["s_x", "s_y", "s_z", "s_yaw", "s_pitch"]): r.choice([NAN, INF, -INF])})
        if r.random() < .002:
            s.set(s_owner=r.choice(["body-A", "body-B"]))
        for knob, chance in (("s_ads", .012), ("s_shoulder", .012), ("s_sprint", .015), ("s_reload", .006)):
            if r.random() < chance:
                s.set(**{knob: 1 - s.w[knob]})
                event = True
        if r.random() < .008:
            posture(s, r.choice(["stand", "stand", "crouch", "prone", "slide", "dive"]))
            event = True
        if r.random() < .004:
            s.set(s_verified=r.choice([0, 1, 1]), s_has_action=r.choice([0, 1, 1, 1]), s_resource=r.choice([RESOURCE, RESOURCE, "0000000000000000"]),
                  s_l1=r.choice([1, 5, 7, 75, 78]), s_l3=r.choice([1, 2, 6, 9, 71]), s_l14=r.choice([3, 36]), s_has_l=r.choice([7, 7, 7, 5, 6, 0]),
                  b_mapping_set=r.choice([1, 2]), b_mapping_mode=r.choice([0, 1, 1, 1]))
        if r.random() < .003:
            s.set(b_has_mapping=r.choice([0, 1]), b_pa=r.choice([0, 1, 1, 2]), b_pr=r.choice([0, 1, 1, 2]))
        # Leases.
        if r.random() < .006:
            lease = lease % LEASES + 1
            if r.random() < .5:
                weapon = r.choice(weapons)
            identity = weapon
            s.lease(lease, identity=r.choice([identity, identity, identity, None]), player=r.choice(["body", "body", "body", "body2", None]),
                    origin=r.random() < .95, blend=r.random() < .4, both=r.random() < .9)
            event = True
            if s.w["b_alignment"] != lease:
                restore.append((i + r.randint(1, 4), dict(b_alignment=lease)))
        if r.random() < .006:
            fault(i, r.randint(1, 12), c_phase=r.choice([1, 1, 2, 3, 4]))
        if r.random() < .002 * faults:
            fault(i, r.randint(1, 3), **r.choice([dict(c_mode=1), dict(c_mode=2), dict(c_enabled=0), dict(c_enabled=2), dict(c_enabled=3),
                                                  dict(c_enabled=4), dict(c_result=1), dict(c_result=2), dict(c_lease=0),
                                                  dict(**{f"l{lease}_done": 1}), dict(**{f"l{lease}_cleanup": 1})]))
        # Options.
        if r.random() < .008:
            key = r.randint(1, 9)
            speed_key = key in (2, 4, 7, 9)
            s.set(**r.choice([dict(o_scale=r.choice([0, .5, 1, 1, 2, r.uniform(0, 2)])), dict(o_enabled=r.choice([0, 1, 2, 2])),
                              {f"o_k{key}": 1, f"o_v{key}": r.uniform(.25, 2) if speed_key else r.uniform(0, 3)},
                              {f"o_k{key}": 0}, dict(o_shoulder=r.choice([0, 1, 2])), dict(o_frozen=r.choice([0, 1, 1, 2])), dict(o_scale_kind=r.choice([0, 1, 1]))]))
        if r.random() < .002 * faults:
            key = r.randint(1, 9)
            fault(i, r.randint(1, 3), **r.choice([dict(o_mode=1), dict(o_mode=2), dict(o_scale=r.choice([NAN, INF, -1, 2.5])), dict(o_scale_kind=2),
                                                  dict(o_enabled=3), dict(o_frozen=3), dict(o_shoulder=3), {f"o_k{key}": 2},
                                                  {f"o_k{key}": 1, f"o_v{key}": r.choice([-1, 5, NAN])}]))
        # Editor.
        if r.random() < .004:
            fault(i, r.randint(1, 40), e_active=1, e_has_preview=r.choice([0, 1, 1]), e_preview_mode=r.choice([0, 0, 0, 1]),
                  e_yaw=r.choice([0, 0, r.uniform(-40, 40)]), e_pitch=r.choice([0, r.uniform(-40, 40)]), e_roll=r.choice([0, r.uniform(-40, 40)]),
                  e_present=r.choice([7, 7, 3, 5, 0]))
        if s.w["e_active"] == 1 and r.random() < .1:
            s.set(e_yaw=r.uniform(-40, 40))
        if r.random() < .002 * faults:
            fault(i, r.randint(1, 3), **r.choice([dict(e_active=2), dict(e_active=2, e_why="editor is busy"), dict(e_active=3), dict(e_active=4),
                                                  dict(e_active=1, e_has_preview=1, e_preview_mode=2), dict(e_active=1, e_has_preview=1, e_yaw=95, e_present=7)]))
        # Sampler, clock, submissions.
        if r.random() < .004 * faults:
            fault(i, r.randint(1, 4), **r.choice([dict(s_mode=1, s_why="Motion owner changed during observation"), dict(s_mode=1, s_why="Memory unavailable"),
                                                  dict(s_mode=2), dict(s_mode=3)]))
        if r.random() < .004 * faults:
            fault(i, r.randint(1, 2), **{f"clock_mode{r.randint(1, 4)}": r.randint(1, 5)})
        if r.random() < .002:
            fault(i, r.randint(1, 5), clock_step=r.choice([.0005, .002, -.001, .02]))
        if r.random() < .005 * faults:
            fault(i, r.randint(1, 3), **r.choice([dict(b_pose=2), dict(b_pose=2, b_pose_why="Pose owner changed"), dict(b_pose=3), dict(b_pose=4)]))
        # Bases.
        if r.random() < .05:
            camera_angles = [yaw, s.w["s_pitch"]]
            parent_angles = [yaw + r.uniform(-.2, .2), s.w["s_pitch"] + r.uniform(-.2, .2), r.uniform(-.2, .2)]
            s.bases(basis(*camera_angles, 0, as_float=r.random() < .8, scale=r.choice([1, 1, 1, .7, 1.6])),
                    basis(*parent_angles, as_float=r.random() < .8, scale=r.choice([1, 1, 1, .8, 1.3])))
        if r.random() < .003 * faults:
            knob = r.choice(["s_cam", "s_par"]) + str(r.choice([1, 2, 3, 5, 6, 7, 9, 10, 11]))
            fault(i, r.randint(1, 3), **{knob: r.choice([NAN, INF, 3.0, s.w[knob] + .5, -s.w[knob], 0.0])})
        # Placements.
        if ap:
            if r.random() < .004:
                placements()
                if r.random() < .8:
                    s.set(b_revision=s.w["b_revision"] + 1)
            if r.random() < .003:
                s.set(b_revision=s.w["b_revision"] + 1, b_revision_nil=r.choice([0, 0, 0, 1]))
            if r.random() < .003 * faults:
                action = r.choice(ACTIONS)
                fault(i, r.randint(1, 3), **{f"ap_{action}_mode": r.choice([1, 2])})
            if r.random() < .003 * faults:
                fault(i, r.randint(1, 3), **r.choice([dict(c_get=1), dict(c_get=2), dict(c_set=1), dict(c_set=2), dict(c_set=2, c_set_why="Placement was not accepted"),
                                                      dict(c_set=3), dict(c_set=4), dict(b_ads_set=2), dict(b_ads_set=2, b_ads_why="Pose owner changed"),
                                                      dict(b_ads_set=3), dict(b_ads_set=4), dict(b_has_mapping=1, b_mapping_mode=2), dict(b_key=None)]))
            if r.random() < .002:
                fault(i, r.randint(1, 30), b_has_ap=0, o_enabled=r.choice([1, 2]))
        else:
            if r.random() < .01:
                s.set(o_enabled=r.choice([1, 1, 2]), o_scale=r.choice([0, 0, 1]))
            if r.random() < .004 * faults:
                fault(i, r.randint(1, 4), b_hint=r.choice([1, 2, 3, 4]))
            if r.random() < .002:
                fault(i, r.randint(1, 30), b_has_ap=1)
        s.set(b_hint_active=s.w["s_ads"] if r.random() > .01 else 1 - s.w["s_ads"],
              b_hint_shoulder=(s.w["s_shoulder"] and not s.w["s_ads"]) if r.random() > .01 else r.choice([0, 1]))
        # The shared phase.
        if packet is None and r.random() < .01:
            packet = [epoch, 1.0]
        if packet is not None:
            if event and r.random() < .8:
                epoch += 1
                packet = [epoch, 0.0]
            else:
                packet[1] = min(1.0, packet[1] + max(dt, 0) / .18)
            s.set(b_pt=1, b_pt_epoch=packet[0], b_pt_progress=packet[1], b_pt_ads=s.w["s_ads"] if r.random() > .02 else 1 - s.w["s_ads"])
            if r.random() < .003:
                packet = None
                s.set(b_pt=0)
            elif r.random() < .003 * faults:
                fault(i, r.randint(1, 3), **r.choice([dict(b_pt=2), dict(b_pt=3), dict(b_pt_progress=NAN), dict(b_pt_progress=1.5), dict(b_pt_progress=-.2)]))
        # The seam.
        if seam:
            if r.random() < .02:
                kind = r.random()
                if kind < .35:
                    s.vector(mode=0)
                elif kind < .9:
                    s.vector(dx=r.uniform(-.15, .15), dy=r.uniform(-.15, .15), dz=r.uniform(-.15, .15), **random_rotation(r))
                else:
                    s.vector(**random_rotation(r, 119.0))
            if s.w["tp_mode"] == 2 and r.random() < .05:
                s.vector(dx=s.w["tp_dx"] + r.uniform(-.004, .004) if abs(s.w["tp_dx"]) < .14 else 0.0, dy=s.w["tp_dy"], dz=s.w["tp_dz"],
                         qx=s.w["tp_qx"], qy=s.w["tp_qy"], qz=s.w["tp_qz"], qw=s.w["tp_qw"])
            if r.random() < .004 * faults:
                fault(i, r.randint(1, 3), **r.choice([dict(tp_mode=1), dict(tp_mode=1, tp_why="Transform basis is skewed"), dict(tp_mode=2, tp_dx=.2),
                                                      dict(tp_mode=2, tp_qw=NAN), dict(tp_mode=2, tp_qw=.3), dict(tp_mode=2, tp_qx=.5)]))
        # The operation.
        roll = r.random()
        if roll < .004:
            s.set(time=s.w["time"] + dt)
            s.capture(r.choice([None, True, False]))
        elif roll < .007:
            s.reset()
        else:
            s.step(dt)


def build():
    script = Script()
    fixtures(script)
    hand(script)
    gaps(script)
    random_session(script, 101, 3000, ap=True, rate=60)
    random_session(script, 102, 2500, ap=True, rate=144)
    random_session(script, 103, 2500, ap=True, rate=30)
    random_session(script, 104, 2500, ap=True, seam=True, rate=60)
    random_session(script, 105, 2500, ap=False, rate=60)
    random_session(script, 106, 2000, ap=False, seam=True, rate=100)
    random_session(script, 107, 2500, ap=True, rate=60, faults=6)
    random_session(script, 108, 2000, ap=True, seam=True, rate=120, faults=6)
    random_session(script, 109, 1500, ap=False, seam=True, rate=60, faults=6)
    return script


# ------------------------------------------------------------------ running and writing
def lua_number(value):
    if value != value:
        return "(0/0)"
    if value in (INF, -INF):
        return "math.huge" if value > 0 else "(-math.huge)"
    if value == 0 and math.copysign(1, value) < 0:
        return "(-1/math.huge)"
    return repr(value) if value != int(value) or abs(value) >= 1e15 else str(int(value))


def lua_script(script, evidence):
    out = ["return {names={" + ",".join(f'"{n}"' for n in KNOBS) + "},",
           "strings={" + ",".join('"%s"' % t for t in script.strings) + "},",
           'evidence="%s",' % evidence, "ops={"]
    for kind, sets, _ in script.ops:
        row = [f'"{kind}"']
        for knob, value in sets:
            row += [str(knob), lua_number(value)]
        out.append("{" + ",".join(row) + "},")
    out.append("}}")
    return "\n".join(out) + "\n"


SQUARE_SHIM = r"""
/* x^2 as the correctly rounded square. LuaJIT's compiled traces compute x^2 as x*x; its
 * interpreter calls the C library's pow(x, 2), which with glibc is the same number for about
 * 999 of 1000 arguments and one unit in the last place off for the rest. Which of the two a
 * given x^2 gets depends on what the trace compiler happened to compile. Preloading this makes
 * the interpreter agree with the compiled code, so the reference is one definite function. */
#define _GNU_SOURCE
#include <dlfcn.h>
double pow(double x, double y) {
  static double (*real)(double, double);
  if (y == 2.0) return x * x;
  if (!real) real = (double (*)(double, double))dlsym(RTLD_NEXT, "pow");
  return real(x, y);
}
"""


def square_shim(folder):
    source, library = Path(folder) / "square.c", Path(folder) / "square.so"
    source.write_text(SQUARE_SHIM)
    subprocess.check_call(["cc", "-shared", "-fPIC", "-O1", str(source), "-o", str(library), "-ldl"])
    return library


def run(interpreter, retail, script, evidence, coverage=None, libm_pow=False):
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as handle:
        handle.write(lua_script(script, evidence))
    command = interpreter + [str(HERE / "run_motion_runtime.lua"), str(retail), handle.name] + ([str(coverage)] if coverage else [])
    with tempfile.TemporaryDirectory() as folder:
        environment = dict(os.environ)
        if not libm_pow:
            environment["LD_PRELOAD"] = str(square_shim(folder))
        done = subprocess.run(command, capture_output=True, text=True, env=environment)
    Path(handle.name).unlink()
    if done.returncode:
        sys.exit(done.stderr)
    lines = done.stdout.splitlines()
    if len(lines) != len(script.ops):
        sys.exit(f"{len(lines)} answers for {len(script.ops)} operations")
    return lines, done.stderr


def c_double(value):
    if value != value:
        return "NAN"
    if value in (INF, -INF):
        return "INFINITY" if value > 0 else "-INFINITY"
    if value == int(value) and abs(value) < 1e9 and not (value == 0 and math.copysign(1, value) < 0):
        return str(int(value))
    return float(value).hex()


def c_text(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def header(script, lines, stamp, evidence):
    kinds = {"begin": 0, "new": 1, "update": 2, "capture": 3, "reset": 4}
    out = [stamp, "#include <math.h>", f"#define MR_SPRINT_EVIDENCE {c_text(evidence)}",
           "enum { MR_BEGIN, MR_NEW, MR_UPDATE, MR_CAPTURE, MR_RESET };",
           f"enum {{ MR_LEASES = {LEASES} }};",
           "/* The knobs of the scripted fakes, as tools/retail_ref/run_motion_runtime.lua names them. */",
           "enum { K_none, " + ", ".join("K_" + n for n in KNOBS) + ", K_COUNT };",
           "static const char *const mr_strings[] = {0, " + ", ".join(c_text(t) for t in script.strings) + "};",
           "typedef struct MrSet { unsigned short knob; double value; } MrSet;",
           "typedef struct MrOp { unsigned char kind; unsigned short set_count; unsigned set_first; const char *name, *expect; } MrOp;",
           "static const MrSet mr_sets[] = {"]
    row, first, firsts = [], 0, []
    for _, sets, _ in script.ops:
        firsts.append(first)
        first += len(sets)
        for knob, value in sets:
            row.append("{%d,%s}" % (knob, c_double(value)))
            if len(row) == 12:
                out.append(",".join(row) + ",")
                row = []
    out.append(",".join(row + ["{0,0}"]) + "};")
    out.append("static const MrOp mr_ops[] = {")
    for (kind, sets, name), line, at in zip(script.ops, lines, firsts):
        out.append("{%d,%d,%d,%s,%s}," % (kinds[kind], len(sets), at, c_text(name) if name else "0", c_text(line)))
    out.append("};")
    return "\n".join(out) + "\n"


def executable_lines(source):
    listing = subprocess.check_output(["luac5.1", "-l", "-l", "-p", str(source)], text=True)
    return sorted({int(m.group(1)) for m in re.finditer(r"^\s+\d+\s+\[(\d+)\]\s+\w+", listing, re.M)})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--lua", default="luajit", help="interpreter and arguments, e.g. 'luajit -joff'")
    parser.add_argument("--lines", type=Path, help="also write the raw answers here (to compare interpreters)")
    parser.add_argument("--coverage", action="store_true", help="report the line coverage of motion_runtime.lua")
    parser.add_argument("--libm-pow", action="store_true",
                        help="leave x^2 in the interpreter to the C library's pow (see SQUARE_SHIM); for comparison only")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    source = args.retail / "src/first_person/motion_runtime.lua"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    evidence = re.search(r"sprint_evidence='([^']*)'", (args.retail / "src/first_person/locomotion.lua").read_text()).group(1)
    script = build()
    if args.coverage:
        with tempfile.NamedTemporaryFile(suffix=".txt") as handle:
            run(args.lua.split(), args.retail, script, evidence, Path(handle.name))
            hit = {int(line.split()[0]) for line in Path(handle.name).read_text().splitlines()}
        wanted = executable_lines(source)
        missed = [n for n in wanted if n not in hit]
        print(f"motion_runtime.lua: {len(wanted)} executable lines, {len(wanted) - len(missed)} hit; not hit: {missed}")
        return 0
    lines, noise = run(args.lua.split(), args.retail, script, evidence, libm_pow=args.libm_pow)
    if args.libm_pow and args.out:
        sys.exit("--libm-pow is for comparison; the vectors are cut with x^2 as the exact square.")
    if args.lines:
        args.lines.write_text("\n".join(lines) + "\n")
    counts = {}
    for kind, _, _ in script.ops:
        counts[kind] = counts.get(kind, 0) + 1
    raised = sum(line.startswith("R|") for line in lines)
    summary = (f"{len(script.ops)} operations in {counts['begin']} sessions: {counts.get('update', 0)} updates, {counts.get('capture', 0)} captures, "
               f"{counts.get('reset', 0)} resets, {counts.get('new', 0)} constructions; {raised} raised")
    print("motion_runtime: " + summary)
    sys.stderr.write(noise)
    if not args.out:
        return 0
    stamp = (f"/* Generated by tools/retail_ref/gen_motion_runtime.py. Do not edit.\n * Retail file: src/first_person/motion_runtime.lua at commit "
             f"{commit}, sha256 {digest}.\n * {summary}.\n * Each answer: the call's answer | the ordered trace of every call into a scripted "
             f"fake | the state table\n * ('=': unchanged). A number is decimal when whole, else 'x' and the hex digits of the double.\n * x^2 is the exact square in the interpreter too, as in LuaJIT's compiled traces (SQUARE_SHIM in the generator). */\n"
             f'#define MR_VECTOR_COMMIT "{commit}"\n#define MR_VECTOR_SHA256 "{digest}"')
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "motion_runtime_vectors.h").write_text(header(script, lines, stamp, evidence))
    return 0


if __name__ == "__main__":
    sys.exit(main())
