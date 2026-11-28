"""Round 55C, the runs: the "was it played" checks that were missing and the matrix's setup, from the feel.63 run of
2026-11-28 (matrix, soak 67-A Stable, soak 68-C Bodycam); windows of those logs in tests/reference/round55c.
  1. the soak passes each action's context (the log before its stretch) to the judge;
  3. the soak's dives and clambers must have been played (a dive line, a traversal mask);
  4. the matrix's setup: a stance read while the view was elsewhere is stale, a stand-up is let settle, and a phase
     whose body could not be stood up is a harness failure, not a camera result;"""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
sys.path.insert(0, str(ROOT / "tests"))
import dive  # noqa: E402
import matrix  # noqa: E402
import soak  # noqa: E402
from test_harness_aim import Clock, FakeGame, FakeLog  # noqa: E402

REF = ROOT / "tests/reference/round55c"
LANDING = ROOT / "tests/reference/landing/feel58-soak51B-232320.log"


def lines_of(name):
    return (REF / name).read_text().splitlines()


def split_at(lines, stamp):
    """(the lines before the first Trace line written at `stamp`, the lines from it): a stretch and what came before."""
    k = next(i for i, line in enumerate(lines) if line.startswith(stamp + " Trace:"))
    return lines[:k], lines[k:]





class ContextRuns(unittest.TestCase):
    """Change 1 in the runs: soak 68-C #20 (crouch) judged by the soak with the log before its stretch."""
    def setUp(self):
        self.before, self.stretch = split_at(lines_of("feel63-C20-crouch.log"), "11:29:46.337")

    def test_the_soak_judges_with_them(self):
        j = soak.judged("crouch", matrix.PHASES["crouch"][0], self.stretch, before=self.before)
        self.assertFalse(soak.flagged(j, matrix.PHASES["crouch"][0]))
        self.assertTrue(soak.flagged(soak.judged("crouch", matrix.PHASES["crouch"][0], self.stretch), matrix.PHASES["crouch"][0]))


class SoakPlayed(unittest.TestCase):
    """Change 3. Stable #212 (clamber-high): no traversal mask in its stretch (a drop of 0.6 m and the game's own
    landing), so not a clamber; the matrix's dive-back of the same run had no dive line."""
    def test_a_clamber_without_the_traversal_mask_was_not_played(self):
        lines = lines_of("feel63-A212-clamber-not-played.log")
        notes = soak.unplayed("clamber-high", lines)
        self.assertEqual(len(notes), 1)
        self.assertIn("0x20", notes[0])
        self.assertEqual(soak.unplayed("clamber-high", LANDING.read_text().splitlines()), [])

    def test_a_dive_without_a_dive_line_was_not_played(self):
        self.assertTrue(soak.unplayed("dive-back", lines_of("feel63-matrix-dive-back-not-played.log")))
        self.assertEqual(soak.unplayed("dive", lines_of("feel63-A54-dive-launch.log")), [])
        self.assertEqual(soak.unplayed("walk", lines_of("feel63-matrix-dive-back-not-played.log")), [])

    def test_an_action_not_played_is_not_flagged_for_its_steps(self):
        script = matrix.PHASES["clamber-high"][0]
        j = soak.judged("clamber-high", script, lines_of("feel63-A212-clamber-not-played.log"))
        self.assertFalse(j["played"])
        self.assertGreaterEqual(j["largest_jump_m"], matrix.POP_M)   # 0.131 m: still measured and printed
        self.assertFalse(soak.flagged(j, script))
        self.assertTrue(soak.flagged({**j, "missed": 1}, script))       # a frame out of first person is one whatever

    def test_a_played_one_is_judged_as_before(self):
        script = matrix.PHASES["dive"][0]
        j = soak.judged("dive", script, lines_of("feel63-A54-dive-launch.log"))
        self.assertTrue(j["played"])
        self.assertEqual(j["unplayed"], [])

    def test_the_summary_says_which(self):
        text = soak.unplayed_summary([(212, "clamber-high", ["NOT PLAYED: x"])])
        self.assertIn("1 action not played", text)
        self.assertIn("#212 clamber-high", text)
        self.assertEqual(soak.unplayed_summary([]), "")

    def test_the_matrix_clambers_need_the_mask_too(self):
        self.assertTrue(matrix.unplayed("clamber-high", lines_of("feel63-A212-clamber-not-played.log")))
        self.assertTrue(matrix.unplayed("clamber-low", lines_of("feel63-A212-clamber-not-played.log")))
        self.assertEqual(matrix.unplayed("clamber-high", LANDING.read_text().splitlines()), [])


NATIVE = "10:00:00.100 View: t=1.0 frames=30 gaps=0 on=1 phase=running kind=6 ads=0 safe=1 eye_step=0.0003"
PRONE = "10:00:02.900 Motion: action=prone placement=idle>imported(own) x=0.1 softened=1 phase=active"
LEFT = "10:00:03.100 State: left the native view: Gameplay action or camera owns the view"
ENTERED = "10:00:09.000 State: entered the native view request=0x1 kind=6"
IDLE = "10:00:09.000 Motion: action=idle placement=imported(own) x=0.1 softened=1 phase=active"
STAND = "10:00:03.200 Motion: action=idle placement=imported(own) x=0.1 softened=1 phase=active"


class MatrixSetup(unittest.TestCase):
    """Change 4. The matrix's stand-up-prone-head: the phase before left the body prone and then the head camera (the
    gun out of the hands, 11:08:53.804); no Motion line is written there, so stand_up read "prone" for a body that had
    stood up, pressed z (it lay down: eye 2.94 m at 11:09:03), the dive key and z. And dive-back: the dive key went in
    while the body was still getting up from prone (Motion: idle 11:08:17.948; the stand ramp to 11:08:18.315)."""
    def setUp(self):
        self.log, self.clock = FakeLog(self), Clock()

    def stand(self, react, before):
        self.log.write(*before)
        game = FakeGame(self.log, react)
        return game.scripts, dive.stand_up(game, dive.aim.LogTail(self.log.path), sleep=self.clock.sleep, clock=self.clock)

    def test_a_stance_from_before_the_view_left_is_not_trusted(self):
        scripts, r = self.stand(None, (PRONE, LEFT))
        self.assertEqual(scripts, [dive.PRIMARY])   # the primary back, never z on a stance it cannot know
        self.assertFalse(r["standing"])
        self.assertIn("not known", r["why"])

    def test_back_in_the_game_view_its_motion_line_says(self):
        def react(script, log, n):
            if script == dive.PRIMARY:
                log.write(ENTERED, IDLE)
        scripts, r = self.stand(react, (PRONE, LEFT))
        self.assertEqual((scripts, r["after"], r["standing"], r["why"]), ([dive.PRIMARY], "idle", True, None))

    def test_back_in_the_game_view_still_prone_is_stood_up(self):
        def react(script, log, n):
            if script == dive.PRIMARY:
                log.write(ENTERED, PRONE)
            elif script == "key z":
                log.write(STAND)
        scripts, r = self.stand(react, (PRONE, LEFT))
        self.assertEqual((scripts, r["standing"]), ([dive.PRIMARY, "key z"], True))

    def test_a_stand_up_is_let_settle(self):
        times = []
        game = FakeGame(self.log, lambda s, log, n: log.write(STAND) if s == "key z" else None)
        self.log.write(PRONE)
        tail = dive.aim.LogTail(self.log.path)
        r = dive.stand_up(game, tail, sleep=lambda s: (times.append(s), self.clock.sleep(s)), clock=self.clock)
        self.assertTrue(r["standing"])
        self.assertEqual(times[-1], dive.STAND_SETTLE_S)

    def test_standing_already_is_not_waited_for(self):
        times = []
        self.log.write(STAND)
        r = dive.stand_up(FakeGame(self.log), dive.aim.LogTail(self.log.path), sleep=times.append, clock=self.clock)
        self.assertEqual((r["standing"], times), (True, []))

    def test_a_phase_whose_body_could_not_stand_is_a_harness_failure(self):
        judged = {"pass": True, "notes": ["x"]}
        out = matrix.judge_stood("stand-up-prone-head", {"standing": False, "before": "prone", "after": "prone",
                                                         "presses": ["key z"], "why": None}, judged)
        self.assertFalse(out["pass"])
        self.assertTrue(out["harness"])
        self.assertTrue(out["notes"][0].startswith("HARNESS FAILURE"))
        self.assertEqual(out["notes"][1:], ["x"])
        ok = matrix.judge_stood("dive", {"standing": True, "before": "prone", "after": "idle", "presses": [], "why": None},
                                {"pass": True, "notes": []})
        self.assertTrue(ok["pass"])
        self.assertFalse(ok["harness"])
        self.assertTrue(matrix.judge_stood("walk", None, {"pass": True, "notes": []})["pass"])

    def test_a_phase_not_played_is_a_harness_failure_too(self):
        j = matrix.judge_played("clamber-high", lines_of("feel63-A212-clamber-not-played.log"), {"pass": True, "notes": []})
        self.assertEqual((j["pass"], j["played"], j["harness"]), (False, False, True))
        j = matrix.judge_stood("clamber-high", {"standing": True, "before": None, "after": None, "presses": [], "why": None}, j)
        self.assertTrue(j["harness"])   # not undone by a stand-up that went well
        j = matrix.judge_played("clamber-high", LANDING.read_text().splitlines(), {"pass": True, "notes": []})
        self.assertEqual((j["pass"], j["played"], j["harness"]), (True, True, False))

    def test_the_summary_keeps_harness_failures_apart(self):
        text = matrix.result_summary({"a": {"pass": True}, "b": {"pass": False}, "c": {"pass": False, "harness": True}})
        self.assertEqual(text, "1 of 3 passed; failed: b; harness failures, not camera results: c")


if __name__ == "__main__":
    unittest.main()
