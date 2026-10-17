"""ORIGINAL (PyBoy) + NATIVE (C) differential tests for state timing and progression.

Each test drives the ORIGINAL cartridge under PyBoy with documented private injections, then
runs `alleyway-lab.exe --run-scenario` AUTONOMOUSLY from the original's snapshot and its latched
inputs, and compares the per-frame state/stage/lives/score/bonus/prng sequences (plus ball,
paddle, bricks, and scrolling/drop fields during active play). The C executable is invoked
explicitly here (tools/scenario_diff.py), not in some other independent test.

Skipped when the local ROM is absent (public CI uses only synthetic fixtures).
"""
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE_PATH = ROOT / "build/alleyway-lab.exe"
LOCAL_ROM = ROOT.parent / "Local-Privado/roms/Alleyway (USA).gb"

_spec = importlib.util.spec_from_file_location("scenario_diff", ROOT / "tools/scenario_diff.py")
sd = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sd)


def _build_mutant(out_path: Path) -> None:
    """Compile a COPY of main.c in which stage1_enter_state() never loads timers."""
    src = (ROOT / "src/main.c").read_text(encoding="utf-8").replace("\r\n", "\n")
    marker = "    s->state = new_state;\n    switch (new_state) {"
    assert marker in src, "stage1_enter_state body changed; update the mutation"
    mutated = src.replace(marker, "    s->state = new_state;\n    s->state_timer = 0;\n    switch (0) {", 1)
    tmp = ROOT / "build/mutant_no_timers.c"
    tmp.write_text(mutated, encoding="utf-8")
    try:
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O2", "-w", "-I", str(ROOT / "src"),
                        str(tmp), "-o", str(out_path), "-luser32", "-lgdi32", "-lwinmm"],
                       check=True, capture_output=True)
    finally:
        tmp.unlink(missing_ok=True)


@unittest.skipUnless(LOCAL_ROM.is_file(), "local ROM not present in ../Local-Privado/roms")
class OriginalVsNativeDifferentialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run([sys.executable, str(ROOT / "tools/build.py")], check=True, capture_output=True)
        cls.cache = {}

    def _run(self, name):
        if name not in self.cache:
            self.cache[name] = sd.run_differential(EXE_PATH, LOCAL_ROM, name)
        return self.cache[name]

    def _assert_identical(self, name):
        expected, native, mismatch = self._run(name)
        self.assertIsNone(mismatch, f"{name}: first mismatch {mismatch}")
        self.assertEqual(sd.state_runs(expected), sd.state_runs(native))
        return expected, native

    def test_normal_clear_curtain_serve(self):
        """Last brick -> state 8 (145) -> curtain 4 (66, stage+1) -> serve 5."""
        expected, native = self._assert_identical("normal_clear")
        runs = sd.state_runs(expected)
        self.assertEqual(runs[:4], [(6, 103), (8, 145), (4, 66), (5, 106)])
        self.assertEqual(expected[-1]["stage"], 1)
        self.assertEqual(native[-1]["stage"], 1)

    def test_bonus_drop_clear_curtain_serve_keeps_lives(self):
        """Bonus ball drop: state 7 (229) -> curtain 4 (66) -> serve; lives unchanged; no reward.
        Observed in the ORIGINAL: no state 8 between 7 and 4 (the old native 7->8 claim was wrong)."""
        expected, native = self._assert_identical("bonus_drop")
        self.assertEqual(sd.state_runs(expected)[:3], [(7, 229), (4, 66), (5, 405)])
        for seq in (expected, native):
            self.assertEqual({r["lives"] for r in seq}, {4})
            self.assertEqual({r["score_tens"] for r in seq}, {0})
            self.assertEqual(seq[-1]["stage"], 4)

    def test_bonus_expire_keeps_lives(self):
        expected, native = self._assert_identical("bonus_expire")
        self.assertEqual(sd.state_runs(expected)[:4], [(6, 24), (7, 229), (4, 66), (5, 381)])
        self.assertEqual({r["lives"] for r in native}, {4})

    def test_bonus_perfect_reward_exactly_once(self):
        """Perfect bonus (Round 0, Stage 4): state 8 lasts 499 frames; +500 tens in 50 steps of 10."""
        expected, native = self._assert_identical("bonus_perfect")
        self.assertEqual(sd.state_runs(expected)[:4], [(6, 61), (8, 499), (4, 66), (5, 174)])
        for seq in (expected, native):
            scores = [r["score_tens"] for r in seq]
            steps = [b - a for a, b in zip(scores, scores[1:]) if b != a]
            hits_ticks = [i for i, (a, b) in enumerate(zip(scores, scores[1:])) if b - a == 10]
            self.assertEqual(len(hits_ticks), 50, "tally must be 50 steps of +10 tens")
            self.assertEqual(scores[-1], 502, "2 tens from the brick + 500 tens reward, once")
            self.assertEqual(sum(s for s in steps if s == 10), 500)

    def test_bonus_perfect_multi_round_rewards_and_1up(self):
        """Perfect bonus across Rounds 1..3 (Stages 8, 12, 16): runtime reward from 0x1B71
        (+700 tens in 70 steps / 519f; +1000 tens in 100 steps + 1-UP / 550f; +1500 tens in 150 steps + 1-UP / 600f)."""
        exp1, nat1 = self._assert_identical("bonus_perfect_r1")
        self.assertEqual(sd.state_runs(exp1)[:4], [(6, 80), (8, 519), (4, 67), (5, 134)])
        for seq in (exp1, nat1):
            scores = [r["score_tens"] for r in seq]
            self.assertEqual(sum(1 for a, b in zip(scores, scores[1:]) if b - a == 10), 70)
            self.assertEqual(seq[-1]["stage"], 8)
            self.assertEqual(seq[-1]["lives"], 4)

        exp2, nat2 = self._assert_identical("bonus_perfect_r2")
        self.assertEqual(sd.state_runs(exp2)[:4], [(6, 66), (8, 550), (4, 66), (5, 118)])
        for seq in (exp2, nat2):
            scores = [r["score_tens"] for r in seq]
            self.assertEqual(sum(1 for a, b in zip(scores, scores[1:]) if b - a == 10), 100)
            self.assertEqual((seq[0]["lives"], seq[-1]["lives"]), (4, 5))
            self.assertEqual(seq[-1]["stage"], 12)

        exp3, nat3 = self._assert_identical("bonus_perfect_r3")
        self.assertEqual(sd.state_runs(exp3)[:4], [(6, 67), (8, 600), (4, 66), (5, 117)])
        for seq in (exp3, nat3):
            scores = [r["score_tens"] for r in seq]
            self.assertEqual(sum(1 for a, b in zip(scores, scores[1:]) if b - a == 10), 150)
            self.assertEqual((seq[0]["lives"], seq[-1]["lives"]), (4, 5))
            self.assertEqual(seq[-1]["stage"], 16)

    def test_life_lost_prng_and_reserve_gameplay_matches_original(self):
        """7 (101) -> 11 (1 frame, lives--) -> 5 (15f, paddle move + serve A) -> active play 6 (303f)
        with exact PRNG, ball, paddle, and bricks_left parity."""
        expected, native = self._assert_identical("life_lost")
        self.assertEqual(
            sd.state_runs(expected),
            [(7, 101), (11, 1), (5, 15), (6, 303)],
        )
        self.assertEqual((expected[0]["lives"], expected[102]["lives"], expected[-1]["lives"]), (4, 3, 3))
        self.assertLess(native[-1]["bricks_left"], native[0]["bricks_left"])
        self.assertGreater(native[-1]["score_tens"], native[0]["score_tens"])

    def test_game_over_to_title_restart_second_game_and_pause(self):
        """Last life: 7 (101) -> 11 Game Over (377) -> 1 (1) -> title wait 2 -> Start restart ->
        first curtain 4 (221f) -> serve 5 (score=0, lives=4, stage=0) -> serve A into 2nd game (6) ->
        Start pause (12) -> Start resume (6)."""
        expected, native = self._assert_identical("game_over")
        self.assertEqual(
            sd.state_runs(expected),
            [(7, 101), (11, 377), (1, 1), (2, 25), (4, 221), (5, 7), (6, 29), (12, 20), (6, 34), (7, 35)],
        )
        for seq in (expected, native):
            # At tick 725 (entering State 5 after Title restart): score=0, lives=4, stage=0
            self.assertEqual((seq[725]["state"], seq[725]["stage"], seq[725]["lives"], seq[725]["score_tens"]), (5, 0, 4, 0))
            # During Pause (ticks 761..780): state == 12 (0x0C), ball frozen
            self.assertEqual({r["state"] for r in seq[761:781]}, {12})
            self.assertEqual(len({(r["ball_x"], r["ball_y"]) for r in seq[761:781]}), 1)

    def test_stage32_clear_ending_and_wrap_to_stage1(self):
        """Stage 32 (stage=31) clear -> State 8 (600f) -> State 9 Ending (1445f, stage=0) ->
        State 4 curtain (66f) -> State 5 serve at Stage 1 preserving score and lives."""
        expected, native = self._assert_identical("stage32_ending")
        self.assertEqual(sd.state_runs(expected), [(6, 73), (8, 600), (9, 1445), (4, 66), (5, 66)])
        for seq in (expected, native):
            self.assertEqual((seq[-1]["state"], seq[-1]["stage"], seq[-1]["lives"]), (5, 0, 5))
            self.assertGreaterEqual(seq[-1]["score_tens"], 1500)

    def test_advanced_stage14_horizontal_and_stage23_vertical_play(self):
        """Differential active-play regressions on Stage 14 (horizontal scroll) and Stage 23
        (40-row vertical drop + 0x0BF2 bottom-row clearing). Note: this verifies these two
        advanced stages, not all 32 stages."""
        exp14, nat14 = self._assert_identical("stage14_horizontal")
        self.assertTrue(any(r["row_shift_6"] > 0 for r in nat14))
        self.assertLess(nat14[-1]["bricks_left"], nat14[0]["bricks_left"])

        exp23, nat23 = self._assert_identical("stage23_vertical")
        self.assertEqual((nat23[0]["row_offset"], nat23[-1]["row_offset"]), (20, 19))
        self.assertLess(nat23[-1]["bricks_left"], nat23[0]["bricks_left"])

    def test_mutation_without_timer_initialisation_fails_differential(self):
        """Mutation check: a build whose stage1_enter_state() never loads state_timer must FAIL
        the same differential (the mutated copy lives only in build/, never in Git)."""
        mutant = ROOT / "build/mutant_no_timers.exe"
        try:
            _build_mutant(mutant)
            snap, inputs, expected = sd.run_original(LOCAL_ROM, "normal_clear")
            native = sd.run_native(mutant, LOCAL_ROM, snap, inputs)
            mismatch = sd.compare(expected, native)
            self.assertIsNotNone(mismatch, "mutant without timers passed: the test would not catch the regression")
            self.assertEqual(mismatch["field"], "state")
        finally:
            mutant.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()

