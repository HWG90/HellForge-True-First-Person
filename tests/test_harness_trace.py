"""The harness's judge of the Trace lines, over a recorded log; never a game."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import matrix  # noqa: E402
import trace  # noqa: E402

# The soak of 2026-11-22 (0.6.0-feel.37), 19:46:57: running at 5 m/s (0.165 m a frame at 30 frames a second), the
# view leaves the native view inside a swap. On the frame it leaves the game sets the camera twice, 0.7 ms apart
# (kind 6, then kind 4), so that frame has two Trace rows with the same eye: "step=0.0001", and "step=0.1628" on
# the next frame, which is the body's own travel as on every frame before.
LOG = ROOT / "tests/reference/trace-two-calls-one-frame.log"
# In the recorded log a State line falls between those two rows and the next frame and ends the window there. It
# need not: the binder writes it on its own tick.
LATE = ("State: hold ended", "State: left the native view", "State: swap")


class SameFrame(unittest.TestCase):
    def lines(self):
        return [line for line in LOG.read_text().splitlines() if not any(word in line for word in LATE)]

    def test_two_rows_of_one_frame_are_one_frame(self):
        # Read as two frames, the second "stood still" and the next "moved 0.163 m": a pop of 0.165 m.
        result = matrix.judge(self.lines())
        self.assertTrue(result["from_trace"])
        self.assertLess(result["largest_jump_m"], 0.03)
        for _, rows in trace.windows(self.lines()):
            for a, b in zip(rows, rows[1:]):
                self.assertFalse(0 <= b[0] - a[0] < trace.SAME_FRAME_MS, (a, b))

    def test_the_recorded_log_as_it_is(self):
        self.assertLess(matrix.judge(LOG.read_text().splitlines())["largest_jump_m"], 0.03)

    def test_a_step_that_changes_is_still_a_pop(self):
        lines = self.lines()
        last = [i for i, line in enumerate(lines) if "Trace:" in line][-1]
        # the last frame 0.3 m further on than it was
        lines[last] = lines[last].replace("eye_world=(-22.6380", "eye_world=(-22.9380")
        self.assertIn("-22.9380", lines[last])
        self.assertGreater(matrix.judge(lines)["largest_jump_m"], 0.25)


def row(t, kind, x, step=0.0):
    return f"19:00:00.000 Trace: t={t:.1f} kind={kind} eye_world=({x:.4f} 1.0000 2.0000) step={step:.4f} wanted=({x:.4f} 1.0000 2.0000) correction=0.0000"


class FastFrames(unittest.TestCase):
    """Above 200 frames a second every frame is under 5 ms after the one before. They are still frames."""

    def test_frames_4_ms_apart_with_a_moving_eye_are_all_kept(self):
        lines = ["19:00:00.000 State: swap"] + [row(4.0 * i, 4, 0.02 * i, 0.02) for i in range(50)]
        (_, rows), = trace.windows(lines)
        self.assertEqual(len(rows), 50)
        self.assertEqual([r[0] for r in rows], [4.0 * i for i in range(50)])

    def test_a_pop_at_250_frames_a_second_is_seen(self):
        lines = ["19:00:00.000 State: swap"] + [row(4.0 * i, 4, 0.02 * i + (0.3 if i >= 25 else 0)) for i in range(50)]
        self.assertGreater(matrix.judge(lines)["largest_jump_m"], 0.25)

    def test_a_frame_set_twice_at_250_frames_a_second_is_one_frame(self):
        # the view leaves the native view on frame 25: that frame is set twice, 0.7 ms apart, kind 6 and then kind 4
        # with the same eye, as in the recorded log
        lines = ["19:00:00.000 State: swap"]
        for i in range(50):
            if i == 25:
                lines.append(row(4.0 * i - 0.7, 6, 0.02 * i))
            lines.append(row(4.0 * i, 6 if i < 25 else 4, 0.02 * i))
        (_, rows), = trace.windows(lines)
        self.assertEqual(len(rows), 50)
        self.assertEqual([r[1] for r in rows], [6] * 25 + [4] * 25)  # the later row is the one kept
        self.assertLess(matrix.judge(lines)["largest_jump_m"], 0.03)

    def test_a_third_row_is_measured_from_the_frames_first_row(self):
        # 0, 3 (the same frame again, same eye), 6 ms (the next frame: 6 ms after the first row, though 3 after the second)
        lines = ["19:00:00.000 State: swap", row(0.0, 6, 0.0), row(3.0, 4, 0.0), row(6.0, 4, 0.02), row(12.0, 4, 0.04)]
        (_, rows), = trace.windows(lines)
        self.assertEqual([r[0] for r in rows], [3.0, 6.0, 12.0])

    def test_the_same_kind_with_a_moved_eye_is_another_frame(self):
        lines = ["19:00:00.000 State: swap", row(0.0, 4, 0.0), row(2.0, 4, 0.05), row(4.0, 4, 0.10)]
        (_, rows), = trace.windows(lines)
        self.assertEqual(len(rows), 3)


def full(ms, eye, wanted=None, root=(0.0, 0.0, 0.0), carried=0.0, kind=6, looked=1, taken="-", tick=True, t=None):
    """A Trace line as 0.6.0-feel.41 on writes it: the root, what was looked for and the helper's own clock."""
    wanted = eye if wanted is None else wanted
    stamp = "19:00:%02d.%03d" % (int(ms // 1000), int(ms % 1000))
    line = (f"{stamp} Trace: t={ms if t is None else t:.1f} kind={kind} eye_world=({eye[0]:.5f} {eye[1]:.5f} {eye[2]:.5f}) step=0.0000 "
            f"wanted=({wanted[0]:.5f} {wanted[1]:.5f} {wanted[2]:.5f}) correction={carried:.4f} lease=0x0 pose_armed=1 entry_ease=1.00 "
            f"source={'native' if kind == 6 else 'head'} root=({root[0]:.5f} {root[1]:.5f} {root[2]:.5f}) "
            f"axes=(1.000000 0.000000 0.000000 0.000000 1.000000 0.000000 0.000000 0.000000 1.000000) looked={looked} taken={taken} quiet=0")
    return line + (f" tick={1000000 + ms:.3f}" if tick else "")


HELD = "19:00:00.000 Lock: +30 frames held (+0 in another camera, kind now 6), +0 missed (kind 0, helper error 0), largest kick 0.0 degrees"


def path(steps, start=0.0):
    """Places along x from the steps between them."""
    places = [start]
    for step in steps:
        places.append(places[-1] + step)
    return places


class Measure(unittest.TestCase):
    """How a jump is measured (round 10, 2026-11-24): the change of the presented eye's step as a vector, and what
    the body root itself does, where the wanted eye makes it in this very frame, is the body's own (round 11: in that
    frame only, and one change of the root once)."""

    def judge(self, eyes, roots=None, wanted=None, carried=None, **more):
        lines = ["19:00:00.000 State: swap", HELD]
        for i, x in enumerate(eyes):
            lines.append(full(33.0 * i, (x, 1.0, 2.0), None if wanted is None else (wanted[i], 1.0, 2.0),
                              (0.0, 0.0, 0.0) if roots is None else (roots[i], 0.0, 0.0), 0.0 if carried is None else carried[i], **more))
        return matrix.judge(lines)

    def test_a_step_back_is_a_jump_though_its_length_is_the_same(self):
        # 0.04 m forwards and then 0.04 m back: the lengths say nothing changed, the eye's step changed by 0.08 m
        result = self.judge(path([0.04, 0.04, 0.04, -0.04, -0.04]))
        self.assertAlmostEqual(result["largest_jump_m"], 0.08, places=3)
        self.assertLess(result["by_lengths_m"], 0.001)
        self.assertFalse(result["pass"])

    def test_the_bodys_own_lunge_is_not_the_eyes_jump(self):
        # a climb: the root's step goes 0.03, 0.13, 0.29, 0.38, 0.30, 0.15, 0.02 m and the rig eye is the root a frame late
        steps = [0.01, 0.03, 0.13, 0.29, 0.38, 0.30, 0.15, 0.02, 0.0, 0.0]
        roots = path(steps)
        eyes = [1.0] + [1.0 + x for x in roots[:-1]]
        result = self.judge(eyes, roots)
        self.assertLess(result["largest_jump_m"], 0.03)   # the 0.02 m with which it sets off: under the limit, so the eye's
        self.assertTrue(result["pass"])
        self.assertAlmostEqual(result["body_m"], 0.16, places=3)       # still in view: what the body did
        self.assertAlmostEqual(result["by_lengths_m"], 0.16, places=3)  # the figure written down before round 10
        self.assertTrue(all(p["whose"] == "body" for p in result["pops"]) and result["pops"])

    def test_the_same_change_without_the_root_is_a_jump(self):
        steps = [0.01, 0.03, 0.13, 0.29, 0.38, 0.30, 0.15, 0.02, 0.0, 0.0]
        result = self.judge(path(steps, 1.0))
        self.assertAlmostEqual(result["largest_jump_m"], 0.16, places=3)
        self.assertFalse(result["pass"])

    def test_a_root_checked_under_an_eye_that_goes_on_is_a_jump(self):
        # the head camera in a dive: the root stops, the wanted eye (the head) goes on, the presented eye was slowed
        # with the root. The root made the change; the eye asked for did not.
        roots = path([0.2, 0.2, 0.2, 0.05, 0.05, 0.05])
        wanted = path([0.2] * 6, 1.0)
        eyes = [1.0 + x for x in roots]
        result = self.judge(eyes, roots, wanted, [0.0, 0.0, 0.0, 0.0, 0.15, 0.3, 0.45], kind=4)
        self.assertAlmostEqual(result["largest_jump_m"], 0.15, places=3)
        self.assertEqual(result["pops"][0]["whose"], "filter")

    def test_a_small_change_of_the_root_takes_nothing_off(self):
        # the root's own change is under the limit: 0.07 m of the eye is 0.07 m, not 0.04 m
        roots = path([0.05, 0.05, 0.08, 0.08, 0.08])
        eyes = path([0.05, 0.05, 0.05, 0.12, 0.12], 1.0)
        result = self.judge(eyes, roots)
        self.assertAlmostEqual(result["largest_jump_m"], 0.07, places=3)

    def test_only_what_the_root_did_is_taken_off(self):
        # the root lunges by 0.15 m and the eye, a frame late, by 0.20 m: 0.05 m is the eye's
        roots = path([0.0, 0.0, 0.15, 0.15, 0.15, 0.15])
        result = self.judge(path([0.0, 0.0, 0.0, 0.20, 0.20, 0.20], 1.0), roots)
        self.assertAlmostEqual(result["largest_jump_m"], 0.05, places=3)
        # and by 0.30 m: that is not the root's lunge at all, and nothing is taken off
        result = self.judge(path([0.0, 0.0, 0.0, 0.30, 0.30, 0.30], 1.0), roots)
        self.assertAlmostEqual(result["largest_jump_m"], 0.30, places=3)

    def test_one_lunge_of_the_root_is_taken_off_once(self):
        # Scene C2 of the review of round 10: the body sets off at 0.15 m a frame and the eye takes that; in the next
        # frame the eye lurches a further 0.15 m a frame and eases back, ending 0.30 m ahead of the body. The root's
        # one lunge was taken off the eye's change in its own frame and again in the next: 0.050 m, a pass.
        roots = path([0.0, 0.0, 0.15, 0.15, 0.15, 0.15, 0.15, 0.15])
        eyes = path([0.0, 0.0, 0.15, 0.30, 0.25, 0.20, 0.15, 0.15], 1.0)
        for wanted in (None, [1.0 + x for x in roots]):          # the eye its own wanted one, and the wanted eye with the body
            for ms in (8.0, 16.7, 33.3, 50.0):
                lines = ["19:00:00.000 State: swap", HELD]
                for i, x in enumerate(eyes):
                    lines.append(full(ms * i, (x, 1.0, 2.0), None if wanted is None else (wanted[i], 1.0, 2.0), (roots[i], 0.0, 0.0)))
                result = matrix.judge(lines)
                self.assertAlmostEqual(result["largest_jump_m"], 0.15, places=3, msg=(ms, wanted is None))
                self.assertFalse(result["pass"], (ms, wanted is None))
                self.assertAlmostEqual(result["body_m"], 0.15, places=3)   # the first frame is the body's, and said so

    def test_two_lunges_of_the_root_are_each_taken_off(self):
        # the same eye over a body that does lurch twice: both frames are the body's
        roots = path([0.0, 0.0, 0.15, 0.30, 0.30, 0.30, 0.30])
        result = self.judge([1.0 + x for x in roots], roots)
        self.assertLess(result["largest_jump_m"], 0.001)
        self.assertEqual([p["whose"] for p in result["pops"]], ["body", "body"])

    def test_an_eye_that_goes_with_the_root_a_frame_before_the_rig_eye_is_the_bodys(self):
        # 08:07:04.723, a climb: the root brakes by 0.113 m a frame and the shown eye with it (0.110), in the root's
        # own frame; the rig eye, the root a frame late, makes it in the next, where the shown eye does nothing.
        roots = path([0.3, 0.3, 0.3, 0.2, 0.2, 0.2, 0.2])
        wanted = [0.7] + [1.0 + x for x in roots[:-1]]
        result = self.judge([1.0 + x for x in roots], roots, wanted, [0.0, 0.0, 0.0, 0.0, 0.1, 0.0, 0.0, 0.0])
        self.assertLess(result["largest_jump_m"], 0.001)
        self.assertEqual([(p["whose"], p["after"]) for p in result["pops"]], [("body", 0)])
        # and with the rig eye, a frame after the root: the same lunge, said so
        result = self.judge(wanted, roots)
        self.assertEqual([(p["whose"], p["after"]) for p in result["pops"]], [("body", 1)])
        self.assertIn("a frame after the root, with the wanted eye", matrix.pop_lines(result)[0])

    def test_an_eye_given_the_bodys_lunge_a_frame_after_the_wanted_eye_is_a_jump(self):
        # R2: the head camera; the root and the head (the wanted eye) brake by 0.10 m a frame in one frame, the shown
        # eye goes on for that frame (0.10 m carried) and brakes in the next. Round 10 called that frame the body's.
        roots = path([0.2, 0.2, 0.2, 0.1, 0.1, 0.1, 0.1])
        wanted = [1.0 + x for x in roots]
        eyes = path([0.2, 0.2, 0.2, 0.2, 0.1, 0.1, 0.1], 1.0)
        result = self.judge(eyes, roots, wanted, [0.0, 0.0, 0.0, 0.0, 0.1, 0.1, 0.1, 0.1], kind=4)
        self.assertAlmostEqual(result["largest_jump_m"], 0.10, places=3)
        self.assertEqual([p["whose"] for p in result["pops"]], ["filter"])
        self.assertFalse(result["pass"])

    def test_an_eye_that_makes_the_bodys_lunge_a_frame_before_the_wanted_eye_is_a_jump(self):
        # (a guard: round 10 flagged this already)
        roots = path([0.2, 0.2, 0.2, 0.2, 0.1, 0.1, 0.1])
        eyes = path([0.2, 0.2, 0.2, 0.1, 0.1, 0.1, 0.1], 1.0)
        result = self.judge(eyes, roots, [1.0 + x for x in roots], kind=4)
        self.assertAlmostEqual(result["largest_jump_m"], 0.10, places=3)
        self.assertFalse(result["pass"])

    def test_the_wanted_eye_has_to_make_the_roots_change_in_the_frame_judged(self):
        # the root lunges in one frame; the shown eye makes that change in the next and the wanted eye only in the
        # one after. Round 10 looked for the wanted eye a frame either side of the frame judged, whichever frame the
        # root's change was from, and found it two frames after the root.
        roots = path([0.0, 0.0, 0.0, 0.1, 0.1, 0.1, 0.1, 0.1])
        wanted = path([0.0, 0.0, 0.0, 0.0, 0.0, 0.1, 0.1, 0.1], 1.0)
        eyes = path([0.0, 0.0, 0.0, 0.0, 0.1, 0.1, 0.1, 0.1], 1.0)
        result = self.judge(eyes, roots, wanted)
        self.assertAlmostEqual(result["largest_jump_m"], 0.10, places=3)
        self.assertFalse(result["pass"])

    def test_a_frame_over_the_limit_has_its_own_time_and_tick(self):
        # Frames from before an edge are written with the edge, all under one stamp (four of 08:10:44.575 in soak 24).
        # Each is its own frame: the time is the frame's, and the tick tells two of them apart.
        eyes = path([0.0, 0.0, 0.1, 0.1, 0.0, 0.0, 0.0])
        lines = ["19:00:01.000 State: swap", HELD]
        for i, x in enumerate(eyes):
            t = 33.0 * i - 198.0                                                          # the edge is the seventh frame
            line = full(1000.0 + max(0.0, t), (x, 1.0, 2.0), t=t)                         # six written at 19:00:01.000
            lines.append(line.rsplit(" tick=", 1)[0] + f" tick={1000802.0 + 33.0 * i:.3f}")
        pops = matrix.judge(lines)["pops"]
        self.assertEqual([p["at"] for p in pops], ["19:00:00.901", "19:00:00.967"])
        self.assertEqual([p["written"] for p in pops], ["19:00:01.000", "19:00:01.000"])
        self.assertEqual([p["tick"] for p in pops], [1000901.0, 1000967.0])
        text = matrix.pop_lines({"pops": pops})
        self.assertIn("19:00:00.901 (written 19:00:01.000)", text[0])

    def test_every_frame_over_the_limit_is_listed_with_its_time(self):
        result = self.judge(path([0.0, 0.0, 0.1, 0.1, 0.1, 0.0, 0.0]), carried=[0, 0, 0, 0.1, 0.1, 0.1, 0, 0], looked=1)
        self.assertEqual([p["at"] for p in result["pops"]], ["19:00:00.099", "19:00:00.198"])
        first = result["pops"][0]
        self.assertEqual((first["kind"], first["looked"], first["whose"]), (6, 1, "filter"))
        self.assertAlmostEqual(first["carried"], 0.1, places=3)
        self.assertAlmostEqual(first["shown"], 0.1, places=3)

    def test_the_eyes_own_stop_is_listed_as_its_own_and_still_fails(self):
        result = self.judge(path([0.07, 0.07, 0.07, 0.0, 0.0]))
        self.assertEqual([p["whose"] for p in result["pops"]], ["own"])
        self.assertFalse(result["pass"])

    def test_frames_on_either_side_of_a_state_line_are_neighbours(self):
        # the landing of 08:18:55.525 (soak 24): 0.120 m, with a State line between its frames, was never judged
        eyes = path([0.0, 0.0, 0.1, 0.1])
        lines = ["19:00:00.000 State: swap"] + [full(33.0 * i, (x, 1.0, 2.0)) for i, x in enumerate(eyes)]
        for at in (2, 3, 4):
            cut = lines[:at] + ["19:00:00.050 State: hold ended"] + lines[at:]
            self.assertAlmostEqual(matrix.judge(cut)["largest_jump_m"], 0.1, places=3, msg=at)

    def test_frames_before_the_first_state_line_are_judged(self):
        # a soak's stretch of the log begins where the action began, not at a State line
        eyes = path([0.0, 0.0, 0.1, 0.1])
        lines = [full(33.0 * i, (x, 1.0, 2.0)) for i, x in enumerate(eyes)]
        result = matrix.judge(lines)
        self.assertTrue(result["from_trace"])
        self.assertAlmostEqual(result["largest_jump_m"], 0.1, places=3)

    def test_a_pause_in_the_trace_is_not_a_step(self):
        lines = ["19:00:00.000 State: swap"] + [full(ms, (x, 1.0, 2.0)) for ms, x in ((0, 0.0), (33, 0.0), (66, 0.0), (700, 3.0), (733, 3.0), (766, 3.0))]
        result = matrix.judge(lines)
        self.assertLess(result["largest_jump_m"], 0.001)
        self.assertEqual(result["judged"], 2)

    def test_nothing_traced_says_so(self):
        result = matrix.judge([HELD])
        self.assertFalse(result["from_trace"])
        self.assertEqual(result["judged"], 0)
        self.assertEqual(matrix.describe(result), "not traced")


def viewed(ms, eye, view, **more):
    """A Trace line as 0.6.0-feel.49 on writes it: with which way the view pointed."""
    return full(ms, eye, **more) + f" off_foot=0 damped=0 damp=(0.00000 0.00000 0.00000) view=({view[0]:.5f} {view[1]:.5f} {view[2]:.5f})"


class AlongTheView(unittest.TestCase):
    """Round 13: the judge says how much of a flagged change lies along view= and how much across it. Printed with the
    frame; nothing is excused for it. The melee of 10:17:00 in the Stable soak of 2026-11-24: the body still, the view
    15.5 degrees up, the eye -0.254, -0.170, 0, 0, +0.711, +0.277 m along the view in six frames at 33.3 ms."""
    VIEW = (0.0, 0.96363, 0.26724)
    MELEE = (-0.254, -0.170, 0.0, 0.0, 0.711, 0.277)

    def lines(self, steps, way, dt=33.3, carried=0.0, view=True):
        out, place = ["19:00:00.000 State: swap", HELD], [0.0, 1.0, 2.0]
        for i, step in enumerate([0.0, 0.0] + list(steps) + [0.0, 0.0]):
            place = [p + step * w for p, w in zip(place, way)]
            wanted = [p + carried * w for p, w in zip(place, way)]
            out.append(viewed(dt * i, place, self.VIEW, wanted=wanted, carried=carried, looked=0) if view else full(dt * i, place, wanted=wanted, carried=carried, looked=0))
        return out

    def test_the_melee_is_flagged_and_named_along_the_view_at_every_frame_time(self):
        for dt in (8.0, 16.7, 33.3, 50.0):
            steps = [s * dt / 33.3 * (4.0 if dt < 10 else 1.0) for s in self.MELEE]   # at 8 ms the same speeds are under the limit: four times as fast
            result = matrix.judge(self.lines(steps, self.VIEW, dt))
            self.assertFalse(result["pass"], dt)
            pops = result["pops"]
            self.assertTrue(pops, dt)
            for p in pops:
                self.assertEqual(p["whose"], "own")
                self.assertAlmostEqual(p["along"], p["beyond"], places=3)
                self.assertLess(p["across"], 0.01)
            text = matrix.pop_lines(result)
            self.assertTrue(all("the eye's own, along the view" in t and "(along the view " in t and ", across 0.00" in t for t in text), text)
            self.assertEqual(matrix.whose_counts(pops)["along"], len(pops))

    def test_the_same_sideways_is_flagged_and_not_so_named(self):
        side = (1.0, 0.0, 0.0)
        result = matrix.judge(self.lines(self.MELEE, side))
        self.assertFalse(result["pass"])
        for p in result["pops"]:
            self.assertLess(p["along"], 0.01)
            self.assertAlmostEqual(p["across"], p["beyond"], places=3)
        text = matrix.pop_lines(result)
        self.assertTrue(all("the eye's own (nothing carried)" in t and "along the view 0.00" in t for t in text), text)
        self.assertEqual(matrix.whose_counts(result["pops"])["along"], 0)

    def test_with_something_carried_it_is_the_filters_as_before(self):
        result = matrix.judge(self.lines(self.MELEE, self.VIEW, carried=0.05))
        self.assertTrue(result["pops"])
        self.assertTrue(all(p["whose"] == "filter" for p in result["pops"]))
        self.assertTrue(all("the filter's" in t and "(along the view " in t for t in matrix.pop_lines(result)))
        counts = matrix.whose_counts(result["pops"])
        self.assertEqual((counts["filter"], counts["own"]), (len(result["pops"]), 0))

    def test_without_view_the_line_is_as_it_was(self):
        result = matrix.judge(self.lines(self.MELEE, self.VIEW, view=False))
        self.assertTrue(result["pops"])
        for p, text in zip(result["pops"], matrix.pop_lines(result)):
            self.assertIsNone(p["along"])
            self.assertNotIn("along", text)
            self.assertTrue(text.endswith(f"beyond the body {p['beyond']:.3f}: the eye's own (nothing carried)"), text)

    def test_the_figures_judged_are_the_same_with_and_without_view(self):
        with_view, without = matrix.judge(self.lines(self.MELEE, self.VIEW)), matrix.judge(self.lines(self.MELEE, self.VIEW, view=False))
        keep = lambda r: [(p["tick"], round(p["shown"], 6), round(p["beyond"], 6), p["whose"]) for p in r["pops"]]
        self.assertEqual(keep(with_view), keep(without))
        self.assertEqual(with_view["largest_jump_m"], without["largest_jump_m"])

    def test_the_bodys_lunge_is_taken_off_before_the_split(self):
        # the root lunges 0.1 m sideways and the eye goes with it and 0.04 m along the view besides: what is split is the 0.04
        lines = ["19:00:00.000 State: swap", HELD]
        eye, root = [0.0, 1.0, 2.0], [0.0, 0.0, 0.0]
        for i, (side, along) in enumerate([(0, 0), (0, 0), (0, 0), (0.1, 0.04), (0.1, 0.04), (0.1, 0.04)]):
            root = [root[0] + side, root[1], root[2]]
            eye = [eye[0] + side, eye[1] + along * self.VIEW[1], eye[2] + along * self.VIEW[2]]
            lines.append(viewed(33.3 * i, eye, self.VIEW, root=root, looked=0))
        pops = matrix.judge(lines)["pops"]
        first = pops[0]
        self.assertEqual(first["whose"], "body")
        self.assertAlmostEqual(first["beyond"], 0.04, places=3)
        self.assertAlmostEqual(first["along"], 0.04, places=3)
        self.assertLess(first["across"], 0.001)
        self.assertIn("beyond the body 0.040 (along the view 0.040, across 0.000): the body's own, not counted", matrix.pop_lines({"pops": pops})[0])


class CameraStyle(unittest.TestCase):
    """Round 13: the style a soak ran in is the log's ("Camera style:" lines), never a file's name. The two feel.49
    soaks of 2026-11-24 were named the wrong way round."""
    LOG = ["10:01:21.333 Camera style: Bodycam (the view moves with the head: the kick sliders and camera damping apply)",
           "10:02:38.000 State: entered the native view", HELD,
           "10:19:44.016 Saved: locked view options",
           "10:19:44.878 Camera style: Stable (no view kick on foot; a thrown body sways the view at half the thrown strength)",
           "10:19:51.000 State: swap"]

    def test_the_style_in_force_is_the_last_line_before(self):
        self.assertEqual(matrix.camera_styles(self.LOG), [("10:01:21.333", "Bodycam"), ("10:19:44.878", "Stable")])
        self.assertEqual(matrix.style_in_force(self.LOG[:3]), "Bodycam")
        self.assertEqual(matrix.style_in_force(self.LOG), "Stable")
        self.assertIsNone(matrix.style_in_force(self.LOG[1:3]))

    def test_the_header_and_a_change_inside_a_run(self):
        self.assertEqual(matrix.style_header(self.LOG[:3]), "camera style: Bodycam (the log's line of 10:01:21.333)")
        self.assertEqual(matrix.style_header([HELD]), "camera style: not in the log (a build before the camera styles, or the log's start is missing)")
        self.assertEqual(matrix.style_changes(self.LOG[3:]), ["camera style from here: Stable (the log's line of 10:19:44.878)"])
        self.assertEqual(matrix.style_changes(self.LOG[1:3]), [])

    def test_the_summary_names_every_style_the_run_had(self):
        self.assertEqual(matrix.style_summary("Bodycam", self.LOG[1:3]), "camera style: Bodycam throughout")
        self.assertEqual(matrix.style_summary("Bodycam", self.LOG[1:]), "camera style: Bodycam, then Stable from 10:19:44.878: NOT ONE STYLE, judge the stretches apart")
        self.assertEqual(matrix.style_summary(None, self.LOG[1:3]), "camera style: not in the log")
        self.assertEqual(matrix.style_summary("Stable", ["11:00:00.000 Camera style: Stable (again)"]), "camera style: Stable throughout")

    def test_a_whole_log_is_judged_style_by_style(self):
        import soak
        eyes = path([0.0, 0.0, 0.1, 0.1, 0.0, 0.0])
        def stretch(second, style):
            out = [f"19:00:{second:02d}.000 Camera style: {style} (words)", HELD]
            return out + [viewed(1000.0 * second + 33.0 * i, (0.0, 1.0 + x, 2.0), (0.0, 1.0, 0.0), looked=0) for i, x in enumerate(eyes)]
        text = soak.judge_log(stretch(1, "Bodycam") + stretch(5, "Stable") + [viewed(9000.0, (5.0, 1.0, 2.0), (0.0, 1.0, 0.0))])
        self.assertEqual(text[0], "camera style: Bodycam (the log's line of 19:00:01.000), 30 frames held, 0 missed: 2 frames flagged (2 the eye's own, 2 of them along the view; 0 the filter's), largest 0.100 m; 0 the veil's, not counted; 0 the body's own, not counted")
        self.assertEqual(text[1], "camera style: Stable (the log's line of 19:00:05.000), 30 frames held, 0 missed: 2 frames flagged (2 the eye's own, 2 of them along the view; 0 the filter's), largest 0.100 m; 0 the veil's, not counted; 0 the body's own, not counted")
        self.assertEqual(len(text), 2)


def drawn(ms, eye, view, veil=None, kind=6, **more):
    """A Trace line as round 19 on writes it: what the arm drive drew, and veil= last (the helmet veil's move of the
    eye handed to the game, or 0)."""
    tail = "0" if veil is None else f"({veil[0]:.5f} {veil[1]:.5f} {veil[2]:.5f})"
    return viewed(ms, eye, view, kind=kind, **more) + f" sock=0.00000 arms=0 eye_edge=0 veil={tail}"


class Veil(unittest.TestCase):
    """Round 19: the helmet veil (src/camera_helper.c veil_apply) moves the eye handed to the game after the Trace row is
    written; round 18 found it moving the shown eye up to 0.32 m forward for one or two frames at most leaves of the
    native view, and no judge saw it. Now the row says the move (veil=), and the judge measures the shown eye, eye +
    veil. A frame flagged only for the veil is "the veil's", counted apart from the eye's own. A row without veil=
    (before round 19) or with veil=0 reads as before."""
    VIEW = (0.0, 1.0, 0.0)
    MOVE = (0.0, 0.32, 0.0)   # the LAS-5 Scythe's held eye 0.22 m behind the head, moved to 0.10 m in front

    def leave(self, dt, veiled=(1, 2), walk=0.0, own=None):
        """Eight native frames, then the head camera; the eye goes on at `walk` metres a frame along y, the veil moves
        the head-camera frames numbered in `veiled` (1: the first). `own`: (frame, metres) an eye's own jump along x."""
        out = ["19:00:00.000 State: left the native view", HELD]
        for i in range(16):
            eye = [0.0, 1.0 + walk * i, 2.0]
            if own and i >= own[0]:
                eye[0] += own[1]
            head = i - 7
            out.append(drawn(dt * i, eye, self.VIEW, veil=self.MOVE if head in veiled else None, kind=4 if head >= 1 else 6,
                             wanted=eye, root=(0.0, walk * i, 0.0), looked=0))
        return out

    def test_the_veils_frames_are_flagged_as_the_veils_at_every_frame_time(self):
        for dt in (8.0, 16.7, 33.3, 50.0):
            for walk in (0.0, 0.04):
                result = matrix.judge(self.leave(dt, walk=walk))
                # the veil's frames are printed with their own largest, and neither set the eye's nor fail the leave
                self.assertTrue(result["pass"], (dt, walk))
                self.assertLess(result["largest_jump_m"], 1e-3, (dt, walk))
                self.assertAlmostEqual(result["largest_veil_m"], 0.32, 3)
                self.assertIn("veil 0.320 not counted", matrix.describe(result))
                pops = result["pops"]
                self.assertTrue(pops)
                self.assertTrue(all(p["whose"] == "veil" for p in pops), [(p["whose"], p["beyond"]) for p in pops])
                self.assertTrue(all(abs(p["veil"] - 0.32) < 1e-6 for p in pops))
                counts = matrix.whose_counts(pops)
                self.assertEqual((counts["veil"], counts["own"], counts["filter"]), (len(pops), 0, 0))
                text = matrix.pop_lines(result)
                self.assertTrue(all("the veil's" in t and "veil 0.320" in t for t in text), text)

    def test_one_veiled_frame_is_two_changes_of_step(self):
        # veil_second.txt: only the frame with the body moved: in, and out again
        result = matrix.judge(self.leave(33.3, veiled=(2,)))
        self.assertEqual([round(p["beyond"], 3) for p in result["pops"]], [0.32, 0.64, 0.32])
        self.assertEqual({p["whose"] for p in result["pops"]}, {"veil"})
        self.assertTrue(result["pass"])
        self.assertEqual((round(result["largest_jump_m"], 3), round(result["largest_veil_m"], 3)), (0.0, 0.64))

    def test_the_row_keeps_the_eye_before_the_veil(self):
        rows = trace.frames(self.leave(33.3))
        self.assertEqual([r["veil"] for r in rows[7:11]], [None, list(self.MOVE), list(self.MOVE), None])
        self.assertEqual(rows[8]["eye"], [0.0, 1.0, 2.0])

    def test_without_the_veil_the_figures_are_as_before(self):
        plain = self.leave(33.3, veiled=(), own=(10, 0.2))
        old = [line.split(" sock=")[0] for line in plain]   # a feel.51 line: no sock=, no veil=
        keep = lambda r: [(p["tick"], round(p["shown"], 6), round(p["beyond"], 6), p["whose"]) for p in r["pops"]]
        self.assertEqual(keep(matrix.judge(plain)), keep(matrix.judge(old)))
        self.assertTrue(all(p["whose"] == "own" for p in matrix.judge(plain)["pops"]))
        self.assertTrue(all(p["veil"] == 0 for p in matrix.judge(old)["pops"]))

    def test_an_eyes_own_jump_under_the_veil_is_still_the_eyes_own(self):
        # the eye jumps 0.2 m sideways in the second veiled frame: over the limit without the veil, so not the veil's
        result = matrix.judge(self.leave(33.3, own=(9, 0.2)))
        whose = {p["tick"]: p["whose"] for p in result["pops"]}
        self.assertEqual(whose[1000000 + 33.3 * 9], "own")
        self.assertIn("veil", whose.values())
        # the eye's own jump fails the leave, and the eye's largest is its own, without the veil's move
        self.assertFalse(result["pass"])
        self.assertAlmostEqual(result["largest_jump_m"], 0.2, 3)
        self.assertGreater(result["largest_veil_m"], 0.3)

    def test_a_leave_with_nothing_veiled_says_no_veil(self):
        result = matrix.judge(self.leave(33.3, veiled=()))
        self.assertEqual(result["largest_veil_m"], 0.0)
        self.assertNotIn("veil", matrix.describe(result))


class SeenInTheGame(unittest.TestCase):
    """Two stretches of the Stable soak of 2026-11-24 on 0.6.0-feel.48 (~/.cache/tfp-soak-24.modlog)."""

    def test_the_climb_of_a_vault_is_the_bodys(self):
        # 08:07:04.3 to 05.2: the root rises 1.2 m in eight frames (its step 0.133, 0.288, 0.376, 0.305 m) and the rig
        # eye is the root a frame late. Flagged at 0.135 m by the lengths; the filter added nothing.
        result = matrix.judge((ROOT / "tests/reference/trace-climb.log").read_text().splitlines() + [HELD])
        self.assertTrue(result["pass"])
        self.assertAlmostEqual(result["by_lengths_m"], 0.135, places=3)
        self.assertLess(result["largest_jump_m"], matrix.POP_M)
        self.assertGreater(result["body_m"], 0.13)
        self.assertGreaterEqual(sum(p["whose"] == "body" for p in result["pops"]), 4)

    def test_a_landing_given_to_the_eye_a_frame_late_is_the_filters(self):
        # 08:34:07.181, the head camera, a landing inside a swap: the root brakes by 0.099 m a frame at .148 and the
        # head by 0.321 m; the shown eye stands (0.317 m taken up) and brakes by 0.099 m in the next frame, in which
        # the wanted eye does something else (0.180 m, 0.081 m off the root's). Flagged at 0.092 m by the lengths;
        # round 10 called it the body's, a frame late, with the wanted eye of the frame after that.
        result = matrix.judge((ROOT / "tests/reference/trace-landing-late.log").read_text().splitlines() + [HELD])
        self.assertFalse(result["pass"])
        self.assertAlmostEqual(result["largest_jump_m"], 0.099, places=3)
        self.assertEqual([(p["written"], p["whose"]) for p in result["pops"]], [("08:34:07.181", "filter")])

    def test_a_braking_given_to_the_eye_a_frame_late_is_still_the_filters(self):
        # 08:13:20.317: the sprint ends 330 ms after the native view came back; the root's change is set aside in one
        # frame and the rig eye, going on against the root, is taken up in the next: 0.119 m for a wanted 0.053 m
        result = matrix.judge((ROOT / "tests/reference/trace-braking.log").read_text().splitlines() + [HELD])
        self.assertGreater(result["largest_jump_m"], 0.10)
        self.assertIn("filter", [p["whose"] for p in result["pops"]])
        self.assertFalse(result["pass"])


if __name__ == "__main__":
    unittest.main()
