"""ORIGINAL (PyBoy) vs NATIVE (C) visual parity tests for Alleyway title and serve screens.

Compares the framebuffer rendered by the production `render_stage1()` function
(the exact same renderer driving the Win32 window in main.c) against PyBoy's
emulated LCD output shade-by-shade across:
  1. Title screen (State 2: "PUSH START", High Score OAM sprites, LCDC=0x83).
  2. Stage 1 Serve screen (State 5: Playfield BG, brick grid, right HUD window
     with TOP/1UP/STAGE 01/lives, left wall OAM pipe sprites, paddle).
  3. Interactive Win32 windowed session (--verify-window-session).

Captures and side-by-side diffs are saved to ignored `local/visual_diff/`.
Requires a local ROM (kept in ignored local/ or ../Local-Privado/).
"""
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from PIL import Image
import numpy as np

ROM_CANDIDATES = [
    Path("local/roms/Alleyway (USA).gb"),
    Path("local/roms/alleyway.gb"),
    Path("../Local-Privado/roms/Alleyway (USA).gb"),
    Path("../Local-Privado/roms/alleyway.gb"),
]
EXE_PATH = Path("build/alleyway-lab.exe")
DIFF_DIR = Path("local/visual_diff")


def _find_rom():
    for p in ROM_CANDIDATES:
        if p.exists() and p.stat().st_size == 32768:
            return p
    return None


def _pyboy_to_shades(img: Image.Image) -> np.ndarray:
    """Map PyBoy grayscale image to DMG shade indices: 0=white, 1=light, 2=dark, 3=black."""
    arr = np.array(img.convert("L"))
    shades = np.zeros_like(arr, dtype=np.uint8)
    shades[arr == 255] = 0
    shades[arr == 153] = 1
    shades[arr == 85] = 2
    shades[arr == 0] = 3
    return shades


def _ppm_to_shades(ppm_path: Path) -> np.ndarray:
    """Map native 24-bit RGB PPM to DMG shade indices."""
    rgb = np.array(Image.open(ppm_path).convert("RGB"))
    shades = np.zeros((144, 160), dtype=np.uint8)
    for y in range(144):
        for x in range(160):
            r, _, _ = rgb[y, x]
            if r > 200:
                shades[y, x] = 0
            elif r > 100:
                shades[y, x] = 1
            elif r > 30:
                shades[y, x] = 2
            else:
                shades[y, x] = 3
    return shades


class VisualParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom_path = _find_rom()
        cls.has_rom = cls.rom_path is not None
        DIFF_DIR.mkdir(parents=True, exist_ok=True)

    def test_window_session_verification_e2e(self):
        """Verifies the actual Win32 window loop: CreateWindowW, WM_PAINT,
        QPC scheduler, WM_KEYDOWN/UP for Start, Move, Serve, Pause, Resume, Reset, Esc,
        and audio backend shutdown."""
        if not self.has_rom:
            self.skipTest("Local ROM required for window session verification")
        self.assertTrue(EXE_PATH.exists(), f"Executable {EXE_PATH} must exist")
        proc = subprocess.run(
            [str(EXE_PATH), "--verify-window-session", str(self.rom_path), str(DIFF_DIR)],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.returncode, 0, f"Window session failed:\n{proc.stderr}")
        out = json.loads(proc.stdout.strip())
        self.assertTrue(out.get("window_verified"), "Window session must report verified")
        self.assertGreater(out.get("paint_count", 0), 0, "WM_PAINT must have executed")
        self.assertEqual(out.get("final_state"), 5, "Session must end at State 5 reset")

    def test_title_screen_bit_exact_visual_parity(self):
        """Validates that Title Screen (State 2: PUSH START, High Score OAM sprites)
        rendered by native C matches the original cartridge under PyBoy with 100.00%
        pixel shade equality."""
        if not self.has_rom:
            self.skipTest("Local ROM required for Title visual parity")
        from pyboy import PyBoy

        gb = PyBoy(str(self.rom_path), window="null", sound_emulated=False)
        try:
            # Advance to frame 90 where Title screen BG & OAM sprites are fully active
            for _ in range(90):
                gb.tick()
            pyboy_img = gb.screen.image
            pyboy_shades = _pyboy_to_shades(pyboy_img)
            pyboy_img.save(DIFF_DIR / "pyboy_title.png")
        finally:
            gb.stop()

        # Render native title screen via --run-scenario on snapshot in State 2
        with tempfile.TemporaryDirectory() as tmp:
            snap_file = Path(tmp) / "snap_title.jsonl"
            out_ppm = Path(tmp) / "native_title.ppm"
            snap = {"state": 2, "high_score_tens": 200, "stage": 0, "lives": 4}
            snap_file.write_text(json.dumps(snap) + "\n", encoding="utf-8")
            proc = subprocess.run(
                [str(EXE_PATH), "--run-scenario", str(self.rom_path), str(snap_file), "--dump-ppm", "-1", str(out_ppm)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(proc.returncode, 0, f"Native render failed: {proc.stderr}")
            native_shades = _ppm_to_shades(out_ppm)
            Image.open(out_ppm).save(DIFF_DIR / "native_title.png")

        diff = (pyboy_shades != native_shades)
        mismatched_pixels = int(np.sum(diff))
        match_pct = 100.0 * (144 * 160 - mismatched_pixels) / (144 * 160)
        self.assertEqual(mismatched_pixels, 0,
                         f"Title screen mismatch: {mismatched_pixels} pixels differ ({match_pct:.2f}% match)")

    def test_stage1_serve_bit_exact_visual_parity(self):
        """Validates that Stage 1 Serve screen (State 5: playfield, brick grid,
        HUD window with TOP/1UP/STAGE 01, paddle, left wall) rendered by native C
        matches the original cartridge under PyBoy with 100.00% pixel shade equality."""
        if not self.has_rom:
            self.skipTest("Local ROM required for Stage 1 Serve visual parity")
        from pyboy import PyBoy
        from tools.scenario_diff import _boot_past_title, _record

        gb = _boot_past_title(self.rom_path)
        try:
            # Step 2 ticks in State 5 for full PPU scanline refresh of State 5
            gb.tick()
            gb.tick()
            pyboy_img = gb.screen.image
            pyboy_shades = _pyboy_to_shades(pyboy_img)
            pyboy_img.save(DIFF_DIR / "pyboy_stage1_serve.png")

            latched = (int(gb.memory[0xFF8C]), int(gb.memory[0xFF8D]))
            snap = _record(gb, latched)
        finally:
            gb.stop()

        with tempfile.TemporaryDirectory() as tmp:
            snap_file = Path(tmp) / "snap_serve.jsonl"
            out_ppm = Path(tmp) / "native_serve.ppm"
            snap_file.write_text(json.dumps(snap) + "\n", encoding="utf-8")
            proc = subprocess.run(
                [str(EXE_PATH), "--run-scenario", str(self.rom_path), str(snap_file), "--dump-ppm", "-1", str(out_ppm)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(proc.returncode, 0, f"Native render failed: {proc.stderr}")
            native_shades = _ppm_to_shades(out_ppm)
            Image.open(out_ppm).save(DIFF_DIR / "native_stage1_serve.png")

        diff = (pyboy_shades != native_shades)
        mismatched_pixels = int(np.sum(diff))
        match_pct = 100.0 * (144 * 160 - mismatched_pixels) / (144 * 160)
        self.assertEqual(mismatched_pixels, 0,
                         f"Stage 1 Serve screen mismatch: {mismatched_pixels} pixels differ ({match_pct:.2f}% match)")


if __name__ == "__main__":
    unittest.main()
