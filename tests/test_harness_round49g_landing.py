"""Round 49g: the landing of a vault, judged on the raw vertical step the eye shows (tools/harness/matrix.py landings).

At the end of a low vault the game drops the body root about 0.27 m in one frame and brings it back up over the next
frames; the rig eye is given the drop a frame late. The build that wrote these logs takes the drop up and eases it.
round49d's vault rule stopped looking for jumps for 600 ms after a traversal and showed it raw: 0.26 m down in one
frame and 0.16 m up in the next, which matrix.measure names "the body's own" and does not count (the independent
check of 2026-11-27, blocking finding 1).

The rows: the Trace, Hold, State, Lock and Smooth lines of
  tests/reference/landing/feel58-soak51B-232320.log   ~/.cache/tfp-soak-51-B.modlog 23:23:20.1 to 21.2 (feel.58)
  tests/reference/landing/feel50-soak27-125657.log    ~/.cache/tfp-soak-27.modlog   12:56:56.7 to 57.7 (feel.50)
  tests/reference/landing/feel49-soak25-101352.log    ~/.cache/tfp-soak-25.modlog   10:13:51.9 to 53.0 (feel.49)
  tests/reference/landing/feel58-soak51B-232343.log   ~/.cache/tfp-soak-51-B.modlog 23:23:42.9 to 43.7 (feel.58)
"raw" below is the same rows with the wanted eye shown and nothing carried, which is what the vault rule showed in
these frames (tools/harness/replay.py with round49d's header: the eye shown equal to the wanted eye, 0 carried)."""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import matrix  # noqa: E402
import trace  # noqa: E402

B = ROOT / "tests/reference/landing/feel58-soak51B-232320.log"
S27 = ROOT / "tests/reference/landing/feel50-soak27-125657.log"
S25 = ROOT / "tests/reference/landing/feel49-soak25-101352.log"
B2 = ROOT / "tests/reference/landing/feel58-soak51B-232343.log"
EYES = re.compile(r"eye_world=\(([-\d. ]+)\)(.*?) wanted=\(([-\d. ]+)\) correction=[\d.]+")


def raw(lines, share=1.0):
    """The rows with the eye shown moved `share` of the way to the wanted eye (1: the wanted eye, nothing carried)."""
    def one(m):
        eye, wanted = ([float(x) for x in m[k].split()] for k in (1, 3))
        new = [e + share * (w - e) for e, w in zip(eye, wanted)]
        left = sum((w - n) ** 2 for w, n in zip(wanted, new)) ** 0.5
        return f"eye_world=({' '.join(f'{x:.5f}' for x in new)}){m[2]} wanted=({m[3]}) correction={left:.4f}"
    return [EYES.sub(one, line) if " Trace: " in line else line for line in lines]


TRACE_EYE = re.compile(r"eye_world=\(([-\d. ]+)\)(.*?) wanted=\(([-\d. ]+)\) correction=[\d.]+")
TRACE_TICK = re.compile(r" tick=([\d.]+)")


def nolag(lines, drop_at, share=1.0, count=1000):
    """The rows with the eye made to follow the root with no lag from the drop frame `drop_at` on, for `count` frames
    (share 1: the eye keeps the offset from the root it had the frame before the drop; 0.5: half the root's move),
    wanted equal to it and no veil (the independent check's repro, ~/.cache/tfp-verify72-stance/scripts/nolag.py, which
    let the eye go back to the logged one after four frames; here it stays with the root, so that nothing but the
    landing is left to judge)."""
    rows = trace.frames(lines)
    i = next(k for k, r in enumerate(rows) if r["at"] == drop_at)
    base, origin = rows[i - 1]["eye"], rows[i - 1]["root"]
    eyes = {f"{r['tick']:.3f}": [b + share * (q - o) for b, q, o in zip(base, r["root"], origin)] for r in rows[i:i + count]}

    def one(line):
        tick = TRACE_TICK.search(line)
        new = eyes.get(f"{float(tick[1]):.3f}") if tick and " Trace: " in line else None
        if new is None:
            return line
        text = " ".join(f"{x:.5f}" for x in new)
        line = TRACE_EYE.sub(lambda m: f"eye_world=({text}){m[2]} wanted=({text}) correction=0.0000", line)
        return re.sub(r" veil=\([-\d. ]+\)", " veil=0", line)
    return [one(line) for line in lines]


class Rows(unittest.TestCase):
    """What the rows say, before any judge."""

    def test_the_root_drops_in_one_frame_after_the_hold_and_the_eye_is_given_it_a_frame_late(self):
        for log, drop_at, drop, eye_at, eye in ((B, "23:23:20.749", -0.272, "23:23:20.781", -0.259), (S27, "12:56:57.287", -0.272, "12:56:57.320", -0.25)):
            lines = log.read_text().splitlines()
            rows = trace.frames(lines)
            i = next(k for k, r in enumerate(rows) if r["at"] == drop_at)
            self.assertAlmostEqual(rows[i]["root"][2] - rows[i - 1]["root"][2], drop, places=2)
            self.assertAlmostEqual(rows[i]["wanted"][2] - rows[i - 1]["wanted"][2], 0.0, places=1)   # the rig eye not yet
            self.assertEqual(rows[i + 1]["at"], eye_at)
            self.assertAlmostEqual(rows[i + 1]["wanted"][2] - rows[i]["wanted"][2], eye, places=2)
            self.assertGreater(rows[i + 1]["root"][2] - rows[i]["root"][2], 0.08)                    # the root already back up
            masks = matrix.mask_lines(lines)
            self.assertTrue(any(m & matrix.TRAVERSAL for _, m in masks))
            self.assertEqual(masks[-1][1], 0)
            self.assertTrue(all(r["kind"] == 6 for r in rows))


class Measure(unittest.TestCase):
    def test_beyond_the_bodys_own_step(self):
        for shown, body, want in ((-0.259, 0.108, 0.259), (0.088, 0.236, 0.0), (0.30, 0.15, 0.15), (-0.19, -0.14, 0.05), (0.02, -0.27, 0.02),
                                  (-0.30, -0.27, 0.03), (0.0, 0.0, 0.0)):
            self.assertAlmostEqual(matrix.beyond_body(shown, body), want, places=6, msg=(shown, body))

    def found(self, lines):
        return matrix.landings(trace.frames(lines), matrix.mask_lines(lines))

    def test_the_logged_rows_ease_it(self):
        # (the landing, the drop, then for each of its two frames: when, shown, the body's own, beyond)
        for log, at, frames in ((B, "23:23:20.749", (("23:23:20.781", -0.007, 0.108, 0.007), ("23:23:20.815", 0.088, 0.236, 0.0))),
                                (S27, "12:56:57.287", (("12:56:57.320", -0.012, 0.092, 0.012), ("12:56:57.354", 0.217, 0.244, 0.0)))):
            found = self.found(log.read_text().splitlines())
            self.assertEqual([l["at"] for l in found], [at])
            self.assertAlmostEqual(found[0]["drop"], -0.272, places=2)
            self.assertEqual(len(found[0]["frames"]), matrix.LAND_FRAMES)
            for f, (when, shown, body, beyond) in zip(found[0]["frames"], frames):
                self.assertEqual(f["at"], when)
                self.assertAlmostEqual(f["shown"], shown, places=2)
                self.assertAlmostEqual(f["body"], body, places=2)
                self.assertAlmostEqual(f["beyond"], beyond, places=2)

    def test_shown_raw_it_is_a_quarter_metre_beyond_the_body(self):
        for log, first, second in ((B, (-0.259, 0.259), (0.162, 0.0)), (S27, (-0.25, 0.25), (0.15, 0.0))):
            found = self.found(raw(log.read_text().splitlines()))
            self.assertEqual(len(found), 1)
            for f, (shown, beyond) in zip(found[0]["frames"], (first, second)):
                self.assertAlmostEqual(f["shown"], shown, places=2)
                self.assertAlmostEqual(f["beyond"], beyond, places=2)

    def test_a_drop_is_a_landing_only_just_after_a_traversal(self):
        lines = B.read_text().splitlines()
        never = [line.replace("mask=00000020", "mask=00000000") for line in lines]
        self.assertEqual(self.found(never), [])
        still = [line.replace("mask=00000000", "mask=00000020") for line in lines]
        self.assertEqual(self.found(still), [])                   # the hold not yet over: its frames are the vault's own
        self.assertEqual(self.found([line for line in lines if " Hold: " not in line]), [])   # the mask not known
        early = [line for line in lines if not (" Hold: " in line and "mask=00000020" in line and line[:12] > "23:23:20.400")]
        self.assertEqual(self.found(early), [])                   # the last traversal frame over LAND_AFTER_MS before


class Judge(unittest.TestCase):
    def test_the_judge_until_now_called_the_raw_drop_the_bodys_own(self):
        lines = raw(B.read_text().splitlines())
        by_at = {f["at"]: f for f in matrix.measure(trace.frames(lines))}
        for at, shown in (("23:23:20.781", 0.256), ("23:23:20.815", 0.422)):
            self.assertEqual(by_at[at]["whose"], "body")
            self.assertAlmostEqual(by_at[at]["shown"], shown, places=2)
            self.assertLess(by_at[at]["beyond"], matrix.POP_M)
        # the landing by itself (the last frames of the hold to 150 ms after the drop): the phase passed
        near = [line for line in lines if "23:23:20.600" <= line[:12] <= "23:23:20.930"]
        result = matrix.judge(near)
        self.assertNotIn("landing_m", result)
        self.assertLess(result["largest_jump_m"], matrix.POP_M)
        self.assertGreater(result["body_m"], 0.38)
        self.assertTrue(result["pass"])            # a quarter of a metre down in one frame and up again, and it passed
        self.assertFalse(matrix.judge(near, landing=True)["pass"])

    def test_a_vault_phase_fails_on_it(self):
        for log, at, shown, body in ((B, "23:23:20.781", "-0.259", "+0.108"), (S27, "12:56:57.320", "-0.253", "+0.087")):
            result = matrix.judge(raw(log.read_text().splitlines()), landing=True)
            self.assertFalse(result["pass"])
            self.assertEqual(result["landings"], 1)
            self.assertGreater(result["landing_m"], 0.24)
            self.assertEqual(result["landing_at"], at)
            note = next(n for n in result["notes"] if n.startswith("landing"))
            for text in (at, shown, body, "beyond the body's own", "over"):
                self.assertIn(text, note)
            self.assertIn("landing", matrix.describe(result))
            self.assertIn(shown, matrix.describe(result))

    def test_the_logged_rows_do_not_fail_on_it_and_the_step_is_printed(self):
        for log, at, shown in ((B, "23:23:20.815", "+0.088"), (S27, "12:56:57.354", "+0.217")):
            lines = log.read_text().splitlines()
            result = matrix.judge(lines, landing=True)
            self.assertEqual(result["landings"], 1)
            self.assertLess(result["landing_m"], 0.02)
            self.assertLessEqual(result["landing_m"], matrix.LAND_M)
            note = next(n for n in result["notes"] if n.startswith("landing"))
            self.assertNotIn("over", note)
            self.assertEqual(result["landing_step_at"], at)        # the largest raw vertical step shown in its frames
            self.assertIn(shown, matrix.describe(result))
            # and nothing else of the verdict changed for it
            plain = matrix.judge(lines)
            self.assertEqual(result["pass"], plain["pass"])
            self.assertEqual(result["largest_jump_m"], plain["largest_jump_m"])

    def test_half_of_it_shown_still_fails(self):
        result = matrix.judge(raw(B.read_text().splitlines(), 0.5), landing=True)
        self.assertAlmostEqual(result["landing_m"], 0.133, places=2)
        self.assertFalse(result["pass"])
        self.assertIn("over", next(n for n in result["notes"] if n.startswith("landing")))

    def test_a_landing_with_no_frame_after_it_is_said_not_to_be_measured(self):
        lines = [line for line in B.read_text().splitlines() if line[:12] <= "23:23:20.760"]
        result = matrix.judge(lines, landing=True)
        self.assertEqual((result["landings"], result["landing_frames"]), (1, 0))
        self.assertIn("not measured", next(n for n in result["notes"] if n.startswith("landing")))

    def test_the_phases_and_the_limit(self):
        self.assertEqual(matrix.VAULTS, {"jump-vault", "clamber-low", "clamber-high"})
        self.assertTrue(matrix.VAULTS <= set(matrix.PHASES))
        self.assertEqual((matrix.LAND_M, matrix.LAND_DROP_M, matrix.LAND_FRAMES), (0.05, 0.15, 2))
        self.assertEqual((matrix.POP_M, matrix.OWN_M, trace.FLOAT_M), (0.06, 0.03, 0.26))


class DropFrame(unittest.TestCase):
    """The independent check's major finding: the drop frame was never measured, and an eye that moves with the root
    in the same frame was 0 beyond the body's own in every frame measured."""

    def test_the_logged_rows_hardly_move_the_eye_in_the_drop_frame(self):
        for log, at, shown in ((B, "23:23:20.749", -0.005), (S27, "12:56:57.287", -0.018)):
            lines = log.read_text().splitlines()
            found = matrix.landings(trace.frames(lines), matrix.mask_lines(lines))
            self.assertEqual(found[0]["dropped"]["at"], at)
            self.assertAlmostEqual(found[0]["dropped"]["shown"], shown, places=2)
            self.assertAlmostEqual(found[0]["dropped"]["body"], -0.272, places=2)
            result = matrix.judge(lines, landing=True)
            self.assertAlmostEqual(result["landing_drop_m"], abs(shown), places=2)
            self.assertEqual(result["landing_drop_at"], at)
            self.assertLessEqual(result["landing_drop_m"], matrix.LAND_STEP_M)
            self.assertEqual(result["pass"], matrix.judge(lines)["pass"])
            note = next(n for n in result["notes"] if n.startswith("landing"))
            self.assertIn(f"the eye shown {shown:+.3f}", note)
            self.assertNotIn("drop frame", note)
            self.assertIn("in the drop frame", matrix.describe(result))

    def test_the_largest_logged_drop_frame_step_is_under_the_bound(self):
        # soak 25 (feel.49; main's rules replayed give the same rows): a step off a ledge 167 ms after a landing, the
        # root going on down. 0.089 m in the drop frame is the most of any logged landing, so the bound passes it; the
        # landing itself fails on the frames after it (0.071 beyond the body's own), as before.
        lines = S25.read_text().splitlines()
        found = matrix.landings(trace.frames(lines), matrix.mask_lines(lines))
        self.assertEqual([l["at"] for l in found], ["10:13:52.412", "10:13:52.579"])
        self.assertAlmostEqual(found[1]["dropped"]["shown"], -0.089, places=3)
        self.assertLess(abs(found[1]["dropped"]["shown"]), matrix.LAND_STEP_M)
        result = matrix.judge(lines, landing=True)
        self.assertAlmostEqual(result["landing_drop_m"], 0.089, places=3)
        self.assertAlmostEqual(result["landing_m"], 0.071, places=3)
        note = [n for n in result["notes"] if n.startswith("landing")][1]
        self.assertNotIn("drop frame", note)
        self.assertIn("a snap of the view at the landing", note)

    def test_an_eye_that_follows_the_root_with_no_lag_was_beyond_nothing(self):
        for log, at, snap in ((B, "23:23:20.749", -0.272), (S27, "12:56:57.287", -0.272), (B2, "23:23:43.183", -0.188)):
            lines = nolag(log.read_text().splitlines(), at)
            found = matrix.landings(trace.frames(lines), matrix.mask_lines(lines))
            self.assertEqual(len(found), 1)
            self.assertTrue(all(f["beyond"] < 1e-3 for f in found[0]["frames"]))   # what the measure saw until now
            self.assertAlmostEqual(found[0]["dropped"]["shown"], snap, places=2)
            self.assertGreater(abs(found[0]["dropped"]["shown"]), matrix.LAND_STEP_M)
            by_at = {f["at"]: f for f in matrix.measure(trace.frames(lines))}
            self.assertEqual(by_at[at]["whose"], "body")                            # and the step judge lets it off

    def test_and_the_phase_passed_without_the_drop_frame_and_fails_with_it(self):
        # the rows from just before the drop, so that nothing but the landing is left to judge
        for log, at, begin, snap in ((B, "23:23:20.749", "23:23:20.600", "-0.272"), (B2, "23:23:43.183", "23:23:42.900", "-0.188")):
            lines = [line for line in nolag(log.read_text().splitlines(), at) if line[:12] >= begin]
            self.assertTrue(matrix.judge(lines)["pass"])
            result = matrix.judge(lines, landing=True)
            self.assertLessEqual(result["landing_m"], matrix.LAND_M)             # the frames after it: nothing beyond
            self.assertAlmostEqual(result["landing_drop_m"], abs(float(snap)), places=2)
            self.assertEqual(result["landing_drop_at"], at)
            self.assertFalse(result["pass"])
            note = next(n for n in result["notes"] if n.startswith("landing"))
            self.assertIn(f"the eye shown {snap}", note)
            self.assertIn("a snap of the view in the drop frame", note)
            self.assertIn(f"{abs(float(snap)):.3f} in the drop frame", matrix.describe(result))

    def test_half_the_drop_in_the_frame_still_fails(self):
        lines = nolag(B.read_text().splitlines(), "23:23:20.749", 0.5)
        found = matrix.landings(trace.frames(lines), matrix.mask_lines(lines))
        self.assertAlmostEqual(found[0]["dropped"]["shown"], -0.136, places=2)
        self.assertGreater(abs(found[0]["dropped"]["shown"]), matrix.LAND_STEP_M)
        self.assertFalse(matrix.judge(lines, landing=True)["pass"])

    def test_the_logged_second_landing_of_51B(self):
        lines = B2.read_text().splitlines()
        result = matrix.judge(lines, landing=True)
        self.assertEqual(result["landings"], 1)
        self.assertAlmostEqual(result["landing_drop_m"], 0.044, places=3)     # the most of the feel.50+ landings
        self.assertLessEqual(result["landing_drop_m"], matrix.LAND_STEP_M)
        self.assertLessEqual(result["landing_m"], matrix.LAND_M)

    def test_the_soak_flags_it(self):
        import soak
        script = matrix.PHASES["jump-vault"][0]
        lines = [line for line in nolag(B.read_text().splitlines(), "23:23:20.749") if line[:12] >= "23:23:20.600"]
        self.assertFalse(soak.flagged(matrix.judge(lines, mouse="look" in script), script))   # without the landing judge
        vault = soak.judged("jump-vault", script, lines)
        self.assertLessEqual(vault["landing_m"], matrix.LAND_M)
        self.assertLess(vault["largest_jump_m"], matrix.POP_M)
        self.assertTrue(soak.flagged(vault, script))
        logged = soak.judged("jump-vault", script, B.read_text().splitlines())
        self.assertEqual(soak.flagged(logged, script), logged["largest_jump_m"] >= matrix.POP_M)   # not for the landing

    def test_the_eye_judged_is_the_eye_shown_with_the_veil(self):
        # the veil's move counts in the drop frame and in the frames after it: what is shown is eye + veil
        def veiled(line, at, z):
            return re.sub(r" veil=(?:0|\([-\d. ]+\))", f" veil=(0.00000 0.00000 {z:.5f})", line) if line[:12] == at and " Trace: " in line else line
        lines = B.read_text().splitlines()
        drop = [veiled(line, "23:23:20.749", -0.2) for line in lines]
        found = matrix.landings(trace.frames(drop), matrix.mask_lines(drop))
        self.assertAlmostEqual(found[0]["dropped"]["shown"], -0.205, places=2)
        after = [veiled(line, "23:23:20.781", -0.2) for line in lines]
        found = matrix.landings(trace.frames(after), matrix.mask_lines(after))
        self.assertAlmostEqual(found[0]["frames"][0]["shown"], -0.207, places=2)
        self.assertGreater(found[0]["frames"][0]["beyond"], matrix.LAND_M)

    def test_the_bound(self):
        self.assertEqual(matrix.LAND_STEP_M, 0.10)


class Soak(unittest.TestCase):
    def test_a_vault_action_of_the_soak_is_flagged_on_it(self):
        import soak
        lines = B.read_text().splitlines()
        script = matrix.PHASES["jump-vault"][0]
        near = [line for line in raw(lines) if "23:23:20.600" <= line[:12] <= "23:23:20.930"]   # as in Judge: the landing by itself
        vault = soak.judged("jump-vault", script, near)
        self.assertLess(vault["largest_jump_m"], matrix.POP_M)
        self.assertGreater(vault["landing_m"], 0.24)
        self.assertTrue(soak.flagged(vault))
        self.assertEqual(len(matrix.landing_lines(vault["landed"])), 1)
        self.assertNotIn("landing_m", soak.judged("walk", "hold w 1.5", near))   # no other action is judged so
        self.assertFalse(soak.flagged(soak.judged("walk", "hold w 1.5", near)))
        eased = soak.judged("clamber-low", matrix.PHASES["clamber-low"][0], lines)
        self.assertLessEqual(eased["landing_m"], matrix.LAND_M)
        self.assertEqual(soak.flagged(eased), eased["largest_jump_m"] >= matrix.POP_M)


if __name__ == "__main__":
    unittest.main()
