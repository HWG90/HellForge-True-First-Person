#!/usr/bin/env python3
"""Cut reference vectors from the retail fov.lua (adapter and lease of the field-of-view
helper), run unmodified under LuaJIT against a scripted helper, backend, controller and clock.

  gen_fov_adapter.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import argparse
import copy
import hashlib
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
FROZEN = "1d0d453"
REASONS = ["FOV requires the current owned pose-camera request", "FOV preflight requires a safe normal camera with aiming released",
           "FOV executable identity unavailable", "Memory unavailable", "", "0", "backend.lua:268: bad argument #1 to 'format'",
           "Presentation owner changed"]
CALLS = ["update", "stop", "shutdown", "preflight", "a_validate", "a_begin", "a_bind", "a_transition", "a_stats", "a_finish"]
CFG = [0x7FF612340000, 0x140000000, 0x1F0A1000, 0x1F0B2000, 0x7FF612990000, 5, 0, 11, 12, 7]
NAN, INF = float("nan"), float("inf")
LEASE, NONE, FOREIGN, STALE = -1, -2, -3, -4          # handle arguments of the direct adapter calls; 0.. are the script's own slots


def ans(ret=1, fill=True, inst=1, armed=1, err=0):
    return dict(ret=ret, fill=fill, inst=inst, armed=armed, err=err)


def op(call="update", **kw):
    w = dict(call=call, slot=NONE, en=True, mode=True, running=True, lease=1, lo=True, lp=1, low=7, lw=-1, lr=5,
             cfg_ok=True, cfg_throw=False, cfg_why=0, cfg_missing=0, cfg=list(CFG),
             pk_fn=True, pk=0, pk_epoch_num=True, pk_epoch=1, pk_progress_num=True, pk_progress=0.5, pk_ads=0, pk_why=0,
             clk=0, now=0.0, validate=1, begin=1, begin_gen=41, bind=1, trans=1, endr=[1], st=[ans()], err=[0],
             sbase=10, ssize=88, sabi=1, f1=1.0, f2=0.9, f3=70.0, sim_disarm=-1)
    w.update(kw)
    if not w["cfg_why"]: w["cfg_throw"] = False          # a raised error always carries its text
    return w


def model(call="update", **kw):
    """An operation the model of the real helper answers."""
    return op(call, validate=-1, begin=-1, bind=-1, trans=-1, endr=[-1], st=[ans(ret=-1)], err=[-1], **kw)


class Script:
    def __init__(self, start=10.0, maker=op):
        self.ops, self.t, self.maker = [], start, maker

    def step(self, call="update", dt=0.016, **kw):
        self.t += dt
        kw.setdefault("now", self.t)
        self.ops.append(self.maker(call, **kw))


def packet(epoch=1, progress=0.5, ads=0):
    return dict(pk=1, pk_epoch=epoch, pk_progress=progress, pk_ads=ads)


def session():
    """What a session produces: arm on a lease, steady updates, the lease changing or ending, stop."""
    s = Script()
    s.step("preflight"); s.step("stop")
    s.step(**packet(1, 0.0))                                              # Begin, statistics, first packet
    for i in range(1, 30):
        s.step(dt=0.0167, sbase=10 + i, **packet(1, min(1.0, i / 11)))    # counters refresh on the 10 Hz grid only
    s.step(now=s.t + 0.05, sbase=99); s.step(now=s.t, sbase=100)          # the clock steps back: the cache is dropped
    s.step(lr=6, cfg=CFG[:5] + [6] + CFG[6:], **packet(2, 0.1, 1))        # request changes: publish, then Bind
    s.step(low=8, cfg=CFG[:5] + [6] + CFG[6:9] + [8])                     # weapon changes: Bind
    s.step(lease=2, lr=6, low=8, cfg=CFG[:5] + [6] + CFG[6:9] + [8])      # a new lease, same request and weapon: no Bind
    s.step(lease=2, lr=6, low=8, lo=False, lw=8)                          # no origin: weapon from the lease, the player unknown
    s.step(lease=2, lr=6, low=8)
    for i in range(4):
        s.step(running=False, **packet(3, i / 4))                         # weapon gap: the lens survives
    s.step(lease=0); s.step(lease=0, pk=0)
    s.step(lease=3, lr=9, cfg=CFG[:5] + [9] + CFG[6:])                    # back on another request: Bind
    s.step(lease=3, lr=9, lp=2, cfg=CFG[:5] + [9] + CFG[6:])              # another player: End, then Begin
    s.step(lease=3, lr=9, lp=2)
    s.step(lease=3, lr=9, lp=0)                                           # the player turns unknown: End, Begin
    s.step(lease=3, lr=9, lp=0)
    s.step(mode=False); s.step()
    s.step(en=False); s.step()
    s.step("stop"); s.step("stop"); s.step()
    s.step("preflight")                                                   # while armed: refused
    s.step("shutdown"); s.step(); s.step("preflight"); s.step("shutdown")
    return s.ops


def refusals():
    """The helper refusing at each call with each error code; cleanup owed and retried."""
    s = Script(50.0)
    for code in list(range(0, 20)) + [4294967295]:
        s.step("preflight", validate=0, err=[code])
        s.step(begin=0, begin_gen=0, err=[code])                          # refused, nothing owned
        s.step(begin=0, begin_gen=77, err=[code, 0])                      # refused but a generation came back: cleaned up
        s.step(begin=2, begin_gen=78, err=[code], endr=[0])               # partial installation, End refuses
        s.step(endr=[0], err=[code]); s.step("stop", endr=[0], err=[code])
        s.step(st=[ans(ret=0)], err=[code, code + 1])                     # cleanup works although statistics do not
        s.step(); s.step(lr=6, cfg=CFG[:5] + [6] + CFG[6:], bind=0, err=[code])
        s.step(); s.step(trans=0, err=[code], **packet())
        s.step(); s.step(st=[ans(ret=0), ans()], err=[code])              # statistics refuse in the steady state
        s.step(); s.step(running=False, st=[ans(ret=0), ans()], err=[code])
        s.step(st=[ans(armed=0, err=code), ans()])                        # disarmed, steady
        s.step(); s.step(running=False, st=[ans(armed=0, err=code), ans()])   # disarmed while waiting
        s.step(st=[ans(), ans(armed=0, err=code)])                        # arms, then does not stay armed (stale first answer)
        s.step(); s.step(st=[ans(err=code)])                              # armed but reporting an error
        s.step(); s.step(lr=6, cfg=CFG[:5] + [6] + CFG[6:], st=[ans(armed=0, err=code)])   # retired before binding
        s.step(); s.step(lr=6, cfg=CFG[:5] + [6] + CFG[6:], st=[ans(), ans(armed=0, err=code)])   # bound, then disarmed
        s.step(); s.step(lp=2, endr=[0], err=[code])                      # another player, End refuses
        s.step(lp=2, endr=[1])
        s.step(); s.step("stop", endr=[0], err=[code]); s.step("stop", endr=[0], err=[code + 1]); s.step("preflight")
        s.step("stop"); s.step("stop")
    # Begin answers that own nothing, or own although refused.
    for result, generation in ((1, 0), (2, 0), (3, 0), (0, 0), (1, 5), (7, 9), (4294967295, 1), (1, 2 ** 64 - 1)):
        s.step(begin=result, begin_gen=generation, err=[6]); s.step("stop")
    # Statistics that do not look like ours, or that the helper did not write.
    s.step()
    s.step(ssize=80); s.step(); s.step(sabi=2); s.step(); s.step(st=[ans(fill=False)], sbase=555)
    s.step(st=[ans(ret=2)]); s.step(); s.step(st=[ans(ret=1, armed=2, inst=5)], sbase=2 ** 64 - 1, f1=NAN, f2=INF, f3=-INF)
    s.step(sbase=2 ** 53 + 1, f1=-0.0, f2=1e30, f3=1e-30); s.step(sbase=123456789012345678, f1=0.05, f2=1 / 3, f3=179.99999)
    s.step("stop", st=[ans(ret=0), ans(ret=0)]); s.step()
    s.step("stop", st=[ans(), ans(ret=0)]); s.step(sbase=10)
    s.step("stop", dt=0.001, sbase=77, st=[ans(), ans(ret=0)]); s.step()     # the last counters are read fresh, inside a tick too
    s.step("stop", st=[ans(ret=0), ans(armed=0, inst=0)]); s.step()
    s.step("stop", ssize=1); s.step()
    s.step("shutdown", endr=[0], err=[13]); s.step(endr=[0], err=[12]); s.step("stop", endr=[0], err=[3]); s.step(); s.step()
    return s.ops


def inputs():
    """Configuration, packets and clocks that fail or carry odd values."""
    s = Script(200.0)
    for why in range(0, len(REASONS) + 1):
        s.step("preflight", cfg_ok=False, cfg_why=why); s.step(cfg_ok=False, cfg_why=why)
        if why:
            s.step("preflight", cfg_ok=False, cfg_throw=True, cfg_why=why); s.step(cfg_ok=False, cfg_throw=True, cfg_why=why)
    for bit in range(10):
        s.step("preflight", cfg_missing=1 << bit); s.step(cfg_missing=1 << bit)
    s.step("preflight", cfg_missing=0x3FF); s.step(cfg_missing=0x3FF); s.step(cfg_missing=0x220)
    s.step()                                                              # armed
    s.step(cfg_ok=False, cfg_why=1, lr=6)                                 # configuration refused with a lens held: kept
    s.step(lr=6, cfg=CFG[:5] + [6] + CFG[6:], cfg_missing=1 << 5)         # Bind without a request: the call raises
    s.step(); s.step(low=8, cfg_missing=1 << 9); s.step(); s.step(lr=6, cfg_missing=0x220)
    s.step(); s.step(lr=6, cfg_missing=1); s.step(lr=6, cfg=CFG[:5] + [6] + CFG[6:], cfg_missing=1)
    s.step("stop")
    # Packets.
    odd = [dict(pk_fn=False), dict(pk=0), dict(pk=2), dict(pk=2, pk_why=8), dict(pk=3), packet(0), packet(0.5), packet(1.5),
           packet(2.0 ** 53), packet(2.0 ** 53 - 1), packet(-1), packet(NAN), packet(INF), dict(pk=1, pk_epoch_num=False),
           packet(1, -0.001), packet(1, 1.0000001), packet(1, NAN), packet(1, INF), dict(pk=1, pk_progress_num=False),
           packet(1, 0.5, 2), packet(1, 0.0, 1), packet(1, 1.0, 1), packet(4, 0.1 + 0.2), packet(4, 1e-50), packet(1, -0.0)]
    for p in odd:
        s.step(**p); s.step(**p); s.step(running=False, **p); s.step(lr=6, cfg=CFG[:5] + [6] + CFG[6:], **p); s.step("stop")
    # Clocks.
    s.step()
    for kw in (dict(clk=1), dict(clk=2), dict(now=NAN), dict(now=INF), dict(now=-INF), dict(now=0.0), dict(now=-5.0),
               dict(now=0.3), dict(now=0.30000000000000004), dict(now=0.39999999999999997), dict(now=0.4), dict(now=1e9),
               dict(now=1e9), dict(now=1e9 + 0.05), dict(now=1e17), dict(now=1e17), dict(now=1e300), dict(now=4.25), dict(now=4.3),
               dict(now=4.35), dict(now=8.05), dict(now=8.1), dict(now=8.19), dict(now=8.2)):   # at 4.3 and 8.1 the next tick is not later: one more
        s.step(sbase=len(s.ops), **kw); s.step(sbase=len(s.ops), **kw)
    s.step("stop", clk=1)
    return s.ops


def orders():
    """Calls in orders a session does not produce, on the lease and straight on the adapter."""
    s = Script(400.0)
    s.step("stop"); s.step("shutdown"); s.step(); s.step("preflight")
    s2 = Script(420.0)
    s2.step(); s2.step("a_finish", slot=LEASE)                            # the lens ended behind the lease's back
    s2.step(); s2.step(); s2.step("stop"); s2.step()
    s2.step("a_finish", slot=LEASE); s2.step(running=False); s2.step()
    s2.step("a_finish", slot=LEASE); s2.step(lr=6, cfg=CFG[:5] + [6] + CFG[6:]); s2.step()
    s2.step("a_begin", slot=0, begin_gen=90)                              # refused: the lease's generation is owned
    s2.step("a_bind", slot=LEASE, cfg=CFG[:5] + [44] + CFG[6:]); s2.step("a_transition", slot=LEASE, **packet(9, 0.25, 1))
    s2.step("a_validate"); s2.step("stop")
    s2.step("a_begin", slot=0, begin_gen=91)                              # owned by the script; the lease adopts it
    s2.step(begin_gen=92); s2.step(); s2.step("a_finish", slot=0); s2.step("a_finish", slot=0); s2.step(); s2.step("stop")
    s2.step("a_begin", slot=1, begin_gen=93); s2.step("preflight"); s2.step("a_finish", slot=1, endr=[0], err=[13])
    s2.step("stop"); s2.step(); s2.step("stop"); s2.step("a_finish", slot=1)
    return s.ops, s2.ops


def adapter_only(clocked=True):
    """The adapter on its own (no lease update ever runs in this script)."""
    s = Script(600.0)
    for slot in (NONE, FOREIGN, STALE, 0):
        for call in ("a_stats", "a_finish", "a_bind", "a_transition"):
            s.step(call, slot=slot, **packet())
    s.step("a_validate", cfg_ok=False); s.step("a_begin", slot=0, cfg_ok=False)
    s.step("a_validate", cfg_missing=4); s.step("a_begin", slot=0, cfg_missing=0x200)
    s.step("a_validate", validate=0, err=[5]); s.step("a_validate", validate=2)
    s.step("a_stats", st=[ans(inst=0, armed=0)]); s.step("a_stats", st=[ans(ret=0)], err=[3])
    s.step("a_begin", slot=0, begin=0, begin_gen=0, err=[4]); s.step("a_stats", slot=0)
    s.step("a_begin", slot=0, begin_gen=7)
    for i in range(14):
        s.step("a_stats", slot=0, dt=0.03, sbase=20 + i)
    s.step("a_stats", sbase=50); s.step("a_stats", slot=0, sbase=51)      # generation 0 does not touch the shared counters
    s.step("a_stats", slot=0, sbase=52, st=[ans(err=3)]); s.step("a_stats", slot=0, sbase=53)
    s.step("a_stats", slot=0, sbase=54, clk=1); s.step("a_stats", slot=0, sbase=55); s.step("a_stats", slot=0, sbase=56, now=1.0)
    s.step("a_stats", slot=0, sbase=57, ssize=87); s.step("a_stats", slot=0, sbase=58, sabi=0)
    s.step("a_stats", slot=0, sbase=59); s.step("a_validate"); s.step("a_stats", slot=0, sbase=60)
    s.step("a_begin", slot=1, begin_gen=8)                                # refused; slot 1 is the owned handle too
    s.step("a_stats", slot=1, sbase=61); s.step("a_stats", slot=FOREIGN)
    for call in ("a_stats", "a_finish", "a_bind", "a_transition"):
        s.step(call, slot=STALE, **packet())                              # ours by name, but another generation is owned
    s.step("a_bind", slot=0); s.step("a_bind", slot=0, bind=0, err=[17]); s.step("a_bind", slot=0, cfg_missing=0x20)
    s.step("a_bind", slot=0, cfg_missing=0x200); s.step("a_stats", slot=0, sbase=62); s.step("a_stats", slot=0, sbase=63)
    s.step("a_transition", slot=0, **packet(3, 0.5, 1)); s.step("a_transition", slot=0, trans=0, err=[17], **packet(3, 0.5, 1))
    s.step("a_transition", slot=0, pk=0); s.step("a_transition", slot=0, pk=2); s.step("a_transition", slot=0, **packet(0))
    s.step("a_stats", slot=0, sbase=64)
    s.step("a_finish", slot=0, endr=[0], err=[13]); s.step("a_stats", slot=0, sbase=65)
    s.step("a_finish", slot=0, st=[ans(ret=0), ans(armed=0, inst=0)], err=[3], sbase=66)
    for call in ("a_stats", "a_finish", "a_bind", "a_transition", "a_finish"):
        s.step(call, slot=0, **packet())
    s.step("a_begin", slot=2, begin=2, begin_gen=9, err=[11]); s.step("a_stats", slot=2); s.step("a_stats", slot=0)
    s.step("a_finish", slot=0); s.step("a_bind", slot=0)
    s.step("a_finish", slot=2, st=[ans(), ans(ret=0)], err=[3], sbase=70); s.step("a_stats", slot=2); s.step("a_finish", slot=2)
    s.step("a_begin", slot=2, begin_gen=10); s.step("a_finish", slot=2, st=[ans(ret=0), ans(ret=0)], err=[3, 4])
    s.step("a_stats", slot=2); s.step("a_finish", slot=2); s.step("stop"); s.step("preflight"); s.step("shutdown")
    return s.ops


def walk(seed, count):
    rng = random.Random(seed)
    ops, t = [], 1000.0
    lease, player, request, weapon = 1, 1, 5, 7
    for _ in range(count):
        t += rng.choice((0.004, 0.0167, 0.0167, 0.033, 0.11, 0.6)) if rng.random() < 0.98 else -rng.random() * 2
        roll = rng.random()
        if roll < 0.03: lease = rng.randint(1, 3)
        if 0.03 < roll < 0.05: request = rng.randint(4, 6)
        if 0.05 < roll < 0.07: weapon = rng.randint(7, 8)
        if 0.07 < roll < 0.08: player = rng.randint(0, 2)
        call = rng.choices(["update", "stop", "shutdown", "preflight", "a_finish", "a_bind", "a_transition", "a_validate"],
                           weights=(88, 2.5, 0.05, 3, 1.2, 0.6, 0.6, 0.4))[0]
        cfg = CFG[:5] + [rng.choice((request, request, request, 4))] + CFG[6:9] + [rng.choice((weapon, weapon, weapon, 8))]
        good = rng.random() < 0.82                                        # most operations meet a helper that agrees
        def r(ok=1, bad=(0, 0, 2, 3)): return ok if good or rng.random() < 0.6 else rng.choice(bad)
        def a(): return ans(ret=r(), fill=rng.random() < 0.97, inst=rng.choice((1, 1, 1, 0)),
                            armed=1 if good or rng.random() < 0.6 else 0, err=0 if good or rng.random() < 0.5 else rng.choice((3, 7, 8, 17)))
        ops.append(op(call, slot=LEASE if call.startswith("a_") else NONE, now=rng.choice((t, t, t, t, NAN)) if rng.random() < 0.02 else t,
                      clk=rng.choices((0, 1, 2), weights=(96, 2, 2))[0], en=rng.random() < 0.97, mode=rng.random() < 0.98,
                      running=rng.random() < 0.88, lease=lease if rng.random() < 0.95 else 0, lo=rng.random() < 0.95, lp=player,
                      low=weapon if rng.random() < 0.95 else -1, lw=rng.choice((-1, weapon, 8)), lr=request if rng.random() < 0.97 else -1,
                      cfg_ok=rng.random() < 0.95 or call == "a_bind", cfg_throw=rng.random() < 0.3, cfg_why=rng.randint(0, len(REASONS)),
                      cfg_missing=0 if rng.random() < 0.96 else 1 << rng.randint(0, 9), cfg=cfg,
                      pk_fn=rng.random() < 0.97, pk=rng.choices((0, 1, 2, 3), weights=(30, 66, 2, 2))[0],
                      pk_epoch_num=rng.random() < 0.99, pk_epoch=rng.choice((1, 2, 3, 3, 3, 0, 2.5)) if rng.random() < 0.9 else rng.randint(1, 9),
                      pk_progress_num=rng.random() < 0.99, pk_progress=rng.random() if rng.random() < 0.95 else rng.choice((-0.1, 1.5, NAN, 0.0, 1.0)),
                      pk_ads=rng.choices((0, 1, 2), weights=(60, 38, 2))[0], pk_why=rng.choice((0, 8)),
                      validate=r(), begin=r(1, (0, 0, 2, 2, 5)), begin_gen=rng.choice((0, 41, 41, 41, 41, 41, 41, 42)) if not good else 41,
                      bind=r(), trans=r(), endr=[r(), r()], st=[a(), a(), a()],
                      err=[rng.choice((0, 2, 3, 3, 7, 8, 13, 17, 18)) for _ in range(3)], sbase=rng.randint(0, 10 ** 6),
                      ssize=88 if rng.random() < 0.985 else 80, sabi=1 if rng.random() < 0.985 else 2,
                      f1=rng.random() * 2, f2=rng.random() * 2, f3=rng.choice((55.0, 70.0, 90.5))))
    return ops


def calm(seed, count):
    """A helper that answers as the real one does (the runner's and the C test's small model of
    it): generations, ownership, error 17 for a foreign token. On top of it what a session does,
    and now and then what goes wrong: the helper disarming itself, a refused End, a lost read."""
    rng = random.Random(seed)
    ops, t = [], 2000.0
    lease, player, request, weapon = 1, 1, 5, 7
    gap_until, epoch, progress, ads = 0.0, 1, 0.0, 0
    for _ in range(count):
        dt = rng.choice((0.008, 0.0167, 0.0167, 0.0333))
        t += dt
        roll = rng.random()
        if roll < 0.004:                                   # weapon swap: gap, then another lease, request and weapon
            gap_until, lease, request, weapon = t + rng.choice((0.2, 0.7)), lease % 5 + 1, rng.randint(4, 6), rng.randint(7, 9)
        elif roll < 0.010:                                 # native ADS in or out: the game makes a new request on the same lease
            request, epoch, progress, ads = request % 6 + 4, epoch + 1, 0.0, 1 - ads
        elif roll < 0.012:                                 # respawn
            player, lease, gap_until = 3 - player, lease % 5 + 1, t + 0.4
        elif roll < 0.014:                                 # stim or dive: the pose lease pauses
            gap_until = t + rng.choice((0.05, 0.5))
        progress = min(1.0, progress + dt / 0.18)
        gap = t < gap_until
        call = rng.choices(["update", "stop", "preflight", "a_finish"], weights=(98.5, 0.5, 0.7, 0.3))[0]
        kw = dict(slot=LEASE if call == "a_finish" else NONE, now=t, en=rng.random() > 0.002, mode=True, running=not gap,
                  lease=0 if gap and rng.random() < 0.5 else lease, lp=player, low=weapon, lr=request,
                  cfg_ok=rng.random() > 0.01, cfg_why=1, cfg=CFG[:5] + [request] + CFG[6:9] + [weapon],
                  pk=1 if rng.random() < 0.9 else 0, pk_epoch=epoch, pk_progress=progress, pk_ads=ads)
        o = model(call, **kw)
        glitch = rng.random()
        if glitch < 0.004: o["sim_disarm"] = rng.choice((7, 7, 8, 3, 0, 14))
        elif glitch < 0.008: o["begin"], o["begin_gen"], o["err"] = 0, 0, [rng.choice((2, 3, 5, 7, 14))]
        elif glitch < 0.012: o["endr"], o["err"] = [0], [rng.choice((13, 12, 3))]
        elif glitch < 0.015: o["st"] = [ans(ret=0), ans(ret=-1)]; o["err"] = [3]
        elif glitch < 0.018: o["trans"], o["err"] = 0, [17]
        elif glitch < 0.020: o["clk"] = rng.choice((1, 2))
        ops.append(o)
    return ops


def cnum(value):
    value = float(value)
    if value != value: return "NAN"
    if value in (INF, -INF): return "INFINITY" if value > 0 else "-INFINITY"
    return repr(value)


def lua(value):
    if isinstance(value, bool): return "true" if value else "false"
    if isinstance(value, float) and value != value: return "(0/0)"
    if isinstance(value, float) and value in (INF, -INF): return "math.huge" if value > 0 else "-math.huge"
    if isinstance(value, int) and value > 2 ** 53: return f"{value}ULL"
    if isinstance(value, (int, float)): return repr(value)
    if isinstance(value, str): return '"%s"' % value.replace("\\", "\\\\").replace('"', '\\"')
    if isinstance(value, dict): return "{" + ",".join(f"{k}={lua(v)}" for k, v in value.items()) + "}"
    if isinstance(value, list): return "{" + ",".join(lua(v) for v in value) + "}"
    raise TypeError(value)


def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("??", "?\\?") + '"'


def pad(items, count):
    return (list(items) + [items[-1]] * count)[:count]


def c_vector(run, o, line):
    st = ",".join("{%d,%d,%d,%d,%d}" % (a["ret"], a["fill"], a["inst"], a["armed"], a["err"]) for a in pad(o["st"], 3))
    fields = [run, CALLS.index(o["call"]), o["slot"], int(o["en"]), int(o["mode"]), int(o["running"]), o["lease"], int(o["lo"]),
              o["lp"], o["low"], o["lw"], o["lr"], int(o["cfg_ok"]), int(o["cfg_throw"]), o["cfg_why"], o["cfg_missing"],
              "{" + ",".join("%dull" % v for v in o["cfg"]) + "}", int(o["pk_fn"]), o["pk"], int(o["pk_epoch_num"]),
              cnum(o["pk_epoch"]), int(o["pk_progress_num"]), cnum(o["pk_progress"]), o["pk_ads"], o["pk_why"], o["clk"],
              cnum(o["now"]), "%du" % o["validate"] if o["validate"] >= 0 else -1, "%dll" % o["begin"], "%dull" % o["begin_gen"],
              "%dll" % o["bind"], "%dll" % o["trans"], "{" + ",".join("%dll" % v for v in pad(o["endr"], 2)) + "}", "{" + st + "}",
              "{" + ",".join("%dll" % v for v in pad(o["err"], 3)) + "}", "%dull" % o["sbase"], o["ssize"], o["sabi"],
              cnum(o["f1"]), cnum(o["f2"]), cnum(o["f3"]), o["sim_disarm"], c_string(line)]
    return "  {" + ",".join(str(f) for f in fields) + "},"


HEADER = """typedef struct FovAnswer { long long ret; int fill, inst, armed; long long err; } FovAnswer;
typedef struct FovAdapterVector {
  int run, call, slot, en, mode, running, lease, lo, lp; long long low, lw, lr;
  int cfg_ok, cfg_throw, cfg_why; unsigned cfg_missing; unsigned long long cfg[10];
  int pk_fn, pk, pk_epoch_num; double pk_epoch; int pk_progress_num; double pk_progress; int pk_ads, pk_why, clk; double now;
  long long validate, begin; unsigned long long begin_gen; long long bind, trans, endr[2]; FovAnswer st[3]; long long err[3];
  unsigned long long sbase; unsigned ssize, sabi; double f1, f2, f3; int sim_disarm;
  const char *expect;
} FovAdapterVector;
enum { FV_UPDATE, FV_STOP, FV_SHUTDOWN, FV_PREFLIGHT, FV_A_VALIDATE, FV_A_BEGIN, FV_A_BIND, FV_A_TRANSITION, FV_A_STATS, FV_A_FINISH };
typedef struct FovAdapterRun { const char *name; int nolog, noclock; } FovAdapterRun;"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runner", type=Path, default=HERE / "run_fov_adapter.lua", help="for the coverage review: a wrapper of the runner")
    parser.add_argument("--only", default="", help="comma-separated run names (review only; writes no header)")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    source = args.retail / "src/first_person/fov.lua"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    profile_digest = hashlib.sha256((args.retail / "src/first_person/profile.lua").read_bytes()).hexdigest()
    lease_orders, adapter_orders = orders()
    runs = [dict(name="session", ops=session()), dict(name="session_plain", ops=session(), nolog=True, noclock=True, scope=True),
            dict(name="refusals", ops=refusals()), dict(name="inputs", ops=inputs()), dict(name="orders", ops=lease_orders),
            dict(name="orders_adapter", ops=adapter_orders), dict(name="adapter", ops=adapter_only()),
            dict(name="adapter_plain", ops=adapter_only(), noclock=True, nolog=True)]
    runs += [dict(name=f"walk{seed}", ops=walk(seed, 2500)) for seed in (1, 2)]
    runs += [dict(name="walk3_plain", ops=walk(3, 1000), noclock=True, nolog=True)]
    runs += [dict(name=f"calm{seed}", ops=calm(seed, 3000)) for seed in (1, 2, 3)]
    if args.only:
        runs = [r for r in runs if r["name"] in args.only.split(",")]
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
        script.write("{reasons=" + lua(REASONS) + "}\n")
        for r in runs:
            script.write("{run=%s,nolog=%s,noclock=%s,scope=%s}\n" % (lua(r["name"]), lua(bool(r.get("nolog"))),
                         lua(bool(r.get("noclock"))), lua(bool(r.get("scope")))))
            script.write("".join(lua(o) + "\n" for o in r["ops"]))
    done = subprocess.run(["luajit", str(args.runner), str(args.retail), script.name], capture_output=True, text=True)
    Path(script.name).unlink()
    if done.returncode:
        sys.exit(f"run_fov_adapter.lua failed:\n{done.stdout[-2000:]}\n{done.stderr}")
    lines = done.stdout.split("\n")[:-1]
    total = sum(len(r["ops"]) for r in runs)
    if len(lines) != total:
        sys.exit(f"{len(lines)} results for {total} operations")
    if args.only:
        print("\n".join(lines))
        return 0
    out = [f"/* Generated by tools/retail_ref/gen_fov_adapter.py. Do not edit.\n * Retail file: src/first_person/fov.lua at commit {commit}, "
           f"sha256 {digest}\n * (with src/first_person/profile.lua, sha256 {profile_digest}).\n"
           " * Each run starts a fresh lease; every operation carries what the scripted helper, backend, controller\n"
           " * and clock answer during it, and `expect`: what the retail file returned, its state afterwards and the\n"
           " * ordered trace of the calls it made. */",
           f'#define FOV_ADAPTER_VECTOR_COMMIT "{commit}"', f'#define FOV_ADAPTER_VECTOR_SHA256 "{digest}"',
           "static const char *const fov_adapter_reasons[] = {\"\", " + ", ".join(c_string(r) for r in REASONS) + "};", HEADER,
           "static const FovAdapterRun fov_adapter_runs[] = {"]
    out += ["  {%s,%d,%d}," % (c_string(r["name"]), bool(r.get("nolog")), bool(r.get("noclock"))) for r in runs]
    out += ["};", "static const FovAdapterVector fov_adapter_vectors[] = {"]
    at = 0
    for index, r in enumerate(runs):
        for o in r["ops"]:
            out.append(c_vector(index, o, lines[at]))
            at += 1
        print(f"{r['name']}: {len(r['ops'])} operations")
    out.append("};")
    (args.out / "fov_adapter_vectors.h").write_text("\n".join(out) + "\n")
    print(f"fov adapter: {total} operations in {len(runs)} runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
