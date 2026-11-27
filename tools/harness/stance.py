"""Round 49: the game's own stance ramp, for tools/harness/matrix.py measure().

Standing up from prone, or going down, the game moves the native view's eye against the body root at one even speed
from its first frame, with no ease in: 0.0534 m a frame, 1.6 m/s at the harness's 30 frames a second (the eye goes
0.26 m back along the body and then 0.35 m up). The base game shows the same in its own first-person aim. By itself
the first frame is a change of step of 0.053 m, under POP_M, and nothing here touches that.

What read 0.066 m (the harness run of 2026-11-27 on feel.59, phase "prone", 19:34:38.419): the stand-up began while
the body was still slowing from a crawl. The rig eye is the root a frame late, in every native frame (the game's own
lag; matrix.measure says so of a lunge), so the frame in which the ramp begins also carries the root's change of step
of the frame before: 0.0122 m there (the root's step 0.0309 then 0.0187 m). The eye's step less the root's step of
the frame before is under 0.005 m in the frames of the crawl, and 0.0534 m, the same vector to 0.0001 m, in that
frame and in each of the four after it (frame times of 31.9 to 34.2 ms): the ramp and nothing else.
The filter did nothing in any of them (kind 6, looked=0, nothing taken, nothing carried, wanted eye = eye shown), and
the Steady line of that second says the mod's own hold of the rig stood still (held at 0.0561 to 0.0561 m, the rig
moved by -0.0794 to -0.0794 m), which a reload's does not.

matrix.measure takes the root's own change of step off the eye's only where that change is POP_M or more ("a marginal
step is not argued down by a few centimetres of the root"). That stays. This names one narrower case, where the mod
can have had no part, and takes the root's change of the frame before off the eye's change in it; what is left is
judged against POP_M as ever. No limit is raised. A frame is the stance ramp's where all of this holds:
  - the frame is "the eye's own" by measure() (the wanted eye makes it, under OWN_M carried) and under CAP_M;
  - in it, the two before and the three after: kind 6, jumps not looked for, nothing taken up but "root", under
    NOTHING_M carried, the wanted eye within NOTHING_M of the eye, no veil;
  - the eye's step less the root's step of the frame before (the eye against the root, a frame late) goes at
    RAMP_M_S, within RAMP_BAND_M_S, in the frame and the three after, and stays within STEADY_M of itself: the
    ramp, begun in this frame and still running;
  - the eye's change less the root's change of the frame before is under the limit given (POP_M);
  - that change of the root is itself under the limit (round 49g; the first version did not hold it there, and on the
    soaks of feel.50 on it named 28 dive launches with a root change of up to 0.069 m). A change of the root of POP_M
    or more is the body's lunge, which measure() takes off by its own rule or not at all.
A step the filter made cannot fall in it (nothing was looked for, taken or carried), and a reload's cannot either: a
reload phase is judged without this class (matrix.judge), because a reload's eye also reads "wanted = shown" and was
the mod's (the reload hold, round 48). The soak judges an action that presses the reload key without it too
(matrix.reloading; soak.judged). The whole log judged in one piece at a soak's end cannot tell a reload's frames, and
is judged with the class: a frame flagged in its own action's stretch stays flagged there."""
import math

RAMP_M_S = 1.6        # the game's stance ramp, eye against root (measured: 0.0534 m a frame at 33.4 ms)
RAMP_BAND_M_S = 0.15  # frame times of 31.9 to 34.2 ms read 1.56 to 1.67 m/s
STEADY_M = 0.006      # the ramp's step stays this near its first frame's (measured: 0.0001 m)
CAP_M = 0.10          # a frame at or over this is never the stance ramp's
NOTHING_M = 0.001     # carried, and wanted eye less eye: nothing
GAP_MS = 80.0         # as matrix.GAP_MS
AFTER = 3             # the frames after the first in which the ramp must still run


def _sub(a, b):
    return [x - y for x, y in zip(a, b)]


def _untouched(row):
    """The filter did nothing in this frame, and it is a native frame nothing else moved."""
    taken = row.get("taken") or "-"
    return (row["kind"] == 6 and row.get("looked") == 0 and taken in ("-", "root") and row["carried"] < NOTHING_M and not row.get("veil")
            and row.get("wanted") and row.get("root") and math.dist(row["wanted"], row["eye"]) < NOTHING_M)


def against_root(rows, k):
    """The eye's step into frame k less the root's step into the frame before: the eye against the root a frame late."""
    return _sub(_sub(rows[k]["eye"], rows[k - 1]["eye"]), _sub(rows[k - 1]["root"], rows[k - 2]["root"]))


def ramp(rows, i, shown, limit):
    """For frame i of `rows` (trace.frames), whose eye changed its step by `shown`: (what is left of that change with
    the root's change of the frame before taken off, that root's change) where the frame is the stance ramp's first
    and what is left is under `limit`; None otherwise."""
    if shown >= CAP_M or i < 3 or i + AFTER >= len(rows):
        return None
    span = rows[i - 3:i + AFTER + 1]
    if any(not 0 < b["tick"] - a["tick"] <= GAP_MS for a, b in zip(span, span[1:])):
        return None
    if not all(_untouched(row) for row in span[1:]) or not span[0].get("root"):
        return None
    first = against_root(rows, i)
    for k in range(i, i + AFTER + 1):
        step = against_root(rows, k)
        speed = math.hypot(*step) * 1000.0 / (rows[k]["tick"] - rows[k - 1]["tick"])
        if abs(speed - RAMP_M_S) > RAMP_BAND_M_S or math.dist(step, first) > STEADY_M:
            return None
    root = _sub(_sub(rows[i - 1]["root"], rows[i - 2]["root"]), _sub(rows[i - 2]["root"], rows[i - 3]["root"]))
    left = math.dist(first, against_root(rows, i - 1))
    return (left, math.hypot(*root)) if left < limit and math.hypot(*root) < limit else None
