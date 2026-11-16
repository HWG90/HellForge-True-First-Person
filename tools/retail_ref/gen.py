#!/usr/bin/env python3
"""Cut reference vectors from the retail Lua files, run unmodified under LuaJIT.

Writes tests/vectors/<module>_vectors.h: the script of operations and what the
retail module answered, plus the retail commit and the file's SHA-256, so a C
test can refuse vectors that were cut from something else.

  gen.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import argparse
import hashlib
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
FROZEN = "1d0d453"


def lua(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, str):
        return '"%s"' % value
    if value is None:
        return "nil"
    if isinstance(value, (list, tuple)):
        return "{" + ",".join(lua(v) for v in value) + "}"
    raise TypeError(value)


def lua_script(ops):
    rows = []
    for op in ops:
        fields = [lua(op["op"])] + [f"{k}={lua(v)}" for k, v in op.items() if k != "op" and v is not None]
        rows.append("{" + ",".join(fields) + "}")
    return "return {\n" + ",\n".join(rows) + "\n}\n"


def run(runner, retail_file, ops):
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as script:
        script.write(lua_script(ops))
    out = subprocess.run(["luajit", str(HERE / runner), str(retail_file), script.name],
                         capture_output=True, text=True)
    Path(script.name).unlink()
    if out.returncode:
        sys.exit(f"{runner} failed:\n{out.stderr}")
    lines = out.stdout.splitlines()
    if len(lines) != len(ops):
        sys.exit(f"{runner}: {len(lines)} results for {len(ops)} operations")
    return lines


# ---------------------------------------------------------------- transition
def transition_ops():
    ops = []
    state = dict(player=1, identity=1, active=False, seated=False, shoulder=False, kind=4, action=-1, zoom=-1)

    def update(now, enabled=True, blocked=False, available=True, **change):
        state.update(change)
        ops.append(dict(op="update", now=now, enabled=enabled, blocked=blocked, available=available, **state))

    # The situations a player produces, in order, at 100 frames a second.
    t = 10.0
    update(t)                                        # initial
    for _ in range(3):
        t += 0.01; update(t)
    t += 0.01; update(t, active=True)                # sights in
    for _ in range(10):
        t += 0.01; update(t)
    t += 0.01; update(t, active=False)               # and out again inside the blend
    for _ in range(25):
        t += 0.01; update(t)
    t += 0.01; update(t, shoulder=True)              # shoulder aim
    t += 0.03; update(t, action=2)                   # an action joins the same blend
    ops.append(dict(op="action", action=3, now=t))
    t += 0.09; update(t)                             # past half: the next edge restarts
    t += 0.01; update(t, action=4)
    t += 0.2; update(t)                              # one slow frame: a third of a blend at most
    t += 0.3; update(t)                              # longer than the gap: discarded
    t += 0.01; update(t, identity=2)                 # weapon swap
    ops.append(dict(op="resume", identity=2, player=1, now=t + 0.01))
    ops.append(dict(op="resume", identity=3, player=1, now=t + 0.01))
    ops.append(dict(op="resume", identity=2, player=2, now=t + 0.01))
    t += 0.02; update(t, seated=True)
    t += 0.02; update(t, seated=False, kind=6)       # 4 <-> 6 is not a camera change
    t += 0.02; update(t, kind=10)                    # another camera is
    t += 0.02; update(t, kind=4, zoom=1)
    t += 0.02; update(t, zoom=2)
    t += 0.02; update(t, player=2, identity=5)       # respawn
    t += 0.02; update(t, blocked=True)
    t += 0.02; update(t, enabled=False)
    t += 0.02; update(t, available=False)
    t += 0.02; update(t)
    t -= 1.0; update(t)                              # clock rollback
    ops.append(dict(op="result"))
    ops.append(dict(op="reset"))
    ops.append(dict(op="result"))
    ops.append(dict(op="action", action=1, now=t))
    for active in (True, False):
        for seated in (True, False):
            for b in ((0, 0, 0, 0, 1, 0), (2, 0, 0, 0, 1, 0), (64, 0, 0, 0, 1, 0), (0, 0, 0, 0, 0, 0),
                      (0, 0, 0, 0, 1, 1), (0, 1, 0, 0, 1, 0), (0, 0, 0, 0, 255, 0)):
                ops.append(dict(op="native_ads", active=active, seated=seated, b=list(b)))
    # A long seeded walk through every kind of edge at uneven frame times.
    rng = random.Random(25480438)
    for _ in range(4000):
        roll = rng.random()
        t += rng.choice((0.004, 0.008, 0.0167, 0.0333, 0.07, 0.2, 0.26, 0.6)) if roll < 0.97 else -rng.random()
        if roll < 0.70:
            change = {}
            pick = rng.random()
            if pick < 0.10: change["active"] = not state["active"]
            elif pick < 0.18: change["shoulder"] = not state["shoulder"]
            elif pick < 0.26: change["action"] = rng.choice((-1, 1, 2, 3))
            elif pick < 0.31: change["zoom"] = rng.choice((-1, 0, 1, 2))
            elif pick < 0.35: change["identity"] = rng.randint(1, 4)
            elif pick < 0.37: change["player"] = rng.randint(1, 2)
            elif pick < 0.40: change["seated"] = not state["seated"]
            elif pick < 0.44: change["kind"] = rng.choice((4, 6, 10))
            update(t, enabled=rng.random() > 0.02, blocked=rng.random() < 0.02, available=rng.random() > 0.01, **change)
        elif roll < 0.85:
            ops.append(dict(op="action", action=rng.choice((-1, 1, 2, 3)), now=t))
        elif roll < 0.93:
            ops.append(dict(op="resume", identity=rng.randint(1, 4), player=rng.randint(1, 2), now=t))
        elif roll < 0.98:
            ops.append(dict(op="result"))
        else:
            ops.append(dict(op="reset"))
    return ops


REASONS = ["-", "initial", "weapon", "ads_enter", "ads_leave", "zoom", "shoulder", "action", "seat", "camera", "reacquire"]


def transition_header(ops, lines, stamp):
    out = [stamp, "typedef struct TransitionVector {", "  int op; double now; int enabled, blocked, available;",
           "  int player, identity, active, seated, shoulder, kind, action, zoom; unsigned char b[6];",
           "  int has_packet; unsigned long long epoch; double progress; int ads, reason, merged, continuous; double started;",
           "  int flag;", "} TransitionVector;",
           "enum { TV_UPDATE, TV_ACTION, TV_RESUME, TV_RESULT, TV_RESET, TV_NATIVE_ADS };",
           "static const TransitionVector transition_vectors[] = {"]
    names = {"update": "TV_UPDATE", "action": "TV_ACTION", "resume": "TV_RESUME", "result": "TV_RESULT",
             "reset": "TV_RESET", "native_ads": "TV_NATIVE_ADS"}
    for op, line in zip(ops, lines):
        g = lambda k, d=0: op.get(k, d)
        b = op.get("b", [0] * 6)
        fields = [names[op["op"]], repr(float(g("now", 0.0))), int(g("enabled")), int(g("blocked")), int(g("available")),
                  g("player"), g("identity"), int(g("active")), int(g("seated")), int(g("shoulder")), g("kind"),
                  g("action", -1), g("zoom", -1), "{%s}" % ",".join(map(str, b))]
        if op["op"] == "native_ads":
            fields += [0, 0, 0.0, 0, 0, 0, 0, 0.0, 1 if line == "T" else 0]
        elif line.startswith("P "):
            _, epoch, progress, ads, reason, merged, continuous, started = line.split(" ")
            fields += [1, epoch + "ULL", progress, ads, REASONS.index(reason), REASONS.index(merged), continuous, started, 0]
        else:
            fields += [0, "0ULL", 0.0, 0, 0, 0, 0, 0.0, 0]
        out.append("  {" + ",".join(str(f) for f in fields) + "},")
    out.append("};")
    return "\n".join(out) + "\n"


# ------------------------------------------------------------------ recovery
POLICIES = ["fov", "blur", "camera", "head", "movement"]
PHASES = ["-", "cleanup", "waiting", "waiting_proof", "retrying", "exhausted", "fatal", "unavailable", "healthy",
          "update", "wait"]
CLASSES = ["-", "cleanup", "retry", "unavailable", "fatal"]


def recovery_ops():
    ops = []
    good = [True, True, True, False]
    temp = [True, 3]
    t = 100.0
    add = lambda **op: ops.append(op)
    for name in POLICIES:
        for clean in (True, False):
            for state in (temp, [True, 5], [False, 3], None):
                for proof in (good, [True, True, True, True], [False, True, True, False], None):
                    add(op="classify", name=name, state=state, clean=clean, proof=proof)
    # Three temporary failures of the camera in a row, then success.
    for _ in range(3):
        add(op="failure", name="camera", state=temp, clean=True, proof=good, now=t)
        add(op="blocks_view", now=t)
        add(op="action", name="camera", now=t + 0.1)
        t += 2.5
        add(op="action", name="camera", now=t)
        add(op="needs_proof", name="camera")
        add(op="revalidate", name="camera", proof=good, now=t)
    add(op="success", name="camera", now=t, active=False)
    add(op="success", name="camera", now=t, active=True)
    add(op="success", name="camera", now=t + 2.1, active=True)
    add(op="has_policies")
    # A failure during cleanup, an optional policy, an unknown error, exhaustion.
    add(op="failure", name="fov", state=temp, clean=False, proof=good, now=t)
    add(op="cleanup", name="fov", done=False, now=t)
    add(op="action", name="fov", now=t)
    add(op="cleanup", name="fov", done=True, now=t + 0.1)
    add(op="row", name="fov")
    add(op="failure", name="head", state=[False, 9], clean=True, proof=good, now=t)
    add(op="action", name="head", now=t)
    add(op="failure", name="blur", state=temp, clean=True, proof=[True, False, True, False], now=t)
    add(op="owner_reset")
    add(op="row", name="head")
    add(op="row", name="fov")
    for _ in range(5):
        add(op="failure", name="camera", state=temp, clean=True, proof=good, now=t)
    add(op="blocks_view", now=t)
    add(op="reset")
    add(op="has_policies")
    add(op="failure", name="fov", state=temp, clean=True, proof=good, now=t)
    add(op="action", name="fov", now=t + 1)
    add(op="revalidate", name="fov", proof=[True, False, True, False], now=t + 1)
    add(op="action", name="fov", now=t + 1.1)
    add(op="action", name="fov", now=t + 1.3)
    add(op="revalidate", name="fov", proof=None, now=t + 20)
    add(op="row", name="fov")
    rng = random.Random(26568081)
    proofs = (good, good, good, [True, False, True, False], [True, True, False, False], [True, True, True, True], None)
    states = (temp, temp, temp, [True, 5], [False, 3], None)
    for _ in range(4000):
        t += rng.choice((0.0, 0.01, 0.1, 0.25, 0.3, 0.6, 1.1, 2.2, 11.0))
        name = rng.choice(POLICIES)
        roll = rng.random()
        if roll < 0.18: add(op="failure", name=name, state=rng.choice(states), clean=rng.random() > 0.2, proof=rng.choice(proofs), now=t)
        elif roll < 0.30: add(op="cleanup", name=name, done=rng.random() > 0.3, now=t)
        elif roll < 0.55: add(op="action", name=name, now=t)
        elif roll < 0.62: add(op="needs_proof", name=name)
        elif roll < 0.72: add(op="revalidate", name=name, proof=rng.choice(proofs), now=t)
        elif roll < 0.86: add(op="success", name=name, now=t, active=rng.random() > 0.2)
        elif roll < 0.93: add(op="blocks_view", now=t)
        elif roll < 0.96: add(op="row", name=name)
        elif roll < 0.975: add(op="has_policies")
        elif roll < 0.99: add(op="owner_reset")
        else: add(op="reset")
    return ops


def recovery_header(ops, lines, stamp):
    out = [stamp, "typedef struct RecoveryVector {", "  int op, name, has_state, temporary, native_error, clean, has_proof;",
           "  unsigned char proof[4]; int done, active; double now;",
           "  int flag, phase, attempts, classification, pending, blocked_name, wanted; double next_retry, proof_deadline; int has_row;",
           "} RecoveryVector;",
           "enum { RV_CLASSIFY, RV_FAILURE, RV_CLEANUP, RV_ACTION, RV_NEEDS_PROOF, RV_HAS_POLICIES, RV_REVALIDATE,",
           "       RV_SUCCESS, RV_BLOCKS_VIEW, RV_RESET, RV_OWNER_RESET, RV_ROW };",
           "static const RecoveryVector recovery_vectors[] = {"]
    kinds = ["classify", "failure", "cleanup", "action", "needs_proof", "has_policies", "revalidate", "success",
             "blocks_view", "reset", "owner_reset", "row"]
    for op, line in zip(ops, lines):
        state, proof = op.get("state"), op.get("proof")
        fields = [kinds.index(op["op"]), POLICIES.index(op["name"]) if "name" in op else -1,
                  int(state is not None), int(bool(state and state[0])), state[1] if state else 0,
                  int(bool(op.get("clean"))), int(proof is not None),
                  "{%s}" % ",".join(str(int(v)) for v in (proof or [0, 0, 0, 0])),
                  int(bool(op.get("done"))), int(bool(op.get("active"))), repr(float(op.get("now", 0.0)))]
        flag = phase = attempts = classification = pending = wanted = has_row = 0
        blocked_name = -1
        next_retry = proof_deadline = "-1.0"
        kind = op["op"]
        if kind == "classify":
            classification = CLASSES.index(line)
        elif kind in ("failure", "success", "row"):
            if line != "-":
                p, a, c, pc, nr, pd = line.split(" ")
                has_row, phase, attempts, classification, pending = 1, PHASES.index(p), int(a), CLASSES.index(c), CLASSES.index(pc)
                next_retry, proof_deadline = nr, pd
        elif kind == "action":
            wanted = PHASES.index(line)
        elif kind == "blocks_view":
            b, n, w = line.split(" ")
            flag = int(b == "true")
            blocked_name = POLICIES.index(n) if n != "-" else -1
            wanted = PHASES.index(w) if w != "-" else 0
        elif kind in ("cleanup", "needs_proof", "has_policies", "revalidate"):
            flag = int(line == "true")
        fields += [flag, phase, attempts, classification, pending, blocked_name, wanted, next_retry, proof_deadline, has_row]
        out.append("  {" + ",".join(str(f) for f in fields) + "},")
    out.append("};")
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}. Fold later commits in deliberately.")
    args.out.mkdir(parents=True, exist_ok=True)
    for module, make_ops, runner, header in (("transition", transition_ops, "run_transition.lua", transition_header),
                                             ("recovery", recovery_ops, "run_recovery.lua", recovery_header)):
        source = args.retail / "src/first_person" / f"{module}.lua"
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        ops = make_ops()
        lines = run(runner, source, ops)
        stamp = (f"/* Generated by tools/retail_ref/gen.py. Do not edit.\n * Retail file: src/first_person/{module}.lua at commit "
                 f"{commit}, sha256 {digest}.\n * {len(ops)} operations replayed against that file under LuaJIT. */\n"
                 f'#define {module.upper()}_VECTOR_COMMIT "{commit}"\n#define {module.upper()}_VECTOR_SHA256 "{digest}"')
        (args.out / f"{module}_vectors.h").write_text(header(ops, lines, stamp))
        print(f"{module}: {len(ops)} operations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
