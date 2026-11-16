"""Summarize copied native pose TSVs; never attaches to or changes the game.

Relative positions retain each reference matrix's scale, so a rotated/scaled
camera or rig cannot manufacture apparent world-space grip motion. Samples are
not synchronized to one native evaluation. Variation is not a jitter verdict.
"""
import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path


CAMERA, BODY, RIG, WEAPON = 2, 4, 8, 32
MATRICES = {
    "camera_world": CAMERA,
    "rig0_world": RIG,
    "rig5_world": RIG,
    "rig50_world": RIG,
    "weapon_world": WEAPON,
    "weapon_parent_world": WEAPON,
    "hand13_world": RIG,
    "hand34_world": RIG,
    "weapon_socket8_world": RIG,
}
PAIRS = {
    "camera_to_rig0": ("camera_world", "rig0_world"),
    "camera_to_branch5": ("camera_world", "rig5_world"),
    "camera_to_rig_camera50": ("camera_world", "rig50_world"),
    "camera_to_weapon": ("camera_world", "weapon_world"),
    "camera_to_socket8": ("camera_world", "weapon_socket8_world"),
    "camera_to_hand13": ("camera_world", "hand13_world"),
    "camera_to_hand34": ("camera_world", "hand34_world"),
    "root0_to_camera50": ("rig0_world", "rig50_world"),
    "root0_to_branch5": ("rig0_world", "rig5_world"),
    "socket8_to_weapon": ("weapon_socket8_world", "weapon_world"),
    "weapon_to_hand13": ("weapon_world", "hand13_world"),
    "weapon_to_hand34": ("weapon_world", "hand34_world"),
}
IDENTITY = ("enabled", "kind", "full_ads", "request", "body", "rig_object",
            "camera_object", "weapon_object", "camera_parent", "weapon_parent",
            "weapon_link")


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2],
            a[0]*b[1]-a[1]*b[0])


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def relative_position(reference, point):
    """Inverse-affine a world point, with Stingray axes at 0/4/8, origin12."""
    if not all(math.isfinite(x) for x in (*reference, *point)):
        raise ValueError("nonfinite matrix")
    if (max(abs(reference[i]) for i in (3, 7, 11)) > .001 or
            abs(reference[15]-1) > .001):
        raise ValueError("non-affine reference")
    a, b, c = (reference[i:i+3] for i in (0, 4, 8))
    determinant = dot(a, cross(b, c))
    if abs(determinant) < 1e-9:
        raise ValueError("singular reference")
    delta = [point[i]-reference[12+i] for i in range(3)]
    result = tuple(dot(delta, v)/determinant
                   for v in (cross(b, c), cross(c, a), cross(a, b)))
    if not all(math.isfinite(x) for x in result):
        raise ValueError("nonfinite relative position")
    return result


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    at = (len(ordered)-1)*fraction
    low, high = math.floor(at), math.ceil(at)
    return ordered[low]+(ordered[high]-ordered[low])*(at-low)


def summarize(values, steps):
    return {
        "samples": len(values), "adjacent_steps": len(steps),
        "minimum_xyz": [min(p[i] for p in values) for i in range(3)],
        "maximum_xyz": [max(p[i] for p in values) for i in range(3)],
        "median_xyz": [percentile([p[i] for p in values], .5) for i in range(3)],
        "step_distance": {"median": percentile(steps, .5),
                          "p95": percentile(steps, .95),
                          "maximum": max(steps, default=None)},
    }


def state(row):
    enabled, kind, ads = (int(row[n]) for n in ("enabled", "kind", "full_ads"))
    if enabled:
        # Native attachment operations are queued. A valid metadata copy can
        # precede their completion and contain a still-cached FPS rig at origin.
        # Keep that phase separate from steady FP; it is not a 500-unit shake.
        needed = CAMERA | RIG | WEAPON
        if kind == 6 and int(row["available"]) & needed == needed:
            rig = int(row["rig_object"], 16)
            if (not rig or int(row["camera_parent"], 16) != rig or
                    int(row["weapon_parent"], 16) != rig or
                    int(row["weapon_link"]) != 0x80002):
                return "TFP_attachment_pending"
        return "TFP_native_ADS" if ads else "TFP_hip_or_handoff"
    return "native_ADS" if ads and kind == 6 else "mod_off_other"


def analyze(path):
    raw = path.read_bytes()
    lines = raw.decode("utf-8", errors="strict").splitlines()
    footer = any(line.startswith("# samples=") for line in lines)
    content = [line for line in lines if line and not line.startswith("#")]
    reader = csv.DictReader(content, delimiter="\t")
    required = {"elapsed_ms", "valid", "available", "failure", *IDENTITY}
    required |= {f"{name}_{i}" for name in MATRICES for i in range(16)}
    required |= {f"body_head{i}" for i in range(3)}
    missing = required-set(reader.fieldnames or ())
    if missing:
        raise ValueError("missing columns: "+", ".join(sorted(missing)))
    groups = defaultdict(lambda: defaultdict(lambda: {"values": [], "steps": []}))
    rejected, failures, counts = Counter(), Counter(), Counter()
    transitions, previous = [], None
    rows = valid_rows = 0
    for row in reader:
        rows += 1
        # An interrupted .part can have a short final row. Never reuse its data.
        if None in row or any(v is None for v in row.values()):
            rejected["malformed_row"] += 1
            previous = None
            continue
        try:
            now = int(row["elapsed_ms"])
            if now < 0:
                raise ValueError("negative time")
            if not int(row["valid"]):
                failures[row["failure"]] += 1
                previous = None
                continue
            available, label = int(row["available"]), state(row)
            key = tuple(row[n] for n in IDENTITY)
            matrices = {name: [float(row[f"{name}_{i}"]) for i in range(16)]
                        for name, bit in MATRICES.items() if available & bit}
            points = {}
            for name, (reference, target) in PAIRS.items():
                if reference not in matrices or target not in matrices:
                    continue
                # weapon_parent_world is not used: an unparented gun has no
                # external attachment sample despite WEAPON being available.
                try:
                    points[name] = relative_position(matrices[reference],
                                                      matrices[target][12:15])
                except ValueError as exc:
                    rejected[str(exc)] += 1
            if available & (CAMERA | BODY) == CAMERA | BODY:
                try:
                    points["camera_to_body_head"] = relative_position(
                        matrices["camera_world"],
                        [float(row[f"body_head{i}"]) for i in range(3)])
                except ValueError as exc:
                    rejected[str(exc)] += 1
        except (ValueError, OverflowError):
            rejected["invalid_numeric_field"] += 1
            previous = None
            continue
        valid_rows += 1
        counts[label] += 1
        close = previous is not None and 0 < now-previous["time"] <= 150
        same = close and previous["key"] == key
        for name, point in points.items():
            metric = groups[label][name]
            metric["values"].append(point)
            if same and name in previous["points"]:
                metric["steps"].append(math.dist(point, previous["points"][name]))
        if close and previous["key"] != key:
            transitions.append({
                "elapsed_ms": now, "gap_ms": now-previous["time"],
                "from": previous["state"], "to": label,
                "changed_identity_fields": [name for i, name in enumerate(IDENTITY)
                                            if previous["key"][i] != key[i]],
                "relative_position_steps": {
                    name: math.dist(point, previous["points"][name])
                    for name, point in points.items() if name in previous["points"]},
            })
        previous = {"key": key, "time": now, "points": points, "state": label}
    return {
        "source": str(path.resolve()), "sha256": hashlib.sha256(raw).hexdigest(),
        "finalized_footer_present": footer, "rows": rows,
        "valid_rows": valid_rows, "native_failure_codes": dict(failures),
        "rejected_rows_or_metrics": dict(rejected), "state_samples": dict(counts),
        "units": "native world units; relative positions preserve reference scale",
        "limits": ["Game-thread copies are not same-native-evaluation proof.",
                   "Pose variation includes authored animation; it is not a jitter verdict.",
                   "Coordinate ranges pool each mode across observed weapons/owners.",
                   "Matching recorded parents does not prove the unlogged camera parent-node or evaluation phase.",
                   "Steps require identical owner/request/state and a 1..150ms gap.",
                   "Transition steps can include native ownership/rig changes.",
                   "Position metrics do not measure rotation or input latency."],
        "states": {label: {name: summarize(m["values"], m["steps"])
                           for name, m in metrics.items()}
                   for label, metrics in groups.items()},
        "transitions": transitions,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(analyze(args.capture), indent=2, allow_nan=False)+"\n"
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end="")
