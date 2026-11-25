"""Round 26: the harness reaches full aim (tools/harness/aim.py, drive.py `keep`, win_input.c "k" lines, the aim phases
of matrix.py and soak.py). Offline: the input sequence is what is intended and the log is read as the mod writes it.
Only the game can say that the sequence puts it in full aim (docs/guides/AIM-TEST.md "The harness")."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import aim  # noqa: E402
import drive  # noqa: E402
import matrix  # noqa: E402
import soak  # noqa: E402

CACHE = Path.home() / ".cache"
AIM_IN_LINE = '10:00:01.000 Aim: in: "R-4 Hyena"; fov 30.8 shown 30.8 (Aim field of view: the game\'s zoom); sight 0.220 m (near clip), ' \
    'wanted 0.000: pull 0.000, closer 0.000 (ADS fwd +0.000), near clip 0.220 m; depth 0.270 m; editor closed | 9 frames: gate set 0 of 9 noted'


class Clock:
    """A clock that only moves when the code sleeps."""

    def __init__(self):
        self.now = 0.0
        self.slept = []

    def sleep(self, seconds):
        self.slept.append(seconds)
        self.now += seconds

    def __call__(self):
        return self.now


class FakeLog:
    """A log file the fake game writes to; aim.LogTail reads it."""

    def __init__(self, test):
        self.dir = Path(tempfile.mkdtemp(prefix="tfp-aim-test-", dir=CACHE if CACHE.is_dir() else None))
        test.addCleanup(shutil.rmtree, self.dir, True)
        self.path = self.dir / "mod.log"
        self.path.write_text("09:59:59.000 Ready: something before\n")

    def write(self, *lines):
        with open(self.path, "a") as handle:
            handle.write("".join(line + "\n" for line in lines))


class FakeGame:
    """Records every script; `react(script, log)` writes what the game would log after it. With a Clock, a script
    takes its own time on it (drive.events: the real helper blocks for the whole script), and `times` keeps each
    script's (start, end)."""

    def __init__(self, log, react=None, clock=None):
        self.log, self.react, self.scripts, self.clock, self.times = log, react, [], clock, []

    def run(self, script):
        self.scripts.append(script)
        begun = self.clock() if self.clock else 0.0
        if self.clock:
            self.clock.now += drive.events(script)[1]
        self.times.append((begun, self.clock() if self.clock else 0.0))
        if self.react:
            self.react(script, self.log, len(self.scripts))
        return ""


class Events(unittest.TestCase):
    def test_keep_is_a_line_for_the_helper_and_takes_no_time(self):
        lines, total = drive.events("down right; keep right; wait 0.35")
        self.assertEqual(lines, ["0 d R", "30 k R"])
        self.assertAlmostEqual(total, 0.38)

    def test_the_aim_mode_button_is_the_middle_button(self):
        self.assertEqual(drive.events(f"click {aim.MODE_KEY}")[0], ["0 d M", "80 u M"])

    def test_the_weapon_functions_menu_is_r_held_then_an_arrow(self):
        lines, _ = drive.events(f"down r; wait {aim.MENU_S}; key arrowright; wait 0.2; up r")
        self.assertEqual(lines, ["0 d 82", "830 d 39", "910 u 39", "1140 u 82"])

    def test_the_wheel_is_one_line_a_notch_up_or_down(self):
        # round 30: the game's scope range on PC is hold R and the mouse wheel up
        lines, total = drive.events("wheel 2")
        self.assertEqual(lines, ["0 w 120", "100 w 120"])
        self.assertAlmostEqual(total, 2 * drive.WHEEL_GAP_S)
        self.assertEqual(drive.events("wheel -1")[0], ["0 w -120"])
        for bad in ("wheel 0", f"wheel {drive.WHEEL_MOST + 1}"):
            with self.assertRaises(drive.DriveError):
                drive.events(bad)

    def test_the_range_step_is_r_held_then_the_wheel_up(self):
        lines, _ = drive.events(aim.ZOOM_STEP)
        self.assertEqual(lines, ["0 d 82", "830 w 120", "1230 u 82"])
        self.assertEqual(drive.held_after(aim.ZOOM_STEP, {"right"}), {"right"})   # the aim stays held through it

    def test_what_is_held_after_a_script(self):
        self.assertEqual(drive.held_after("down right; keep right; wait 0.35"), {"right"})
        self.assertEqual(drive.held_after("click middle", {"right"}), {"right"})
        self.assertEqual(drive.held_after("up right", {"right"}), set())
        self.assertEqual(drive.held_after("down w; wait 1; up w"), set())
        self.assertEqual(drive.held_after("down right; wait 1"), set())   # not kept: the helper lets go at its end


@unittest.skipUnless(shutil.which("cc"), "no C compiler")
class Helper(unittest.TestCase):
    """win_input.c built against tests/harness_win_stub/windows.h: what it sends, and what it lets go of at its end."""

    @classmethod
    def setUpClass(cls):
        cls.dir = Path(tempfile.mkdtemp(prefix="tfp-win-input-", dir=CACHE if CACHE.is_dir() else None))
        cls.exe = cls.dir / "win_input"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "tests/harness_win_stub"),
                        str(ROOT / "tools/harness/win_input.c"), "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, True)

    def play(self, script):
        work = Path(tempfile.mkdtemp(dir=self.dir))
        lines, _ = drive.events(script)
        (work / "script.txt").write_text("\n".join(lines) + "\n")
        code = subprocess.run([str(self.exe)], cwd=work).returncode
        sent = [line.split(" ", 1)[1] for line in (work / "sent.txt").read_text().splitlines()]
        return code, sent, (work / "result.txt").read_text()

    def test_a_kept_aim_button_stays_down(self):
        code, sent, result = self.play("down right; keep right; wait 0.35")
        self.assertEqual(code, 0)
        self.assertEqual(sent, ["button R down"])
        self.assertIn("kept=1", result)

    def test_without_keep_the_helper_lets_go_at_its_end(self):
        # what every aim phase did before round 26 across two runs: the button went up when the first helper ended
        _, sent, result = self.play("down right; wait 0.35")
        self.assertEqual(sent, ["button R down", "button R up"])
        self.assertIn("kept=0", result)

    def test_the_mode_switch_and_the_release_in_later_runs(self):
        self.assertEqual(self.play("click middle")[1], ["button M down", "button M up"])
        self.assertEqual(self.play("up right")[1], ["button R up"])

    def test_the_wheel_reaches_send_input_with_whole_notches(self):
        code, sent, result = self.play("down r; wait 0.8; wheel 1; wait 0.3; up r; wheel -2")
        self.assertEqual(code, 0)
        self.assertEqual(sent, ["key 82 down", "wheel 120", "key 82 up", "wheel -120", "wheel -120"])
        self.assertIn("played=5 failed=0", result)

    def test_a_part_notch_or_none_is_refused(self):
        work = Path(tempfile.mkdtemp(dir=self.dir))
        (work / "script.txt").write_text("0 w 60\n10 w 0\n20 w 120\n")
        code = subprocess.run([str(self.exe)], cwd=work).returncode
        self.assertEqual(code, 4)
        self.assertEqual([l.split(" ", 1)[1] for l in (work / "sent.txt").read_text().splitlines()], ["wheel 120"])
        self.assertIn("played=3 failed=2", (work / "result.txt").read_text())

    def test_keep_keeps_only_what_it_names(self):
        _, sent, _ = self.play("down right; keep right; down w; wait 0.2")
        self.assertEqual(sent, ["button R down", "key 87 down", "key 87 up"])


class FullAim(unittest.TestCase):
    def setUp(self):
        self.log, self.clock = FakeLog(self), Clock()

    def aim(self, react, during=None, hold=aim.HOLD_S):
        game = FakeGame(self.log, react)
        tail = aim.LogTail(self.log.path)
        result = aim.full_aim(game, tail, hold, during, sleep=self.clock.sleep, clock=self.clock)
        return game.scripts, result, tail

    def test_the_sequence_when_the_first_press_gives_full_aim(self):
        def react(script, log, n):
            if script == "click middle":
                log.write("10:00:00.700 State: aim in", AIM_IN_LINE)
            if script == "up right":
                log.write("10:00:03.200 State: aim out", "10:00:03.200 Aim: out after 2.5 s; zoom levels (game fov: sight m, depth m, optic): 30.8: sight 0.220, depth 0.270 | x")
        scripts, result, tail = self.aim(react)
        self.assertEqual(scripts, ["down right; keep right; wait 0.35", "click middle", "up right"])
        self.assertTrue(result["reached"])
        self.assertEqual(result["switches"], 1)
        self.assertFalse(result["lost"])
        self.assertIn(aim.HOLD_S, self.clock.slept)   # held 2 s once the log said full aim
        tail.read()
        self.assertTrue(aim.verdict(result, tail.lines)["pass"])

    def test_a_second_press_where_the_first_took_a_remembered_mode_out(self):
        def react(script, log, n):
            if script == "click middle" and n == 3:
                log.write("10:00:02.300 State: aim in")
        scripts, result, _ = self.aim(react)
        self.assertEqual(scripts, ["down right; keep right; wait 0.35", "click middle", "click middle", "up right"])
        self.assertTrue(result["reached"])
        self.assertEqual(result["switches"], 2)

    def test_never_in_full_aim_lets_go_and_says_so(self):
        # the shoulder aim of the harness before round 26: Motion says aim, the State line never comes
        scripts, result, tail = self.aim(lambda s, log, n: log.write("10:00:00.400 Motion: action=aim placement=x"))
        self.assertEqual(scripts, ["down right; keep right; wait 0.35", "click middle", "click middle", "up right"])
        self.assertFalse(result["reached"])
        self.assertIsNone(result["to_full_s"])
        v = aim.verdict(result, tail.lines)
        self.assertFalse(v["pass"])
        self.assertIn("did not go into full aim", v["notes"][0])
        self.assertLessEqual(self.clock.now, 2 * aim.WAIT_S + 2 * aim.POLL_S + aim.RAISE_S)

    def test_a_partial_line_is_read_when_it_is_whole(self):
        tail = aim.LogTail(self.log.path)
        with open(self.log.path, "a") as handle:
            handle.write("10:00:00.700 State: aim i")
        self.assertEqual(tail.read(), [])
        with open(self.log.path, "a") as handle:
            handle.write("n\n")
        self.assertEqual(tail.read(), ["10:00:00.700 State: aim in"])

    def test_the_aim_is_let_go_when_the_action_fails(self):
        game = FakeGame(self.log, lambda s, log, n: log.write("10:00:00.700 State: aim in") if s == "click middle" else None)

        def during(device, tail, result):
            raise drive.DriveError("the game does not have focus")
        with self.assertRaises(drive.DriveError):
            aim.full_aim(game, aim.LogTail(self.log.path), 2.0, during, sleep=self.clock.sleep, clock=self.clock)
        self.assertEqual(game.scripts[-1], "up right")

    def test_lost_full_aim_fails(self):
        def react(script, log, n):
            if script == "click middle":
                log.write("10:00:00.700 State: aim in", "10:00:01.100 State: aim out")
        _, result, _ = self.aim(react)
        self.assertTrue(result["reached"])
        self.assertTrue(result["lost"])

    def test_an_action_in_full_aim(self):
        def react(script, log, n):
            if script == "click middle":
                log.write("10:00:00.700 State: aim in")
        scripts, _, _ = self.aim(react, aim.during_script("click left 0.5; wait 0.4"), hold=0.3)
        self.assertEqual(scripts, ["down right; keep right; wait 0.35", "click middle", "click left 0.5; wait 0.4", "up right"])


def _aim_line(kind, fov, sight=0.220, depth=0.270, at="10:00:01.000"):
    return (AIM_IN_LINE.replace("10:00:01.000 Aim: in", f"{at} Aim: {kind}").replace("fov 30.8 shown 30.8", f"fov {fov} shown {fov}")
            .replace("sight 0.220 m", f"sight {sight:.3f} m").replace("depth 0.270 m", f"depth {depth:.3f} m"))


class ZoomCycle(unittest.TestCase):
    """Round 30: the scope's range is hold R and the mouse wheel up (aim.ZOOM_STEP), in full aim first, then before
    aiming; each range held aim.ZOOM_HOLD_S in full aim, judged by the field of view of the "Aim:" lines."""

    def setUp(self):
        self.log, self.clock = FakeLog(self), Clock()

    def zoom(self, react):
        self.game = game = FakeGame(self.log, react, self.clock)
        tail = aim.LogTail(self.log.path)
        result = aim.scope_zoom(game, tail, sleep=self.clock.sleep, clock=self.clock)
        tail.read()
        return game.scripts, result, tail

    def free_holds(self, step):
        """For each run of `step`: the seconds from its end (R let go: the menu shut) to the next script."""
        times, scripts = self.game.times, self.game.scripts
        return [round(times[i + 1][0] - times[i][1], 3) for i, s in enumerate(scripts) if s == step]

    def test_the_hold_is_two_seconds(self):
        # the brief: each range held at least 2 s in full aim (the testers' shake is in a range held)
        self.assertEqual(aim.ZOOM_HOLD_S, 2.0)

    def test_each_range_in_full_aim_is_held_and_the_ranges_come_round(self):
        fovs = iter([20.0, 10.0, 30.8])

        def react(script, log, n):
            if script == "click middle":
                log.write("10:00:00.700 State: aim in", AIM_IN_LINE)
            elif script == aim.ZOOM_STEP:
                fov = next(fovs)
                log.write(_aim_line("changed", fov, sight=0.22 if fov == 30.8 else 0.35))
            elif script == "up right":
                log.write("10:00:08.000 State: aim out", "10:00:08.000 Aim: out after 6.6 s; zoom levels (game fov: sight m, depth m, optic): "
                          "30.8: sight 0.220, depth 0.270; 20.0: sight 0.350, depth 0.270; 10.0: sight 0.350, depth 0.270 | x")
        scripts, result, tail = self.zoom(react)
        self.assertEqual(scripts, ["down right; keep right; wait 0.35", "click middle", aim.ZOOM_STEP, aim.ZOOM_STEP, aim.ZOOM_STEP, "up right"])
        self.assertEqual(result["zoom_where"], "in aim")
        self.assertEqual([(z["fov_before"], z["fov_after"], z["zoomed"]) for z in result["zoom_steps"]],
                         [(30.8, 20.0, True), (20.0, 10.0, True), (10.0, 30.8, True)])
        self.assertTrue(result["zoom_steps"][0]["seat_moved"])          # 0.220 -> 0.350 m: the game reseated the sight
        self.assertTrue(all(z["full_aim"] for z in result["zoom_steps"]))
        # every range, the last too, held 2 s of free full aim after R is let go: the menu's own time is not the hold
        # (round 30's review: begun before the step left 0.77 s with the menu shut, the last range 1.1 s)
        holds = self.free_holds(aim.ZOOM_STEP)
        self.assertEqual(len(holds), 3)
        self.assertTrue(all(h >= 2.0 for h in holds), holds)
        self.assertEqual(aim.zoom_levels(tail.lines), [10.0, 20.0, 30.8])
        self.assertTrue(aim.verdict(result, tail.lines, aim.ZOOM_HOLD_S, zoom=True)["pass"])

    def test_no_change_in_aim_then_the_ranges_before_aiming(self):
        aims = [0]

        def react(script, log, n):
            if script == "click middle":
                aims[0] += 1
                log.write(f"10:00:{aims[0]:02d}.700 State: aim in", _aim_line("in", 30.8 if aims[0] < 3 else 15.0, at=f"10:00:{aims[0]:02d}.900"))
            elif script == "up right":
                log.write(f"10:00:{aims[0]:02d}.990 State: aim out", f"10:00:{aims[0]:02d}.990 Aim: out after 2.4 s; zoom levels (game fov: "
                          "sight m, depth m, optic): 30.8: sight 0.220, depth 0.270 | x")
        scripts, result, tail = self.zoom(react)
        in_aim = [z for z in result["zoom_steps"] if z["where"] == "in aim"]
        before = [z for z in result["zoom_steps"] if z["where"] == "before aim"]
        self.assertEqual(len(in_aim), aim.ZOOM_STEPS)
        self.assertFalse(any(z["zoomed"] for z in in_aim))
        self.assertEqual(result["zoom_where"], "before aim")
        self.assertEqual([(z["fov_before"], z["fov_after"], z["zoomed"]) for z in before[:2]], [(30.8, 30.8, False), (30.8, 15.0, True)])
        # before aiming the step comes with no aim held, then a whole full aim of its own
        first_before = scripts.index(aim.ZOOM_STEP + "; wait 0.5")
        self.assertEqual(scripts[first_before - 1], "up right")
        self.assertEqual(scripts[first_before + 1:first_before + 4], ["down right; keep right; wait 0.35", "click middle", "up right"])

    def test_no_range_changes_the_zoom(self):
        # feel.53's soak: the BR-14 at fov 30.0 in every Aim: line
        def react(script, log, n):
            if script == "click middle":
                log.write("10:00:00.700 State: aim in", AIM_IN_LINE)
            elif script == "up right":
                log.write("10:00:03.000 State: aim out")
        scripts, result, tail = self.zoom(react)
        self.assertIsNone(result["zoom_where"])
        self.assertEqual(len(result["zoom_steps"]), 2 * aim.ZOOM_STEPS)
        self.assertIn("no range step changed the field of view", result["notes"][-1])
        self.assertFalse(aim.verdict(result, tail.lines, aim.ZOOM_HOLD_S, zoom=True)["pass"])

    def test_full_aim_lost_by_r_in_the_sights(self):
        def react(script, log, n):
            if script == "click middle":
                log.write("10:00:00.700 State: aim in", AIM_IN_LINE)
            elif script == aim.ZOOM_STEP and n == 3:
                log.write("10:00:01.700 State: aim out")
        _, result, _ = self.zoom(react)
        self.assertIn("full aim was lost while the scope's range was stepped", " ".join(result["notes"]))
        self.assertFalse(result["zoom_steps"][0]["full_aim"])

    def test_a_capture_for_each_range(self):
        shots = []
        game = FakeGame(self.log, lambda s, log, n: log.write("10:00:00.700 State: aim in", AIM_IN_LINE) if s == "click middle" else None)
        result = aim.scope_zoom(game, aim.LogTail(self.log.path), self.clock.sleep, self.clock, capture=lambda i: shots.append(i) or f"z-{i}.png")
        self.assertEqual(shots, list(range(1, 2 * aim.ZOOM_STEPS + 1)))
        self.assertEqual(result["zoom_steps"][0]["shot"], "z-1.png")


class Reading(unittest.TestCase):
    def test_the_out_line_lists_each_zoom_level(self):
        line = ("10:00:05.000 Aim: out after 6.2 s; zoom levels (game fov: sight m, depth m, optic): 30.8: sight 0.220, depth 0.270, "
                "optic 30.00 deg x3.00; 15.0: sight 0.350, depth 0.300, optic 15.00 deg x6.00 | 186 frames: gate set 0")
        row = aim.aim_lines([line])[0]
        self.assertEqual((row["kind"], row["held"], row["levels"], row["tap"]), ("out", 6.2, [30.8, 15.0], False))

    def test_a_tap(self):
        row = aim.aim_lines(["10:00:05.000 Aim: out after 0.2 s (a tap: not settled) | 6 frames"])[0]
        self.assertTrue(row["tap"])

    def test_a_tap_is_not_two_seconds_of_full_aim(self):
        lines = ["10:00:00.700 State: aim in", AIM_IN_LINE, "10:00:01.000 Aim: out after 0.2 s (a tap: not settled) | 6 frames"]
        self.assertFalse(aim.verdict({"reached": True, "lost": False, "notes": []}, lines)["pass"])


class Phases(unittest.TestCase):
    def test_every_aim_phase_is_played_in_full_aim(self):
        for name in ("aim", "aim-fire", "aim-walk", "pistol-aim", "aim-full", "aim-scope-zoom"):
            self.assertIn(name, matrix.AIMED)
            self.assertIn(name, matrix.PHASES)

    def test_a_plain_run_of_an_aim_phase_also_presses_the_aim_mode_button(self):
        for name in matrix.AIMED:
            script = matrix.PHASES[name][0]
            self.assertLess(script.index("down right"), script.index("click middle"), name)
            self.assertLess(script.index("click middle"), script.index("up right"), name)

    def test_no_aim_is_a_bare_right_click_any_more(self):
        for name, (script, _) in matrix.PHASES.items():
            self.assertNotIn("click right", script, name)
        held = [o for o in soak.OVERLAPS if "down right" in o]
        self.assertTrue(held and all("click middle" in o for o in held))

    def test_the_soak_still_has_its_sixteen_overlaps(self):
        # round 26 put a comment on the full-aim swap's line and took the next two overlaps into it
        self.assertEqual(len(soak.OVERLAPS), 16)
        self.assertIn("key c; wait 0.4; key g; wait 2.5; key c", soak.OVERLAPS)

    def test_the_soak_plays_the_aim_phases_and_not_the_zoom(self):
        names = [n for n in matrix.PHASES if n not in soak.SKIP]
        self.assertTrue({"aim", "aim-fire", "aim-walk", "pistol-aim", "aim-full"} <= set(names))
        self.assertNotIn("aim-scope-zoom", names)

    def test_play_hands_an_aim_phase_to_full_aim(self):
        log, calls = FakeLog(self), []
        original, zoom = aim.full_aim, aim.scope_zoom
        aim.full_aim = lambda device, tail, hold, action: calls.append((hold, action)) or {"reached": False}
        aim.scope_zoom = lambda device, tail, capture=None: calls.append(("zoom", capture)) or {"reached": False}
        try:
            matrix.play(FakeGame(log), "aim-full", log.path)
            matrix.play(FakeGame(log), "aim-scope-zoom", log.path)
            game = FakeGame(log)
            self.assertIsNone(matrix.play(game, "walk", log.path))
        finally:
            aim.full_aim, aim.scope_zoom = original, zoom
        self.assertEqual(calls, [(aim.HOLD_S, None), ("zoom", None)])
        self.assertEqual(game.scripts, ["hold w 1.5"])

    def test_the_zoom_phase_steps_the_range_with_the_wheel(self):
        # round 30: R held and the wheel up; no arrow key (round 26's guess) is left in any phase
        self.assertIn("down r; wait 0.8; wheel 1; wait 0.3; up r", matrix.PHASES["aim-scope-zoom"][0])
        for name, (script, _) in matrix.PHASES.items():
            self.assertNotIn("arrow", script, name)

    def test_the_zoom_line(self):
        text = matrix.aim_text({"reached": True, "to_full_s": 0.5, "switches": 1, "zoom_where": "in aim",
                                "zoom_steps": [{"fov_after": 20.0, "zoomed": True}, {"fov_after": 20.0, "zoomed": False}]})
        self.assertIn("range steps 2: the zoom changed in aim (fov 20.0* 20.0)", text)


class Before(unittest.TestCase):
    """The numbers of round 26's before, from the soak logs in ~/.cache (skipped where they are not there)."""

    def counts(self, name):
        path = CACHE / name
        if not path.is_file():
            self.skipTest(f"{path} is not here")
        return aim.counts(path.read_text(errors="replace").splitlines())

    def test_feel52_soaks_never_reached_full_aim(self):
        for name in ("tfp-soak-33.modlog", "tfp-soak-34.modlog"):
            c = self.counts(name)
            self.assertEqual((c["aim_in"], c["ads_seconds"], sum(c["aim_lines"].values())), (0, 0, 0), name)
            self.assertGreater(c["motion_aim"], 0, name)   # the shoulder aim: the right button did reach the game

    def test_the_users_own_play_did(self):
        c = self.counts("tfp-soak-31-user.modlog")
        self.assertEqual(c["aim_in"], 13)
        self.assertGreater(c["ads_seconds"], 0)


if __name__ == "__main__":
    unittest.main()
