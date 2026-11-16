#!/usr/bin/env python3
"""Cut reference vectors from the retail pose.lua (adapter of the editor/pose helper), run
unmodified under LuaJIT against a scripted helper and clock.

  gen_pose_adapter.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import argparse
import hashlib
import random
import subprocess
import sys
import tempfile
from pathlib import Path

from gen_fov_adapter import FROZEN, INF, NAN, c_string, cnum, lua, pad

HERE = Path(__file__).resolve().parent
CALLS = ["stats", "depth", "input", "prepare", "begin", "placement", "ads", "motion", "zoom", "transition", "owned", "finish"]
# game, exe, context, request, avatar_owner, avatar_index, avatar, weapon, body, viewmodel, duration_ms, weapon_descriptor,
# weapon_unit, blend_ms, offset_x, offset_y, offset_z
CFG = [0x7FF612340000, 0x140000000, 0x1F0A1000, 0x1F0A1200, 0x7FF612990000, 3, 11, 7, 12, 13, 30500, 0x1F0C3000, 21, 150,
       0.15, -0.02, 0.05]
OWNED, NONE, FOREIGN, STALE = -1, -2, -3, -4          # handle arguments; 0..3 are the script's own slots
CODES = list(range(0, 20)) + [4294967295]
IDLE = dict(inst=0, armed=0)
Q = [0.0, 0.0, 0.0, 1.0]


def ans(ret=1, fill=True, inst=1, armed=1, expired=0, err=0):
    return dict(ret=ret, fill=fill, inst=inst, armed=armed, expired=expired, err=err)


def diag(ret=1, fill=True, size=0, abi=1, reserved=0, i1=1, i2=1, i3=1):
    return dict(ret=ret, fill=fill, size=size, abi=abi, reserved=reserved, i1=i1, i2=i2, i3=i3)


def op(call="stats", **kw):
    w = dict(call=call, slot=0 if call == "begin" else OWNED, prep=1, cfg_ok=True, cfg_missing=0, cfg_false=False, steady=False,
             cfg=list(CFG), v=[0.01, -0.02, 0.03] + Q, nilmask=0, authored=False, active=1,
             pk=1, pk_epoch_num=True, pk_epoch=1.0, pk_progress_num=True, pk_progress=0.5, pk_ads=1,
             clk=0, now=0.0, tick=0, validate=1, begin=1, begin_gen=41, setr=1, endr=[1],
             st=[ans(**IDLE)] if call == "prepare" else [ans()], err=[0], sbase=10, ssize=80, sabi=2, f1=1.0, f2=0.5,
             dg=diag(), sim_disarm=-1)
    w.update(kw)
    w["v"] = [float(x) for x in w["v"]]
    w["pk_epoch"], w["pk_progress"] = float(w["pk_epoch"]), float(w["pk_progress"])
    if not w["dg"]["size"]:
        w["dg"] = dict(w["dg"], size=96 if call == "input" else 104)
    return w


def model(call="stats", **kw):
    """An operation the model of the real helper answers."""
    kw.setdefault("st", [ans(ret=-1)])
    return op(call, validate=-1, begin=-1, setr=-1, endr=[-1], err=[-1], dg=diag(ret=-1), **kw)


class Script:
    def __init__(self, start=10.0, maker=op):
        self.ops, self.t, self.maker = [], start, maker

    def step(self, call="stats", dt=0.016, **kw):
        self.t += dt
        kw.setdefault("now", self.t)
        now = kw["now"]
        kw.setdefault("tick", int(round(now * 1000)) if now == now and 0 <= now < 2 ** 50 else 0)
        self.ops.append(self.maker(call, **kw))


def session():
    """What a session produces: preparation, Begin, the statistics poll on its 10 Hz display tick, placement,
    motion and phase publication, End; then a steady entry."""
    s = Script()
    s.step("owned"); s.step("stats", slot=NONE, st=[ans(**IDLE)]); s.step("depth", slot=NONE); s.step("input", slot=NONE)
    s.step("prepare"); s.step("begin", slot=0, begin_gen=41); s.step("owned")
    for i in range(30):
        s.step(dt=0.0167, sbase=10 + i, f1=min(1.0, i / 9), f2=1.0)       # counters refresh on the 10 Hz grid only
    s.step(slot=0, sbase=50); s.step(slot=NONE, sbase=51)                 # by its handle; without one: the owned handle
    s.step("placement", v=[0.1, -0.05, 0.02] + Q); s.step("ads", v=[0.0, 0.01, -0.03] + Q)
    s.step("motion", v=[0.01, 0.0, -0.02] + Q, nilmask=0x78)              # no rotation: identity
    s.step("motion", v=[0.01, 0.0, -0.02, 0.0, 0.1, 0.0, 0.99498743710662])
    s.step("motion", v=[0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0], authored=True)
    s.step("zoom", active=1); s.step("zoom", active=0)
    for i in range(12):
        s.step("transition", pk_epoch=3, pk_progress=i / 11, pk_ads=i % 2); s.step(sbase=60 + i, f1=i / 11)
    s.step(now=s.t - 0.05, sbase=99); s.step(sbase=100)                   # the clock steps back: the cache is dropped
    s.step(sbase=101, st=[ans(err=7)]); s.step(sbase=102); s.step(sbase=103, st=[ans(expired=1)]); s.step(sbase=104)
    s.step("depth"); s.step("input"); s.step("depth", slot=0, sbase=77, f1=0.3, f2=65.0); s.step("input", slot=NONE, sbase=78)
    s.step("prepare", st=[ans()]); s.step("begin", slot=1, begin_gen=42)  # while armed: both refused
    s.step("finish", sbase=120, st=[ans(), ans(**IDLE)]); s.step("owned")
    s.step(slot=0); s.step("finish", slot=0); s.step("placement", slot=0); s.step(slot=NONE, st=[ans(**IDLE)])
    s.step("prepare"); s.step("begin", slot=1, begin_gen=42, steady=True)
    for i in range(8):
        s.step(dt=0.05, sbase=200 + i)
    s.step("finish", slot=1, sbase=300, st=[ans(), ans(**IDLE)])
    s.step("begin", slot=2, begin_gen=43, steady=False); s.step(dt=0.3, sbase=400)  # the old token again
    s.step("finish", sbase=401, st=[ans(ret=0), ans(**IDLE)]); s.step("finish", slot=2); s.step(slot=2)
    return s.ops


def refusals():
    """The helper refusing at each call with each error code; cleanup owed and retried."""
    s = Script(50.0)
    for code in CODES:
        s.step("prepare", validate=0, err=[code])
        s.step("prepare", st=[ans(ret=0)], err=[code]); s.step("prepare", st=[ans(**IDLE), ans(ret=0)], err=[code])
        s.step("begin", begin=0, begin_gen=0, err=[code]); s.step("owned")
        s.step("begin", begin=0, begin_gen=77, err=[code, 0]); s.step("owned"); s.step(st=[ans(armed=0, err=code)])
        s.step("finish", endr=[0], err=[code]); s.step("finish", st=[ans(armed=0), ans(**IDLE)])
        s.step("begin", begin=2, begin_gen=78, err=[code])                # partial installation
        s.step("prepare", st=[ans(armed=0)]); s.step("begin", slot=1)     # owned, not armed: validated; a second Begin refused
        s.step("finish", endr=[0], err=[code]); s.step("finish", endr=[2], err=[code + 1])
        s.step("finish", st=[ans(ret=0), ans(ret=0)], err=[code, code + 1])   # ended although no statistics could be read
        s.step(slot=0); s.step("finish", slot=0)
        s.step("begin", begin_gen=79)
        for call in ("placement", "ads", "motion", "zoom", "transition"):
            s.step(call, setr=0, err=[code])
        s.step("motion", authored=True, setr=2, err=[code])
        s.step(st=[ans(ret=0)], err=[code]); s.step("depth", dg=diag(ret=0), err=[code]); s.step("input", dg=diag(ret=0), err=[code])
        s.step(sbase=500 + code % 50); s.step(dt=0.001, sbase=600, st=[ans(ret=0)], err=[code]); s.step(dt=0.001, sbase=601)
        s.step("finish", sbase=602, st=[ans(), ans(ret=0)], err=[code])   # the last counters come from before End
        s.step(slot=0)
    for result, generation in ((1, 0), (2, 0), (3, 0), (0, 0), (1, 5), (2, 6), (7, 9), (4294967295, 1), (1, 2 ** 64 - 1), (0, 2 ** 63)):
        s.step("begin", begin=result, begin_gen=generation, err=[6]); s.step("owned"); s.step(); s.step("finish", slot=0)
    # Statistics that do not look like this ABI, are left unfilled or carry extreme values.
    s.step("begin", begin_gen=80)
    s.step(ssize=88); s.step(sabi=1); s.step(ssize=0, sabi=0); s.step(st=[ans(fill=False)], sbase=555); s.step(st=[ans(ret=2)])
    s.step(dt=0.2, st=[ans(armed=2, inst=5)], sbase=2 ** 64 - 1, f1=NAN, f2=INF); s.step(dt=0.2, sbase=2 ** 53 + 1, f1=-INF, f2=-0.0)
    s.step(dt=0.2, sbase=123456789012345678, f1=1e30, f2=1 / 3); s.step(dt=0.2, sbase=2 ** 63, st=[ans(err=4294967295)])
    s.step(dt=0.2, st=[ans(inst=0)]); s.step(dt=0.2, st=[ans(armed=0)]); s.step(dt=0.2, st=[ans(expired=7)])
    s.step(dt=0.2, st=[ans(expired=1, err=18, armed=0)]); s.step(dt=0.2)
    for d in (diag(size=100), diag(abi=2), diag(reserved=1), diag(fill=False), diag(ret=2), diag(i1=0, i2=0, i3=0), diag(i1=7, i2=2, i3=9)):
        s.step("depth", dg=d, sbase=700, f1=0.25, f2=70.0, err=[3])
    for d in (diag(size=104), diag(abi=0), diag(reserved=4294967295), diag(fill=False), diag(ret=0), diag(i1=0, i2=0), diag(i1=7, i2=2, i3=9)):
        s.step("input", dg=d, sbase=701, f1=0.25, f2=70.0, err=[4])
    s.step("depth", sbase=2 ** 64 - 1, f1=NAN, f2=INF); s.step("input", sbase=2 ** 53 + 1, f1=-INF, f2=1e-30)
    s.step("depth", sbase=2 ** 63 + 2 ** 11 + 1, f1=1e30, f2=-0.0); s.step("input", sbase=6, f1=1 / 3, f2=2 / 3)
    s.step("finish", st=[ans(ret=0), ans(ret=0)], err=[3, 4]); s.step(slot=0); s.step("finish", slot=0)
    # Read-only preparation: another lease armed, the helper's state moving under it.
    s.step("prepare", st=[ans(inst=1, armed=1)]); s.step("prepare", st=[ans(inst=0, armed=1)])
    s.step("prepare", st=[ans(**IDLE), ans(inst=0, armed=1)]); s.step("prepare", st=[ans(**IDLE), ans(inst=1, armed=0)])
    s.step("prepare", st=[ans(inst=1, armed=0), ans(**IDLE)]); s.step("prepare", st=[ans(inst=1, armed=0), ans(inst=2, armed=0)])
    s.step("prepare", st=[ans(**IDLE), ans(inst=1, armed=1)]); s.step("prepare", st=[ans(**IDLE)], ssize=72)
    s.step("prepare", st=[ans(**IDLE), ans(fill=False)], validate=2, err=[0]); s.step("prepare", st=[ans(expired=1, **IDLE)])
    s.step("prepare", st=[ans(err=9, **IDLE), ans(err=3, **IDLE)])
    return s.ops


def inputs():
    """Arguments and configurations that are missing, out of range or odd."""
    s = Script(200.0)
    s.step("prepare", cfg_ok=False); s.step("begin", cfg_ok=False)
    for bit in range(17):
        s.step("prepare", cfg_missing=1 << bit); s.step("begin", cfg_missing=1 << bit)
    s.step("prepare", cfg_missing=0x1FFFF); s.step("begin", cfg_missing=0x10100); s.step("prepare", cfg_missing=0x18, cfg_false=True)
    s.step("begin", cfg_missing=0x4000, cfg_false=True); s.step("begin", prep=0); s.step("begin", prep=2)
    s.step("begin", prep=0, cfg_ok=False); s.step("begin", prep=2, cfg_missing=1)
    odd = [[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0, 0.0, 0.0],
           [2 ** 64 - 1, 2 ** 63, 2 ** 53 + 1, 1, 2 ** 64 - 2, 4294967295, 4294967295, 1, 2, 3, 0, 2 ** 64 - 1, 4294967295, 4294967295,
            NAN, INF, -INF],
           CFG[:14] + [1e39, -1e-46, -0.0], CFG[:14] + [0.1, 1 / 3, 16777217.0]]
    for cfg in odd:
        for steady in (False, True):
            s.step("prepare", cfg=cfg, steady=steady); s.step("begin", cfg=cfg, steady=steady, begin_gen=90)
            s.step("begin", cfg=cfg, slot=1); s.step("finish")
    s.step("begin", begin_gen=91); s.step("begin", prep=0); s.step("begin", prep=2, cfg_ok=False); s.step("begin", cfg_ok=False)
    s.step("prepare", cfg_ok=False, st=[ans(armed=0)]); s.step("prepare", cfg_missing=2, st=[ans(armed=0)])
    # Placement: finite XYZ within half a metre.
    edge = [0.5, -0.5, 0.5000001, -0.5000001, 0.0, -0.0, NAN, INF, -INF, 1e-320, 0.49999999999999994, 2.0]
    for call in ("placement", "ads"):
        for value in edge:
            for axis in range(3):
                v = [0.1, -0.2, 0.3] + Q
                v[axis] = value
                s.step(call, v=v)
        for mask in range(1, 8):
            s.step(call, nilmask=mask); s.step(call, nilmask=mask, v=[0.6, NAN, -0.7] + Q)
        s.step(call, v=[0.1 + 0.2, 1 / 3, -0.1] + Q); s.step(call, v=[0.5, 0.5, 0.5] + Q); s.step(call, v=[-0.5, -0.5, -0.5] + Q)
    # Motion: XYZ within 0.15 metres and a unit quaternion within 120 degrees.
    for value in (0.15, -0.15, 0.1500001, -0.1500001, 0.0, -0.0, NAN, INF, -INF, 0.15000000000000002, 1.0):
        for axis in range(3):
            v = [0.01, -0.02, 0.03] + Q
            v[axis] = value
            s.step("motion", v=v); s.step("motion", v=v, nilmask=0x78, authored=True)
    for mask in list(range(1, 8)) + [0x08, 0x10, 0x20, 0x40, 0x18, 0x70, 0x38, 0x78, 0x7F, 0x79, 0x48]:
        s.step("motion", nilmask=mask); s.step("motion", nilmask=mask, v=[0.2, 0.0, 0.0, 0.0, 0.0, 0.0, 2.0])
    h = 0.5 ** 0.5
    quats = [[0, 0, 0, 1], [0, 0, 0, -1], [1, 0, 0, 0], [h, 0, 0, h], [0.5, 0.5, 0.5, 0.5], [0.5, 0.5, 0.5, -0.5],
             [0.8660254037844387, 0, 0, 0.5], [0.8660254037844386, 0, 0, 0.49999999999999994], [0.87, 0, 0, 0.4930517214248420],
             [0, 0, 0, 1.001], [0, 0, 0, 1.0011], [0, 0, 0, 1.0009], [0, 0, 0, 0.999], [0, 0, 0, 0.9989], [0, 0, 0, 1.0009995],
             [0, 0, 0, 0.99899949], [0, 0, 0, 0.9990004], [0.0316, 0, 0, 1.0005], [1.0011, 0, 0, 0], [0, -1.0011, 0, 1], [0, 0, 1.002, 0],
             [0, 0, 0, 0], [NAN, 0, 0, 1], [0, NAN, 0, 1], [0, 0, NAN, 1], [0, 0, 0, NAN], [INF, 0, 0, 1], [0, 0, 0, -INF],
             [0.6, 0.0, 0.0, 0.8], [0.0, -0.6, 0.0, -0.8], [0.3, 0.4, 0.5, 0.7071067811865476], [0.1, 0.2, 0.3, 0.9273618495495703],
             [0.865390, 0, 0, 0.4996], [0.866660, 0, 0, 0.5004], [0, 0.865390, 0, -0.4996], [0, 0, 0.866660, -0.5004],
             [1e-170, 0, 0, 1], [0.7, 0.5, 0.1, 0.5], [0.70, 0.5, 0.1, 0.4999], [0.7071, 0.5, 0, 0.5], [0.5, 0.5, 0.5, 0.5019]]
    for q in quats:
        s.step("motion", v=[0.01, -0.02, 0.03] + q); s.step("motion", v=[0.15, -0.15, 0.0] + q, authored=True)
    for active in (0, 1, 2):
        s.step("zoom", active=active)
    # Transition: the shared presentation phase.
    s.step("transition", pk=0)
    for epoch in (1, 2, 0, -1, 0.5, 1.5, 1.0000000001, 2 ** 53 - 1, 2 ** 53, 2 ** 52 + 0.5, 4294967296, 1e300, NAN, INF, -INF, -0.0):
        s.step("transition", pk_epoch=epoch); s.step("transition", pk_epoch=epoch, pk_ads=0, pk_progress=1.0)
    for progress in (0.0, -0.0, 1.0, 1.0000000000000002, -1e-320, 1e-320, 0.3333333333333333, NAN, INF, -INF, 2.0, -0.5):
        s.step("transition", pk_epoch=7, pk_progress=progress); s.step("transition", pk_epoch=8, pk_progress=progress, pk_ads=0)
    s.step("transition", pk_epoch_num=False); s.step("transition", pk_progress_num=False); s.step("transition", pk_ads=2)
    s.step("transition", pk_epoch_num=False, pk_progress_num=False, pk_ads=2); s.step("transition", pk_epoch=0, pk_ads=2)
    s.step("finish"); s.step("transition"); s.step("motion"); s.step("zoom")
    return s.ops


def handles():
    """Every method with every kind of handle, in every state of the adapter."""
    s = Script(400.0)
    every = ("stats", "depth", "input", "placement", "ads", "motion", "zoom", "transition", "finish")

    def sweep(slots):
        for slot in slots:
            for call in every:
                s.step(call, slot=slot, st=[ans(**IDLE)], sbase=len(s.ops))
        s.step("owned")

    sweep((NONE, OWNED, FOREIGN, STALE, 0, 3))                             # nothing owned
    s.step("begin", slot=0, begin_gen=41)
    sweep((NONE, FOREIGN, STALE, 1))                                      # one generation owned
    for call in every[:-1]:
        s.step(call, slot=0, sbase=len(s.ops)); s.step(call, slot=OWNED, sbase=len(s.ops))
    s.step("finish", slot=0, endr=[0], err=[13]); sweep((STALE, FOREIGN)); s.step("finish", slot=0)
    sweep((0, OWNED, STALE))                                              # disarmed: final statistics, nothing else
    s.step("begin", slot=1, begin_gen=42); sweep((0, STALE)); s.step("begin", slot=0, begin_gen=43)
    s.step(slot=0); s.step("finish", slot=1); sweep((0, 1))
    s.step("begin", slot=2, begin=2, begin_gen=44, err=[11]); sweep((0, 1, NONE)); s.step("finish", slot=2); sweep((2,))
    s.step("begin", slot=3, begin=0, begin_gen=0, err=[2]); sweep((3,))
    s.step("begin", slot=3, begin_gen=45); s.step("begin", slot=2, begin_gen=46); s.step("finish", slot=2); s.step("finish", slot=3)
    s.step("finish", slot=3)
    return s.ops


def clocks():
    """The staggered display tick: grid points, time backwards, clocks that fail, number conversion."""
    s = Script(5.0)
    s.step("begin", begin_gen=41)
    for now in (5.0, 5.08, 5.084999, 5.085, 5.0850001, 5.1, 5.184, 5.185, 5.186, 5.285, 5.2849999999999993, 5.385, 5.3851, 5.3849,
                5.9, 4.0, 4.1, 4.2, 0.0, 0.084, 0.085, 0.086, -5.0, -4.95, -4.9, 1e17, 1e17, 1e300, 1e300, 0.3, 0.385, 0.485):
        s.step(now=now, sbase=len(s.ops))
    for kw in (dict(clk=1), dict(clk=2), dict(clk=3), dict(now=NAN), dict(now=INF), dict(now=-INF)):
        s.step(now=6.0, sbase=len(s.ops)); s.step(now=6.01, sbase=len(s.ops))
        s.step(sbase=len(s.ops), **dict(dict(now=6.02), **kw)); s.step(now=6.03, sbase=len(s.ops)); s.step(now=6.04, sbase=len(s.ops))
    for tick in (0, 84, 85, 86, 184, 185, 186, 285, 1000, 999, 1084, 1085, 2 ** 32, 2 ** 53, 2 ** 53 + 1, 2 ** 63, 2 ** 63 + 4096,
                 2 ** 64 - 1, 2 ** 64 - 1, 5000, 5100):
        s.step(now=tick / 1000.0, tick=tick, sbase=len(s.ops))
    s.t = 20.0
    for i in range(400):                                                  # a slow sweep over many grid cells
        s.step(dt=0.0137, sbase=1000 + i)
    for i in range(200):
        s.step(now=round(30 + i * 0.005, 3), sbase=2000 + i)              # times on the millisecond grid
    # The cache belongs to the owned handle and to healthy statistics.
    s.step(now=60.0, sbase=1); s.step(now=60.01, sbase=2, st=[ans(armed=0)]); s.step(now=60.02, sbase=3)
    s.step(now=60.03, sbase=4, st=[ans(inst=0)]); s.step(now=60.04, sbase=5); s.step(now=60.05, sbase=6, st=[ans(expired=1)])
    s.step(now=60.06, sbase=7); s.step(now=60.07, sbase=8, st=[ans(err=3)]); s.step(now=60.08, sbase=9)
    s.step(now=60.081, sbase=10, st=[ans(ret=0)], err=[3]); s.step(now=60.082, sbase=11); s.step(now=60.083, sbase=12, ssize=1)
    s.step(now=60.084, sbase=13); s.step(now=60.0845, sbase=14, f1=0.25, f2=0.75); s.step(now=60.0846, sbase=15, st=[ans(armed=3, inst=2)])
    s.step("placement", now=60.0847); s.step(now=60.0848, sbase=16); s.step("depth", now=60.0849); s.step(now=60.08495, sbase=17)
    s.step("prepare", now=60.08496, st=[ans()]); s.step(now=60.08497, sbase=18)
    s.step(now=60.2, sbase=19); s.step("begin", slot=1, now=60.21); s.step(now=60.22, sbase=20)   # a refused Begin drops the cache
    s.step(now=60.3, sbase=21); s.step("finish", now=60.31, endr=[0], err=[13], sbase=22); s.step(now=60.32, sbase=23)
    s.step("finish", now=60.33, sbase=24, st=[ans(), ans(**IDLE)]); s.step(slot=0, now=60.34); s.step(slot=NONE, now=60.35, st=[ans(**IDLE)])
    return s.ops


def walk(seed, count):
    rng = random.Random(seed)
    ops, t, generation = [], 1000.0, 40
    weights = dict(stats=34, depth=2, input=2, prepare=7, begin=8, placement=6, ads=4, motion=9, zoom=3, transition=9, owned=1, finish=6)
    for _ in range(count):
        t += rng.choice((0.004, 0.0167, 0.0167, 0.033, 0.11, 0.26)) if rng.random() < 0.98 else -rng.random() * 2
        call = rng.choices(list(weights), weights=list(weights.values()))[0]
        good = rng.random() < 0.88

        def r(ok=1, bad=(0, 0, 2, 3)):
            return ok if good or rng.random() < 0.6 else rng.choice(bad)

        def a():
            healthy = good or rng.random() < 0.6
            return ans(ret=r(), fill=rng.random() < 0.97, inst=rng.choice((1, 1, 1, 1, 0)), armed=1 if healthy else 0,
                       expired=0 if healthy or rng.random() < 0.7 else 1, err=0 if healthy or rng.random() < 0.5 else rng.choice((3, 7, 8, 17, 18)))

        def number(limit):
            roll = rng.random()
            if roll < 0.9: return rng.uniform(-limit, limit)
            if roll < 0.95: return rng.choice((limit, -limit, limit * 1.0000001, -limit * 1.01, 0.0))
            return rng.choice((NAN, INF, -INF, 1.0))

        limit = 0.15 if call == "motion" else 0.5
        v = [number(limit) for _ in range(3)]
        roll = rng.random()
        if roll < 0.6:
            q = [rng.gauss(0, 0.3) for _ in range(3)] + [rng.choice((1, 1, 1, -1)) * rng.uniform(0.5, 1.5)]
            norm = sum(x * x for x in q) ** 0.5
            q = [x / norm for x in q]
            if rng.random() < 0.15: q[rng.randrange(4)] *= rng.choice((1.001, 0.999, 1.003, 0.99))
        elif roll < 0.9: q = list(Q)
        else: q = [rng.choice((0.0, 1.0, -1.0, 0.5, NAN, 1.002)) for _ in range(4)]
        nilmask = 0
        if rng.random() < 0.25: nilmask = 0x78
        if rng.random() < 0.05: nilmask = rng.randrange(128)
        slot = rng.randrange(4) if call == "begin" else OWNED
        if call != "begin" and rng.random() < 0.2: slot = rng.choice((NONE, FOREIGN, STALE, 0, 1, 2, 3))
        if call == "begin" and good: generation += 1
        first = a()
        if call == "prepare" and rng.random() < 0.8: first = ans(**IDLE)
        size = 96 if call == "input" else 104
        ops.append(op(call, slot=slot, prep=rng.choices((1, 0, 2), weights=(94, 3, 3))[0], cfg_ok=rng.random() < 0.97,
                      cfg_missing=0 if rng.random() < 0.95 else 1 << rng.randrange(17), cfg_false=rng.random() < 0.3,
                      steady=rng.random() < 0.5, v=v + q, nilmask=nilmask, authored=rng.random() < 0.3, active=rng.choices((0, 1, 2), weights=(48, 48, 4))[0],
                      pk=int(rng.random() < 0.97), pk_epoch_num=rng.random() < 0.97,
                      pk_epoch=rng.choice((1, 2, 3, 77, 2 ** 40)) if rng.random() < 0.93 else rng.choice((0, 0.5, -3, NAN, 2 ** 53)),
                      pk_progress_num=rng.random() < 0.97, pk_progress=rng.random() if rng.random() < 0.93 else rng.choice((-0.1, 1.1, NAN, 0.0, 1.0)),
                      pk_ads=rng.choices((0, 1, 2), weights=(48, 48, 4))[0], clk=0 if rng.random() < 0.96 else rng.randint(1, 3),
                      now=t if rng.random() < 0.99 else rng.choice((NAN, INF)), tick=int(t * 1000),
                      validate=r(), begin=r(1, (0, 0, 2, 2, 5)), begin_gen=generation if good or rng.random() < 0.7 else 0, setr=r(),
                      endr=[r(), r()], st=[first, a(), a()], err=[rng.choice((0, 2, 3, 3, 7, 8, 13, 17, 18, 19)) for _ in range(3)],
                      sbase=rng.randint(0, 10 ** 6), ssize=80 if rng.random() < 0.985 else 88, sabi=2 if rng.random() < 0.985 else 1,
                      f1=rng.random(), f2=rng.random(),
                      dg=diag(ret=r(), fill=rng.random() < 0.97, size=size if rng.random() < 0.97 else 80, abi=1 if rng.random() < 0.97 else 2,
                              reserved=0 if rng.random() < 0.97 else 1, i1=rng.randint(0, 1), i2=rng.randint(0, 1), i3=rng.randint(0, 2))))
    return ops


def calm(seed, count):
    """A helper that answers as the real one does (the runner's and the C test's small model of
    it). On top of it what the backend does with the adapter: prepare, Begin, a statistics poll
    every update with placement, motion and phase publication in between, End; now and then the
    helper disarming itself, a refused End or setter, a lost read, a bad clock sample."""
    rng = random.Random(seed)
    ops, t, phase, slot, left = [], 2000.0, "idle", 0, 0
    for _ in range(count):
        t += rng.choice((0.008, 0.0167, 0.0167, 0.0333))
        kw = dict(now=t, tick=int(t * 1000))
        if phase == "idle":
            call, phase = "prepare", "prepared"
        elif phase == "prepared":
            slot = (slot + 1) % 4
            call, phase, left = "begin", "armed", rng.randint(5, 400)
            kw.update(slot=slot, steady=rng.random() < 0.5)
        else:
            left -= 1
            if left <= 0:
                call, phase = "finish", "idle"
            else:
                call = rng.choices(["stats", "motion", "transition", "placement", "ads", "zoom", "depth", "input", "owned"],
                                   weights=(70, 12, 10, 2, 1, 1, 1, 1, 0.5))[0]
                kw.update(v=[rng.uniform(-0.1, 0.1) for _ in range(3)] + Q, nilmask=0x78 if rng.random() < 0.5 else 0,
                          authored=rng.random() < 0.3, active=rng.randint(0, 1), pk_epoch=rng.randint(1, 9), pk_progress=rng.random(),
                          pk_ads=rng.randint(0, 1))
        o = model(call, **kw)
        glitch = rng.random()
        if glitch < 0.004: o["sim_disarm"] = rng.choice((7, 7, 8, 3, 18, 0)); left = min(left, rng.randint(1, 30))
        elif glitch < 0.008 and call == "begin": o["begin"], o["begin_gen"], o["err"] = 0, 0, [rng.choice((2, 3, 5, 7))]; phase = "idle"
        elif glitch < 0.012 and call == "finish": o["endr"], o["err"] = [0], [rng.choice((13, 12, 3))]; phase, left = "armed", 2
        elif glitch < 0.016: o["st"] = [ans(ret=0), ans(ret=-1)]; o["err"] = [3]
        elif glitch < 0.019: o["setr"], o["err"] = 0, [17]
        elif glitch < 0.022: o["clk"] = rng.randint(1, 3); o["now"] = NAN
        elif glitch < 0.024 and call == "prepare": o["validate"], o["err"] = 0, [rng.choice((3, 4, 5, 14, 15))]
        ops.append(o)
    return ops


def c_vector(run, o, line):
    st = ",".join("{%d,%d,%d,%d,%d,%d}" % (a["ret"], a["fill"], a["inst"], a["armed"], a["expired"], a["err"]) for a in pad(o["st"], 3))
    d = o["dg"]
    fields = [run, CALLS.index(o["call"]), o["slot"], o["prep"], int(o["cfg_ok"]), o["cfg_missing"], int(o["steady"]),
              "{" + ",".join("%dull" % v for v in o["cfg"][:14]) + "}", "{" + ",".join(cnum(v) for v in o["cfg"][14:]) + "}",
              "{" + ",".join(cnum(v) for v in o["v"]) + "}", o["nilmask"], int(o["authored"]), o["active"],
              o["pk"], int(o["pk_epoch_num"]), cnum(o["pk_epoch"]), int(o["pk_progress_num"]), cnum(o["pk_progress"]), o["pk_ads"],
              o["clk"], cnum(o["now"]), "%dull" % o["tick"], "%dll" % o["validate"], "%dll" % o["begin"], "%dull" % o["begin_gen"],
              "%dll" % o["setr"], "{" + ",".join("%dll" % v for v in pad(o["endr"], 2)) + "}", "{" + st + "}",
              "{" + ",".join("%dll" % v for v in pad(o["err"], 3)) + "}", "%dull" % o["sbase"], o["ssize"], o["sabi"],
              cnum(o["f1"]), cnum(o["f2"]),
              "{%d,%d,%du,%du,%du,%d,%d,%d}" % (d["ret"], d["fill"], d["size"], d["abi"], d["reserved"], d["i1"], d["i2"], d["i3"]),
              o["sim_disarm"], c_string(line)]
    return "  {" + ",".join(str(f) for f in fields) + "},"


HEADER = """typedef struct PoseAnswer { long long ret; int fill, inst, armed, expired; long long err; } PoseAnswer;
typedef struct PoseDiagnostic { long long ret; int fill; unsigned size, abi, reserved; int i1, i2, i3; } PoseDiagnostic;
typedef struct PoseAdapterVector {
  int run, call, slot, prep, cfg_ok; unsigned cfg_missing; int steady; unsigned long long cfg[14]; double offset[3];
  double v[7]; unsigned nilmask; int authored, active;
  int pk, pk_epoch_num; double pk_epoch; int pk_progress_num; double pk_progress; int pk_ads;
  int clk; double now; unsigned long long tick;
  long long validate, begin; unsigned long long begin_gen; long long setr, endr[2]; PoseAnswer st[3]; long long err[3];
  unsigned long long sbase; unsigned ssize, sabi; double f1, f2; PoseDiagnostic dg; int sim_disarm;
  const char *expect;
} PoseAdapterVector;
enum { PV_STATS, PV_DEPTH, PV_INPUT, PV_PREPARE, PV_BEGIN, PV_PLACEMENT, PV_ADS, PV_MOTION, PV_ZOOM, PV_TRANSITION, PV_OWNED, PV_FINISH };
typedef struct PoseAdapterRun { const char *name; int clock, normalize, tick; } PoseAdapterRun;"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runner", type=Path, default=HERE / "run_pose_adapter.lua", help="for the coverage review: a wrapper of the runner")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    source = args.retail / "src/first_person/pose.lua"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    profile_digest = hashlib.sha256((args.retail / "src/first_person/profile.lua").read_bytes()).hexdigest()
    # clock: Pose.new got a clock function (False: the shipped path, GetTickCount64). normalize: profile.pose.normalize_zoom_input.
    # tick=False: kernel32 cannot be loaded, so the shipped path has no time at all.
    runs = [dict(name="session", ops=session()), dict(name="session_clock", ops=session(), clock=True),
            dict(name="session_plain", ops=session(), normalize=False, scope=True),
            dict(name="refusals", ops=refusals()), dict(name="refusals_clock_plain", ops=refusals(), clock=True, normalize=False),
            dict(name="inputs", ops=inputs()), dict(name="inputs_plain", ops=inputs(), normalize=False),
            dict(name="handles", ops=handles()), dict(name="handles_clock", ops=handles(), clock=True, scope=True),
            dict(name="clocks", ops=clocks(), clock=True), dict(name="clocks_tick", ops=clocks()),
            dict(name="session_no_tick", ops=session(), tick=False), dict(name="clocks_no_tick", ops=clocks(), tick=False)]
    runs += [dict(name="walk1", ops=walk(1, 3000)), dict(name="walk2_clock", ops=walk(2, 3000), clock=True),
             dict(name="walk3_clock_plain", ops=walk(3, 2000), clock=True, normalize=False)]
    runs += [dict(name="calm1", ops=calm(1, 3000)), dict(name="calm2_clock", ops=calm(2, 3000), clock=True),
             dict(name="calm3_plain", ops=calm(3, 2000), normalize=False)]
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
        for r in runs:
            script.write("{run=%s,clock=%s,normalize=%s,scope=%s,tick=%s}\n" % (lua(r["name"]), lua(bool(r.get("clock"))),
                         lua(bool(r.get("normalize", True))), lua(bool(r.get("scope"))), lua(bool(r.get("tick", True)))))
            script.write("".join(lua(o) + "\n" for o in r["ops"]))
    done = subprocess.run(["luajit", str(args.runner), str(args.retail), script.name], capture_output=True, text=True)
    Path(script.name).unlink()
    if done.returncode:
        sys.exit(f"run_pose_adapter.lua failed:\n{done.stdout[-2000:]}\n{done.stderr}")
    lines = done.stdout.split("\n")[:-1]
    total = sum(len(r["ops"]) for r in runs)
    if len(lines) != total:
        sys.exit(f"{len(lines)} results for {total} operations")
    out = [f"/* Generated by tools/retail_ref/gen_pose_adapter.py. Do not edit.\n * Retail file: src/first_person/pose.lua at commit {commit}, "
           f"sha256 {digest}\n * (with src/first_person/profile.lua, sha256 {profile_digest}).\n"
           " * Each run starts a fresh adapter; every operation carries its arguments and what the scripted helper and\n"
           " * clock answer during it, and `expect`: what the retail file returned, its state afterwards and the ordered\n"
           " * trace of the calls it made. */",
           f'#define POSE_ADAPTER_VECTOR_COMMIT "{commit}"', f'#define POSE_ADAPTER_VECTOR_SHA256 "{digest}"', HEADER,
           "static const PoseAdapterRun pose_adapter_runs[] = {"]
    out += ["  {%s,%d,%d,%d}," % (c_string(r["name"]), bool(r.get("clock")), bool(r.get("normalize", True)), bool(r.get("tick", True)))
            for r in runs]
    out += ["};", "static const PoseAdapterVector pose_adapter_vectors[] = {"]
    at = 0
    for index, r in enumerate(runs):
        for o in r["ops"]:
            out.append(c_vector(index, o, lines[at]))
            at += 1
        print(f"{r['name']}: {len(r['ops'])} operations")
    out.append("};")
    (args.out / "pose_adapter_vectors.h").write_text("\n".join(out) + "\n")
    print(f"pose adapter: {total} operations in {len(runs)} runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
