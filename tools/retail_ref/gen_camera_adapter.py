#!/usr/bin/env python3
"""Cut camera-adapter reference vectors from the retail camera.lua, run unmodified under LuaJIT
(with the real profile.lua) against a scripted helper, backend, controller and clock.

  gen_camera_adapter.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
  gen_camera_adapter.py --retail ... --scripts <folder>     only write the Lua scripts (coverage runs)
"""
import argparse
import hashlib
import math
import random
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
FROZEN = "1d0d453"
REASONS = ["Memory unavailable", "Photo Mode or another camera owns the view", "No supported local gameplay camera",
           "Head camera requires the verified current on-foot lifecycle", "",
           "cannot load module 'MOD/native/native_ads_camera.dll': The specified module could not be found.",
           "Camera is not owned by local player"]
CALLS = ["update", "prepare", "prearm", "stop", "shutdown", "preflight", "set_handoff", "set_cut_hold"]
FIELDS = ["game", "exe", "context", "camera", "avatar_owner", "avatar_index", "avatar", "body", "camera_unit",
          "forward", "up", "smoothing_ms", "blend_ms", "max_gap_ms"]
ALL = 0x3fff
NAN, INF = float("nan"), float("inf")


class U64(int):
    """A value written as an unsigned 64-bit literal on both sides."""


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


class Run:
    """One fresh camera: its settings, the answers the world can give, and the operations."""

    def __init__(self, name, clock=True, log=True, snapshot=True, profile=None, start=100.0):
        self.name, self.clock, self.log, self.snapshot, self.profile = name, clock, log, snapshot, profile or {}
        self.stats, self.configs, self.ops = [], [], []
        self.t = start

    def _pool(self, pool, entry):
        key = repr(sorted(entry.items()))
        for index, (known, _) in enumerate(pool):
            if known == key:
                return index + 1
        pool.append((key, entry))
        return len(pool)

    def stat(self, ret=1, fill=True, size=80, abi=1, installed=1, armed=1, error=0, seen=0, forwarded=0, replaced=0,
             native6=0, resets=0, blend=1.0, head=0.0, dt=0.0):
        return self._pool(self.stats, dict(ret=ret, fill=fill, size=size, abi=abi, installed=installed, armed=armed, error=error,
                                           seen=U64(seen), forwarded=U64(forwarded), replaced=U64(replaced), native6=U64(native6),
                                           resets=U64(resets), blend=f32(blend), head=f32(head), dt=f32(dt)))

    def conf(self, ok=1, why=0, present=ALL, game=0x7ff8a0000000, exe=0x7ff6c0000000, context=0x1c2a3b4c5d0, camera=0x1c2a3b40000,
             avatar_owner=0x1c2a3b48888, avatar_index=3, avatar=0x40012, body=0x40013, camera_unit=0x40777, forward=.18, up=.04,
             smoothing_ms=100, blend_ms=120, max_gap_ms=250, kind=4, pres=1):
        return self._pool(self.configs, dict(ok=ok, why=why, present=present, game=game, exe=exe, context=context, camera=camera,
                                             avatar_owner=avatar_owner, avatar_index=avatar_index, avatar=avatar, body=body,
                                             camera_unit=camera_unit, forward=float(forward), up=float(up),
                                             smoothing_ms=float(smoothing_ms), blend_ms=float(blend_ms), max_gap_ms=max_gap_ms,
                                             kind=int(kind), pres=int(pres)))

    def op(self, call="update", dt=1 / 60, **kw):
        self.t += dt
        o = dict(call=call, a=0.0, b=0.0, hold=0, enabled=True, mode=1, cfg=[self.conf()], snap=2, unit=0x40abc, clock=[self.t],
                 load=0, follow=False, validate=1, begin=1, gen=5, ends=[1], st=[self.stat()], errs=[0], pres=1, hand=1, cut=1, key=1)
        for key, value in kw.items():
            if key not in o:
                raise KeyError(key)
            o[key] = value
        for key, size in (("cfg", 2), ("clock", 4), ("ends", 3), ("st", 6), ("errs", 4)):
            o[key] = (list(o[key]) + [o[key][-1]] * size)[:size]
        o["a"], o["b"], o["unit"] = float(o["a"]), float(o["b"]), float(o["unit"])
        for key in ("validate", "begin", "pres", "hand", "cut", "key", "load", "snap", "mode", "hold"):
            o[key] = int(o[key])
        o["ends"], o["errs"] = [int(e) for e in o["ends"]], [int(e) for e in o["errs"]]
        o["enabled"], o["follow"] = bool(o["enabled"]), bool(o["follow"])
        self.ops.append(o)
        return o


def session():
    """What a play session produces, in order."""
    r = Run("session")
    armed = lambda n=0, **kw: r.stat(seen=10 + n, forwarded=2, replaced=5 + n, native6=n // 3, resets=1, blend=min(1, n / 8), head=.11,
                                     dt=.0167, **kw)
    idle = r.stat(installed=0, armed=0)
    gone = [idle, idle]
    # Before anything: status and result of a camera that never read statistics; preflight; controller off; comparison mode.
    r.op("preflight"); r.op(enabled=False); r.op(mode=2); r.op(mode=0)
    # No gameplay camera yet: wait. Then arm from update.
    r.op(cfg=[r.conf(ok=0, why=3)]); r.op(cfg=[r.conf(kind=12)]); r.op(cfg=[r.conf(ok=0)])
    r.op(st=[armed(0)])
    for n in range(1, 30):                                  # steady frames; counters convert on the 0.1 s grid
        r.op(st=[armed(n)])
    r.op(st=[armed(31)], unit=0x40abd)                      # weapon swap: a new key
    r.op(st=[armed(32)], unit=0x40abe, key=0)               # the helper refuses a key: sent again next frame
    r.op(st=[armed(33)], unit=0x40abe)
    r.op(st=[armed(34)], unit=0x40abe)
    for code in (14, 15, 16, 18, 14):                       # per-frame gates: reported, nothing stops
        r.op(st=[armed(40, error=code)])
        r.op(st=[armed(41)])
    r.op("prepare", st=[armed(42)])                         # entry (camera_before_entry) on the same owner: kept
    r.op("prearm", st=[armed(43)], cfg=[r.conf(kind=6)])
    r.op("prepare", st=[armed(44), armed(44), idle, armed(0)], cfg=[r.conf(body=0x40099)], gen=6)   # another body: re-armed
    r.op(st=[armed(1)])                                     # the key is sent again after a re-arm
    r.op(st=[armed(2, armed=0, error=7), armed(2, armed=0, error=7), idle])     # the helper let go: player changed
    r.op(cfg=[r.conf(ok=0, why=1)])
    r.op(st=[armed(0)], gen=7)
    r.op(st=[armed(3, armed=0, error=0), armed(3, armed=0), idle])              # disarmed without a code
    r.op(st=[armed(0)], gen=8)
    for code in (3, 14, 15, 16, 18, 8, 1):                  # disarmed with a typed cause: 3 is a read fault, others unsafe
        r.op(st=[armed(4, armed=0, error=code), armed(4, armed=0, error=code), idle])
        r.op(st=[armed(0)], gen=9)
    r.op(st=[armed(5, armed=0, error=3), armed(5, armed=0, error=3)], ends=[0], errs=[13])     # ... while cleanup is still owed
    r.op(ends=[0], errs=[13]); r.op("prepare"); r.op("preflight")
    r.op(st=[armed(5, armed=0, error=3), idle])
    r.op(st=[armed(0)], gen=10)
    r.op(st=[armed(6, armed=0, error=7), armed(6, armed=0, error=7)], ends=[0], errs=[13])     # player changed, cleanup owed
    r.op(st=[armed(6, armed=0, error=7), idle])
    r.op(st=[armed(0)], gen=11)
    r.op(enabled=False, st=[armed(7), idle]); r.op(enabled=False)                # F11 off
    r.op(st=[armed(0)], gen=12)
    r.op(mode=2, st=[armed(8), idle])                       # compare-native mode
    r.op(st=[armed(0)], gen=13)
    r.op("stop", st=[armed(9), idle]); r.op("stop")         # foreign camera
    r.op(st=[armed(0)], gen=14)
    r.op("shutdown", st=[armed(9)], ends=[0], errs=[13])    # shutdown with cleanup refused, then granted
    r.op("prepare"); r.op("preflight"); r.op(st=gone)
    r.op(); r.op("prepare"); r.op("shutdown"); r.op("preflight"); r.op("stop")
    return r


def refusals():
    """The helper refusing at every call, with every error code."""
    r = Run("refusals")
    ok, idle = r.stat(seen=4, replaced=2, blend=.5), r.stat(installed=0, armed=0)
    for code in range(0, 20):
        r.op("preflight", validate=0, errs=[code])
    r.op("preflight", validate=2, errs=[6]); r.op("preflight", validate=1)
    for code in list(range(0, 20)) + [4294967295]:
        r.op("prepare", begin=0, gen=0, errs=[code])        # refused
        r.op(begin=0, gen=0, errs=[code])
    for code in (13, 9, 0):
        r.op("prepare", begin=2, gen=21, errs=[code, 0], st=[ok, idle])          # partial installation: cleaned up at once
        r.op("prepare", begin=2, gen=22, errs=[code, 13], ends=[0], st=[ok])     # ... or cleanup stays owed
        r.op(ends=[0], errs=[13]); r.op(st=[ok, idle])
    r.op("prepare", begin=2, gen=0); r.op("prepare", begin=1, gen=0)             # says armed, hands out no generation
    r.op("prepare", begin=0, gen=23, errs=[2], st=[ok, idle])                    # refused but a generation is owned
    r.op("prepare", begin=3, gen=24, errs=[2], st=[ok, idle]); r.op("prepare", begin=3, gen=0, errs=[2])
    r.op("prepare", begin=4294967295, gen=0, errs=[1])
    # Presentation refused: the lease is given back. Hand-off and hold refused: kept as notes, the lease stays.
    r.op("prepare", pres=0, st=[ok, idle]); r.op("prepare", pres=2, st=[ok, idle])
    r.op("prepare", pres=0, st=[ok], ends=[0], errs=[13]); r.op(st=[ok, idle])
    r.op("prepare", hand=0, errs=[6]); r.op("stop", st=[ok, idle])
    r.op("prepare", cut=0, errs=[5]); r.op("stop", st=[ok, idle])
    r.op("prepare", hand=0, cut=0, errs=[6, 5, 5]); r.op("stop", st=[ok, idle])
    r.op("prepare", hand=2, cut=2, errs=[1]); r.op("stop", st=[ok, idle])
    r.op("prepare"); r.op("stop", st=[ok, idle])            # a clean arm clears both notes
    # Statistics refused: right after arming; in an update; before and after End; every code.
    for code in (3, 7, 13, 0):
        bad = r.stat(ret=0)
        r.op("prepare", st=[bad, bad, idle], errs=[code])
        r.op("prepare", st=[bad, bad, bad], errs=[code, 2, 4])
        r.op("prepare", st=[bad], errs=[code, code, 13], ends=[0]); r.op(st=[ok, idle])
        r.op("prepare"); r.op(st=[bad, ok, idle], errs=[code]); r.op("prepare"); r.op(st=[bad, bad, idle], errs=[code])
        r.op("prepare"); r.op(st=[bad, bad, bad], errs=[code]); r.op("prepare"); r.op("stop", st=[bad, idle], errs=[code])
        r.op("prepare"); r.op("stop", st=[ok, bad], errs=[code]); r.op("prepare"); r.op("stop", st=[bad, bad], errs=[code])
        r.op("prepare"); r.op("prepare", st=[bad, ok, idle, ok], errs=[code]); r.op("stop", st=[ok, idle])
        r.op("prepare"); r.op("prepare", st=[bad, ok], ends=[0], errs=[code, 13]); r.op(st=[ok, idle])
    r.op("prepare", st=[r.stat(ret=2)], errs=[1]); r.op("prepare", st=[r.stat(ret=0, armed=0)], errs=[1])
    # Not armed any more right after arming.
    r.op("prepare", st=[r.stat(armed=0, error=7), ok, idle]); r.op("prepare", st=[r.stat(armed=0), ok, ok])
    r.op("prepare", st=[r.stat(armed=0, error=3), ok], ends=[0], errs=[13]); r.op(st=[ok, idle])
    r.op("prepare", st=[r.stat(armed=2, installed=7, error=0)]); r.op("stop", st=[ok, idle])
    # The statistics block coming back with another size or ABI; a helper that writes nothing.
    r.op("prepare", st=[r.stat(size=81), ok, idle]); r.op("prepare", st=[r.stat(abi=2), ok, idle])
    r.op("prepare"); r.op(st=[r.stat(size=0), ok, idle]); r.op("prepare"); r.op(st=[r.stat(abi=0), r.stat(abi=3), idle])
    r.op("prepare", st=[r.stat(seen=77, replaced=66, blend=.25)]); r.op(st=[r.stat(fill=False)]); r.op(st=[r.stat(fill=False, ret=0)], errs=[3])
    r.op("prepare", st=[r.stat(fill=False)]); r.op("stop", st=[r.stat(fill=False)])
    # End refused with every code, retried from update, from stop, from shutdown.
    for code in list(range(0, 20)):
        r.op("prepare"); r.op("stop", st=[ok], ends=[0], errs=[code]); r.op(ends=[0, 1], errs=[code], st=[ok, idle]); r.op("stop", st=[ok, idle])
    r.op("prepare"); r.op("stop", st=[ok], ends=[2], errs=[13]); r.op("stop", st=[ok], ends=[0], errs=[13]); r.op("shutdown", ends=[0], errs=[13])
    r.op(ends=[0], errs=[13]); r.op("shutdown", st=[ok, idle]); r.op()
    return r


def settings():
    """Presentation, hand-off, hold and weapon key: set once, set again after a re-arm, switched live."""
    r = Run("settings")
    ok, idle = r.stat(seen=4, replaced=2, blend=.5), r.stat(installed=0, armed=0)
    r.op("set_handoff", a=30, b=40); r.op("set_cut_hold", hold=5)                # before any lease: kept for the next one
    r.op("prepare"); r.op(); r.op()
    r.op("set_handoff", a=0, b=500); r.op("set_handoff", a=500, b=0, hand=0, errs=[1]); r.op("set_handoff", a=12.5, b=.1, hand=2, errs=[6])
    for a, b in ((-1, 0), (0, -1), (500.001, 0), (0, 501), (NAN, 0), (0, NAN), (INF, 0), (0, -INF), (-0.0, 0), (1e-9, 499.999)):
        r.op("set_handoff", a=a, b=b)
    r.op("stop", st=[ok, idle]); r.op("prepare"); r.op("stop", st=[ok, idle])    # an out-of-range choice is kept and refused at the arm
    r.op("set_handoff", a=NAN, b=1); r.op("prepare"); r.op("set_handoff", a=1, b=1); r.op("stop", st=[ok, idle]); r.op("prepare")
    for hold in range(16):
        r.op("set_cut_hold", hold=hold)
    r.op("set_cut_hold", hold=9, cut=0, errs=[1]); r.op("set_cut_hold", hold=9, cut=2, errs=[6])
    r.op("stop", st=[ok, idle])
    for hold in range(16):                                  # every choice at the arm; 0 sends nothing and keeps the old note
        r.op("set_cut_hold", hold=hold); r.op("prepare", cut=hold % 3 != 1, errs=[5]); r.op("stop", st=[ok, idle])
    # Presentation off (the backend's direct mode): neither hand-off nor hold are sent.
    r.op("prepare", cfg=[r.conf(pres=0)]); r.op(cfg=[r.conf(pres=0)]); r.op("stop", st=[ok, idle])
    # The weapon key: every kind of value a snapshot can carry.
    r.op("prepare")
    for unit in (0x40abc, 0x40abc, 0, 0, -0.0, 4294967295, 4294967296, -1, 1.5, NAN, NAN, INF, -INF, 7, 7.0000001, 1e300, 7):
        r.op(unit=unit)
    for snap in (0, 0, 2, 1, 1, 2, 3, 3, 2, 4, 4, 2):
        r.op(snap=snap, unit=99)
    r.op(unit=100, key=0); r.op(unit=100, key=2); r.op(unit=100, key=1); r.op(unit=100, key=0)
    r.op("prepare", cfg=[r.conf(camera=0x1c2a3b4ffff)], st=[ok, ok, idle, ok], gen=6); r.op(unit=100)      # re-arm: the key is forgotten
    r.op("stop", st=[ok, idle])
    return r


def configs():
    """What the backend can hand over as a configuration, and how a change between frames is taken."""
    r = Run("configs")
    ok, idle = r.stat(seen=4, replaced=2, blend=.5), r.stat(installed=0, armed=0)
    for call in ("preflight", "prepare", "update"):
        for c in (r.conf(ok=0), r.conf(ok=0, why=1), r.conf(ok=0, why=5), r.conf(ok=2, why=2), r.conf(ok=2, why=5), r.conf(kind=0),
                  r.conf(kind=5), r.conf(kind=12), r.conf(kind=6), r.conf(kind=4)):
            r.op(call, cfg=[c])
            r.op("stop", st=[ok, idle])
        for bit in range(14):                               # each field missing; the first missing one is named
            r.op(call, cfg=[r.conf(present=ALL & ~(1 << bit))]); r.op("stop", st=[ok, idle])
        r.op(call, cfg=[r.conf(present=0)]); r.op(call, cfg=[r.conf(present=ALL & ~0x2010)]); r.op("stop", st=[ok, idle])
    # update asks twice when it arms: the second answer decides the arm.
    for second in (r.conf(ok=0, why=1), r.conf(ok=0), r.conf(kind=12), r.conf(present=ALL & ~4), r.conf(body=9)):
        r.op(cfg=[r.conf(), second]); r.op("stop", st=[ok, idle])
    # Held lease, entry asks again: same owner keeps it; each identity field changing re-arms; the others do not.
    r.op("prepare")
    for name in ("game", "exe", "context", "camera", "avatar_owner", "avatar_index", "avatar", "body", "camera_unit"):
        r.op("prepare", cfg=[r.conf(**{name: 1})], st=[ok, ok, idle, ok], gen=30)
        r.op("prepare", cfg=[r.conf(**{name: 1})]); r.op("prepare", st=[ok, ok, idle, ok], gen=31)
    for name, value in (("forward", .5), ("up", 0), ("smoothing_ms", 0), ("blend_ms", 0), ("max_gap_ms", 1)):
        r.op("prepare", cfg=[r.conf(**{name: value})])
    r.op("prepare", cfg=[r.conf(pres=0)]); r.op("prepare", cfg=[r.conf(kind=6)])
    r.op(cfg=[r.conf(body=77)]); r.op(cfg=[r.conf(kind=12)]); r.op(cfg=[r.conf(ok=0)])      # update does not ask while it holds a lease
    # Held lease, the entry's configuration is no longer usable.
    r.op("prepare", cfg=[r.conf(ok=0, why=2)]); r.op("prepare", cfg=[r.conf(kind=12)])
    for bit in range(14):
        r.op("prepare", cfg=[r.conf(present=ALL & ~(1 << bit))], st=[ok, ok, idle]); r.op("prepare")
    # Held lease no longer armed at an entry, with and without a code; cleanup refused in the middle of a re-arm.
    r.op("prepare", st=[r.stat(armed=0, error=7), ok, idle, ok], gen=32)
    r.op("prepare", st=[r.stat(armed=0, error=3), ok, idle, ok], gen=33)
    r.op("prepare", cfg=[r.conf(body=5)], st=[ok, ok], ends=[0], errs=[13]); r.op("prepare"); r.op(st=[ok, idle])
    # Values at the edge of their fields.
    r.op("prepare", cfg=[r.conf(game=2 ** 53 - 1, exe=0, context=1, camera=2 ** 47, avatar_owner=2 ** 48 + 1, avatar_index=0,
                                avatar=4294967295, body=1, camera_unit=0x3fffff, forward=-.5, up=1e-3, smoothing_ms=.1,
                                blend_ms=1e9, max_gap_ms=4294967295)], gen=2 ** 53 - 1)
    r.op(); r.op("stop", st=[ok, idle])
    r.op("prepare", gen=2 ** 32); r.op(); r.op("stop", st=[ok, idle])
    return r


def clocks():
    """The statistics tick: counters convert on a 0.1 s grid offset .0333 s; health is fresh every call."""
    r = Run("clocks", start=0.0)
    n = [0]

    def live(**kw):
        n[0] += 1
        return r.stat(seen=n[0] * 3, forwarded=n[0], replaced=n[0] * 2, native6=n[0] // 2, resets=n[0] // 7, blend=(n[0] % 9) / 8,
                      head=n[0] / 100, dt=1 / (59 + n[0] % 3), **kw)
    idle = r.stat(installed=0, armed=0)
    r.op("prepare", st=[live()])
    for step in (.001, .01, .02, .0333, .05, .1, .0999, .0001, .2, .3, .0333, .0667, .1, .1, .1, .0166, .0166, .0166, .0166, .0166,
                 .0166, .0166, 1.0, 0, 0, .1, 100.0, .05, .05, 1e-9, .0333):
        r.op(dt=step, st=[live()])
    for at in (.0333, .1333, .13329999999999997, .2333, .23330000000000004, 1.0333, 1.0332999999999999, 1.0333000000000001, 10.0333,
               .0332, .0333, 0, -.0333, -1, -.0667, -.06670000000000001, 5, 5, 4.999, 5.1, 1e9, 1e9 + .05, 1e15, 1e15 + 1, 2 ** 53, 3, 3.1):
        r.op(dt=0, clock=[at], st=[live()])                 # exact grid points, time going backwards, large times
    for at in (.3333, .34, .6333, .64, .7333, .9332999999999999, 1.3333, 2.2333, 5.7333):
        r.op(dt=0, clock=[at], st=[live()])                 # where dividing by .1 and multiplying by 10 round apart
    for value in (NAN, 7.0, INF, 7.01, -INF, 7.02, "raise", 7.03, 7.04, NAN, NAN, 7.2):
        r.op(dt=0, clock=[value], st=[live()])              # a clock without a usable answer: nothing is kept
    for code in (14, 0, 0, 15, 15, 0, 16, 0, 18, 0):       # a gate code: fresh counters, the tick starts over
        r.op(dt=.01, st=[live(error=code)])
    r.op(dt=.01, st=[live(armed=0, error=7), live(), idle]); r.op("prepare", st=[live()])
    r.op("prepare", st=[live()], dt=.001); r.op("prepare", st=[live()], dt=.2)
    r.op("prepare", dt=.001, cfg=[r.conf(body=4)], clock=[20, 20.001, 20.002], st=[live(), live(), live(), live()])
    r.op("stop", clock=[NAN], st=[live(), idle]); r.op("prepare", st=[live()]); r.op("stop", clock=["raise"], st=[live(), live()])
    r.op("prepare", st=[live()]); r.op(st=[live()]); r.op("stop", st=[r.stat(ret=0), live()], errs=[3])
    r.op("prepare", st=[live()]); r.op(st=[live()]); r.op("stop", st=[live(), r.stat(ret=0)], errs=[3])
    # What the status text prints: counters past 2^53, floats without a short decimal form, nan and inf.
    r.op("prepare", st=[live()])
    for seen, replaced, native6, blend in ((0, 0, 0, 0), (1, 2 ** 53, 2 ** 53 + 1, .1), (2, 2 ** 64 - 1, 10 ** 15, 1 / 3), (3, 10 ** 14, 99999999999999, 1e-5),
                                           (4, 123456789012345, 10 ** 16 + 1, NAN), (5, 999999999999999, 2 ** 63, INF), (6, 1, 2, -INF),
                                           (7, 2 ** 32, 2 ** 31, -0.0), (8, 100000000000000, 3, 1e-45), (9, 12345678901234567, 4, 3.4028234e38),
                                           (10, 5, 6, .999999), (11, 5, 6, 1e20), (12, 5, 6, -1.5), (13, 5, 6, 123456.789)):
        r.op(dt=.2, st=[r.stat(seen=seen, forwarded=seen, replaced=replaced, native6=native6, resets=2 ** 64 - 1 - seen, blend=blend,
                               head=blend if blend == blend else 0, dt=-blend if blend == blend else NAN, error=(0, 14)[seen % 2])])
    r.op("stop", st=[r.stat(seen=1, blend=NAN), r.stat(installed=0, armed=0, replaced=2 ** 60, blend=1e-7)])
    return r


def loading():
    """The helper that cannot be loaded yet (a missing DLL), and wrong orders around it."""
    r = Run("loading")
    ok, idle = r.stat(seen=4, replaced=2, blend=.5), r.stat(installed=0, armed=0)
    r.op("stop"); r.op("set_handoff", a=1, b=2); r.op("set_cut_hold", hold=15)
    r.op("preflight", load=6); r.op("prepare", load=6); r.op(load=6); r.op(load=5); r.op("preflight", load=5)
    r.op("preflight", cfg=[r.conf(present=ALL & ~1)], load=6); r.op("preflight", cfg=[r.conf(present=ALL & ~1)])
    r.op("preflight", cfg=[r.conf(present=ALL & ~2)]); r.op("preflight", load=6); r.op(load=6); r.op("stop", st=[ok, idle])
    r.op("preflight"); r.op("prepare"); r.op("preflight"); r.op("set_cut_hold", hold=0); r.op("shutdown", st=[ok, idle])
    r.op("set_handoff", a=1, b=2); r.op("set_cut_hold", hold=3); r.op("shutdown"); r.op(); r.op("prepare"); r.op("preflight")
    return r


def bare(name, **kw):
    """A camera made without a clock, a logger or a snapshot source, or with another profile."""
    r = Run(name, **kw)
    ok, idle = r.stat(seen=4, replaced=2, blend=.5, abi=r.profile.get("abi", 1)), r.stat(installed=0, armed=0, abi=r.profile.get("abi", 1))
    r.op("preflight"); r.op(st=[ok]); r.op(st=[ok]); r.op(dt=.3, st=[r.stat(seen=9, abi=r.profile.get("abi", 1))]); r.op(st=[ok], unit=5)
    r.op("prepare", st=[ok]); r.op("prepare", cfg=[r.conf(body=1)], st=[ok, ok, idle, ok]); r.op(st=[r.stat(abi=1)])
    r.op(st=[r.stat(ret=0)], errs=[3]); r.op(st=[ok]); r.op("prepare", begin=0, gen=0, errs=[2])
    r.op(st=[r.stat(abi=r.profile.get("abi", 1), armed=0, error=9), ok, idle]); r.op(st=[ok]); r.op("set_handoff", a=5, b=6)
    r.op("stop", st=[ok, idle]); r.op(st=[ok], hand=0, cut=0, errs=[4]); r.op("set_cut_hold", hold=0); r.op("stop", st=[ok, idle]); r.op(st=[ok])
    r.op("shutdown", st=[ok, idle])
    return r


def walk(seed, count, calm):
    """Seeded operations. calm: the helper answers a generation as the real one does (`follow`) and the
    world mostly behaves, so the run spends its time armed; otherwise every answer is drawn freely."""
    rng = random.Random(seed)
    r = Run(("calm" if calm else "walk") + str(seed), start=1000.0 * seed)
    identity = dict(body=0x40013, avatar=0x40012, camera_unit=0x40777)
    unit, frames, away_until, off_until, gate_until = 0x40abc, 0, 0.0, 0.0, 0.0
    for step in range(count):
        dt = rng.choice((.004, .0083, .0167, .0167, .0333, .05)) if rng.random() < .995 else rng.choice((-.5, 2.0, .3))
        frames += 1
        roll = rng.random()
        if calm:
            if roll < .003: identity = dict(body=rng.choice((0x40013, 0x40020, 0x40021)), avatar=rng.choice((0x40012, 0x40030)),
                                            camera_unit=rng.choice((0x40777, 0x40778)))
            elif roll < .006: away_until = r.t + rng.choice((.05, .4, 1.5))
            elif roll < .008: off_until = r.t + rng.choice((.1, .6))
            elif roll < .02: unit = rng.choice((0x40abc, 0x40abd, 0x40abe, 0))
            elif roll < .03: gate_until = r.t + rng.choice((.02, .1))
            away, off = r.t + dt < away_until, r.t + dt < off_until
            gate = rng.choice((14, 15, 16)) if r.t + dt < gate_until else 0
            disarm = rng.random() < .006
            code = rng.choice((0, 7, 7, 3, 14, 9)) if disarm else gate
            live = r.stat(seen=frames, forwarded=frames // 5, replaced=frames - frames // 5, native6=frames // 9, resets=frames // 400,
                          blend=min(1, (frames % 240) / 8), head=.1, dt=abs(dt), armed=0 if disarm else 1, error=code,
                          ret=1 if rng.random() > .004 else 0)
            call = rng.choices(CALLS, weights=(93, 2.5, .3, .8, 0, 1, 1.2, 1.2))[0]
            config = r.conf(kind=12) if away and rng.random() < .5 else r.conf(ok=0, why=rng.choice((0, 3))) if away else \
                r.conf(kind=rng.choice((4, 4, 6)), **identity)
            r.op(call, dt=dt, follow=rng.random() > .004, enabled=not off, mode=1 if rng.random() > .003 else 2, cfg=[config],
                 snap=2 if rng.random() > .02 else rng.choice((0, 1, 3)), unit=unit, a=rng.choice((0, 0, 25, 600)), b=rng.choice((0, 40)),
                 hold=rng.randrange(16), validate=rng.random() > .1, begin=1 if rng.random() > .04 else rng.choice((0, 2)),
                 gen=rng.randrange(1, 9), ends=[1 if rng.random() > .06 else 0], st=[live], errs=[rng.choice((13, 2, 3, 7))],
                 pres=rng.random() > .02, hand=rng.random() > .05, cut=rng.random() > .05, key=rng.random() > .05,
                 clock=[r.t + dt, r.t + dt + 1e-4, r.t + dt + 2e-4, r.t + dt + 3e-4], load=6 if rng.random() < .002 else 0)
        else:
            def answer():
                return r.stat(ret=rng.choice((1, 1, 1, 1, 1, 0, 2)), fill=rng.random() > .05, size=rng.choice((80,) * 12 + (0, 88)),
                              abi=rng.choice((1,) * 12 + (0, 2)), installed=rng.choice((1, 1, 0, 5)), armed=rng.choice((1, 1, 1, 1, 0, 2)),
                              error=rng.choice((0,) * 8 + (3, 7, 13, 14, 15, 16, 18, 4294967295)), seen=rng.getrandbits(rng.choice((8, 40, 64))),
                              forwarded=rng.getrandbits(20), replaced=rng.getrandbits(rng.choice((8, 54, 64))),
                              native6=rng.getrandbits(rng.choice((8, 50))), resets=rng.getrandbits(6),
                              blend=rng.choice((0, 1, .5, rng.random(), NAN, -INF)), head=rng.random() * 3, dt=rng.random() / 30)
            fail = rng.choice((r.conf(ok=0), r.conf(ok=0, why=rng.randrange(1, 6)), r.conf(ok=2, why=rng.randrange(1, 6)), r.conf(kind=rng.choice((0, 5, 12))),
                               r.conf(present=ALL & ~(1 << rng.randrange(14)))))
            good = lambda: r.conf(kind=rng.choice((4, 6)), body=rng.choice((1, 1, 1, 2)), camera=rng.choice((7, 7, 7, 8)), pres=rng.random() > .1)
            pick = lambda: fail if rng.random() < .08 else good()
            call = rng.choices(CALLS, weights=(70, 8, 2, 5, 1 if step > count * .9 else 0, 5, 5, 5))[0]   # shutdown ends a session: late only
            now = lambda: rng.choice((r.t + dt, r.t + dt, r.t + dt, r.t + dt + rng.random() * .2, r.t - rng.random(), NAN, INF, "raise"))
            r.op(call, dt=dt, follow=rng.random() < .5, enabled=rng.random() > .06, mode=rng.choice((1,) * 12 + (0, 2)), cfg=[pick(), pick()],
                 snap=rng.choice((2, 2, 2, 2, 0, 1, 3, 4)), unit=rng.choice((1, 2, 2, 0, -1, 1.5, 4294967295, 4294967296, NAN)),
                 a=rng.choice((0, 30, 500, 501, -1, NAN, .5)), b=rng.choice((0, 0, 40, 600, INF)), hold=rng.randrange(16),
                 validate=rng.choice((1, 1, 0, 2)), begin=rng.choice((1,) * 8 + (0, 2, 3)), gen=rng.choice((0, 1, 2, 3, 3, 4, 2 ** 40, 2 ** 53 - 1)),
                 ends=[rng.choice((1, 1, 1, 0, 2)) for _ in range(3)], st=[answer() for _ in range(rng.choice((1, 1, 2, 6)))],
                 errs=[rng.choice((0, 1, 2, 3, 7, 13, 17, 4294967295)) for _ in range(4)], pres=rng.choice((1,) * 9 + (0,)),
                 hand=rng.choice((1,) * 6 + (0, 2)), cut=rng.choice((1,) * 6 + (0, 2)), key=rng.choice((1,) * 6 + (0, 2)),
                 clock=[now() for _ in range(4)], load=rng.choice((0,) * 30 + (5, 6)))
    r.op("shutdown", follow=calm)
    r.op("shutdown", follow=calm, ends=[1], st=[r.stat(), r.stat(installed=0, armed=0)])
    return r


def all_runs():
    return ([session(), refusals(), settings(), configs(), clocks(), loading(),
             bare("no_clock", clock=False), bare("no_log", log=False), bare("no_snapshot", snapshot=False),
             bare("profile_no_handoff", profile=dict(handoff="none")),
             bare("profile_no_holds", profile=dict(cut_hold=False, eye_hold=False, ads_anchor=False, seated_head=False)),
             bare("profile_some_holds", profile=dict(cut_hold="none", eye_hold=True, ads_anchor=False, seated_head=True,
                                                     handoff=dict(entry_ms=30, rotation_ms=600))),
             bare("profile_eased", profile=dict(abi=2, handoff=dict(entry_ms=30.5, rotation_ms=250), eye_hold=False))]
            + [walk(seed, 1500, False) for seed in (1, 2)] + [walk(seed, 2000, True) for seed in (1, 2, 3)])


def lua(value):
    if isinstance(value, bool): return "true" if value else "false"
    if isinstance(value, U64): return "%dULL" % value
    if isinstance(value, float) and value != value: return "(0/0)"
    if isinstance(value, float) and value in (INF, -INF): return "math.huge" if value > 0 else "-math.huge"
    if isinstance(value, (int, float)): return repr(value)
    if isinstance(value, str): return '"%s"' % value.replace("\\", "\\\\").replace('"', '\\"')
    if isinstance(value, dict): return "{" + ",".join(f"{k}={lua(v)}" for k, v in value.items()) + "}"
    if isinstance(value, list): return "{" + ",".join(lua(v) for v in value) + "}"
    raise TypeError(value)


def script_text(run):
    return ("return {reasons=" + lua(REASONS) + f",clock={lua(run.clock)},log={lua(run.log)},snapshot={lua(run.snapshot)},profile="
            + lua(run.profile) + ",\nstats={\n" + ",\n".join(lua(e) for _, e in run.stats) + "},\nconfigs={\n"
            + ",\n".join(lua(e) for _, e in run.configs) + "},\nops={\n" + ",\n".join(lua(op) for op in run.ops) + "\n}}\n")


def cnum(value):
    value = float(value)
    if value != value: return "NAN"
    if value in (INF, -INF): return "INFINITY" if value > 0 else "-INFINITY"
    return repr(value)


def cstr(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def profile_fields(profile):
    """The run's profile as the C struct, starting from the shipped profile.lua values."""
    p = dict(abi=1, handoff=dict(entry_ms=0, rotation_ms=0), cut_hold=True, eye_hold=True, ads_anchor=True, seated_head=True)
    p.update(profile)
    h = p["handoff"] if isinstance(p["handoff"], dict) else None
    return "{%d,%d,%s,%s,%d,%d,%d,%d}" % (p["abi"], h is not None, cnum(h["entry_ms"] if h else 0), cnum(h["rotation_ms"] if h else 0),
                                          p["cut_hold"] is True, p["eye_hold"] is True, p["ads_anchor"] is True, p["seated_head"] is True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--scripts", type=Path)
    parser.add_argument("--runner", type=Path, default=HERE / "run_camera_adapter.lua")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    source = args.retail / "src/first_person/camera.lua"
    dirty = subprocess.check_output(["git", "-C", str(args.retail), "status", "--porcelain", "--", "src/first_person/camera.lua",
                                     "src/first_person/profile.lua"], text=True).strip()
    if dirty:
        sys.exit(f"Retail camera.lua or profile.lua differ from commit {FROZEN}:\n{dirty}")
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    profile_digest = hashlib.sha256((args.retail / "src/first_person/profile.lua").read_bytes()).hexdigest()
    runs = all_runs()
    if args.scripts:
        args.scripts.mkdir(parents=True, exist_ok=True)
        for run in runs:
            (args.scripts / f"{run.name}.lua").write_text(script_text(run))
        print(f"{len(runs)} scripts written to {args.scripts}")
        return 0
    if not args.out:
        sys.exit("--out or --scripts is required")
    out = [f"/* Generated by tools/retail_ref/gen_camera_adapter.py. Do not edit.\n * Retail file: src/first_person/camera.lua at commit "
           f"{commit}, sha256 {digest};\n * with src/first_person/profile.lua, sha256 {profile_digest}.\n"
           f" * Each run starts a fresh camera; every operation carries what the scripted helper, backend,\n"
           f" * controller and clock answer during it, and what the retail file did and left behind. */",
           f'#define CAMERA_ADAPTER_VECTOR_COMMIT "{commit}"', f'#define CAMERA_ADAPTER_VECTOR_SHA256 "{digest}"',
           "static const char *const camera_adapter_reasons[] = {\"\", " + ", ".join(cstr(r) for r in REASONS) + "};",
           "enum { CAV_UPDATE, CAV_PREPARE, CAV_PREARM, CAV_STOP, CAV_SHUTDOWN, CAV_PREFLIGHT, CAV_SET_HANDOFF, CAV_SET_CUT_HOLD };",
           "typedef struct CameraAdapterRun { const char *name; int clock, log, snapshot;",
           "  struct { unsigned abi; int has_handoff; double entry_ms, rotation_ms; int cut_hold, eye_hold, ads_anchor, seated_head; } profile; } CameraAdapterRun;",
           "typedef struct CameraAdapterStatsAnswer { unsigned ret; int fill; unsigned size, abi, installed, armed, error;",
           "  unsigned long long seen, forwarded, replaced, native6, resets; double blend, head, dt; } CameraAdapterStatsAnswer;",
           "typedef struct CameraAdapterConfigAnswer { int ok, why; unsigned present; unsigned long long game, exe, context, camera, avatar_owner;",
           "  unsigned avatar_index, avatar, body, camera_unit; double forward, up, smoothing_ms, blend_ms; unsigned max_gap_ms, kind; int pres; } CameraAdapterConfigAnswer;",
           "typedef struct CameraAdapterVector {",
           "  int run, call; double a, b; int hold, enabled, mode, cfg[2], snap; double unit, clock[4]; int load, follow;",
           "  unsigned validate, begin; unsigned long long gen; unsigned ends[3]; int st[6]; unsigned errs[4], pres, hand, cut, key;",
           "  /* what retail answered and left behind */",
           "  int result, has_text; const char *text; int clean, phase, closing, has_stats; double counters[8]; int installed, armed; unsigned error;",
           "  int has_fault; const char *fault; int has_native_error; unsigned native_error; int failure_kind; const char *status;",
           "  int handle; unsigned long long generation; int has_weapon_key; double weapon_key; int has_handoff; double entry_ms, rotation_ms;",
           "  int has_cut_hold; unsigned cut_hold; int has_handoff_issue; const char *handoff_issue; int has_cut_hold_issue; const char *cut_hold_issue;",
           "  int has_bound; unsigned long long bound[9]; int loaded, has_cache, has_at; double stats_at, stats_due; int has_last_code; unsigned last_code;",
           "  const char *trace;", "} CameraAdapterVector;"]
    run_rows, stat_rows, config_rows, vector_rows, blocks = [], [], [], [], {}
    for index, run in enumerate(runs):
        with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
            script.write(script_text(run))
        done = subprocess.run(["luajit", str(args.runner), str(args.retail), script.name], capture_output=True, text=True)
        Path(script.name).unlink()
        if done.returncode:
            sys.exit(f"run_camera_adapter.lua failed in {run.name}:\n{done.stderr}")
        lines = done.stdout.split("\n")[:-1]
        if len(lines) != len(run.ops):
            sys.exit(f"{run.name}: {len(lines)} results for {len(run.ops)} operations")
        run_rows.append("  {%s,%d,%d,%d,%s}," % (cstr(run.name), run.clock, run.log, run.snapshot, profile_fields(run.profile)))
        stat_base, config_base = len(stat_rows), len(config_rows)
        for _, e in run.stats:
            stat_rows.append("  {%d,%d,%d,%d,%d,%d,%sU,%dULL,%dULL,%dULL,%dULL,%dULL,%s,%s,%s}," % (
                e["ret"], e["fill"], e["size"], e["abi"], e["installed"], e["armed"], e["error"], e["seen"], e["forwarded"], e["replaced"],
                e["native6"], e["resets"], cnum(e["blend"]), cnum(e["head"]), cnum(e["dt"])))
        for _, e in run.configs:
            config_rows.append("  {%d,%d,%d,%dULL,%dULL,%dULL,%dULL,%dULL,%dU,%dU,%dU,%dU,%s,%s,%s,%s,%dU,%d,%d}," % (
                e["ok"], e["why"], e["present"], e["game"], e["exe"], e["context"], e["camera"], e["avatar_owner"], e["avatar_index"],
                e["avatar"], e["body"], e["camera_unit"], cnum(e["forward"]), cnum(e["up"]), cnum(e["smoothing_ms"]), cnum(e["blend_ms"]),
                e["max_gap_ms"], e["kind"], e["pres"]))
        for op, line in zip(run.ops, lines):
            f = line.split("\t")
            if len(f) != 46:
                sys.exit(f"{run.name}: a result line with {len(f)} fields")
            # A configuration block the helper was handed is 88 bytes; the trace names it by its number in a table.
            trace = re.sub(r"\(([0-9a-f]{176})\)", lambda m: "(#%d)" % blocks.setdefault(m.group(1), len(blocks)), f[45])
            bound = [0] * 9 if f[37] == "-" else [int(x) for x in f[37].split(",")]
            given = [index, CALLS.index(op["call"]), cnum(op["a"]), cnum(op["b"]), op["hold"], int(op["enabled"]), op["mode"],
                     "{%s}" % ",".join(str(config_base + c - 1) for c in op["cfg"]), op["snap"], cnum(op["unit"]),
                     "{%s}" % ",".join(cnum(NAN if c == "raise" else c) for c in op["clock"]), op["load"], int(op["follow"]),
                     "%dU" % int(op["validate"]), "%dU" % int(op["begin"]), "%dULL" % op["gen"], "{%s}" % ",".join("%dU" % e for e in op["ends"]),
                     "{%s}" % ",".join(str(stat_base + s - 1) for s in op["st"]), "{%s}" % ",".join("%dU" % e for e in op["errs"]),
                     "%dU" % int(op["pres"]), "%dU" % int(op["hand"]), "%dU" % int(op["cut"]), "%dU" % int(op["key"])]
            seen = [f[0], f[1], cstr(f[2]), f[3], f[4], f[5], f[6], "{%s}" % ",".join(cnum(x) for x in f[7:15]), f[15], f[16],
                    "%dU" % int(float(f[17])), f[18], cstr(f[19]), f[20], "%dU" % int(float(f[21])), f[22], cstr(f[23]), f[24], f[25] + "ULL",
                    f[26], cnum(f[27]), f[28], cnum(f[29]), cnum(f[30]), f[31], f[32] + "U", f[33], cstr(f[34]), f[35], cstr(f[36]),
                    int(f[37] != "-"), "{%s}" % ",".join("%dULL" % b for b in bound), f[38], f[39], f[40], cnum(f[41]), cnum(f[42]), f[43],
                    "%dU" % int(float(f[44])), cstr(trace)]
            vector_rows.append("  {" + ",".join(str(x) for x in given + seen) + "},")
        print(f"{run.name}: {len(run.ops)} operations")
    out += ["static const CameraAdapterRun camera_adapter_runs[] = {"] + run_rows + ["};",
            "static const CameraAdapterStatsAnswer camera_adapter_stats[] = {"] + stat_rows + ["};",
            "static const CameraAdapterConfigAnswer camera_adapter_configs[] = {"] + config_rows + ["};",
            "/* Every distinct configuration block handed to Validate or Begin, as bytes; a trace names it #index. */",
            "static const char *const camera_adapter_blocks[] = {"] + [f"  {cstr(b)}," for b in blocks] + ["};",
            "static const CameraAdapterVector camera_adapter_vectors[] = {"] + vector_rows + ["};"]
    (args.out / "camera_adapter_vectors.h").write_text("\n".join(out) + "\n")
    print(f"camera adapter: {len(vector_rows)} operations in {len(runs)} runs, {len(blocks)} configuration blocks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
