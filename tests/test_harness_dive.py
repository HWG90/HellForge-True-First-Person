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
        lines, total = drive.events(dive.script(0.15))   # drive.events truncates to the ms: 0.03 + 0.3 is 329
        self.assertEqual(lines, ["0 d 87", "329 d 18", "390 u 18", "480 d 50", "560 u 50", "3090 u 87"])
        self.assertAlmostEqual(total, 3.12)

    def test_the_swap_goes_down_its_delay_after_the_dive_key(self):
        for after in dive.SWAP_AFTER_S:
            times = {line.split()[1] + line.split()[2]: int(line.split()[0]) for line in drive.events(dive.script(after))[0]}
            self.assertAlmostEqual((times["d50"] - times["d18"]) / 1000, after, delta=0.0015, msg=after)
            self.assertAlmostEqual((times["u18"] - times["d18"]) / 1000, dive.DIVE_TAP_S, delta=0.0015)
            self.assertLess(times["u18"], times["d50"])   # the dive key is let go before the swap goes down

    def test_a_swap_before_the_dive_key_is_let_go_is_refused(self):
        with self.assertRaises(ValueError):
            dive.script(0.05)

    def test_the_delays_are_short_ones_inside_a_dive(self):
        # round 32: feel.54's dives ran 0.93 and 1.27 s and a swap at 1.0 / 1.3 s was held until after the landing
        self.assertEqual(dive.SWAP_AFTER_S, (0.1, 0.15, 0.2, 0.3, 0.4))
        self.assertTrue(all(d < 0.93 for d in dive.SWAP_AFTER_S))
        # 0.4: the soak's overlap, whose swap went down 0.41 s after the dive key and left the view in the dive
        overlap = {l.split()[1] + l.split()[2]: int(l.split()[0])
                   for l in drive.events("down w; wait 0.2; key alt; wait 0.3; key 2; wait 1.0; up w")[0]}
        self.assertAlmostEqual((overlap["d50"] - overlap["d18"]) / 1000, dive.SWAP_AFTER_S[-1], delta=0.015)


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


PRONE = "10:00:02.900 Motion: action=prone placement=idle>imported(own) x=0.1 softened=1 phase=active"
CROUCH = "10:00:03.000 Motion: action=crouch placement=idle>imported(own) x=0.1 softened=1 phase=active"
STAND = "10:00:03.200 Motion: action=idle placement=imported(own) x=0.1 softened=1 phase=active"


def standing_game(leave_at=None, landed=True):
    """A fake game: the primary gives the game's view; a try dives (and lands prone) and, at the delay `leave_at`,
    leaves the view in the dive; z from prone and c from a crouch stand the body up."""
    def react(script, log, n):
        if script == dive.PRIMARY:
            log.write(NATIVE)
        elif script.startswith("down w"):
            lines = [Leave.DIVE] + (LEFT + VEILED if leave_at is not None and script == dive.script(leave_at) else []) + ([PRONE] if landed else [])
            log.write(*lines)
        elif script in ("key z", "key c"):
            log.write(STAND)
    return react


class StandUp(unittest.TestCase):
    def setUp(self):
        self.log, self.clock = FakeLog(self), Clock()

    def stand(self, react, before=()):
        self.log.write(*before)
        game = FakeGame(self.log, react)
        return game.scripts, dive.stand_up(game, dive.aim.LogTail(self.log.path), sleep=self.clock.sleep, clock=self.clock)

    def test_standing_or_unknown_presses_nothing(self):
        for before in ((), (STAND,), (Leave.IDLE,)):
            scripts, r = self.stand(None, before)
            self.assertEqual((scripts, r["presses"], r["standing"]), ([], [], True), before)

    def test_prone_is_stood_up_by_z_read_from_the_log_before_the_tail(self):
        # the phase before left the body prone: the tail starts after that line, the look-back finds it
        scripts, r = self.stand(lambda s, log, n: log.write(STAND) if s == "key z" else None, (PRONE,))
        self.assertEqual(scripts, ["key z"])
        self.assertEqual((r["before"], r["after"], r["standing"]), ("prone", "idle", True))

    def test_a_crouch_is_stood_up_by_c(self):
        scripts, r = self.stand(lambda s, log, n: log.write(STAND) if s == "key c" else None, (CROUCH,))
        self.assertEqual((scripts, r["standing"]), (["key c"], True))

    def test_prone_to_a_crouch_then_up(self):
        def react(script, log, n):
            log.write(CROUCH if script == "key z" else STAND)
        scripts, r = self.stand(react, (PRONE,))
        self.assertEqual((scripts, r["after"]), (["key z", "key c"], "idle"))

    def test_z_not_heard_falls_back_on_the_dive_key_from_prone(self):
        # feel.54: alt from prone stood the body up (09:56:19.015, 09:56:32.503)
        scripts, r = self.stand(lambda s, log, n: log.write(STAND) if s == dive.STAND_FALLBACK else None, (PRONE,))
        self.assertEqual(scripts, ["key z", dive.STAND_FALLBACK])
        self.assertTrue(r["standing"])

    def test_it_gives_up_after_its_presses(self):
        scripts, r = self.stand(None, (PRONE,))
        self.assertEqual(len(scripts), dive.STAND_PRESSES)
        self.assertEqual((r["after"], r["standing"]), ("prone", False))

    def test_a_dive_lands_before_anything_is_pressed(self):
        log = self.log
        log.write(Leave.DIVE)
        game = FakeGame(log, lambda s, lg, n: lg.write(STAND) if s == "key z" else None)
        tail = dive.aim.LogTail(log.path)
        log.write(PRONE)   # the landing comes while stand_up reads
        r = dive.stand_up(game, tail, sleep=self.clock.sleep, clock=self.clock)
        self.assertEqual((game.scripts, r["before"], r["after"]), (["key z"], "dive", "idle"))


class Timing(unittest.TestCase):
    def test_a_leave_after_the_landing(self):
        lines = ["10:00:01.000 Motion: action=dive placement=x", "10:00:01.935 Motion: action=prone placement=x",
                 "10:00:02.469 State: left the native view: Gameplay action or camera owns the view"]
        self.assertEqual(dive.dive_timing(lines), {"dive_s": 0.935, "leave_after_s": 1.469})
        self.assertIsNone(dive.dive_leave(lines))

    def test_no_dive(self):
        self.assertEqual(dive.dive_timing(LEFT), {"dive_s": None, "leave_after_s": None})

    def test_feel54s_tries(self):
        # round 32: tfp-soak-37.modlog's dive-swap phase: try 1 dived 0.935 s and left 1.469 s in (after the landing),
        # try 2 stood up from prone and never dived
        path = CACHE / "tfp-soak-37.modlog"
        if not path.is_file():
            self.skipTest(f"{path} is not here")
        lines = path.read_text(errors="replace").splitlines()
        between = lambda a, b: [l for l in lines if a <= l[:12] < b and l[2:3] == ":"]
        first = between("09:56:24.500", "09:56:28.000")
        self.assertEqual(dive.dive_timing(first), {"dive_s": 0.935, "leave_after_s": 1.469})
        self.assertIsNone(dive.dive_leave(first))
        second = between("09:56:30.500", "09:56:36.000")
        self.assertEqual(dive.last_action(second), "idle")
        self.assertFalse(any((m := dive.MOTION.match(l)) and m[2] == "dive" for l in second))


class SoakLeaves(unittest.TestCase):
    def test_a_leave_in_the_dive_and_one_after(self):
        lines = [Leave.DIVE] + LEFT + VEILED + ["10:00:03.000 State: entered the native view request=0x1 kind=6",
                                               PRONE.replace("10:00:02.900", "10:00:03.100"),
                                               "10:00:04.000 State: left the native view: x"]
        self.assertEqual(dive.dive_leaves(lines), [{"at": "10:00:02.400", "after_dive_s": 0.4, "veiled": 2,
                                                    "lowest_up": -0.51, "ahead": 0.12}])

    def test_feel54s_soaks_measured_the_head_in_a_dive(self):
        # round 32: the soak's overlap (swap 0.41 s after the dive key) left the view 0.77 s into the dive
        for name, at, low in (("tfp-soak-37.modlog", "10:04:58.182", -0.626), ("tfp-soak-38.modlog", "10:24:38.160", -0.6898)):
            path = CACHE / name
            if not path.is_file():
                self.skipTest(f"{path} is not here")
            leaves = {r["at"]: r for r in dive.dive_leaves(path.read_text(errors="replace").splitlines())}
            self.assertEqual(leaves[at]["lowest_up"], low)
            self.assertAlmostEqual(leaves[at]["after_dive_s"], 0.767, delta=0.002)
            self.assertEqual(min(r["lowest_up"] for r in leaves.values() if r["veiled"]), low)


class DiveSwap(unittest.TestCase):
    def setUp(self):
        self.log, self.clock = FakeLog(self), Clock()

    def run_dive(self, react):
        game = FakeGame(self.log, react)
        result = dive.dive_swap(game, dive.aim.LogTail(self.log.path), sleep=self.clock.sleep, clock=self.clock)
        return game.scripts, result

    def test_the_view_left_at_the_second_delay(self):
        scripts, result = self.run_dive(standing_game(dive.SWAP_AFTER_S[1]))
        back = dive.PRIMARY + "; wait 1.7"
        # the try before landed prone: z stands the body up before the next try and after the last
        self.assertEqual(scripts, [dive.PRIMARY, dive.script(0.1), back, dive.PRIMARY, "key z", dive.script(0.15), back,
                                   "key z"])
        self.assertTrue(result["left"])
        self.assertEqual(result["swap_after"], 0.15)
        self.assertEqual([a["left"] for a in result["attempts"]], [False, True])
        self.assertEqual([a["stood"]["presses"] for a in result["attempts"]], [[], ["key z"]])
        self.assertTrue(result["attempts"][1]["dived"] and result["attempts"][1]["from_native"])
        self.assertTrue(result["stood"]["standing"])
        self.assertEqual(result["lowest_up"], -0.51)
        self.assertTrue(dive.verdict(result)["pass"])
        self.assertEqual(result["notes"], [])
        self.assertIn("swap 0.15 s into the dive (try 2); 2 veiled frames, the head lowest -0.510 m", dive.text(result))

    def test_every_try_starts_on_its_feet(self):
        scripts, result = self.run_dive(standing_game(None))
        self.assertEqual(scripts.count("key z"), len(dive.SWAP_AFTER_S))   # before tries 2 to 5 and after the last
        self.assertTrue(all(a["dived"] and a["stood"]["standing"] for a in result["attempts"]))
        for i, script in enumerate(scripts):
            if script.startswith("down w") and i > 1:
                self.assertEqual(scripts[i - 2:i], [dive.PRIMARY, "key z"])

    def test_never_left_says_the_head_is_unmeasured(self):
        scripts, result = self.run_dive(lambda s, log, n: log.write(NATIVE) if s == dive.PRIMARY else None)
        self.assertEqual(len(result["attempts"]), len(dive.SWAP_AFTER_S))
        self.assertFalse(result["left"])
        v = dive.verdict(result)
        self.assertFalse(v["pass"])
        self.assertIn("still unmeasured", v["notes"][0])
        self.assertIn("no dive in the try", " ".join(v["notes"]))
        self.assertEqual(scripts[-1], dive.PRIMARY + "; wait 1.7")
        self.assertIn("never left (5 tries: 0.10 s no dive;", dive.text(result))

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
        self.assertEqual(result["attempts"][0]["dive_s"], 0.3)
        self.assertEqual(result["attempts"][0]["leave_after_s"], 0.4)
        self.assertIn("left after the dive, not in it", " ".join(result["notes"]))
        self.assertIn("0.10 s dive 0.30 s left at 0.40 s", dive.text(result))
        self.assertFalse(dive.verdict(result)["pass"])

    def test_a_body_that_will_not_stand_is_noted(self):
        self.log.write(PRONE)
        _, result = self.run_dive(lambda s, log, n: log.write(NATIVE) if s == dive.PRIMARY else None)
        self.assertIn("could not be stood up before a try (the log said prone", " ".join(result["notes"]))

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
