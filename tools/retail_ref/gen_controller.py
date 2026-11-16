#!/usr/bin/env python3
"""Cut controller reference vectors from the retail controller.lua (manual
first-person branch), run unmodified under LuaJIT against a scripted backend.

  gen_controller.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
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
REASONS = ["Memory unavailable", "Camera is not owned by local player",
           "Equip a firearm before starting the camera test", "Entity absent",
           "Character is not in the verified on-foot lifecycle", "Photo Mode or another camera owns the view",
           "Pose helper error 3", "Native ADS request still present",
           "Waiting for owned native request to retire: Entity absent", "Already clean", "Native camera exited",
           "Waiting for native ADS camera initialization (evaluator)", "Field of view policy unavailable"]
PHASES = ["idle", "waiting", "running", "cleanup_pending", "disabled"]
CALLS = ["start", "stop", "shutdown", "suspend", "update", "retry_cleanup"]


def snap(ok=True, err=0, identity=1, player=1, aiming=False, active=False, kind=4, safe=True, record=0, reason=0, guard=0):
    return dict(ok=ok, err=err, identity=identity, player=player, aiming=aiming, active=active, kind=kind, safe=safe,
                record=record, reason=reason, guard=guard)


def world(now, call="update", arg=0, **kw):
    w = dict(call=call, arg=arg, now=now, follow=False, snap=snap(), snap2=None, psnap=snap(), hammer=False, prepare=0, enter=0,
             lease_request=7, lease_cleanup_only=False,
             status=dict(ok=True, armed=True, expired=False, tw=1, blend=1, error=0, why=0, retire=False),
             release=0, release_note=0, set_transition=0, packet=dict(ok=False, identity=1, player=1, epoch=1, progress=0.5))
    w.update(kw)
    if w["snap2"] is None:
        w["snap2"] = copy.deepcopy(w["snap"])
    return w


def scripted():
    ops = []
    t = [100.0]

    def step(dt=0.01, **kw):
        t[0] += dt
        ops.append(world(t[0], **kw))

    hip, rig = snap(), snap(kind=6, record=7)
    # start_enters_at_once when the camera is safe; stop_while_running
    step(call="start"); [step(snap=rig) for _ in range(3)]
    step(call="stop")
    # start while unsafe waits; the first attempt comes 0.1 s after the session starts, then 0.25 s apart
    step(call="start", snap=snap(safe=False, reason=5))
    for _ in range(8):
        step(dt=0.02, snap=snap(safe=False, reason=5))
    for _ in range(4):
        step(dt=0.05, prepare=12)                       # safe, pose not ready: attempts 0.25 s apart
    step(dt=0.3)                                        # entry
    [step(snap=rig) for _ in range(3)]
    # swap: mask 2 (unsafe) -> release -> waiting -> safe on another weapon -> steady entry
    step(snap=snap(ok=False, err=2), psnap=snap(kind=6, safe=False))
    [step(snap=snap(ok=False, err=2), psnap=snap(safe=False)) for _ in range(5)]
    [step(dt=0.1, snap=snap(identity=2)) for _ in range(4)]
    [step(snap=snap(identity=2, kind=6, record=7)) for _ in range(3)]
    # ads_and_back: controller active on the lease, the game ends the request, blended re-entry on the same weapon
    [step(snap=snap(identity=2, kind=6, record=7, aiming=True, active=True),
          status=dict(ok=True, armed=True, expired=False, tw=0, blend=0.5, error=0, why=0, retire=False)) for _ in range(4)]
    step(snap=snap(identity=2, kind=4, aiming=True, active=True))
    [step(dt=0.1, snap=snap(identity=2)) for _ in range(4)]
    [step(snap=snap(identity=2, kind=6, record=7)) for _ in range(2)]
    # guard_holds_750ms
    step(snap=snap(identity=2, kind=6, record=7, guard=1, safe=False), psnap=snap(guard=1, safe=False))
    for _ in range(12):
        step(dt=0.08, snap=snap(identity=2))
    # camera_only_weapon_never_leases
    [step(dt=0.1, snap=snap(identity=3), hammer=True) for _ in range(4)]
    [step(dt=0.1, snap=snap(identity=2)) for _ in range(4)]
    step(snap=snap(identity=2, kind=6, record=7))
    step(snap=snap(identity=3, kind=4), hammer=True)   # switching to the hammer retires the lease first
    step(dt=0.3, snap=snap(identity=2))
    # cleanup_pending_retries: release fails twice
    step(snap=snap(identity=2, kind=6, record=7))
    step(snap=snap(ok=False, err=2), psnap=snap(safe=False), release=8)
    step(release=9); step(release=0, release_note=10)
    # snapshot_error_while_running, respawn_while_running
    step(dt=0.3, snap=snap(identity=2)); step(snap=snap(identity=2, kind=6, record=7))
    step(snap=snap(ok=False, err=1), psnap=snap(ok=False, err=1))
    step(dt=0.3, snap=snap(identity=2)); step(snap=snap(identity=2, kind=6, record=7))
    step(snap=snap(identity=4, player=2, kind=6, record=7), psnap=snap(player=2))
    step(dt=0.3, snap=snap(identity=4, player=2), psnap=snap(player=2))
    # suspend_and_resume
    step(call="suspend", arg=13, snap=snap(identity=4, player=2, kind=6, record=7))
    step(dt=0.3, snap=snap(identity=4, player=2), psnap=snap(player=2))
    # helper no longer armed: owner changed (waits) against same owner (stops)
    step(snap=snap(identity=4, player=2, kind=6, record=7), psnap=snap(player=2),
         status=dict(ok=True, armed=False, expired=False, tw=1, blend=1, error=7, why=0, retire=False))
    step(dt=0.3, snap=snap(identity=4, player=2), psnap=snap(player=2))
    step(snap=snap(identity=4, player=2, kind=6, record=7), psnap=snap(player=2),
         status=dict(ok=True, armed=False, expired=False, tw=1, blend=1, error=0, why=0, retire=False))
    # seated, stim, aim held at entry, a refused transition packet, the clock going backwards, shutdown
    step(call="start", snap=snap(identity=5, player=2, safe=False, reason=5), psnap=snap(player=2, safe=False, reason=5))
    [step(dt=0.2, snap=snap(identity=5, player=2, aiming=True), psnap=snap(player=2)) for _ in range(3)]
    step(dt=0.2, snap=snap(identity=5, player=2), psnap=snap(player=2))
    step(snap=snap(identity=5, player=2, kind=6, record=7), psnap=snap(player=2),
         packet=dict(ok=True, identity=5, player=2, epoch=3, progress=0.25))
    step(snap=snap(identity=5, player=2, kind=6, record=7), psnap=snap(player=2),
         packet=dict(ok=True, identity=5, player=2, epoch=3, progress=0.5), set_transition=7)
    step(dt=0.3, snap=snap(identity=5, player=2), psnap=snap(player=2), lease_cleanup_only=True)
    step(dt=0.3, snap=snap(identity=5, player=2), psnap=snap(player=2))
    step(dt=-5.0, snap=snap(identity=5, player=2, kind=6, record=7), psnap=snap(player=2))
    step(call="start"); step(call="start")
    step(call="shutdown", release=8); step(); step(call="start")
    return ops


def edges():
    """Decisions the walks do not reach on their own (found by line coverage in review)."""
    nan, inf = float("nan"), float("inf")
    ops = []
    t = [300.0]

    def step(dt=0.01, **kw):
        t[0] += dt
        ops.append(world(t[0], **kw))

    rig = snap(kind=6, record=7)
    lost = dict(ok=True, armed=False, expired=False, tw=1, blend=1, error=0, why=0, retire=False)
    expired = dict(ok=True, armed=True, expired=True, tw=1, blend=1, error=0, why=0, retire=False)
    silent = dict(ok=False, armed=False, expired=False, tw=1, blend=1, error=0, why=0, retire=False)

    def enter():
        step(dt=0.4)
        step(snap=rig)

    # Start refusals: no player at all; no player with only the full snapshot's text; pose and entry refused without a text.
    step(call="start", snap=snap(ok=False, err=1), psnap=snap(ok=False, err=4))
    step(call="start", snap=snap(ok=False, err=1), psnap=snap(ok=False))
    step(call="start", snap=snap(ok=False), psnap=snap(ok=False))
    step(call="start", prepare=-1)
    step(call="start", enter=-1)
    step(call="start", enter=12)
    # A clock that is not a number: at start (both ways in) and during an update.
    ops.append(world(nan, call="start"))
    ops.append(world(inf, call="start", snap=snap(safe=False)))
    ops.append(world(-inf, call="start", hammer=True))
    step(call="start"); step(snap=rig)
    ops.append(world(nan, snap=rig))
    step(call="start", snap=snap(safe=False), psnap=snap(safe=False))
    ops.append(world(inf))
    # A lease that only needs cleanup: at start (its own first update stops) and on a later entry (it waits).
    step(call="start", lease_cleanup_only=True, snap2=rig)
    step(call="start", snap=snap(safe=False)); step(dt=0.4, lease_cleanup_only=True)
    step(dt=0.4); step(snap=rig)
    # The helper is no longer armed while the first snapshot still looked like ours. The second look decides.
    for label, change in (("stable unavailable", dict(snap2=snap(ok=False, err=1), psnap=snap(ok=False, err=1))),
                          ("other player", dict(snap2=snap(player=2, kind=6, record=7))),
                          ("unsafe", dict(snap2=snap(kind=6, record=7, safe=False))),
                          ("other kind", dict(snap2=snap(kind=10, record=7))),
                          ("fresh invalid, player fine", dict(snap2=snap(ok=False, err=1), psnap=snap(kind=6))),
                          ("fresh invalid, player elsewhere", dict(snap2=snap(ok=False, err=1), psnap=snap(player=2, kind=6))),
                          ("fresh invalid, player unsafe", dict(snap2=snap(ok=False, err=1), psnap=snap(kind=6, safe=False))),
                          ("fresh invalid, player on another camera", dict(snap2=snap(ok=False, err=1), psnap=snap(kind=10))),
                          ("fresh identity", dict(snap2=snap(identity=2, kind=6, record=7))),
                          ("fresh kind 4", dict(snap2=snap(kind=4))),
                          ("fresh record", dict(snap2=snap(kind=6, record=8))),
                          ("fresh record unknown", dict(snap2=snap(kind=6, record=0)))):
        for status in (lost, expired, silent):
            step(snap=rig, snap2=change.get("snap2"), psnap=change.get("psnap", snap()), status=status)
            if ops[-1]["snap2"] is None:
                ops[-1]["snap2"] = copy.deepcopy(rig)
            step(call="start"); step(dt=0.4); step(snap=rig)
    # Same owner on the second look: the session stops, with and without the helper's text; errors 7 and 8 mean the owner retired.
    for status in (lost, expired, dict(silent, why=7), dict(lost, error=7), dict(lost, error=8), dict(lost, error=3),
                   dict(silent, retire=True)):
        step(snap=rig, snap2=copy.deepcopy(rig), status=status)
        step(call="start"); step(dt=0.4); step(snap=rig)
    # A lease whose request is unknown, and a snapshot without a record, still count as ours.
    step(call="stop"); step(call="start", lease_request=0); step(snap=snap(kind=6, record=9)); step(snap=snap(kind=6, record=0))
    # Stop while cleanup is pending, cleanup retried while not enabled, stop again once clean.
    step(call="stop", release=8); step(call="stop", release=8); step(call="retry_cleanup", release=9)
    step(release=8); step(call="retry_cleanup"); step(call="stop"); step(call="retry_cleanup"); step(call="suspend")
    # Suspend with a lease whose release fails, then succeeds.
    step(call="start"); step(snap=rig); step(call="suspend", arg=13, snap=rig, release=8); step(release=0)
    # Shutdown while holding a lease whose release fails first.
    enter()
    step(call="shutdown", snap=rig, release=8); step(call="start"); step(release=9); step(); step(call="stop"); step(call="shutdown")
    return ops


def idle_shutdown():
    ops = [world(50.0, call="update"), world(50.1, call="retry_cleanup"), world(50.2, call="suspend"),
           world(50.3, call="stop", arg=13), world(50.4, call="shutdown"), world(50.5, call="start"), world(50.6)]
    return ops


def walk(seed, count):
    rng = random.Random(seed)
    ops = []
    t = 500.0
    identity, player = 1, 1
    for _ in range(count):
        t += rng.choice((0.004, 0.01, 0.0167, 0.05, 0.11, 0.26, 0.8)) if rng.random() < 0.99 else -rng.random() * 3
        roll = rng.random()
        if roll < 0.02: identity = rng.randint(1, 3)
        if roll > 0.995: player = rng.randint(1, 2)
        kind = rng.choice((4, 4, 4, 6, 6, 6, 10))
        active = rng.random() < 0.15
        full = snap(ok=rng.random() > 0.08, err=rng.randint(0, 4), identity=identity, player=player,
                    aiming=active or rng.random() < 0.05, active=active, kind=kind, safe=rng.random() > 0.15,
                    record=rng.choice((0, 7, 7, 7, 8)), reason=rng.choice((0, 0, 5, 6)), guard=int(rng.random() < 0.03))
        second = copy.deepcopy(full)
        if rng.random() < 0.3:
            second.update(ok=rng.random() > 0.2, kind=rng.choice((4, 6)), identity=rng.choice((identity, identity, 3)),
                          record=rng.choice((0, 7, 8)), safe=rng.random() > 0.2)
        pview = snap(ok=rng.random() > 0.03, err=rng.randint(0, 4), player=rng.choice((player, player, player, 3 - player)),
                     aiming=full["aiming"], active=active, kind=rng.choice((kind, kind, 4, 6)),
                     safe=rng.random() > 0.2, reason=rng.choice((0, 5, 6)), guard=full["guard"])
        call = rng.choices(CALLS, weights=(8, 0.4, 0, 1.5, 88, 2))[0]
        ops.append(world(t, call=call, arg=rng.choice((0, 13)), snap=full, snap2=second, psnap=pview,
                         hammer=rng.random() < 0.04, prepare=rng.choice((0, 0, 0, 0, 12, -1)),
                         enter=rng.choice((0, 0, 0, 0, 0, 12, -1)), lease_request=rng.choice((7, 7, 7, 0)),
                         lease_cleanup_only=rng.random() < 0.03,
                         status=dict(ok=rng.random() > 0.06, armed=rng.random() > 0.08, expired=rng.random() < 0.04,
                                     tw=rng.choice((0, 1, 1)), blend=rng.choice((0.5, 1, 1)),
                                     error=rng.choice((0, 0, 0, 3, 7, 8)), why=rng.choice((0, 7)), retire=rng.random() < 0.3),
                         release=rng.choice((0, 0, 0, 0, 8, 9, -1)), release_note=rng.choice((0, 10, 11)),
                         set_transition=rng.choice((0, 0, 0, 0, 0, 7, -1)),
                         packet=dict(ok=rng.random() < 0.6, identity=rng.choice((identity, identity, 3)),
                                     player=rng.choice((player, player, 3 - player)), epoch=rng.randint(1, 9),
                                     progress=rng.random())))
    return ops


def calm(seed, count):
    """A world that answers the controller the way the game does. `follow` makes the runner (and
    the C test) show the native view exactly while a lease is held; this script adds what a
    player does on top: swaps, stims, respawns, aiming, the guard byte, an occasional glitch."""
    rng = random.Random(seed)
    ops = []
    t = 900.0
    identity, player = 1, 1
    disturbed_until, aim_until = 0.0, 0.0
    for _ in range(count):
        t += rng.choice((0.008, 0.0167, 0.0167, 0.0333))
        roll = rng.random()
        if roll < 0.004:                                   # weapon swap
            identity = identity % 4 + 1
            disturbed_until = t + rng.choice((0.3, 0.8))
        elif roll < 0.007:                                 # stim, dive, vault
            disturbed_until = t + rng.choice((0.05, 0.5, 1.5))
        elif roll < 0.009:                                 # respawn
            player = 3 - player
            disturbed_until = t + 0.4
        elif roll < 0.02:                                  # aim
            aim_until = t + rng.choice((0.1, 0.6, 2.0))
        disturbed = t < disturbed_until
        aiming = t < aim_until and not disturbed
        guard = int(rng.random() < 0.004)
        full = snap(ok=not (disturbed and rng.random() < 0.6), err=rng.choice((2, 4)), identity=identity, player=player,
                    aiming=aiming, active=aiming, safe=not disturbed and not guard,
                    reason=5 if disturbed else 0, guard=guard)
        pview = snap(player=player, aiming=aiming, active=aiming, safe=not disturbed and not guard,
                     reason=5 if disturbed else 0, guard=guard)
        tw = 0 if aiming else 1
        call = rng.choices(CALLS, weights=(3, 0.15, 0, 0.3, 96, 0.55))[0]
        ops.append(world(t, call=call, arg=rng.choice((0, 13)), follow=rng.random() > 0.01, snap=full, psnap=pview,
                         hammer=identity == 4 and rng.random() < 0.9, prepare=0 if rng.random() > 0.05 else 12,
                         enter=0 if rng.random() > 0.03 else 12, lease_request=rng.choice((7, 7, 7, 0)),
                         lease_cleanup_only=rng.random() < 0.004,
                         status=dict(ok=rng.random() > 0.004, armed=rng.random() > 0.004, expired=False, tw=tw,
                                     blend=rng.choice((tw, tw, 0.5)), error=rng.choice((0, 0, 0, 0, 7)), why=0,
                                     retire=rng.random() < 0.5),
                         release=0 if rng.random() > 0.08 else 8, release_note=rng.choice((10, 11)),
                         set_transition=0 if rng.random() > 0.01 else 7,
                         packet=dict(ok=rng.random() < 0.8, identity=identity, player=player, epoch=rng.randint(1, 9),
                                     progress=rng.random())))
    return ops


def cnum(value):
    value = float(value)
    if value != value: return "NAN"
    if value in (float("inf"), float("-inf")): return "INFINITY" if value > 0 else "-INFINITY"
    return repr(value)


def lua(value):
    if isinstance(value, bool): return "true" if value else "false"
    if isinstance(value, float) and value != value: return "(0/0)"
    if isinstance(value, float) and value in (float("inf"), float("-inf")): return "math.huge" if value > 0 else "-math.huge"
    if isinstance(value, (int, float)): return repr(value)
    if isinstance(value, str): return '"%s"' % value
    if isinstance(value, dict): return "{" + ",".join(f"{k}={lua(v)}" for k, v in value.items()) + "}"
    if isinstance(value, list): return "{" + ",".join(lua(v) for v in value) + "}"
    raise TypeError(value)


def c_view(v):
    return "{%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d}" % (v["ok"], v["err"], v["identity"], v["player"], v["aiming"],
        v["active"], v["kind"], v["safe"], v["record"], v["reason"], v["guard"])


def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    source = args.retail / "src/first_person/controller.lua"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    runs = ([("scripted", scripted()), ("edges", edges()), ("idle_shutdown", idle_shutdown())]
            + [(f"walk{seed}", walk(seed, 3000)) for seed in (1, 2)]
            + [(f"calm{seed}", calm(seed, 4000)) for seed in (1, 2, 3)])
    out = [f"/* Generated by tools/retail_ref/gen_controller.py. Do not edit.\n * Retail file: src/first_person/controller.lua at commit "
           f"{commit}, sha256 {digest}.\n * Each run starts a fresh controller; every operation carries what the scripted backend answers\n"
           f" * during it and what the retail controller did. */",
           f'#define CONTROLLER_VECTOR_COMMIT "{commit}"', f'#define CONTROLLER_VECTOR_SHA256 "{digest}"',
           "static const char *const controller_reasons[] = {\"\", " + ", ".join(c_string(r) for r in REASONS) + "};",
           "typedef struct ControllerView { int ok, err, identity, player, aiming, active, kind, safe, record, reason, guard; } ControllerView;",
           "typedef struct ControllerVector {", "  int run, call, arg, follow; double now; ControllerView snap, snap2, psnap;",
           "  int hammer, prepare, enter, lease_request, lease_cleanup_only;",
           "  int status_ok, armed, expired; double tw, blend; int error, status_why, retire;",
           "  int release, release_note, set_transition, packet_ok, packet_identity, packet_player, packet_epoch; double packet_progress;",
           "  int result, phase, enabled, lease, runs, enter_attempts, release_attempts, samples, native_without_aim, reacquisitions, aim_transitions;",
           "  const char *trace, *label;", "} ControllerVector;",
           "enum { CV_START, CV_STOP, CV_SHUTDOWN, CV_SUSPEND, CV_UPDATE, CV_RETRY_CLEANUP };",
           "static const ControllerVector controller_vectors[] = {"]
    total = 0
    for index, (name, ops) in enumerate(runs):
        with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
            script.write("return {reasons=" + lua(REASONS) + ",ops={\n" + ",\n".join(lua(op) for op in ops) + "\n}}\n")
        done = subprocess.run(["luajit", str(HERE / "run_controller.lua"), str(source), script.name], capture_output=True, text=True)
        Path(script.name).unlink()
        if done.returncode:
            sys.exit(f"run_controller.lua failed in {name}:\n{done.stderr}")
        lines = done.stdout.splitlines()
        if len(lines) != len(ops):
            sys.exit(f"{name}: {len(lines)} results for {len(ops)} operations")
        for op, line in zip(ops, lines):
            parts = line.split("|", 12)
            s, p = op["status"], op["packet"]
            fields = [index, CALLS.index(op["call"]), op["arg"], int(op["follow"]), cnum(op["now"]), c_view(op["snap"]), c_view(op["snap2"]),
                      c_view(op["psnap"]), int(op["hammer"]), op["prepare"], op["enter"], op["lease_request"],
                      int(op["lease_cleanup_only"]), int(s["ok"]), int(s["armed"]), int(s["expired"]), repr(float(s["tw"])),
                      repr(float(s["blend"])), s["error"], s["why"], int(s["retire"]), op["release"], op["release_note"],
                      op["set_transition"], int(p["ok"]), p["identity"], p["player"], p["epoch"], repr(float(p["progress"])),
                      parts[0], PHASES.index(parts[1]), parts[2], parts[3], parts[4], parts[5], parts[6], parts[7], parts[8],
                      parts[9], parts[10], c_string(parts[11]), c_string(parts[12])]
            out.append("  {" + ",".join(str(f) for f in fields) + "},")
        total += len(ops)
        print(f"{name}: {len(ops)} operations")
    out.append("};")
    (args.out / "controller_vectors.h").write_text("\n".join(out) + "\n")
    print(f"controller: {total} operations in {len(runs)} runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
