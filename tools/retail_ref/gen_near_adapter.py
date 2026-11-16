#!/usr/bin/env python3
"""Cut reference vectors from the retail near_clip.lua (adapter and lease of the near-clip
helper), run unmodified under LuaJIT against a scripted helper, backend, controller, clock and menu.

  gen_near_adapter.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
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
REASONS = ["FOV requires the current owned pose-camera request", "FOV executable identity unavailable", "Memory unavailable", "",
           "backend.lua:268: bad argument #1 to 'format'"]
CALLS = ["update", "stop", "shutdown", "a_begin", "a_set", "a_stats", "a_finish"]
CFG = [0x7FF612340000, 0x140000000, 0x1F0A1000, 0x1F0B2000, 0x7FF612990000, 3]
LEASE, NONE, FOREIGN, STALE = -1, -2, -3, -4          # handle arguments of the direct adapter calls; 0.. are the script's own slots


def ans(ret=1, fill=True, inst=1, armed=1, err=0):
    return dict(ret=ret, fill=fill, inst=inst, armed=armed, err=err)


def op(call="update", **kw):
    w = dict(call=call, slot=NONE, en=True, mode=True, running=True, lease=1, lo=True, lp=1,
             cfg_ok=True, cfg_throw=False, cfg_why=0, cfg_missing=0, cfg=list(CFG),
             opt_table=True, hip_num=True, hip=0.0, aim_num=True, aim=0.0, clk=0, now=0.0,
             begin=1, begin_gen=41, setr=1, endr=[1], st=[ans()], err=[0],
             sbase=10, ssize=88, sabi=1, f1=0.05, f2=0.12, f3=0.12, f4=0.0, sim_disarm=-1)
    w.update(kw)
    if not w["cfg_why"]: w["cfg_throw"] = False          # a raised error always carries its text
    return w


def model(call="update", **kw):
    """An operation the model of the real helper answers."""
    return op(call, begin=-1, setr=-1, endr=[-1], st=[ans(ret=-1)], err=[-1], **kw)


class Script:
    def __init__(self, start=10.0, maker=op):
        self.ops, self.t, self.maker = [], start, maker

    def step(self, call="update", dt=0.016, **kw):
        self.t += dt
        kw.setdefault("now", self.t)
        self.ops.append(self.maker(call, **kw))


def session(pull):
    """What a session produces: arm on a lease, the 4 Hz health poll, distances changed in the menu,
    the lease pausing or ending, another player, stop."""
    s = Script()
    s.step("stop")
    s.step(); s.step(); s.step(hip=0.12)                                  # zero distances: armed only with pull_sight_distance
    for i in range(40):
        s.step(hip=0.12, sbase=10 + i)                                    # statistics every 0.25 s
    s.step(hip=0.12, aim=0.07); s.step(hip=0.12, aim=0.07); s.step(hip=0.2, aim=0.07); s.step(hip=0.2, aim=0.07, dt=0.3)
    s.step(hip=0.0, aim=0.07); s.step(hip=0.0, aim=0.0); s.step(hip=0.0, aim=0.0); s.step(hip=0.12)
    for i in range(4):
        s.step(hip=0.12, running=False, sbase=60 + i)                     # weapon gap: kept, health read every update
    s.step(hip=0.12, lease=0); s.step(hip=0.12, running=False, st=[ans(armed=0, err=7)])   # disarmed in the gap: ended
    s.step(hip=0.12, running=False); s.step(hip=0.12, dt=0.5); s.step(hip=0.12, dt=0.6)    # Begin again, at most once a second
    s.step(hip=0.12, lp=2); s.step(hip=0.12, lp=2, dt=1.1); s.step(hip=0.12, lp=0, dt=0.3)  # another player: End, Begin
    s.step(hip=0.12, lp=0, dt=1.1); s.step(hip=0.12, lo=False); s.step(hip=0.12, lp=1); s.step(hip=0.12, lp=1, dt=1.1)
    s.step(hip=0.12, lo=False); s.step(hip=0.12, lo=False, dt=1.1)
    s.step(hip=0.12, mode=False); s.step(hip=0.12); s.step(hip=0.12, en=False); s.step(hip=0.12)
    s.step("stop"); s.step("stop"); s.step(hip=0.12); s.step(hip=0.3, dt=0.01)
    s.step("shutdown"); s.step(hip=0.12); s.step("shutdown")
    return s.ops


def refusals():
    """The helper refusing at each call with each error code; cleanup owed and retried."""
    s = Script(50.0)
    for code in list(range(0, 20)) + [4294967295]:
        s.step(hip=0.1, begin=0, begin_gen=0, err=[code]); s.step(hip=0.1, dt=0.5); s.step(hip=0.1, dt=0.6, begin=0, begin_gen=0, err=[code])
        s.step(hip=0.1, dt=1.0, begin=0, begin_gen=77, err=[code, 0])     # refused but a generation came back: cleaned up
        s.step(hip=0.1, dt=1.0, begin=2, begin_gen=78, err=[code], endr=[0])   # partial installation, End refuses
        s.step(hip=0.1, endr=[0], err=[code]); s.step("stop", endr=[0], err=[code]); s.step("stop", endr=[0], err=[code + 1])
        s.step(hip=0.1, st=[ans(ret=0)], err=[code, code + 1])            # cleanup works although statistics do not
        s.step(hip=0.1, dt=1.0); s.step(hip=0.2, setr=0, err=[code])      # Set refused: ended
        s.step(hip=0.2, dt=1.0); s.step(hip=0.2, dt=0.3, st=[ans(ret=0), ans()], err=[code])   # the poll's read refused
        s.step(hip=0.2, dt=1.0); s.step(hip=0.2, dt=0.3, st=[ans(armed=0, err=code), ans()])   # disarmed, found by the poll
        s.step(hip=0.2, dt=1.0, st=[ans(armed=0, err=code), ans()])       # did not remain armed
        s.step(hip=0.2, dt=1.0); s.step(hip=0.2, running=False, st=[ans(ret=0)], err=[code])
        s.step(hip=0.2, running=False, st=[ans(armed=0, err=code)])
        s.step(hip=0.2, dt=1.0); s.step(hip=0.2, lp=2, endr=[0], err=[code]); s.step(hip=0.2, lp=2); s.step("stop")
    for result, generation in ((1, 0), (2, 0), (3, 0), (0, 0), (1, 5), (7, 9), (4294967295, 1), (1, 2 ** 64 - 1)):
        s.step(dt=1.0, begin=result, begin_gen=generation, err=[6]); s.step("stop")
    s.step(dt=1.0)
    s.step(dt=0.3, ssize=80); s.step(dt=1.0); s.step(dt=0.3, sabi=2); s.step(dt=1.0); s.step(dt=0.3, st=[ans(fill=False)], sbase=555)
    s.step(dt=0.3, st=[ans(ret=2)]); s.step(dt=1.0)
    s.step(dt=0.3, st=[ans(ret=1, armed=2, inst=5)], sbase=2 ** 64 - 1, f1=NAN, f2=INF, f3=-INF, f4=-0.0)
    s.step(dt=0.3, sbase=2 ** 53 + 1, f1=1e30, f2=1e-30, f3=1 / 3, f4=0.05)
    s.step("stop", st=[ans(ret=0), ans(ret=0)]); s.step(dt=1.0, sbase=10)
    s.step("stop", sbase=77, st=[ans(), ans(ret=0)]); s.step(dt=1.0)
    s.step("stop", st=[ans(ret=0), ans(armed=0, inst=0)]); s.step(dt=1.0); s.step("stop", ssize=1); s.step(dt=1.0)
    s.step("shutdown", endr=[0], err=[13]); s.step(endr=[0], err=[12]); s.step("stop", endr=[0], err=[3]); s.step(); s.step()
    return s.ops


def inputs():
    """Configuration, menu values and clocks that fail or carry odd values."""
    s = Script(200.0)
    for why in range(0, len(REASONS) + 1):
        s.step(dt=1.0, cfg_ok=False, cfg_why=why); s.step(dt=1.0, cfg_ok=False, cfg_why=why)   # the same refusal is logged once
        if why:
            s.step(dt=1.0, cfg_ok=False, cfg_throw=True, cfg_why=why)
    for bit in range(6):
        s.step(dt=1.0, cfg_missing=1 << bit)
    s.step(dt=1.0, cfg_missing=0x3F)
    odd = [dict(opt_table=False), dict(hip_num=False), dict(aim_num=False), dict(hip=NAN), dict(aim=NAN), dict(hip=-0.001),
           dict(aim=-1.0), dict(hip=1.0000001), dict(aim=INF), dict(hip=-INF), dict(hip=1.0, aim=1.0), dict(hip=-0.0, aim=-0.0),
           dict(hip=1e-40, aim=0.05), dict(hip=0.1, aim=1.0000001), dict(hip=0.1, aim=1.5), dict(hip=2.0, aim=0.1), dict(hip=0.1 + 0.2, aim=1 / 3), dict(hip=0.07), dict(hip=0.07, aim=0.07)]
    for o in odd:
        s.step(dt=1.0, **o); s.step(**o); s.step(dt=0.3, **o); s.step(**dict(o, hip=0.5)); s.step("stop")
    # Clocks: the retail clock raises on a bad engine sample; the update ends there.
    s.step(dt=1.0, clk=1); s.step(dt=1.0); s.step(clk=1); s.step(hip=0.3, clk=1); s.step(hip=0.3); s.step(hip=0.3, dt=0.3)
    for now in (NAN, INF, -INF, 0.0, 5.0, 5.25, 5.25, 5.4999, 5.5, 5.5, 5.75 - 1e-12, 4.0, 4.1, 1e17, 1e17, 1e300):
        s.step(now=now, sbase=len(s.ops))
    s.step("stop")
    for now in (NAN, NAN, 7.0, 7.5, 8.0 - 1e-9, 8.0, INF, INF, 1.0, -INF, 3.0):
        s.step(now=now, begin=0, begin_gen=0, err=[3])
    s.step(now=NAN); s.step(now=NAN, hip=0.1); s.step(now=1.0); s.step("stop", clk=1)
    return s.ops


def orders():
    s = Script(420.0)
    s.step("stop"); s.step("shutdown"); s.step()
    s2 = Script(430.0)
    s2.step(hip=0.1); s2.step("a_finish", slot=LEASE)                     # ended behind the lease's back
    s2.step(hip=0.1); s2.step(hip=0.1, dt=0.3); s2.step("stop"); s2.step(hip=0.1, dt=1.0)
    s2.step("a_finish", slot=LEASE); s2.step(hip=0.1, running=False); s2.step(hip=0.1, dt=1.0)
    s2.step("a_finish", slot=LEASE); s2.step(hip=0.2); s2.step(hip=0.2, dt=1.0)
    s2.step("a_begin", slot=0, begin_gen=90); s2.step("a_set", slot=LEASE, hip=0.4, aim=0.3); s2.step("stop")
    s2.step("a_begin", slot=0, begin_gen=91, hip=0.3)                     # owned by the script; the lease adopts it
    s2.step(hip=0.1, dt=1.0, begin_gen=92); s2.step(hip=0.1); s2.step(hip=0.1, dt=1.0)
    s2.step("a_begin", slot=1, begin_gen=93); s2.step(hip=0.1, dt=1.0); s2.step(hip=0.1, dt=0.3)
    s2.step("a_finish", slot=1, endr=[0], err=[13]); s2.step("stop"); s2.step(hip=0.1, dt=1.0); s2.step("stop")
    s2.step("a_finish", slot=1)
    return s.ops, s2.ops


def adapter_only():
    s = Script(600.0)
    for slot in (NONE, FOREIGN, STALE, 0):
        for call in ("a_stats", "a_finish", "a_set"):
            s.step(call, slot=slot, hip=0.1)
    s.step("a_begin", slot=0, cfg_ok=False); s.step("a_begin", slot=0, cfg_missing=0x20); s.step("a_begin", slot=0, cfg_missing=4)
    s.step("a_stats", st=[ans(inst=0, armed=0)]); s.step("a_stats", st=[ans(ret=0)], err=[3]); s.step("a_stats", ssize=0)
    s.step("a_stats", sabi=7); s.step("a_begin", slot=0, begin=0, begin_gen=0, err=[4]); s.step("a_stats", slot=0)
    s.step("a_begin", slot=0, begin_gen=7, hip=0.25, aim=1.0)
    for i in range(4):
        s.step("a_stats", slot=0, sbase=20 + i)
    s.step("a_stats", sbase=50); s.step("a_begin", slot=1, begin_gen=8); s.step("a_stats", slot=1, sbase=61)
    for call in ("a_stats", "a_finish", "a_set"):
        s.step(call, slot=STALE); s.step(call, slot=FOREIGN)
    s.step("a_set", slot=0, hip=0.5, aim=0.25); s.step("a_set", slot=0, setr=0, err=[17]); s.step("a_set", slot=0, setr=2)
    s.step("a_set", slot=0, hip=NAN, aim=INF); s.step("a_set", slot=0, hip=1e39, aim=-1e-46)
    s.step("a_finish", slot=0, endr=[0], err=[13]); s.step("a_stats", slot=0, sbase=65)
    s.step("a_finish", slot=0, st=[ans(ret=0), ans(armed=0, inst=0)], err=[3], sbase=66)
    for call in ("a_stats", "a_finish", "a_set", "a_finish"):
        s.step(call, slot=0)
    s.step("a_begin", slot=2, begin=2, begin_gen=9, err=[11]); s.step("a_stats", slot=2); s.step("a_stats", slot=0)
    s.step("a_finish", slot=0); s.step("a_set", slot=0)
    s.step("a_finish", slot=2, st=[ans(), ans(ret=0)], err=[3], sbase=70); s.step("a_stats", slot=2); s.step("a_finish", slot=2)
    s.step("a_begin", slot=2, begin_gen=10); s.step("a_finish", slot=2, st=[ans(ret=0), ans(ret=0)], err=[3, 4])
    s.step("a_stats", slot=2); s.step("a_finish", slot=2); s.step("stop"); s.step("shutdown")
    return s.ops


def walk(seed, count):
    rng = random.Random(seed)
    ops, t = [], 1000.0
    player, hip, aim = 1, 0.1, 0.0
    for _ in range(count):
        t += rng.choice((0.004, 0.0167, 0.0167, 0.033, 0.11, 0.26, 0.6, 1.2)) if rng.random() < 0.98 else -rng.random() * 2
        roll = rng.random()
        if roll < 0.02: player = rng.randint(0, 2)
        if 0.02 < roll < 0.06: hip = rng.choice((0.0, 0.0, 0.07, 0.1, 0.25, 1.0, 1.5, -0.1))
        if 0.06 < roll < 0.09: aim = rng.choice((0.0, 0.0, 0.07, 0.3))
        call = rng.choices(["update", "stop", "shutdown", "a_finish", "a_set", "a_stats"], weights=(90, 2.5, 0.03, 1.2, 0.6, 0.8))[0]
        good = rng.random() < 0.85
        def r(ok=1, bad=(0, 0, 2, 3)): return ok if good or rng.random() < 0.6 else rng.choice(bad)
        def a(): return ans(ret=r(), fill=rng.random() < 0.97, inst=rng.choice((1, 1, 1, 0)),
                            armed=1 if good or rng.random() < 0.6 else 0, err=0 if good or rng.random() < 0.5 else rng.choice((3, 7, 8, 17)))
        ops.append(op(call, slot=LEASE if call.startswith("a_") else NONE, now=NAN if rng.random() < 0.005 else t,
                      clk=int(rng.random() < 0.02), en=rng.random() < 0.97, mode=rng.random() < 0.98, running=rng.random() < 0.9,
                      lease=int(rng.random() < 0.95), lo=rng.random() < 0.95, lp=player,
                      cfg_ok=rng.random() < 0.93, cfg_throw=rng.random() < 0.3, cfg_why=rng.randint(0, len(REASONS)),
                      cfg_missing=0 if rng.random() < 0.96 else 1 << rng.randint(0, 5),
                      opt_table=rng.random() < 0.99, hip_num=rng.random() < 0.99, hip=hip, aim_num=rng.random() < 0.99, aim=aim,
                      begin=r(1, (0, 0, 2, 2, 5)), begin_gen=41 if good else rng.choice((0, 41, 41, 41, 42)), setr=r(), endr=[r(), r()],
                      st=[a(), a(), a()], err=[rng.choice((0, 2, 3, 3, 7, 8, 13, 17)) for _ in range(3)], sbase=rng.randint(0, 10 ** 6),
                      ssize=88 if rng.random() < 0.985 else 72, sabi=1 if rng.random() < 0.985 else 2,
                      f1=rng.random(), f2=rng.random(), f3=hip if 0 <= hip <= 1 else 0.0, f4=aim))
    return ops


def calm(seed, count):
    """A helper that answers as the real one does (the runner's and the C test's small model of
    it). On top of it what a session does: the menu moved, gaps, respawns; now and then the helper
    disarming itself, a refused End, a lost read, a bad clock sample."""
    rng = random.Random(seed)
    ops, t = [], 2000.0
    player, hip, aim, gap_until = 1, 0.0, 0.0, 0.0
    for _ in range(count):
        t += rng.choice((0.008, 0.0167, 0.0167, 0.0333))
        roll = rng.random()
        if roll < 0.004: gap_until = t + rng.choice((0.2, 0.7, 1.5))
        elif roll < 0.006: player, gap_until = 3 - player, t + 0.4
        elif roll < 0.012: hip = rng.choice((0.0, 0.08, 0.12, 0.2))
        elif roll < 0.016: aim = rng.choice((0.0, 0.07, 0.1))
        gap = t < gap_until
        call = rng.choices(["update", "stop", "a_finish"], weights=(99.2, 0.5, 0.3))[0]
        o = model(call, slot=LEASE if call == "a_finish" else NONE, now=t, en=rng.random() > 0.002, running=not gap,
                  lease=0 if gap and rng.random() < 0.5 else 1, lp=player, cfg_ok=rng.random() > 0.01, cfg_why=1, hip=hip, aim=aim)
        glitch = rng.random()
        if glitch < 0.004: o["sim_disarm"] = rng.choice((7, 7, 8, 3, 0))
        elif glitch < 0.008: o["begin"], o["begin_gen"], o["err"] = 0, 0, [rng.choice((2, 3, 5, 7))]
        elif glitch < 0.012: o["endr"], o["err"] = [0], [rng.choice((13, 12, 3))]
        elif glitch < 0.015: o["st"] = [ans(ret=0), ans(ret=-1)]; o["err"] = [3]
        elif glitch < 0.018: o["setr"], o["err"] = 0, [17]
        elif glitch < 0.020: o["clk"] = 1
        ops.append(o)
    return ops


def c_vector(run, o, line):
    st = ",".join("{%d,%d,%d,%d,%d}" % (a["ret"], a["fill"], a["inst"], a["armed"], a["err"]) for a in pad(o["st"], 3))
    fields = [run, CALLS.index(o["call"]), o["slot"], int(o["en"]), int(o["mode"]), int(o["running"]), o["lease"], int(o["lo"]),
              o["lp"], int(o["cfg_ok"]), int(o["cfg_throw"]), o["cfg_why"], o["cfg_missing"],
              "{" + ",".join("%dull" % v for v in o["cfg"]) + "}", int(o["opt_table"]), int(o["hip_num"]), cnum(o["hip"]),
              int(o["aim_num"]), cnum(o["aim"]), o["clk"], cnum(o["now"]), "%dll" % o["begin"], "%dull" % o["begin_gen"],
              "%dll" % o["setr"], "{" + ",".join("%dll" % v for v in pad(o["endr"], 2)) + "}", "{" + st + "}",
              "{" + ",".join("%dll" % v for v in pad(o["err"], 3)) + "}", "%dull" % o["sbase"], o["ssize"], o["sabi"],
              cnum(o["f1"]), cnum(o["f2"]), cnum(o["f3"]), cnum(o["f4"]), o["sim_disarm"], c_string(line)]
    return "  {" + ",".join(str(f) for f in fields) + "},"


HEADER = """typedef struct NearAnswer { long long ret; int fill, inst, armed; long long err; } NearAnswer;
typedef struct NearAdapterVector {
  int run, call, slot, en, mode, running, lease, lo, lp, cfg_ok, cfg_throw, cfg_why; unsigned cfg_missing; unsigned long long cfg[6];
  int opt_table, hip_num; double hip; int aim_num; double aim; int clk; double now;
  long long begin; unsigned long long begin_gen; long long setr, endr[2]; NearAnswer st[3]; long long err[3];
  unsigned long long sbase; unsigned ssize, sabi; double f1, f2, f3, f4; int sim_disarm;
  const char *expect;
} NearAdapterVector;
enum { NV_UPDATE, NV_STOP, NV_SHUTDOWN, NV_A_BEGIN, NV_A_SET, NV_A_STATS, NV_A_FINISH };
typedef struct NearAdapterRun { const char *name; int nolog, pull; } NearAdapterRun;"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runner", type=Path, default=HERE / "run_near_adapter.lua", help="for the coverage review: a wrapper of the runner")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    source = args.retail / "src/first_person/near_clip.lua"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    profile_digest = hashlib.sha256((args.retail / "src/first_person/profile.lua").read_bytes()).hexdigest()
    lease_orders, adapter_orders = orders()
    runs = [dict(name="session", ops=session(True)), dict(name="session_no_pull", ops=session(False), pull=False, nolog=True, scope=True),
            dict(name="refusals", ops=refusals()), dict(name="refusals_no_pull", ops=refusals(), pull=False),
            dict(name="inputs", ops=inputs()), dict(name="inputs_no_pull", ops=inputs(), pull=False),
            dict(name="orders", ops=lease_orders), dict(name="orders_adapter", ops=adapter_orders), dict(name="adapter", ops=adapter_only())]
    runs += [dict(name=f"walk{seed}", ops=walk(seed, 2500)) for seed in (1, 2)]
    runs += [dict(name="walk3_no_pull", ops=walk(3, 1500), pull=False, nolog=True)]
    runs += [dict(name=f"calm{seed}", ops=calm(seed, 3000)) for seed in (1, 2)] + [dict(name="calm3_no_pull", ops=calm(3, 3000), pull=False)]
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
        script.write("{reasons=" + lua(REASONS) + "}\n")
        for r in runs:
            script.write("{run=%s,nolog=%s,pull=%s,scope=%s}\n" % (lua(r["name"]), lua(bool(r.get("nolog"))),
                         lua(bool(r.get("pull", True))), lua(bool(r.get("scope")))))
            script.write("".join(lua(o) + "\n" for o in r["ops"]))
    done = subprocess.run(["luajit", str(args.runner), str(args.retail), script.name], capture_output=True, text=True)
    Path(script.name).unlink()
    if done.returncode:
        sys.exit(f"run_near_adapter.lua failed:\n{done.stdout[-2000:]}\n{done.stderr}")
    lines = done.stdout.split("\n")[:-1]
    total = sum(len(r["ops"]) for r in runs)
    if len(lines) != total:
        sys.exit(f"{len(lines)} results for {total} operations")
    out = [f"/* Generated by tools/retail_ref/gen_near_adapter.py. Do not edit.\n * Retail file: src/first_person/near_clip.lua at commit {commit}, "
           f"sha256 {digest}\n * (with src/first_person/profile.lua, sha256 {profile_digest}).\n"
           " * Each run starts a fresh lease; every operation carries what the scripted helper, backend, controller,\n"
           " * clock and menu answer during it, and `expect`: what the retail file returned, its state afterwards and\n"
           " * the ordered trace of the calls it made. */",
           f'#define NEAR_ADAPTER_VECTOR_COMMIT "{commit}"', f'#define NEAR_ADAPTER_VECTOR_SHA256 "{digest}"',
           "static const char *const near_adapter_reasons[] = {\"\", " + ", ".join(c_string(r) for r in REASONS) + "};", HEADER,
           "static const NearAdapterRun near_adapter_runs[] = {"]
    out += ["  {%s,%d,%d}," % (c_string(r["name"]), bool(r.get("nolog")), bool(r.get("pull", True))) for r in runs]
    out += ["};", "static const NearAdapterVector near_adapter_vectors[] = {"]
    at = 0
    for index, r in enumerate(runs):
        for o in r["ops"]:
            out.append(c_vector(index, o, lines[at]))
            at += 1
        print(f"{r['name']}: {len(r['ops'])} operations")
    out.append("};")
    (args.out / "near_adapter_vectors.h").write_text("\n".join(out) + "\n")
    print(f"near adapter: {total} operations in {len(runs)} runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
