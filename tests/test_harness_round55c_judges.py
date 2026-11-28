"""Round 55C, the judges: more honest classes, from the feel.63 run of 2026-11-28 (matrix, soak 67-A Stable, soak 68-C Bodycam).
Every case is a window of those logs (tests/reference/round55c). No limit is raised: each change is a classification
the cited rows back, or a "was it played" check that was missing.
  1. an action's stretch is judged with the frames just before it (matrix.CONTEXT_MS) as context;
  2. the veil's bound judges the eye shown: a veil move the eye itself made up is not a fault;
  (3 and 4, the "was it played" checks and the matrix's setup: tests/test_harness_round55c_played.py)
  5. the game's dive launch (an untouched native frame) is printed and not counted;
  6. a fall landing (the root over FALL_M down in one frame) is the body's own where the game's eye went with it;
  7. a measure printed at its bound shows the digits that put it over."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
sys.path.insert(0, str(ROOT / "tests"))
import matrix  # noqa: E402
import soak  # noqa: E402
import trace  # noqa: E402

REF = ROOT / "tests/reference/round55c"
FLICK = ROOT / "tests/reference/turn/soak60-A-L6760-6800.log"


def lines_of(name):
    return (REF / name).read_text().splitlines()


def split_at(lines, stamp):
    """(the lines before the first Trace line written at `stamp`, the lines from it): a stretch and what came before."""
    k = next(i for i, line in enumerate(lines) if line.startswith(stamp + " Trace:"))
    return lines[:k], lines[k:]


def snap_from(lines, stamp, dz):
    """The Trace lines from `stamp` on with the eye shown moved dz m down off the game's eye, the correction grown by
    dz and the frame marked as the mod's (looked, taken jump): a filter snap the size of its own hold."""
    import re
    out, on = [], False
    for line in lines:
        if line.startswith(stamp + " Trace:"):
            on = True
        if on and " Trace: " in line:
            m = re.search(r"eye_world=\((\S+) (\S+) (\S+)\)", line)
            line = line.replace(m[0], f"eye_world=({m[1]} {m[2]} {float(m[3]) - dz:.5f})")
            line = re.sub(r"correction=(\S+)", lambda c: f"correction={float(c[1]) + dz:.4f}", line)
            line = line.replace("taken=root ", "taken=jump+root ").replace("taken=- ", "taken=jump ").replace("looked=0", "looked=1")
        out.append(line)
    return out


def pop_at(j, at):
    found = [p for p in j["pops"] if p["at"] == at]
    return found[0] if found else None


class Context(unittest.TestCase):
    """Change 1. Bodycam #20 (crouch): its stretch began after the root's change of step at 11:29:46.370 (0.119 m), so
    the eye's at 11:29:46.404 (0.117 m, the rig eye a frame after the root) read as the eye's own."""
    def setUp(self):
        self.before, self.stretch = split_at(lines_of("feel63-C20-crouch.log"), "11:29:46.337")

    def test_alone_it_reads_as_the_eyes_own(self):
        j = matrix.judge(self.stretch)
        self.assertEqual(pop_at(j, "11:29:46.404")["whose"], "own")
        self.assertGreaterEqual(j["largest_jump_m"], matrix.POP_M)

    def test_with_the_frames_before_it_is_the_bodys(self):
        j = matrix.judge(self.stretch, before=self.before)
        p = pop_at(j, "11:29:46.404")
        self.assertEqual((p["whose"], p["after"]), ("body", 1))
        self.assertAlmostEqual(p["body"], 0.119, places=3)
        self.assertLess(p["beyond"], 0.01)
        self.assertLess(j["largest_jump_m"], matrix.POP_M)

    def test_the_frames_before_are_never_judged_themselves(self):
        j = matrix.judge(self.stretch, before=self.before)
        first = min(r["tick"] for r in trace.frames(self.stretch))
        self.assertTrue(j["pops"])
        self.assertTrue(all(p["tick"] >= first for p in j["pops"]))
        self.assertIsNone(pop_at(j, "11:29:46.204"))   # 0.091 m, the eye's own: the action before's
        # the stretch's first frame is measured now, against the two before it: the rig eye a frame after the root
        self.assertEqual((pop_at(j, "11:29:46.337")["whose"], pop_at(j, "11:29:46.337")["after"]), ("body", 1))

    def test_only_the_last_context_ms_are_taken(self):
        rows = matrix.context_rows(self.before, trace.frames(self.stretch))
        first = min(r["tick"] for r in trace.frames(self.stretch))
        self.assertTrue(rows)
        self.assertTrue(all(first - matrix.CONTEXT_MS <= r["tick"] < first and r["context"] for r in rows))


class VeilShown(unittest.TestCase):
    """Change 2. Bodycam #196: the veil let out 0.053 and 0.054 m a frame while the eye before the veil came forward as
    much (THE PUSH, clear= 0.316 to 0.319): with the root still, the eye shown moved 0.003 and 0.006 m."""
    def test_a_let_out_the_eye_made_up_is_not_a_fault(self):
        largest, _, faults = matrix.veil_faults(trace.frames(lines_of("feel63-C196-veil-push.log")))
        self.assertEqual(faults, [])
        self.assertAlmostEqual(largest, 0.312, places=3)

    def test_it_is_said_and_not_counted(self):
        excused = []
        matrix.veil_faults(trace.frames(lines_of("feel63-C196-veil-push.log")), excused)
        self.assertEqual([e["at"] for e in excused], ["11:37:35.471", "11:37:35.505"])
        self.assertTrue(all(e["m"] > matrix.VEIL_STEP_M and e["shown"] < 0.01 for e in excused))
        j = matrix.judge(lines_of("feel63-C196-veil-push.log"))
        self.assertFalse(j["veil_faults"])
        said = [s for s in j["said"] if "not counted" in s and "11:37:35.505" in s]
        self.assertEqual(len(said), 1)
        self.assertIn("the eye shown moved 0.00", said[0])

    def test_the_turn_edge_veil_stays_a_fault(self):
        # Stable #234: the veil turned with a 20.7 degree carry (G2, round 55A's fix), the eye shown went 0.053 m across
        _, _, faults = matrix.veil_faults(trace.frames(lines_of("feel63-A234-veil-turn.log")))
        self.assertEqual([(f["at"], f["what"]) for f in faults], [("11:25:03.951", "step")])
        self.assertGreater(faults[0]["shown"], matrix.VEIL_STEP_M)
        self.assertTrue(soak.flagged(soak.judged("stand-up-prone-head", "key z", lines_of("feel63-A234-veil-turn.log")), "key z"))

    def test_a_veil_that_falls_to_nothing_stays_a_fault(self):
        _, _, faults = matrix.veil_faults(trace.frames(FLICK.read_text().splitlines()))
        self.assertTrue(faults)
        self.assertTrue(all(f["shown"] > matrix.VEIL_STEP_M for f in faults))


LAUNCHES = {   # fixture: (the frame, what it showed, the action's verdict as logged)
    "feel63-A54-dive-launch.log": ("11:14:26.423", 0.070),
    "feel63-A85-dive-launch.log": ("11:15:58.166", 0.071),
    "feel63-C5-dive-launch.log": ("11:27:33.562", 0.069),
    "feel63-C22-dive-launch.log": ("11:29:51.513", 0.090),
    "feel63-C218-dive-launch.log": ("11:38:45.881", 0.072),
    "feel63-matrix-dive-launch.log": ("11:08:13.107", 0.073),
}


class DiveLaunch(unittest.TestCase):
    """Change 5. A dive's launch: the game's own eye, a frame after the root leaves its run (two frames after the dive's
    Motion line), 0.069 to 0.090 m in both styles; kind 6, nothing looked for, taken or carried, the wanted eye shown,
    the source native, the root's own change under POP_M."""
    def test_every_cited_launch_is_named_and_not_counted(self):
        for name, (at, shown) in LAUNCHES.items():
            j = matrix.judge(lines_of(name))
            p = pop_at(j, at)
            self.assertEqual(p["whose"], "launch", name)
            self.assertAlmostEqual(p["shown"], shown, places=3, msg=name)
            self.assertLess(j["largest_jump_m"], matrix.POP_M, name)
            self.assertEqual(j["launch_frames"], 1, name)
            self.assertAlmostEqual(j["largest_launch_m"], shown, places=3, msg=name)
            self.assertIn("dive launch 1", matrix.describe(j), name)
            self.assertTrue(any("the game's dive launch" in t for t in matrix.pop_lines(j)), name)

    def test_the_matrix_dive_passes_and_the_soak_does_not_flag(self):
        lines = lines_of("feel63-matrix-dive-launch.log")
        self.assertFalse(soak.flagged(soak.judged("dive", matrix.PHASES["dive"][0], lines), matrix.PHASES["dive"][0]))
        self.assertIn("1 the game's dive launch", soak.counted(matrix.judge(lines)["pops"]))
        self.assertEqual(soak.unlisted(matrix.judge(lines)["pops"], set()), [])

    def test_not_without_the_dive_line(self):
        lines = [line for line in lines_of("feel63-A54-dive-launch.log") if " Motion: action=dive" not in line]
        self.assertEqual(pop_at(matrix.judge(lines), "11:14:26.423")["whose"], "own")

    def test_not_where_the_mod_looked_carried_or_took(self):
        base = lines_of("feel63-A54-dive-launch.log")
        for old, new in ((" looked=0 ", " looked=1 "), (" correction=0.0000 ", " correction=0.0040 "), (" source=native ", " source=head ")):
            lines = [line.replace(old, new) if line.startswith("11:14:26.389 Trace") else line for line in base]
            self.assertNotEqual(pop_at(matrix.judge(lines), "11:14:26.423")["whose"], "launch", new)

    def test_not_in_a_reload(self):
        j = matrix.judge(lines_of("feel63-A54-dive-launch.log"), stance=False)
        self.assertEqual(pop_at(j, "11:14:26.423")["whose"], "own")

    def test_the_head_camera_stays_what_it_was(self):
        # G1 (Stable #126 stand-up from the back) and G3 (Stable #228 dive in the head camera) are real defects
        j = matrix.judge(lines_of("feel63-A126-head-standup.log"))
        self.assertGreaterEqual(j["largest_jump_m"], 0.2)
        self.assertFalse(any(p["whose"] in ("launch", "fall") for p in j["pops"]))
        j = matrix.judge(lines_of("feel63-A228-head-dive.log"))
        self.assertGreaterEqual(j["largest_jump_m"], 0.19)
        self.assertFalse(any(p["whose"] == "launch" for p in j["pops"]))

    def test_the_turn_edge_flick_stays_flagged(self):
        # G2: Stable #29, 0.541 m across at 11:13:22.423
        j = matrix.judge(lines_of("feel63-A29-turn-edge.log"))
        self.assertGreater(j["largest_jump_m"], 0.3)
        self.assertEqual(pop_at(j, "11:13:22.423")["whose"], "filter")


class FallLanding(unittest.TestCase):
    """Change 6. Stable #216: the root fell 1.109 m in one frame (11:24:25.191), the rig eye a frame late; at
    11:24:25.257 a hold's end opened the window, the filter took up 0.060 m and the frame read as the filter's 0.929.
    The game's eye less the body's lunge is 0.022 m there; what the eye shown has beyond the body is the filter's hold."""
    def test_it_is_the_bodys_own(self):
        j = matrix.judge(lines_of("feel63-A216-fall-landing.log"))
        p = pop_at(j, "11:24:25.257")
        self.assertEqual(p["whose"], "body")
        self.assertAlmostEqual(p["fall"], -1.109, places=3)
        self.assertAlmostEqual(p["held"], 0.060, places=3)
        self.assertLess(p["beyond"], matrix.POP_M)
        self.assertLess(j["largest_jump_m"], matrix.POP_M)
        self.assertTrue(any("a fall landing" in t for t in matrix.pop_lines(j)))

    def test_not_where_the_filter_did_more_than_hold(self):
        lines = [line.replace("correction=0.0601", "correction=0.0201") if line.startswith("11:24:25.257 Trace") else line
                 for line in lines_of("feel63-A216-fall-landing.log")]
        p = pop_at(matrix.judge(lines), "11:24:25.257")
        self.assertNotEqual(p["whose"], "body")
        self.assertNotIn("fall", p)

    def test_a_filter_snap_in_the_landing_still_fails(self):
        # the check's mutation: the eye shown moved 0.10 m, then 0.20 m, off the game's eye from 11:24:25.224 on, with
        # the matching correction (the filter's hold as big as the snap): main fails both as the filter's, and so must this
        for dz in (0.10, 0.20):
            with self.subTest(dz=dz):
                j = matrix.judge(snap_from(lines_of("feel63-A216-fall-landing.log"), "11:24:25.224", dz))
                for at in ("11:24:25.224", "11:24:25.257"):
                    p = pop_at(j, at)
                    self.assertEqual(p["whose"], "filter")
                    self.assertNotIn("fall", p)
                    self.assertGreater(p["beyond"], matrix.FALL_HELD_M)
                self.assertGreater(j["largest_jump_m"], matrix.POP_M)
                self.assertFalse(j["pass"])

    def test_the_bound_is_the_largest_honest_hold_with_margin(self):
        self.assertGreater(matrix.FALL_HELD_M, 0.0603)     # 67-A #216, the only fall landing in the 49 soak logs
        self.assertLessEqual(matrix.FALL_HELD_M, 0.085)    # the smallest mutated hold above (0.10 m snap) must fail

    def test_not_a_smaller_drop(self):
        # the same frames with every root lifted by the drop's excess in the drop frame and after: a 0.9 m drop
        def lift(line):
            if " Trace: " not in line or line[:12] < "11:24:25.191":
                return line
            head, rest = line.split(" root=(", 1)
            xyz, tail = rest.split(")", 1)
            x, y, z = (float(v) for v in xyz.split())
            return f"{head} root=({x:.5f} {y:.5f} {z + 0.21:.5f}){tail}"
        j = matrix.judge([lift(line) for line in lines_of("feel63-A216-fall-landing.log")])
        self.assertNotIn("fall", pop_at(j, "11:24:25.257") or {})


class Bound(unittest.TestCase):
    """Change 7. Bodycam #83 printed "the veil opened with a move of 0.400 m (over 0.40)": the frame at 11:32:35.670 was
    its stretch's first veiled one (the leave was at 11:32:35.603, in #82's stretch); its veil was 0.40021 m."""
    def test_a_measure_at_its_bound_prints_the_digits_that_put_it_over(self):
        self.assertEqual(matrix.over(0.40021, 0.40), "0.4002")
        self.assertEqual(matrix.over(0.39996, 0.40), "0.39996")
        self.assertEqual(matrix.over(0.423, 0.40), "0.423")
        self.assertEqual(matrix.over(0.0601, 0.06), "0.0601")

    def test_the_opening_is_compared_unrounded_and_printed_so(self):
        before, stretch = split_at(lines_of("feel63-C83-veil-bound.log"), "11:32:35.670")
        j = matrix.judge(stretch)
        self.assertEqual([f["what"] for f in j["veil_faults"]][:1], ["opening"])
        self.assertIn("0.4002 m (over 0.40)", j["said"][0])

    def test_with_the_frames_before_it_is_no_opening(self):
        before, stretch = split_at(lines_of("feel63-C83-veil-bound.log"), "11:32:35.670")
        j = matrix.judge(stretch, before=before)
        self.assertEqual([f for f in j["veil_faults"] if f["what"] == "opening"], [])
        self.assertEqual(j["veil_open_m"], 0.0)

    def test_the_real_pop_still_shows_in_the_stretch_it_fell_in(self):
        before, _ = split_at(lines_of("feel63-C83-veil-bound.log"), "11:32:35.670")
        j = matrix.judge(before)
        p = pop_at(j, "11:32:35.603")
        self.assertEqual(p["whose"], "veil")
        self.assertAlmostEqual(p["veil"], 0.394, places=3)
        self.assertAlmostEqual(j["veil_open_m"], 0.394, places=3)

    def test_the_jump_at_its_bound_too(self):
        self.assertIn("jump 0.0601 m", matrix.describe({"from_trace": True, "judged": 3, "largest_jump_m": 0.0601, "body_m": 0.0,
                                                         "by_lengths_m": 0.05}))
        self.assertIn("jump 0.043 m", matrix.describe({"from_trace": True, "judged": 3, "largest_jump_m": 0.043, "body_m": 0.0,
                                                        "by_lengths_m": 0.05}))


if __name__ == "__main__":
    unittest.main()
