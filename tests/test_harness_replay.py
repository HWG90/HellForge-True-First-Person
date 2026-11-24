"""The replay of Trace lines through the continuity filter (tools/harness/replay.py): reading the lines and judging
the steps; never a game, and no compiler."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import replay  # noqa: E402


def line(stamp_ms, t, kind, eye, wanted, carried, root=(0, 0, 0), axes=None, looked=1, taken="-", source="native", tail=""):
    stamp = "22:00:%02d.%03d" % (stamp_ms // 1000, stamp_ms % 1000)
    extra = " axes=(%s)" % " ".join("%.4f" % a for a in axes) if axes else ""
    return ("%s Trace: t=%.1f kind=%d eye_world=(%.4f %.4f %.4f) step=0.0000 wanted=(%.4f %.4f %.4f) correction=%.4f lease=0x0 pose_armed=1 "
            "entry_ease=1.00 source=%s root=(%.4f %.4f %.4f)%s looked=%d taken=%s%s" % (stamp, t, kind, *eye, *wanted, carried, source, *root, extra, looked,
                                                                                       taken, tail))


class NewFields(unittest.TestCase):
    """0.6.0-feel.49 on: off_foot, damped, damp and view follow tick=. The readers take the line as before."""

    def test_a_line_with_the_damping_fields_reads_the_same(self):
        axes = (1, 0, 0, 0, 1, 0, 0, 0, 1)
        plain = [line(33 * i, 33.0 * i, 4, (0.01 * i, 1, 2), (0.01 * i, 1, 2.1), 0.1, axes=axes, source="head", tail=" quiet=0 tick=%.3f" % (5000 + 33.0 * i))
                 for i in range(4)]
        more = [text + " off_foot=1 damped=0 damp=(0.00000 0.00000 0.10000) view=(-0.29552 0.95534 0.00000)" for text in plain]
        self.assertEqual(replay.read(plain), replay.read(more))
        self.assertEqual(len(replay.read(more)), 4)
        self.assertEqual(replay.read(more)[2]["clock"], 5066.0)
        import trace
        self.assertEqual(trace.frames(plain), trace.frames(more))
        self.assertEqual(trace.frames(more)[3]["tick"], 5099.0)


class Frames(unittest.TestCase):
    def test_lines_with_and_without_the_axes(self):
        axes = (1, 0, 0, 0, 1, 0, 0, 0, 1)
        rows = replay.frames(["22:00:00.000 State: one", line(33, 33.0, 6, (1, 2, 3), (1, 2, 3.5), 0.5),
                              line(66, 66.0, 4, (1, 2, 3), (1, 2, 3), 0, axes=axes, source="head", taken="jump+root")])
        self.assertEqual(len(rows), 2)
        self.assertIsNone(rows[0]["axes"])
        self.assertEqual(rows[1]["axes"], list(map(float, axes)))
        self.assertEqual((rows[1]["source"], rows[1]["taken"], rows[0]["carried"]), ("head", "jump+root", 0.5))

    def test_frames_written_late_keep_their_time(self):
        # six frames before the edge are written with it, all at the edge's stamp: their times come from t
        lines = ["22:00:01.000 State: edge"] + [line(1000, -200 + 33.3 * i, 6, (i, 0, 0), (i, 0, 0), 0) for i in range(6)] + [
            line(1034, 0.0, 6, (6, 0, 0), (6, 0, 0), 0)]
        rows = replay.frames(lines)
        gaps = [b["tick"] - a["tick"] for a, b in zip(rows, rows[1:])]
        self.assertEqual(len(rows), 7)
        self.assertTrue(all(30 < gap < 37 for gap in gaps), gaps)

    def test_two_rows_of_one_frame_are_one_frame(self):
        rows = replay.frames(["22:00:00.000 State: one", line(33, 33.0, 6, (1, 0, 0), (1, 0, 0), 0), line(34, 33.7, 4, (1, 0, 0), (1, 0, 0), 0, source="head")])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["kind"], 4)


class Rows(unittest.TestCase):
    AXES = (1, 0, 0, 0, 1, 0, 0, 0, 1)

    def test_every_row_is_a_call_of_the_filter(self):
        # the game set the camera twice in one frame: two calls, one frame
        lines = ["22:00:00.000 State: one", line(33, 33.0, 6, (1, 0, 0), (1, 0, 0), 0), line(34, 33.7, 4, (1, 0, 0), (1, 0, 0), 0, source="head")]
        rows = replay.read(lines)
        self.assertEqual([row["kind"] for row in rows], [6, 4])
        self.assertEqual(len(replay.merge(rows)), 1)

    def test_an_edge_without_a_state_line_starts_the_time_again(self):
        lines = ["22:00:00.000 State: one", line(400, 400.0, 6, (1, 0, 0), (1, 0, 0), 0), line(433, 433.0, 6, (1, 0, 0), (1, 0, 0), 0),
                 line(466, -1.0, 6, (2, 0, 0), (2, 0, 0), 0), line(500, 33.0, 6, (2, 0, 0), (2, 0, 0), 0)]
        rows = replay.read(lines)
        gaps = [b["tick"] - a["tick"] for a, b in zip(rows, rows[1:])]
        self.assertTrue(all(30 < gap < 37 for gap in gaps), gaps)

    def test_the_time_and_full_aim_where_the_line_has_them(self):
        rows = replay.read(["22:00:00.000 State: one", line(33, 33.0, 6, (1, 0, 0), (1, 0, 0), 0, axes=self.AXES, tail=" quiet=1 tick=5000.125"),
                            line(66, 66.0, 6, (1, 0, 0), (1, 0, 0), 0, axes=self.AXES, taken="start", tail=" quiet=0 tick=5033.500")])
        self.assertEqual([(row["quiet"], row["tick"]) for row in rows], [(1, 5000.125), (0, 5033.5)])
        replay.stretches(rows)
        self.assertEqual(rows[1]["fresh"], 1)

    def test_the_state_is_known_from_the_first_row_without_a_correction(self):
        carried = (0.2, 0.1, 0, 0.3, 0.1)
        lines = ["22:00:00.000 State: one"] + [line(33 * i, 33.0 * i, 6, (1, 0, 0), (1, 0, 0), c, axes=self.AXES) for i, c in enumerate(carried)] + [
            "22:00:02.000 State: two", line(2033, 33.0, 6, (1, 0, 0), (1, 0, 0), 0, axes=self.AXES)]
        rows = replay.read(lines)
        replay.stretches(rows)
        self.assertEqual([row["known"] for row in rows], [False, False, True, True, True, True])
        # in the middle of a stretch the replay lets its own correction go; after a pause the filter starts again by itself
        self.assertEqual([row["fresh"] for row in rows], [0, 0, 2, 0, 0, 0])


class Judge(unittest.TestCase):
    def rows(self, eyes, wanted, carried):
        return [{"tick": 33.3 * i, "eye": (e, 0, 0), "wanted": (w, 0, 0), "carried": c, "root": (0, 0, 0), "misfit": 0}
                for i, (e, w, c) in enumerate(zip(eyes, wanted, carried))]

    def test_whose_step_it_is(self):
        # the wanted eye stands and the shown one goes 0.1 m: the filter's
        self.assertEqual([f[0] for f in replay.judge(self.rows((0, 0, .1), (0, 0, 0), (0, 0, .1)))], ["filter"])
        # both go, nothing carried: the game's own
        self.assertEqual([f[0] for f in replay.judge(self.rows((0, 0, .1), (0, 0, .1), (0, 0, 0)))], ["game"])
        # the wanted eye goes 0.3 m, the shown one 0.1 m: reduced
        self.assertEqual([f[0] for f in replay.judge(self.rows((0, 0, .1), (0, 0, .3), (0, 0, .2)))], ["reduced"])
        # under 0.06 m is no step
        self.assertEqual(replay.judge(self.rows((0, 0, .05), (0, 0, 0), (0, 0, .05))), [])

    def test_only_where_the_replay_follows_the_log(self):
        rows = self.rows((0, 0, .1), (0, 0, 0), (0, 0, .1))
        rows[1]["misfit"] = 0.3
        self.assertEqual(replay.judge(rows, known=True), [])
        self.assertEqual(len(replay.judge(rows)), 1)
        # logged axes: "follows" says it (the state known and the replay within 0.005 m)
        rows = self.rows((0, 0, .1), (0, 0, 0), (0, 0, .1))
        for row in rows:
            row["follows"] = True
        self.assertEqual(len(replay.judge(rows, known=True)), 1)
        rows[2]["follows"] = False
        self.assertEqual(replay.judge(rows, known=True), [])

    def test_an_eye_that_turns_round_counts_in_full(self):
        # 0.04 m forwards and then 0.04 m back: the lengths are alike, the step changed by 0.08 m
        rows = self.rows((0, .04, 0), (0, 0, 0), (0, .04, 0))
        self.assertEqual(replay.judge(rows), [])
        found = replay.judge(rows, vector=True)
        self.assertEqual([f[0] for f in found], ["filter"])
        self.assertAlmostEqual(found[0][1], 0.08)
        self.assertAlmostEqual(found[0][2], 0.0)
        # the wanted eye turns round as well and nothing is carried: the game's own, by vectors too
        self.assertEqual([f[0] for f in replay.judge(self.rows((0, .04, 0), (0, .04, 0), (0, 0, 0)), vector=True)], ["game"])
        # a turn to the side: 0.05 m along x and then 0.05 m along y is 0.07 m of change and none by the lengths
        rows = [{"tick": 33.3 * i, "eye": e, "wanted": (0, 0, 0), "carried": .05, "root": (0, 0, 0), "misfit": 0}
                for i, e in enumerate(((0, 0, 0), (.05, 0, 0), (.05, .05, 0)))]
        self.assertEqual(replay.judge(rows), [])
        self.assertAlmostEqual(replay.judge(rows, vector=True)[0][1], 0.05 * 2 ** .5)
        # a step that only grows is the same by both
        rows = self.rows((0, 0, .1), (0, 0, 0), (0, 0, .1))
        self.assertAlmostEqual(replay.judge(rows, vector=True)[0][1], replay.judge(rows)[0][1])


class Carry(unittest.TestCase):
    def rows(self, carried, sources, gap=33.0):
        return [{"tick": gap * i, "carried": c, "source": s, "misfit": 0} for i, (c, s) in enumerate(zip(carried, sources))]

    def test_what_is_carried_summed_over_the_frames(self):
        rows = self.rows((0, .2, .1, .04, 0), ("head",) * 5)
        metres, longest = replay.carry(rows)
        self.assertAlmostEqual(metres, 0.34)
        self.assertEqual(longest, (0, None))  # no change of camera

    def test_the_longest_time_over_the_limit_after_a_change_of_camera(self):
        #                 change           under                 change       still over at the end
        carried = (0, 0, .30, .20, .10, .04, 0, 0, .2, .2, 0, 0, .5, .4, .3, .2, .1)
        sources = ("head",) * 2 + ("native",) * 10 + ("head",) * 5
        rows = self.rows(carried, sources)
        metres, (longest, where) = replay.carry(rows)
        self.assertAlmostEqual(metres, sum(carried))
        # the first: over 0.05 m in three frames, under it in the fourth, 99 ms after the change; the second 132 ms to
        # the last row; "native" and "repeat" are one camera; what is taken up without a change of camera is not counted
        self.assertAlmostEqual(longest, 132.0)
        self.assertIs(where, rows[12])
        self.assertAlmostEqual(replay.carry(rows[:12])[1][0], 99.0)
        rows = self.rows((0, .3, .3, 0), ("native", "repeat", "native", "native"))
        self.assertEqual(replay.carry(rows)[1], (0, None))

    def test_only_where_the_replay_is_exact_and_without_a_pause(self):
        rows = self.rows((0, .3, .3, .3, .3, 0), ("head",) + ("native",) * 5)
        for row in rows:
            row["follows"] = True
        rows[3]["follows"] = False
        metres, (longest, _) = replay.carry(rows, known=True)
        self.assertAlmostEqual(metres, 0.9)
        self.assertAlmostEqual(longest, 33.0)  # known to the row before the one that is not exact
        rows = self.rows((0, .3, .3, .3, .3, 0), ("head",) + ("native",) * 5)
        rows[3]["tick"] += 1000
        for row in rows[4:]:
            row["tick"] += 1000
        self.assertAlmostEqual(replay.carry(rows)[1][0], 33.0)

    def test_a_carry_that_settled_and_one_the_trace_cut_off(self):
        # settles in the fourth frame after the change: 99 ms; nothing was cut off
        rows = self.rows((0, 0, .30, .20, .10, .04, 0), ("head",) * 2 + ("native",) * 5)
        found = replay.carries(rows)
        self.assertAlmostEqual(found["settled"][0], 99.0)
        self.assertIs(found["settled"][1], rows[2])
        self.assertEqual((found["cut"], found["counts"]), ((0, None, 0.0), (1, 0)))
        # the rows end with 0.2 m still carried: cut off, 99 ms, and nothing settled
        rows = self.rows((0, 0, .5, .4, .3, .2), ("head",) * 2 + ("native",) * 4)
        found = replay.carries(rows)
        self.assertEqual((found["settled"], found["counts"]), ((0, None), (0, 1)))
        self.assertAlmostEqual(found["cut"][0], 99.0)
        self.assertIs(found["cut"][1], rows[2])
        self.assertAlmostEqual(found["cut"][2], 0.2)
        # a pause (the trace window ended) cuts it off as the end of the rows does, with what the last row before it carried
        rows = self.rows((0, 0, .5, .4, .3, 0, 0), ("head",) * 2 + ("native",) * 5)
        for row in rows[5:]:
            row["tick"] += 1000
        found = replay.carries(rows)
        self.assertAlmostEqual(found["cut"][0], 66.0)
        self.assertAlmostEqual(found["cut"][2], 0.3)
        self.assertEqual(found["settled"], (0, None))
        # so does a frame in which the replay is not exact (with `known`)
        rows = self.rows((0, .3, .25, .2, .1, 0), ("head",) + ("native",) * 5)
        for row in rows:
            row["follows"] = True
        rows[3]["follows"] = False
        found = replay.carries(rows, known=True)
        self.assertAlmostEqual(found["cut"][0], 33.0)
        self.assertAlmostEqual(found["cut"][2], 0.25)
        self.assertEqual(found["settled"], (0, None))

    def test_both_in_one_log_and_the_old_figure_is_the_longer(self):
        #                 change           under                 change       still over at the end
        carried = (0, 0, .30, .20, .10, .04, 0, 0, .2, .2, 0, 0, .5, .4, .3, .2, .1)
        rows = self.rows(carried, ("head",) * 2 + ("native",) * 10 + ("head",) * 5)
        found = replay.carries(rows)
        self.assertAlmostEqual(found["settled"][0], 99.0)
        self.assertIs(found["settled"][1], rows[2])
        self.assertAlmostEqual(found["cut"][0], 132.0)
        self.assertIs(found["cut"][1], rows[12])
        self.assertAlmostEqual(found["cut"][2], 0.1)
        self.assertEqual(found["counts"], (1, 1))
        metres, (longest, where) = replay.carry(rows)
        self.assertAlmostEqual(longest, 132.0)
        self.assertIs(where, rows[12])
        # the other way about: the longer one settled
        rows = self.rows((0, .5, .4, .3, .2, .04, 0, .3, .3), ("head",) + ("native",) * 5 + ("head",) + ("native",) * 2)
        found = replay.carries(rows)
        self.assertAlmostEqual(found["settled"][0], 132.0)
        self.assertAlmostEqual(found["cut"][0], 33.0)
        self.assertAlmostEqual(replay.carry(rows)[1][0], 132.0)

    def test_the_line_says_which(self):
        def row(at):
            return {"at": at}
        start = "  carried: 257.1 metre-frames; more than 0.05 m for 835 ms at most after a change of camera (07:04:10.351)"
        line = replay.carry_line(257.1, {"settled": (801.0, row("07:10:10.927")), "cut": (835.0, row("07:04:10.351"), 0.18), "counts": (51, 37)})
        self.assertTrue(line.startswith(start), line)  # as it was printed before, to there
        self.assertIn("cut off by the end of the trace with 0.180 m still carried", line)
        self.assertIn("the longest that settled: 801 ms (07:10:10.927)", line)
        self.assertIn("51 settled, 37 cut off", line)
        line = replay.carry_line(10.0, {"settled": (801.0, row("07:10:10.927")), "cut": (467.0, row("07:04:10.351"), 0.07), "counts": (5, 3)})
        self.assertTrue(line.startswith("  carried: 10.0 metre-frames; more than 0.05 m for 801 ms at most after a change of camera (07:10:10.927)"), line)
        self.assertIn("which settled", line)
        self.assertIn("the longest cut off by the end of the trace: 467 ms (07:04:10.351), 0.070 m still carried", line)
        # nothing cut off, nothing settled, no change of camera at all
        line = replay.carry_line(10.0, {"settled": (99.0, row("22:00:00.066")), "cut": (0, None, 0.0), "counts": (1, 0)})
        self.assertIn("which settled; none was cut off by the end of the trace", line)
        line = replay.carry_line(10.0, {"settled": (0, None), "cut": (99.0, row("22:00:00.066"), 0.2), "counts": (0, 1)})
        self.assertIn("cut off by the end of the trace with 0.200 m still carried; none settled", line)
        self.assertEqual(replay.carry_line(0.3, {"settled": (0, None), "cut": (0, None, 0.0), "counts": (0, 0)}),
                         "  carried: 0.3 metre-frames; more than 0.05 m for 0 ms at most after a change of camera")


if __name__ == "__main__":
    unittest.main()
