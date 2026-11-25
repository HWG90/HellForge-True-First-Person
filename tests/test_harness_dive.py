"""Round 30: a dive + swap that leaves the game's view, for the head of a diving body through the veil (tools/harness/
dive.py, the dive-swap phase of matrix.py and soak.py). Offline: the input sequence is what is intended and the log is
read as the mod writes it. Only the game can say at which delay a swap leaves its view (docs/guides/AIM-TEST.md)."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
sys.path.insert(0, str(ROOT / "tests"))
import dive  # noqa: E402
import drive  # noqa: E402
import matrix  # noqa: E402
import soak  # noqa: E402
from test_harness_aim import CACHE, Clock, FakeGame, FakeLog  # noqa: E402

NATIVE = "10:00:00.100 View: t=1.0 frames=30 gaps=0 on=1 phase=running kind=6 ads=0 safe=1 eye_step=0.0003"
LEFT = ["10:00:02.400 State: hold ended: the gun has left the hands",
        "10:00:02.400 State: left the native view: Gameplay action or camera owns the view"]
VEILED = ["10:00:02.430 Trace: t=0.0 kind=4 eye_world=(1 2 0.3) step=0.1 eye_edge=1 veil=(0.1 0 0) turn=(1 0 0) veil_head=(0.1200 -0.5100)",
          "10:00:02.463 Trace: t=33.3 kind=4 eye_world=(1 2 0.3) step=0.02 eye_edge=0 veil=(0.05 0 0) turn=(1 0 0) veil_head=(0.0800 -0.4700)",
          "10:00:02.496 Trace: t=66.6 kind=4 eye_world=(1 2 0.3) step=0.01 eye_edge=0 veil=0 turn=(1 0 0) veil_head=0"]


class Script(unittest.TestCase):
    def test_a_try_is_a_dive_forward_and_a_swap_into_it(self):
        lines, total = drive.events(dive.script(1.0))   # drive.events truncates to the ms: 0.03 + 0.3 is 329
        self.assertEqual(lines, ["0 d 87", "329 d 18", "410 u 18", "1440 d 50", "1520 u 50", "4050 u 87"])
        self.assertAlmostEqual(total, 4.08)

    def test_the_delays_begin_with_the_one_that_left_the_view_in_soak_34(self):
        self.assertEqual(dive.SWAP_AFTER_S[0], 1.0)
        self.assertIn(1.3, dive.SWAP_AFTER_S)


class Reading(unittest.TestCase):
    def test_the_view(self):
        self.assertIsNone(dive.in_native_view(["10:00:00.000 Ready: x"]))
        self.assertTrue(dive.in_native_view([NATIVE]))
        self.assertFalse(dive.in_native_view([NATIVE, NATIVE.replace("kind=6", "kind=4")]))   # the head camera's
        self.assertFalse(dive.in_native_view([NATIVE] + LEFT))
        self.assertTrue(dive.in_native_view(LEFT + ["10:00:05.000 State: entered the native view request=0x1 kind=6"]))

    def test_only_veiled_rows_carry_a_head(self):
        rows = dive.veil_heads(VEILED)
        self.assertEqual([(r["at"], r["ahead"], r["up"]) for r in rows], [("10:00:02.430", 0.12, -0.51), ("10:00:02.463", 0.08, -0.47)])

    def test_feel53s_soak_never_had_the_head_of_a_dive(self):
        path = CACHE / "tfp-soak-36.modlog"
        if not path.is_file():
            self.skipTest(f"{path} is not here")
        rows = dive.veil_heads(path.read_text(errors="replace").splitlines())
        self.assertEqual(len(rows), 112)                             # result-0.md: 112 veiled frames
        self.assertEqual(min(r["up"] for r in rows), -0.2228)       # a crouch; round 26 estimated -0.55 in a dive


class Leave(unittest.TestCase):
    DIVE = "10:00:02.000 Motion: action=dive placement=idle>family(sidearm) x=0.1 softened=0 phase=active"
    IDLE = "10:00:02.300 Motion: action=idle placement=idle x=0.1 softened=0 phase=active"

    def test_a_leave_in_the_dive(self):
        self.assertEqual(dive.dive_leave([self.DIVE] + LEFT), 2)

    def test_a_leave_after_the_dive_landed_is_not_one(self):
        # the swap came once the dive was over: the head is a standing body's, not a diving one's
        self.assertIsNone(dive.dive_leave([self.DIVE, self.IDLE] + LEFT))
        self.assertIsNone(dive.dive_leave(LEFT))

    def test_only_the_action_named_dive(self):
        back = self.DIVE.replace("action=dive ", "action=dive-back ")
        self.assertIsNone(dive.dive_leave([back] + LEFT))
        self.assertEqual(dive.dive_leave([back] + LEFT, "dive-back"), 2)

    def test_the_rows_of_that_leave_only(self):
        # a crouch's veil after the view came back is not the dive's head
        later = VEILED[0].replace("10:00:02.430", "10:00:06.000").replace("veil_head=(0.1200 -0.5100)", "veil_head=(0.0500 -0.9000)")
        lines = [self.DIVE] + LEFT + VEILED + ["10:00:05.000 State: entered the native view request=0x1 kind=6",
                                              "10:00:05.900 State: left the native view: x", later]
        self.assertEqual([r["up"] for r in dive.leave_rows(lines, dive.dive_leave(lines))], [-0.51, -0.47])


class DiveSwap(unittest.TestCase):
    def setUp(self):
        self.log, self.clock = FakeLog(self), Clock()

    def run_dive(self, react):
        game = FakeGame(self.log, react)
        result = dive.dive_swap(game, dive.aim.LogTail(self.log.path), sleep=self.clock.sleep, clock=self.clock)
        return game.scripts, result

    def test_the_view_left_at_the_second_delay(self):
        def react(script, log, n):
            if script == dive.PRIMARY:
                log.write(NATIVE)
            elif script == dive.script(dive.SWAP_AFTER_S[1]):
                log.write(Leave.DIVE, *LEFT, *VEILED)
        scripts, result = self.run_dive(react)
        back = dive.PRIMARY + "; wait 1.7"
        self.assertEqual(scripts, [dive.PRIMARY, dive.script(1.0), back, dive.PRIMARY, dive.script(0.6), back])
        self.assertTrue(result["left"])
        self.assertEqual(result["swap_after"], 0.6)
        self.assertEqual([a["left"] for a in result["attempts"]], [False, True])
        self.assertTrue(result["attempts"][1]["dived"] and result["attempts"][1]["from_native"])
        self.assertEqual(result["lowest_up"], -0.51)
        self.assertTrue(dive.verdict(result)["pass"])
        self.assertIn("swap 0.6 s into the dive (try 2); 2 veiled frames, the head lowest -0.510 m", dive.text(result))

    def test_never_left_says_the_head_is_unmeasured(self):
        scripts, result = self.run_dive(lambda s, log, n: log.write(NATIVE) if s == dive.PRIMARY else None)
        self.assertEqual(len(result["attempts"]), len(dive.SWAP_AFTER_S))
        self.assertFalse(result["left"])
        v = dive.verdict(result)
        self.assertFalse(v["pass"])
        self.assertIn("still unmeasured", v["notes"][0])
        self.assertEqual(scripts[-1], dive.PRIMARY + "; wait 1.7")

    def test_left_without_a_veiled_row(self):
        def react(script, log, n):
            if script == dive.PRIMARY:
                log.write(NATIVE)
            elif script.startswith("down w"):
                log.write(Leave.DIVE, *LEFT)
        _, result = self.run_dive(react)
        self.assertTrue(result["left"])
        self.assertFalse(dive.verdict(result)["pass"])
        self.assertIn("no veiled Trace row", result["notes"][0])

    def test_a_leave_after_the_dive_is_not_counted(self):
        def react(script, log, n):
            if script == dive.PRIMARY:
                log.write(NATIVE)
            elif script.startswith("down w"):
                log.write(Leave.DIVE, Leave.IDLE, *LEFT, *VEILED)
        _, result = self.run_dive(react)
        self.assertFalse(result["left"])
        self.assertTrue(all(a["left_any"] and not a["veil_heads"] for a in result["attempts"]))
        self.assertIn("left after the dive, not in it", " ".join(result["notes"]))
        self.assertFalse(dive.verdict(result)["pass"])

    def test_the_primary_comes_back_when_the_input_fails(self):
        def react(script, log, n):
            if script.startswith("down w"):
                raise drive.DriveError("the game does not have focus")
        game = FakeGame(self.log, react)
        with self.assertRaises(drive.DriveError):
            dive.dive_swap(game, dive.aim.LogTail(self.log.path), sleep=self.clock.sleep, clock=self.clock)
        self.assertEqual(game.scripts[-1], dive.PRIMARY + "; wait 1.7")


class Phases(unittest.TestCase):
    def test_the_matrix_and_the_soak_play_it(self):
        self.assertIn("dive-swap", matrix.PHASES)
        self.assertIn("dive-swap", matrix.DIVES)
        self.assertIn("dive-swap", matrix.DRIVEN)
        self.assertNotIn("dive-swap", soak.SKIP)

    def test_play_hands_it_to_dive_swap(self):
        log, calls = FakeLog(self), []
        original = dive.dive_swap
        dive.dive_swap = lambda device, tail: calls.append(tail.path) or {"left": False}
        try:
            matrix.play(FakeGame(log), "dive-swap", log.path)
        finally:
            dive.dive_swap = original
        self.assertEqual(calls, [log.path])

    def test_the_soak_plays_it_by_dive_swap(self):
        # soak.act: a DRIVEN phase goes to matrix.play (dive-swap is in DIVES, not AIMED), not its plain script
        log, calls = FakeLog(self), []
        original = dive.dive_swap
        dive.dive_swap = lambda device, tail: calls.append(tail.path) or {"left": False}
        try:
            game = FakeGame(log)
            result = soak.act(game, "dive-swap", matrix.PHASES["dive-swap"][0], 0.4, log.path, sleep=lambda s: None)
        finally:
            dive.dive_swap = original
        self.assertEqual((calls, game.scripts, result), ([log.path], [], {"left": False}))
        plain = FakeGame(log)
        self.assertIsNone(soak.act(plain, "walk", "hold w 1.5", 0.4, log.path))
        self.assertEqual(plain.scripts, ["hold w 1.5; wait 0.40"])

    def test_the_judge_and_the_summary(self):
        result = {"left": True, "swap_after": 1.0, "attempts": [{}], "veil_heads": [{"up": -0.5}], "lowest_up": -0.5, "notes": []}
        judged = matrix.judge_aim("dive-swap", result, [], {"pass": True, "notes": []})
        self.assertTrue(judged["pass"])
        self.assertIs(judged["dive"], result)
        self.assertIn("left the game's view", matrix.aim_text(result))
        self.assertEqual(soak.dive_summary([(3, result), (9, {"left": False})]),
                         "dive + swap: 1 of 2 left the game's view; 1 veiled frames, the head lowest -0.500 m from the eye")
        self.assertFalse(matrix.judge_aim("dive-swap", {"left": False, "notes": ["x"]}, [], {"pass": True, "notes": []})["pass"])


if __name__ == "__main__":
    unittest.main()
