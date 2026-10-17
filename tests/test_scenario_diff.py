"""ORIGINAL (PyBoy) + NATIVE (C) differential tests for state timing and progression.

Each test drives the ORIGINAL cartridge under PyBoy with documented private injections, then
runs `alleyway-lab.exe --run-scenario` AUTONOMOUSLY from the original's snapshot and its latched
inputs, and compares the per-frame state/stage/lives/score/bonus sequences. The C executable is
invoked explicitly here (tools/scenario_diff.py), not in some other independent test.

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
        """Last brick -> state 8 (144) -> curtain 4 (67, stage+1) -> serve 5."""
        expected, native = self._assert_identical("normal_clear")
        runs = sd.state_runs(expected)
        self.assertEqual(runs[:4], [(6, 104), (8, 144), (4, 67), (5, 105)])
        self.assertEqual(expected[-1]["stage"], 1)
        self.assertEqual(native[-1]["stage"], 1)

    def test_bonus_drop_clear_curtain_serve_keeps_lives(self):
        """Bonus ball drop: state 7 (228) -> curtain 4 (67) -> serve; lives unchanged; no reward.
        Observed in the ORIGINAL: no state 8 between 7 and 4 (the old native 7->8 claim was wrong)."""
        expected, native = self._assert_identical("bonus_drop")
        self.assertEqual(sd.state_runs(expected)[:3], [(7, 228), (4, 67), (5, 405)])
        for seq in (expected, native):
            self.assertEqual({r["lives"] for r in seq}, {4})
            self.assertEqual({r["score_tens"] for r in seq}, {0})
            self.assertEqual(seq[-1]["stage"], 4)

    def test_bonus_expire_keeps_lives(self):
        expected, native = self._assert_identical("bonus_expire")
        self.assertEqual(sd.state_runs(expected)[:4], [(6, 29), (7, 228), (4, 67), (5, 376)])
        self.assertEqual({r["lives"] for r in native}, {4})

    def test_bonus_perfect_reward_exactly_once(self):
        """Perfect bonus: state 8 lasts 498 frames; +500 tens in 50 steps of 10, exactly once."""
        expected, native = self._assert_identical("bonus_perfect")
        self.assertEqual(sd.state_runs(expected)[:4], [(6, 61), (8, 498), (4, 67), (5, 174)])
        for seq in (expected, native):
            scores = [r["score_tens"] for r in seq]
            steps = [b - a for a, b in zip(scores, scores[1:]) if b != a]
            hits_ticks = [i for i, (a, b) in enumerate(zip(scores, scores[1:])) if b - a == 10]
            self.assertEqual(len(hits_ticks), 50, "tally must be 50 steps of +10 tens")
            self.assertEqual(scores[-1], 502, "2 tens from the brick + 500 tens reward, once")
            self.assertEqual(sum(s for s in steps if s == 10), 500)

    def test_life_lost_with_spare_lives(self):
        """7 (100) -> 11 (1 frame, lives--) -> serve. PRNG excluded here (known divergence below)."""
        expected, native = self._assert_identical("life_lost")
        self.assertEqual(sd.state_runs(expected)[:3], [(7, 100), (11, 1), (5, 319)])
        self.assertEqual((expected[0]["lives"], expected[-1]["lives"]), (4, 3))

    @unittest.expectedFailure
    def test_life_lost_prng_matches_original(self):
        """KNOWN DIVERGENCE: after a normal life loss the native PRNG ends one step ahead
        (first mismatch: tick 102, expected 81, native 146). Remove expectedFailure once fixed."""
        _, _, mismatch = sd.run_differential(EXE_PATH, LOCAL_ROM, "life_lost", check_prng=True)
        self.assertIsNone(mismatch, f"first mismatch {mismatch}")

    def test_game_over_to_title(self):
        """Last life: 7 (100) -> 11 Game Over (377) -> 1 (1) -> title wait 2."""
        expected, native = self._assert_identical("game_over")
        self.assertEqual(sd.state_runs(expected)[:4], [(7, 100), (11, 377), (1, 1), (2, 222)])
        self.assertEqual({r["lives"] for r in native}, {0})

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
