"""Check world-motion invariance and capture ownership gaps, without the game."""
import csv
import importlib.util
import io
import math
from pathlib import Path
import random
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("capture", root/"tools/analyze_native_capture.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


def matrix(angle, scales, origin):
    c, s = math.cos(angle), math.sin(angle)
    return [c*scales[0], s*scales[0], 0, 0,
            -s*scales[1], c*scales[1], 0, 0,
            0, 0, scales[2], 0, *origin, 1]


def point(m, local):
    return [m[12+i]+sum(local[j]*m[j*4+i] for j in range(3)) for i in range(3)]


rng = random.Random(25480438)
for _ in range(1000):
    m = matrix(rng.uniform(-math.pi, math.pi),
               [rng.uniform(.2, 3) for _ in range(3)],
               [rng.uniform(-1000, 1000) for _ in range(3)])
    p = [rng.uniform(-10, 10) for _ in range(3)]
    assert math.dist(p, capture.relative_position(m, point(m, p))) < 1e-11
# Nonorthogonal affine axes must also invert, rather than use a transpose.
m = [2, 0, 0, 0, .4, 3, 0, 0, .3, .2, 4, 0, 1, 2, 3, 1]
assert math.dist((4, 5, 6), capture.relative_position(m, point(m, (4, 5, 6)))) < 1e-12
for bad in ([0]*16, [math.nan]*16):
    try:
        capture.relative_position(bad, (0, 0, 0))
        raise AssertionError("invalid reference accepted")
    except ValueError:
        pass

fields = ["elapsed_ms", "valid", "available", "failure", *capture.IDENTITY]
fields += [f"{name}_{i}" for name in capture.MATRICES for i in range(16)]
fields += [f"body_head{i}" for i in range(3)]


def row(at, angle, origin, request="abcd", valid=1, available=63):
    r = dict.fromkeys(fields, "0")
    r.update(elapsed_ms=str(at), valid=str(valid), available=str(available),
             failure="4" if not valid else "0", enabled="1", kind="6",
             full_ads="0", request=request, body="1", rig_object="fed",
             camera_object="abc", weapon_object="def", camera_parent="fed",
             weapon_parent="fed", weapon_link=str(0x80002))
    camera = matrix(angle, (1, 1, 1), origin)
    for name in capture.MATRICES:
        m = camera.copy()
        if name != "camera_world":
            m[12:15] = point(camera, (.1, .7, -.2))
        for i, value in enumerate(m):
            r[f"{name}_{i}"] = str(value)
    for i, value in enumerate(point(camera, (0, 0, 1))):
        r[f"body_head{i}"] = str(value)
    return r


stream = io.StringIO()
writer = csv.DictWriter(stream, fields, delimiter="\t", lineterminator="\n")
writer.writeheader()
writer.writerows([row(0, 0, (0, 0, 0)), row(50, .5, (30, 40, 50)),
                  row(100, 1, (-90, 40, 200), request="ef00"),
                  row(150, 1, (0, 0, 0), valid=0),
                  row(200, 2, (100, -50, 30), request="ef00"),
                  row(250, 2, (100, -50, 30), request="ef00", available=1),
                  row(300, 2, (100, -50, 30), request="ef00"),
                  row(500, 2, (100, -50, 30), request="ef00")])
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)/"capture.tsv.part"
    path.write_text(stream.getvalue()+"550\t1\t1\n")
    result = capture.analyze(path)
    assert result["rows"] == 9 and result["valid_rows"] == 7
    assert not result["finalized_footer_present"]
    assert result["native_failure_codes"] == {"4": 1}
    assert result["rejected_rows_or_metrics"] == {"malformed_row": 1}
    metric = result["states"]["TFP_hip_or_handoff"]["camera_to_weapon"]
    assert metric["samples"] == 6 and metric["adjacent_steps"] == 1
    assert metric["step_distance"]["maximum"] < 1e-12
    assert len(result["transitions"]) == 1
    assert result["transitions"][0]["changed_identity_fields"] == ["request"]
    path.write_text(stream.getvalue()+"# samples=8 reason=bounded window\n")
    assert capture.analyze(path)["finalized_footer_present"]
    pending = row(0, 0, (300, 450, 10))
    pending.update(camera_parent="0", weapon_parent="bee", weapon_link=str(0x540002))
    for name in capture.MATRICES:
        if name.startswith(("rig", "hand", "weapon_socket")):
            for i in (12, 13, 14):
                pending[f"{name}_{i}"] = "0"
    phased = io.StringIO()
    writer = csv.DictWriter(phased, fields, delimiter="\t", lineterminator="\n")
    writer.writeheader()
    writer.writerows([pending, row(50, .1, (301, 450, 10)),
                      row(100, .2, (302, 450, 10))])
    path.write_text(phased.getvalue())
    attached = capture.analyze(path)
    assert attached["state_samples"] == {"TFP_attachment_pending": 1,
                                         "TFP_hip_or_handoff": 2}
    steady = attached["states"]["TFP_hip_or_handoff"]["camera_to_weapon"]
    assert steady["adjacent_steps"] == 1 and steady["step_distance"]["maximum"] < 1e-12
    assert set(attached["transitions"][0]["changed_identity_fields"]) == {
        "camera_parent", "weapon_parent", "weapon_link"}
print("Native capture analysis: 1001 affine invariance cases and ownership/gap/partial-row checks passed")
