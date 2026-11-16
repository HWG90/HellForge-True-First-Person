#!/usr/bin/env python3
"""Cut reference vectors from the retail blur.lua (adapter and lease of the scoped-blur
helper), run unmodified under LuaJIT against a scripted helper, backend, controller and clock.

  gen_blur_adapter.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
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
REASONS = ["FOV requires the current owned pose-camera request", "FOV preflight requires a safe normal camera with aiming released",
           "FOV executable identity unavailable", "Memory unavailable", "", "backend.lua:268: bad argument #1 to 'format'"]
CALLS = ["update", "stop", "shutdown", "preflight", "a_validate", "a_begin", "a_stats", "a_finish"]
CFG = [0x7FF612340000, 0x140000000, 0x1F0A1000, 0x1F0B2000, 0x7FF612990000, 5, 0, 11, 12, 7]
LEASE, NONE, FOREIGN, STALE = -1, -2, -3, -4          # handle arguments of the direct adapter calls; 0.. are the script's own slots


def ans(ret=1, fill=True, inst=1, armed=1, err=0):
    return dict(ret=ret, fill=fill, inst=inst, armed=armed, err=err)


def op(call="update", **kw):
    w = dict(call=call, slot=NONE, en=True, mode=True, running=True, lease=1, lo=True, lp=1, low=7, lw=-1, lr=5,
             cfg_ok=True, cfg_throw=False, cfg_why=0, cfg_missing=0, cfg=list(CFG), clk=0, now=0.0,
             validate=1, begin=1, begin_gen=41, endr=[1], st=[ans()], err=[0], nosrc=False,
             sbase=10, ssize=80, sabi=1, f1=1.0, f2=0.0, sim_disarm=-1)
    w.update(kw)
    if not w["cfg_why"]: w["cfg_throw"] = False          # a raised error always carries its text
    return w


def model(call="update", **kw):
    """An operation the model of the real helper answers."""
    return op(call, validate=-1, begin=-1, endr=[-1], st=[ans(ret=-1)], err=[-1], **kw)


class Script:
    def __init__(self, start=10.0, maker=op):
        self.ops, self.t, self.maker = [], start, maker

    def step(self, call="update", dt=0.016, **kw):
        self.t += dt
        kw.setdefault("now", self.t)
        self.ops.append(self.maker(call, **kw))


NO = dict(begin=0, begin_gen=0, err=[18])                # Begin: this weapon has no scoped blur source


def session():
    """What a session produces: arm on a lease, steady updates, a weapon without a blur source and
    its half-second recheck, the lease changing, pausing or ending, the helper retiring, stop."""
    s = Script()
    s.step("preflight"); s.step("preflight", validate=0, err=[18]); s.step("stop")
    s.step()
    for i in range(1, 30):
        s.step(dt=0.0167, sbase=10 + i)                                   # counters refresh on the staggered 10 Hz grid only
    s.step(now=s.t + 0.05, sbase=99); s.step(now=s.t, sbase=100)          # the clock steps back: the cache is dropped
    s.step(lr=6); s.step(lr=6)                                            # request changes: End, Begin
    s.step(lr=6, low=8); s.step(lr=6, low=8, lease=2); s.step(lr=6, low=8, lease=2, lp=2); s.step(lr=6, low=8, lease=2, lp=0)
    s.step(lr=6, low=8, lease=2, lo=False, lw=8); s.step(lr=6, low=8, lease=2, lo=False, lw=8)
    s.step(lr=-1, lease=2, lo=False); s.step(lr=-1, lease=2, lo=False)    # nothing known about the lease but itself
    s.step(running=False); s.step(running=False); s.step(lease=0); s.step()   # a gap ends the blur lease: waiting
    for i in range(40):                                                   # no source: rechecked every half second
        s.step(lease=3, low=9, **NO)
    s.step(lease=3, low=9, dt=0.6); s.step(lease=3, low=9)                # the optic appeared
    s.step(lease=4, low=9, **NO); s.step(lease=4, low=9, clk=1, **NO); s.step(lease=4, low=9, now=NAN, **NO)
    s.step(lease=4, low=9, lr=7, clk=2, **NO); s.step(lease=4, low=9, lr=7, now=INF, **NO); s.step(lease=4, low=9, lr=7, **NO)
    s.step(lease=4, low=9, lr=7, now=s.t - 1.0, **NO); s.step(lease=4, low=9, lr=7, running=False); s.step(lease=4, low=9, lr=7)
    s.step(st=[ans(armed=0, err=7)]); s.step(); s.step(); s.step(st=[ans(armed=0, err=8)], endr=[0], err=[13])   # retired
    s.step(endr=[0], err=[13]); s.step("preflight"); s.step(); s.step()
    s.step(mode=False); s.step(); s.step(en=False); s.step()
    s.step("stop"); s.step("stop"); s.step()
    s.step("preflight"); s.step("shutdown"); s.step(); s.step("preflight"); s.step("shutdown")
    return s.ops


def refusals():
    """The helper refusing at each call with each error code; cleanup owed and retried."""
    s = Script(50.0)
    for code in list(range(0, 20)) + [4294967295]:
        s.step("preflight", validate=0, err=[code])
        s.step(begin=0, begin_gen=0, err=[code]); s.step(dt=0.6)          # refused, nothing owned (18: unavailable; 7, 8: retired)
        s.step("stop")
        s.step(begin=0, begin_gen=77, err=[code, 0]); s.step("stop")      # refused but a generation came back: cleaned up
        s.step(begin=0, begin_gen=77, err=[code], endr=[0]); s.step(endr=[0], err=[code]); s.step(); s.step("stop")
        s.step(begin=2, begin_gen=78, err=[code], endr=[0])               # partial installation, End refuses
        s.step(endr=[0], err=[code]); s.step("stop", endr=[0], err=[code]); s.step("stop", endr=[0], err=[code + 1])
        s.step(st=[ans(ret=0)], err=[code, code + 1])                     # cleanup works although statistics do not
        s.step(); s.step(st=[ans(ret=0), ans()], err=[code])              # statistics refuse in the steady state
        s.step(); s.step(st=[ans(ret=0), ans()], err=[code, code], endr=[0])
        s.step(); s.step(); s.step(st=[ans(armed=0, err=code), ans()])    # disarmed, steady (7, 8: retired)
        s.step(); s.step(st=[ans(armed=0, err=code), ans()], endr=[0], err=[13]); s.step(endr=[0], err=[12]); s.step()
        s.step(st=[ans(armed=0, err=code)])                               # arms, does not stay armed
        s.step(st=[ans(armed=0, err=code)], endr=[0], err=[13]); s.step()
        s.step(); s.step(st=[ans(err=code)]); s.step(st=[ans(err=code)])  # armed but reporting an error
        s.step(); s.step("stop", endr=[0], err=[code]); s.step("preflight"); s.step("stop")
    for result, generation in ((1, 0), (2, 0), (3, 0), (0, 0), (1, 5), (7, 9), (4294967295, 1), (1, 2 ** 64 - 1)):
        s.step(begin=result, begin_gen=generation, err=[6]); s.step("stop")
    s.step()
    s.step(ssize=88); s.step(); s.step(sabi=2); s.step(); s.step(st=[ans(fill=False)], sbase=555)
    s.step(st=[ans(ret=2)]); s.step(); s.step(st=[ans(ret=1, armed=2, inst=5)], sbase=2 ** 64 - 1, f1=NAN, f2=INF)
    s.step(sbase=2 ** 53 + 1, f1=-INF, f2=-0.0); s.step(sbase=123456789012345678, f1=1e30, f2=1 / 3)
    s.step("stop", st=[ans(ret=0), ans(ret=0)]); s.step(sbase=10)
    s.step("stop", dt=0.001, sbase=77, st=[ans(), ans(ret=0)]); s.step()
    s.step("stop", st=[ans(ret=0), ans(armed=0, inst=0)]); s.step(); s.step("stop", ssize=1); s.step()
    s.step("shutdown", endr=[0], err=[13]); s.step(endr=[0], err=[12]); s.step("stop", endr=[0], err=[3]); s.step(); s.step()
    return s.ops


def inputs():
    """Configuration and clocks that fail or carry odd values."""
    s = Script(200.0)
    for why in range(0, len(REASONS) + 1):
        s.step("preflight", cfg_ok=False, cfg_why=why); s.step(cfg_ok=False, cfg_why=why)
        if why:
            s.step("preflight", cfg_ok=False, cfg_throw=True, cfg_why=why); s.step(cfg_ok=False, cfg_throw=True, cfg_why=why)
    for bit in range(10):
        s.step("preflight", cfg_missing=1 << bit); s.step(cfg_missing=1 << bit)
    s.step("preflight", cfg_missing=0x3FF); s.step(cfg_missing=0x3FF)
    s.step(); s.step(lr=6, cfg_ok=False, cfg_why=1); s.step(lr=6); s.step(lr=7, cfg_missing=4); s.step("stop")
    s.step()
    for kw in (dict(clk=1), dict(clk=2), dict(now=NAN), dict(now=INF), dict(now=-INF), dict(now=0.0), dict(now=-5.0),
               dict(now=0.3667), dict(now=0.36670000000000003), dict(now=0.4666), dict(now=0.4667), dict(now=1e9),
               dict(now=1e9), dict(now=1e9 + 0.05), dict(now=1e17), dict(now=1e17), dict(now=1e300), dict(now=4.3), dict(now=4.3667),
               dict(now=4.4), dict(now=8.1), dict(now=8.1667), dict(now=8.2), dict(now=8.2667)):   # at 4.3667 and 8.1667 the next tick is not later
        s.step(sbase=len(s.ops), **kw); s.step(sbase=len(s.ops), **kw)
    s.step("stop", clk=1)
    # The recheck window of a weapon without a source: exactly at its ends, and a clock that fails.
    s.step(now=20.0, **NO)
    for now in (20.0, 20.25, 20.5 - 1e-9, 19.999999):
        s.step(now=now, **NO)
    s.step(now=20.0, **NO)
    for kw in (dict(now=20.49), dict(now=20.5), dict(now=20.5), dict(now=21.0, clk=1), dict(now=21.0), dict(now=NAN), dict(now=21.2),
               dict(now=21.5, begin=0, begin_gen=0, err=[7]), dict(now=21.6), dict(now=21.7, begin=0, begin_gen=9, err=[18]), dict(now=21.8)):
        s.step(**dict(NO, **kw))
    return s.ops


def orders():
    s = Script(400.0)
    s.step("stop"); s.step("shutdown"); s.step(); s.step("preflight")
    s2 = Script(420.0)
    s2.step(); s2.step("a_finish", slot=LEASE)                            # ended behind the lease's back
    s2.step(); s2.step(); s2.step("stop"); s2.step()
    s2.step("a_finish", slot=LEASE); s2.step(running=False); s2.step()
    s2.step("a_finish", slot=LEASE); s2.step(lr=6); s2.step()
    s2.step("a_begin", slot=0, begin_gen=90); s2.step("a_validate"); s2.step("preflight"); s2.step("stop")
    s2.step("a_begin", slot=0, begin_gen=91)                              # owned by the script: the lease adopts it and ends it
    s2.step(begin_gen=92); s2.step(); s2.step("a_finish", slot=0); s2.step(); s2.step("stop")
    s2.step("a_begin", slot=1, begin_gen=93); s2.step("preflight"); s2.step("a_finish", slot=1, endr=[0], err=[13])
    s2.step(begin=0, begin_gen=0, err=[18]); s2.step(dt=0.6, endr=[0], err=[8]); s2.step("stop"); s2.step(); s2.step("stop")
    s2.step("a_finish", slot=1)
    return s.ops, s2.ops


def adapter_only():
    """The adapter on its own (no lease update ever runs in this script)."""
    s = Script(600.0)
    for slot in (NONE, FOREIGN, STALE, 0):
        for call in ("a_stats", "a_finish"):
            s.step(call, slot=slot)
    s.step("a_validate", cfg_ok=False); s.step("a_begin", slot=0, cfg_ok=False)
    s.step("a_validate", cfg_missing=4); s.step("a_begin", slot=0, cfg_missing=0x200)
    s.step("a_validate", validate=0, err=[5]); s.step("a_validate", validate=2, err=[0]); s.step("a_validate", validate=0, err=[18])
    s.step("a_stats", st=[ans(inst=0, armed=0)]); s.step("a_stats", st=[ans(ret=0)], err=[3])
    s.step("a_begin", slot=0, begin=0, begin_gen=0, err=[4]); s.step("a_begin", slot=0, begin=0, begin_gen=0, err=[18])
    s.step("a_begin", slot=0, begin=1, begin_gen=0); s.step("a_stats", slot=0)
    s.step("a_begin", slot=0, begin_gen=7)
    for i in range(14):
        s.step("a_stats", slot=0, dt=0.03, sbase=20 + i)
    s.step("a_stats", sbase=50); s.step("a_stats", slot=0, sbase=51)      # generation 0 does not touch the shared counters
    s.step("a_stats", slot=0, sbase=52, st=[ans(err=3)]); s.step("a_stats", slot=0, sbase=53)
    s.step("a_stats", slot=0, sbase=54, clk=1); s.step("a_stats", slot=0, sbase=55); s.step("a_stats", slot=0, sbase=56, now=1.0)
    s.step("a_stats", slot=0, sbase=57, ssize=79); s.step("a_stats", slot=0, sbase=58, sabi=0)
    s.step("a_stats", slot=0, sbase=59); s.step("a_validate"); s.step("a_stats", slot=0, sbase=60)
    s.step("a_stats", slot=0, dt=0.001, sbase=160, st=[ans(ret=0)], err=[3]); s.step("a_stats", slot=0, dt=0.001, sbase=161)   # a refused read drops the shared counters
    s.step("a_stats", slot=0, dt=0.001, sbase=162, sabi=3); s.step("a_stats", slot=0, dt=0.001, sbase=163)
    s.step("a_begin", slot=1, begin_gen=8)                                # refused; slot 1 is the owned handle too
    s.step("a_stats", slot=1, sbase=61)
    for call in ("a_stats", "a_finish"):
        s.step(call, slot=STALE); s.step(call, slot=FOREIGN)
    s.step("a_stats", slot=0, sbase=64)
    s.step("a_finish", slot=0, endr=[0], err=[13]); s.step("a_stats", slot=0, sbase=65)
    s.step("a_finish", slot=0, st=[ans(ret=0), ans(armed=0, inst=0)], err=[3], sbase=66)
    for call in ("a_stats", "a_finish", "a_finish"):
        s.step(call, slot=0)
    s.step("a_begin", slot=2, begin=2, begin_gen=9, err=[11]); s.step("a_stats", slot=2); s.step("a_stats", slot=0)
    s.step("a_finish", slot=0)
    s.step("a_finish", slot=2, st=[ans(), ans(ret=0)], err=[3], sbase=70); s.step("a_stats", slot=2); s.step("a_finish", slot=2)
    s.step("a_begin", slot=2, begin_gen=10); s.step("a_finish", slot=2, st=[ans(ret=0), ans(ret=0)], err=[3, 4])
    s.step("a_stats", slot=2); s.step("a_finish", slot=2); s.step("stop"); s.step("preflight"); s.step("shutdown")
    return s.ops


def walk(seed, count):
    rng = random.Random(seed)
    ops, t = [], 1000.0
    lease, player, request, weapon = 1, 1, 5, 7
    for _ in range(count):
        t += rng.choice((0.004, 0.0167, 0.0167, 0.033, 0.11, 0.3, 0.6)) if rng.random() < 0.98 else -rng.random() * 2
        roll = rng.random()
        if roll < 0.02: lease = rng.randint(1, 3)
        if 0.02 < roll < 0.035: request = rng.randint(4, 6)
        if 0.035 < roll < 0.05: weapon = rng.randint(7, 8)
        if 0.05 < roll < 0.06: player = rng.randint(0, 2)
        call = rng.choices(["update", "stop", "shutdown", "preflight", "a_finish", "a_validate"], weights=(90, 2.5, 0.05, 3, 1.2, 0.4))[0]
        good = rng.random() < 0.85
        def r(ok=1, bad=(0, 0, 2, 3)): return ok if good or rng.random() < 0.6 else rng.choice(bad)
        def a(): return ans(ret=r(), fill=rng.random() < 0.97, inst=rng.choice((1, 1, 1, 0)),
                            armed=1 if good or rng.random() < 0.6 else 0, err=0 if good or rng.random() < 0.5 else rng.choice((3, 7, 8, 17)))
        ops.append(op(call, slot=LEASE if call.startswith("a_") else NONE, now=NAN if rng.random() < 0.005 else t,
                      clk=rng.choices((0, 1, 2), weights=(96, 2, 2))[0], en=rng.random() < 0.97, mode=rng.random() < 0.98,
                      running=rng.random() < 0.92, lease=lease if rng.random() < 0.96 else 0, lo=rng.random() < 0.97, lp=player,
                      low=weapon if rng.random() < 0.97 else -1, lw=rng.choice((-1, weapon, 8)), lr=request if rng.random() < 0.98 else -1,
                      cfg_ok=rng.random() < 0.95, cfg_throw=rng.random() < 0.3, cfg_why=rng.randint(0, len(REASONS)),
                      cfg_missing=0 if rng.random() < 0.97 else 1 << rng.randint(0, 9),
                      validate=r(), begin=r(1, (0, 0, 0, 2, 2, 5)), begin_gen=41 if good else rng.choice((0, 0, 41, 41, 42)),
                      endr=[r(), r()], st=[a(), a(), a()], err=[rng.choice((0, 2, 3, 7, 8, 13, 17, 18, 18, 18)) for _ in range(3)],
                      sbase=rng.randint(0, 10 ** 6), ssize=80 if rng.random() < 0.985 else 88, sabi=1 if rng.random() < 0.985 else 2,
                      f1=rng.random() * 2, f2=rng.random() * 2))
    return ops


def calm(seed, count):
    """A helper that answers as the real one does (the runner's and the C test's small model of
    it): generations, ownership, error 17 for a foreign token, error 18 for a weapon without an
    optic. On top of it what a session does, and now and then what goes wrong."""
    rng = random.Random(seed)
    ops, t = [], 2000.0
    lease, player, request, weapon, gap_until = 1, 1, 5, 7, 0.0
    for _ in range(count):
        t += rng.choice((0.008, 0.0167, 0.0167, 0.0333))
        roll = rng.random()
        if roll < 0.004: gap_until, lease, request, weapon = t + rng.choice((0.2, 0.7)), lease % 5 + 1, rng.randint(4, 6), rng.randint(7, 10)
        elif roll < 0.008: request = request % 6 + 4               # native ADS in or out: a new request on the same lease
        elif roll < 0.010: player, lease, gap_until = 3 - player, lease % 5 + 1, t + 0.4
        elif roll < 0.012: gap_until = t + rng.choice((0.05, 0.5))
        gap = t < gap_until
        call = rng.choices(["update", "stop", "preflight", "a_finish"], weights=(98.5, 0.5, 0.7, 0.3))[0]
        o = model(call, slot=LEASE if call == "a_finish" else NONE, now=t, en=rng.random() > 0.002, running=not gap,
                  lease=0 if gap and rng.random() < 0.5 else lease, lp=player, low=weapon, lr=request, cfg_ok=rng.random() > 0.01,
                  cfg_why=1, cfg=CFG[:5] + [request] + CFG[6:9] + [weapon], nosrc=weapon >= 9)   # weapons 9 and 10 have no optic
        glitch = rng.random()
        if glitch < 0.004: o["sim_disarm"] = rng.choice((7, 7, 8, 3, 0, 14))
        elif glitch < 0.008: o["begin"], o["begin_gen"], o["err"] = 0, 0, [rng.choice((2, 3, 5, 7, 8))]
        elif glitch < 0.012: o["endr"], o["err"] = [0], [rng.choice((13, 12, 3))]
        elif glitch < 0.015: o["st"] = [ans(ret=0), ans(ret=-1)]; o["err"] = [3]
        elif glitch < 0.020: o["clk"] = rng.choice((1, 2))
        ops.append(o)
    return ops


def c_vector(run, o, line):
    st = ",".join("{%d,%d,%d,%d,%d}" % (a["ret"], a["fill"], a["inst"], a["armed"], a["err"]) for a in pad(o["st"], 3))
    fields = [run, CALLS.index(o["call"]), o["slot"], int(o["en"]), int(o["mode"]), int(o["running"]), o["lease"], int(o["lo"]),
              o["lp"], o["low"], o["lw"], o["lr"], int(o["cfg_ok"]), int(o["cfg_throw"]), o["cfg_why"], o["cfg_missing"],
              "{" + ",".join("%dull" % v for v in o["cfg"]) + "}", o["clk"], cnum(o["now"]), "%dll" % o["validate"],
              "%dll" % o["begin"], "%dull" % o["begin_gen"], "{" + ",".join("%dll" % v for v in pad(o["endr"], 2)) + "}", "{" + st + "}",
              "{" + ",".join("%dll" % v for v in pad(o["err"], 3)) + "}", int(o["nosrc"]), "%dull" % o["sbase"], o["ssize"], o["sabi"],
              cnum(o["f1"]), cnum(o["f2"]), o["sim_disarm"], c_string(line)]
    return "  {" + ",".join(str(f) for f in fields) + "},"


HEADER = """typedef struct BlurAnswer { long long ret; int fill, inst, armed; long long err; } BlurAnswer;
typedef struct BlurAdapterVector {
  int run, call, slot, en, mode, running, lease, lo, lp; long long low, lw, lr;
  int cfg_ok, cfg_throw, cfg_why; unsigned cfg_missing; unsigned long long cfg[10]; int clk; double now;
  long long validate, begin; unsigned long long begin_gen; long long endr[2]; BlurAnswer st[3]; long long err[3]; int nosrc;
  unsigned long long sbase; unsigned ssize, sabi; double f1, f2; int sim_disarm;
  const char *expect;
} BlurAdapterVector;
enum { BV_UPDATE, BV_STOP, BV_SHUTDOWN, BV_PREFLIGHT, BV_A_VALIDATE, BV_A_BEGIN, BV_A_STATS, BV_A_FINISH };
typedef struct BlurAdapterRun { const char *name; int nolog; } BlurAdapterRun;"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runner", type=Path, default=HERE / "run_blur_adapter.lua", help="for the coverage review: a wrapper of the runner")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    source = args.retail / "src/first_person/blur.lua"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    profile_digest = hashlib.sha256((args.retail / "src/first_person/profile.lua").read_bytes()).hexdigest()
    lease_orders, adapter_orders = orders()
    runs = [dict(name="session", ops=session()), dict(name="session_plain", ops=session(), nolog=True, scope=True),
            dict(name="refusals", ops=refusals()), dict(name="inputs", ops=inputs()), dict(name="orders", ops=lease_orders),
            dict(name="orders_adapter", ops=adapter_orders), dict(name="adapter", ops=adapter_only())]
    runs += [dict(name=f"walk{seed}", ops=walk(seed, 2500)) for seed in (1, 2)] + [dict(name="walk3_plain", ops=walk(3, 1000), nolog=True)]
    runs += [dict(name=f"calm{seed}", ops=calm(seed, 3000)) for seed in (1, 2, 3)]
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
        script.write("{reasons=" + lua(REASONS) + "}\n")
        for r in runs:
            script.write("{run=%s,nolog=%s,scope=%s}\n" % (lua(r["name"]), lua(bool(r.get("nolog"))), lua(bool(r.get("scope")))))
            script.write("".join(lua(o) + "\n" for o in r["ops"]))
    done = subprocess.run(["luajit", str(args.runner), str(args.retail), script.name], capture_output=True, text=True)
    Path(script.name).unlink()
    if done.returncode:
        sys.exit(f"run_blur_adapter.lua failed:\n{done.stdout[-2000:]}\n{done.stderr}")
    lines = done.stdout.split("\n")[:-1]
    total = sum(len(r["ops"]) for r in runs)
    if len(lines) != total:
        sys.exit(f"{len(lines)} results for {total} operations")
    out = [f"/* Generated by tools/retail_ref/gen_blur_adapter.py. Do not edit.\n * Retail file: src/first_person/blur.lua at commit {commit}, "
           f"sha256 {digest}\n * (with src/first_person/profile.lua, sha256 {profile_digest}).\n"
           " * Each run starts a fresh lease; every operation carries what the scripted helper, backend, controller\n"
           " * and clock answer during it, and `expect`: what the retail file returned, its state afterwards and the\n"
           " * ordered trace of the calls it made. */",
           f'#define BLUR_ADAPTER_VECTOR_COMMIT "{commit}"', f'#define BLUR_ADAPTER_VECTOR_SHA256 "{digest}"',
           "static const char *const blur_adapter_reasons[] = {\"\", " + ", ".join(c_string(r) for r in REASONS) + "};", HEADER,
           "static const BlurAdapterRun blur_adapter_runs[] = {"]
    out += ["  {%s,%d}," % (c_string(r["name"]), bool(r.get("nolog"))) for r in runs]
    out += ["};", "static const BlurAdapterVector blur_adapter_vectors[] = {"]
    at = 0
    for index, r in enumerate(runs):
        for o in r["ops"]:
            out.append(c_vector(index, o, lines[at]))
            at += 1
        print(f"{r['name']}: {len(r['ops'])} operations")
    out.append("};")
    (args.out / "blur_adapter_vectors.h").write_text("\n".join(out) + "\n")
    print(f"blur adapter: {total} operations in {len(runs)} runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
