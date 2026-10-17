"""Native Stage 1 C executable verification tests (synthetic fixtures + optional local ROM parity)."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE_PATH = ROOT / "build/alleyway-lab.exe"


def _load_tool(name: str, rel_path: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


capture_mod = _load_tool("capture_reference", "tools/capture_reference.py")


def build_synthetic_stage1_rom_bytes() -> bytes:
    """Create a 32 KiB synthetic ROM populated with synthetic Stage 1 tables for C verifier testing."""
    rom = bytearray(32768)
    rom[0x134:0x13D] = b"SYNTHSTG1"
    rom[0x100B:0x1013] = bytes([6, 8, 10, 6, 8, 10, 8, 10])
    # Speed table pointers at 0x11EE (speed 1 -> 0x1220, speed 3 -> 0x12B8)
    rom[0x11EE:0x11F0] = (0x1220).to_bytes(2, "little")
    rom[0x11F2:0x11F4] = (0x12B8).to_bytes(2, "little")
    # Speed 1 angle 9 (0x1220 + 36 = 0x1244): vy=181, vx=181
    rom[0x1244:0x1248] = bytes([0x00, 181, 0x00, 181])
    # Speed 3 angle 9 (0x12B8 + 36 = 0x12DC): vy=226, vx=226
    rom[0x12DC:0x12E0] = bytes([0x00, 226, 0x00, 226])
    # Normal paddle bounce table at 0x1B41
    rom[0x1B41:0x1B41 + 15] = bytes([9] * 15)
    # Brick type 1 at 0x1B87: score_tens=1, hp=1, min_spd=0
    rom[0x1B87 + 3] = 0x11
    # Authentic bonus table at 0x1B71: 4 rounds of (timer, reward_tens big endian)
    rom[0x1B71:0x1B71 + 3] = bytes([95, 0x01, 0xF4])
    rom[0x1B74:0x1B74 + 3] = bytes([90, 0x02, 0xBC])
    rom[0x1B77:0x1B77 + 3] = bytes([85, 0x03, 0xE8])
    rom[0x1B7A:0x1B7A + 3] = bytes([80, 0x05, 0xDC])

    # Stage 0 descriptor at 0x1BE1 -> 0x1C44
    rom[0x1BE1] = 0x00
    rom[0x1BE2:0x1BE4] = (0x1C44).to_bytes(2, "little")

    # Stage 3 descriptor at 0x1BEA -> 0x1C44 (bonus_flag = 1)
    rom[0x1BEA] = 0x80
    rom[0x1BEB:0x1BED] = (0x1C44).to_bytes(2, "little")

    # Stage 31 descriptor at 0x1C3E -> 0x1C44
    rom[0x1C3E] = 0x00
    rom[0x1C3F:0x1C41] = (0x1C44).to_bytes(2, "little")

    rom[0x1C44:0x1C44 + 14] = bytes([1] * 14)
    rom[0x1C44 + 14] = 0xFF

    chk = 0
    for b in rom[0x134:0x14D]:
        chk = (chk - b - 1) & 0xFF
    rom[0x14D] = chk
    gchk = (sum(rom) - rom[0x14E] - rom[0x14F]) & 0xFFFF
    rom[0x14E:0x150] = gchk.to_bytes(2, "big")
    return bytes(rom)


class NativeStage1VerifierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run([sys.executable, str(ROOT / "tools/build.py")], check=True, capture_output=True)

    def test_smoke_test_passes(self):
        proc = subprocess.run([str(EXE_PATH), "--smoke-test"], check=True, capture_output=True, text=True)
        self.assertIn("PASS:", proc.stdout)

    def test_multi_stage_rules_pass(self):
        proc = subprocess.run([str(EXE_PATH), "--verify-multi-stage-rules"], check=True, capture_output=True, text=True)
        self.assertIn("PASS: multi-stage rules verified", proc.stdout)

    def test_verify_trace_synthetic_and_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)
            rom_path = tmp_dir / "synth_stage1.gb"
            rom_path.write_bytes(build_synthetic_stage1_rom_bytes())

            trace_ok = tmp_dir / "logic_ok.jsonl"
            ticks = [
                {
                    "tick": 0, "frame": 0, "in_joyp_raw": 0xFF, "in_joyp_edge": 0xFF,
                    "watch": {
                        "state": 5, "prng": 43, "paddle_x": 40, "paddle_w": 24,
                        "ball_x": 0, "ball_x_sub": 0, "ball_y": 0, "ball_y_sub": 0,
                        "ball_vx": 0, "ball_vy": 0, "speed_idx": 0,
                        "bricks_left": 14, "score_tens": 0, "lives": 3,
                    },
                },
                {
                    "tick": 1, "frame": 1, "in_joyp_raw": 0xEF, "in_joyp_edge": 0xEF,
                    "watch": {
                        "state": 5, "prng": 108, "paddle_x": 43, "paddle_w": 24,
                        "ball_x": 0, "ball_x_sub": 0, "ball_y": 0, "ball_y_sub": 0,
                        "ball_vx": 0, "ball_vy": 0, "speed_idx": 0,
                        "bricks_left": 14, "score_tens": 0, "lives": 3,
                    },
                },
                {
                    "tick": 2, "frame": 2, "in_joyp_raw": 0xFE, "in_joyp_edge": 0xFE,
                    "watch": {
                        "state": 6, "prng": 173, "paddle_x": 43, "paddle_w": 24,
                        "ball_x": 79, "ball_x_sub": 0, "ball_y": 116, "ball_y_sub": 0,
                        "ball_vx": 65355, "ball_vy": 181, "speed_idx": 7,
                        "bricks_left": 14, "score_tens": 0, "lives": 3,
                    },
                },
                {
                    "tick": 3, "frame": 3, "in_joyp_raw": 0xFF, "in_joyp_edge": 0xFF,
                    "watch": {
                        "state": 6, "prng": 238, "paddle_x": 43, "paddle_w": 24,
                        "ball_x": 78, "ball_x_sub": 75, "ball_y": 116, "ball_y_sub": 181,
                        "ball_vx": 65355, "ball_vy": 181, "speed_idx": 7,
                        "bricks_left": 14, "score_tens": 0, "lives": 3,
                    },
                },
            ]
            trace_ok.write_text("\n".join(json.dumps(t) for t in ticks) + "\n", encoding="utf-8")

            res_ok = subprocess.run(
                [str(EXE_PATH), "--verify-trace", str(rom_path), str(trace_ok)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(res_ok.returncode, 0, msg=res_ok.stderr)
            report = json.loads(res_ok.stdout.strip())
            self.assertEqual(report["ticks_compared"], 3)
            self.assertEqual(report["mismatches"], 0)

            # Deliberate mismatch on tick 3 paddle_x
            ticks[3]["watch"]["paddle_x"] = 99
            trace_bad = tmp_dir / "logic_bad.jsonl"
            trace_bad.write_text("\n".join(json.dumps(t) for t in ticks) + "\n", encoding="utf-8")
            res_bad = subprocess.run(
                [str(EXE_PATH), "--verify-trace", str(rom_path), str(trace_bad)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(res_bad.returncode, 0)
            bad_report = json.loads(res_bad.stdout.strip())
            self.assertEqual(bad_report["mismatches"], 1)

    def test_local_rom_parity_if_present(self):
        local_rom = ROOT.parent / "Local-Privado/roms/Alleyway (USA).gb"
        if not local_rom.is_file():
            self.skipTest("User local ROM not present in ../Local-Privado/roms/Alleyway (USA).gb")
        watch_spec = json.loads((ROOT / "tools/alleyway_watchpoints.json").read_text(encoding="utf-8"))
        inputs = [
            {"frame": 120, "button": "start", "action": "press"},
            {"frame": 125, "button": "start", "action": "release"},
            {"frame": 450, "button": "right", "action": "press"},
            {"frame": 470, "button": "right", "action": "release"},
            {"frame": 475, "button": "a", "action": "press"},
            {"frame": 480, "button": "a", "action": "release"},
            {"frame": 630, "button": "left", "action": "press"},
            {"frame": 665, "button": "left", "action": "release"},
        ]
        with tempfile.TemporaryDirectory() as tmp:
            meta = capture_mod.run_capture(
                rom_path=local_rom,
                frames=700,
                inputs=inputs,
                watch_spec=watch_spec,
                out_dir=Path(tmp) / "cap",
                logic_hooks=(0x051F, 0x0525),
            )
            logic_trace = Path(meta["out_dir"]) / "logic_trace.jsonl"
            res = subprocess.run(
                [str(EXE_PATH), "--verify-trace", str(local_rom), str(logic_trace)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 0, msg=res.stderr)
            report = json.loads(res.stdout.strip())
            self.assertEqual(report["mismatches"], 0)
            self.assertGreater(report["ticks_compared"], 300)
    def test_local_rom_stage2_parity_if_present(self):
        local_rom = ROOT.parent / "Local-Privado/roms/Alleyway (USA).gb"
        if not local_rom.is_file():
            self.skipTest("User local ROM not present in ../Local-Privado/roms/Alleyway (USA).gb")
        from pyboy import PyBoy

        with tempfile.TemporaryDirectory() as tmp:
            tmp_trace = Path(tmp) / "stage2_parity.jsonl"
            gb = PyBoy(str(local_rom), window="null", sound_emulated=False)
            try:
                # Advance past title
                for f in range(346):
                    if f == 120: gb.button_press("start")
                    elif f == 125: gb.button_release("start")
                    gb.tick()

                # Trigger stage clear on Stage 1 to transition naturally into Stage 2
                gb.memory[0xFFC8] = 0; gb.memory[0xFFC9] = 0; gb.memory[0xFFA4] = 8
                for f in range(346, 558):
                    if f >= 400 and f % 20 < 10: gb.button_press("a")
                    else: gb.button_release("a")
                    gb.tick()

                records = []
                tick_id = 0
                latched = [0xFF, 0xFF]

                def _on_logic_start(context):
                    latched[0] = int(gb.memory[0xFF8C])
                    latched[1] = int(gb.memory[0xFF8D])

                def _on_logic_end(context):
                    nonlocal tick_id
                    rec = {
                        "tick": tick_id,
                        "state": gb.memory[0xFFA4],
                        "prng": gb.memory[0xFFA1],
                        "paddle_x": gb.memory[0xFFC0],
                        "paddle_w": gb.memory[0xFFC2],
                        "ball_x": gb.memory[0xFFB6],
                        "ball_x_sub": gb.memory[0xFFB7],
                        "ball_y": gb.memory[0xFFB4],
                        "ball_y_sub": gb.memory[0xFFB5],
                        "ball_vx": (gb.memory[0xFFBA] << 8) | gb.memory[0xFFBB],
                        "ball_vy": (gb.memory[0xFFB8] << 8) | gb.memory[0xFFB9],
                        "speed_idx": gb.memory[0xFFBE],
                        "bricks_left": (gb.memory[0xFFC8] << 8) | gb.memory[0xFFC9],
                        "score_tens": gb.memory[0xFFCA] | (gb.memory[0xFFCB] << 8),
                        "lives": gb.memory[0xCA44],
                        "stage": gb.memory[0xCA45],
                        "scroll_flag": gb.memory[0xFFAB],
                        "bonus_flag": gb.memory[0xFFAA],
                        "in_joyp_raw": latched[0],
                        "in_joyp_edge": latched[1],
                    }
                    for r in range(20):
                        rec[f"row_pattern_{r}"] = gb.memory[0xCA14 + r]
                        rec[f"row_shift_{r}"] = gb.memory[0xCA00 + r]
                        rec[f"row_timer_{r}"] = gb.memory[0xCA28 + r]
                    records.append(rec)
                    tick_id += 1

                gb.hook_register(0, 0x051F, _on_logic_start, None)
                gb.hook_register(0, 0x0525, _on_logic_end, None)

                for f in range(558, 1200):
                    if f == 570: gb.button_press("a")
                    elif f == 575: gb.button_release("a")
                    elif f == 650: gb.button_press("right")
                    elif f == 670: gb.button_release("right")
                    gb.tick()
            finally:
                gb.stop()

            with open(tmp_trace, "w", encoding="utf-8") as fp:
                for r in records:
                    fp.write(json.dumps(r) + "\n")

            res = subprocess.run(
                [str(EXE_PATH), "--verify-trace", str(local_rom), str(tmp_trace)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 0, msg=res.stderr)
            report = json.loads(res.stdout.strip())
            self.assertEqual(report["mismatches"], 0)
            self.assertGreater(report["ticks_compared"], 500)
            self.assertLess(report["bricks_left"], 84)

            # Coverage assertions for Stage 2
            self.assertTrue(any(r["stage"] == 1 for r in records), "Did not reach Stage 2 (stage == 1)")
            self.assertTrue(all(r["scroll_flag"] == 1 for r in records if r["stage"] == 1), "scroll_flag != 1 in Stage 2")
            self.assertTrue(any(r["row_shift_0"] > 0 for r in records), "Row shift did not advance in Stage 2")

    def test_local_rom_stage3_parity_if_present(self):
        local_rom = ROOT.parent / "Local-Privado/roms/Alleyway (USA).gb"
        if not local_rom.is_file():
            self.skipTest("User local ROM not present in ../Local-Privado/roms/Alleyway (USA).gb")
        from pyboy import PyBoy

        with tempfile.TemporaryDirectory() as tmp:
            tmp_trace = Path(tmp) / "stage3_parity.jsonl"
            gb = PyBoy(str(local_rom), window="null", sound_emulated=False)
            drops = 0
            try:
                # Advance past title
                for f in range(346):
                    if f == 120: gb.button_press("start")
                    elif f == 125: gb.button_release("start")
                    gb.tick()

                # Trigger stage clear on Stage 1 to transition to Stage 2
                gb.memory[0xFFC8] = 0; gb.memory[0xFFC9] = 0; gb.memory[0xFFA4] = 8
                for f in range(346, 558):
                    if f >= 400 and f % 20 < 10: gb.button_press("a")
                    else: gb.button_release("a")
                    gb.tick()

                # Trigger stage clear on Stage 2 to transition into Stage 3
                gb.memory[0xFFC8] = 0; gb.memory[0xFFC9] = 0; gb.memory[0xFFA4] = 8
                for f in range(558, 800):
                    if f >= 600 and f % 20 < 10: gb.button_press("a")
                    else: gb.button_release("a")
                    gb.tick()
                    if gb.memory[0xCA45] == 2 and gb.memory[0xFFA4] == 5:
                        break

                start_frame = f
                records = []
                tick_id = [0]
                latched = [0xFF, 0xFF]

                def _on_logic_start(context):
                    latched[0] = int(gb.memory[0xFF8C])
                    latched[1] = int(gb.memory[0xFF8D])

                def _on_logic_end(context):
                    rec = {
                        "tick": tick_id[0],
                        "state": gb.memory[0xFFA4],
                        "prng": gb.memory[0xFFA1],
                        "vblank_count": gb.memory[0xFFA2],
                        "paddle_x": gb.memory[0xFFC0],
                        "paddle_w": gb.memory[0xFFC2],
                        "ball_x": gb.memory[0xFFB6],
                        "ball_x_sub": gb.memory[0xFFB7],
                        "ball_y": gb.memory[0xFFB4],
                        "ball_y_sub": gb.memory[0xFFB5],
                        "ball_vx": (gb.memory[0xFFBA] << 8) | gb.memory[0xFFBB],
                        "ball_vy": (gb.memory[0xFFB8] << 8) | gb.memory[0xFFB9],
                        "speed_idx": gb.memory[0xFFBE],
                        "bricks_left": (gb.memory[0xFFC8] << 8) | gb.memory[0xFFC9],
                        "score_tens": gb.memory[0xFFCA] | (gb.memory[0xFFCB] << 8),
                        "lives": gb.memory[0xCA44],
                        "stage": gb.memory[0xCA45],
                        "scroll_flag": gb.memory[0xFFAB],
                        "bonus_flag": gb.memory[0xFFAA],
                        "row_offset": gb.memory[0xFFA9],
                        "scroll_timer": gb.memory[0xFFC4],
                        "scroll_step": gb.memory[0xFFC5],
                        "in_joyp_raw": latched[0],
                        "in_joyp_edge": latched[1],
                    }
                    records.append(rec)
                    tick_id[0] += 1

                gb.hook_register(0, 0x051F, _on_logic_start, None)
                gb.hook_register(0, 0x0525, _on_logic_end, None)

                last_offset = gb.memory[0xFFA9]

                # Run simulation and track ball with paddle
                for f in range(start_frame, start_frame + 2500):
                    if f == start_frame + 10:
                        gb.button_press("a")
                    elif f == start_frame + 15:
                        gb.button_release("a")

                    ball_x = gb.memory[0xFFB6]
                    pad_x = gb.memory[0xFFC0]
                    pad_w = gb.memory[0xFFC2]
                    target_x = ball_x - (pad_w // 2)
                    if pad_x < target_x - 1:
                        gb.button_press("right")
                        gb.button_release("left")
                    elif pad_x > target_x + 1:
                        gb.button_press("left")
                        gb.button_release("right")
                    else:
                        gb.button_release("left")
                        gb.button_release("right")

                    gb.tick()
                    if gb.memory[0xFFA9] != last_offset:
                        drops += 1
                        last_offset = gb.memory[0xFFA9]
                        if drops >= 2 and f > start_frame + 1800:
                            break
            finally:
                gb.stop()

            with open(tmp_trace, "w", encoding="utf-8") as fp:
                for r in records:
                    fp.write(json.dumps(r) + "\n")

            res = subprocess.run(
                [str(EXE_PATH), "--verify-trace", str(local_rom), str(tmp_trace)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 0, msg=res.stderr)
            report = json.loads(res.stdout.strip())
            self.assertEqual(report["mismatches"], 0)
            self.assertGreater(report["ticks_compared"], 1000)
            self.assertLess(report["bricks_left"], 84)

            # Assert coverage for Stage 3
            self.assertTrue(any(r["stage"] == 2 for r in records), "Did not reach Stage 3 (stage == 2)")
            self.assertGreaterEqual(drops, 2, "Did not observe at least two vertical drops")
            self.assertLessEqual(last_offset, 18, f"row_offset did not drop to <= 18 (was {last_offset})")

    def test_local_rom_stage4_bonus_parity_if_present(self):
        local_rom = ROOT.parent / "Local-Privado/roms/Alleyway (USA).gb"
        if not local_rom.is_file():
            self.skipTest("User local ROM not present in ../Local-Privado/roms/Alleyway (USA).gb")
        from pyboy import PyBoy

        with tempfile.TemporaryDirectory() as tmp:
            tmp_trace = Path(tmp) / "stage4_bonus_parity.jsonl"
            gb = PyBoy(str(local_rom), window="null", sound_emulated=False)
            try:
                # Advance past title
                for f in range(346):
                    if f == 120: gb.button_press("start")
                    elif f == 125: gb.button_release("start")
                    gb.tick()

                # Fast-forward across stages 1..3 into Stage 4 (first bonus stage)
                # Stage 1 -> 2
                gb.memory[0xFFC8] = 0; gb.memory[0xFFC9] = 0; gb.memory[0xFFA4] = 8
                for f in range(346, 558):
                    if f >= 400 and f % 20 < 10: gb.button_press("a")
                    else: gb.button_release("a")
                    gb.tick()

                # Stage 2 -> 3
                gb.memory[0xFFC8] = 0; gb.memory[0xFFC9] = 0; gb.memory[0xFFA4] = 8
                for f in range(558, 780):
                    if f >= 600 and f % 20 < 10: gb.button_press("a")
                    else: gb.button_release("a")
                    gb.tick()

                # Stage 3 -> 4 (Bonus Stage 1)
                gb.memory[0xFFC8] = 0; gb.memory[0xFFC9] = 0; gb.memory[0xFFA4] = 8
                for f in range(780, 1100):
                    if f >= 820 and f % 20 < 10: gb.button_press("a")
                    else: gb.button_release("a")
                    gb.tick()
                    if gb.memory[0xCA45] == 3 and gb.memory[0xFFA4] == 5:
                        break

                start_frame = f
                records = []
                tick_id = [0]
                latched = [0xFF, 0xFF]

                def _on_logic_start(context):
                    latched[0] = int(gb.memory[0xFF8C])
                    latched[1] = int(gb.memory[0xFF8D])

                def _on_logic_end(context):
                    rec = {
                        "tick": tick_id[0],
                        "state": gb.memory[0xFFA4],
                        "prng": gb.memory[0xFFA1],
                        "vblank_count": gb.memory[0xFFA2],
                        "paddle_x": gb.memory[0xFFC0],
                        "paddle_w": gb.memory[0xFFC2],
                        "ball_x": gb.memory[0xFFB6],
                        "ball_x_sub": gb.memory[0xFFB7],
                        "ball_y": gb.memory[0xFFB4],
                        "ball_y_sub": gb.memory[0xFFB5],
                        "ball_vx": (gb.memory[0xFFBA] << 8) | gb.memory[0xFFBB],
                        "ball_vy": (gb.memory[0xFFB8] << 8) | gb.memory[0xFFB9],
                        "speed_idx": gb.memory[0xFFBE],
                        "bricks_left": (gb.memory[0xFFC8] << 8) | gb.memory[0xFFC9],
                        "score_tens": gb.memory[0xFFCA] | (gb.memory[0xFFCB] << 8),
                        "lives": gb.memory[0xCA44],
                        "stage": gb.memory[0xCA45],
                        "scroll_flag": gb.memory[0xFFAB],
                        "bonus_flag": gb.memory[0xFFAA],
                        "bonus_timer": gb.memory[0xCA48],
                        "row_offset": gb.memory[0xFFA9],
                        "in_joyp_raw": latched[0],
                        "in_joyp_edge": latched[1],
                    }
                    records.append(rec)
                    tick_id[0] += 1

                gb.hook_register(0, 0x051F, _on_logic_start, None)
                gb.hook_register(0, 0x0525, _on_logic_end, None)

                # Serve ball in Stage 4
                gb.button_release("a")
                for _ in range(5): gb.tick()
                gb.button_press("a")
                for _ in range(5): gb.tick()
                gb.button_release("a")

                # Step 150 frames observing ball brick penetration and authentic timer countdown
                initial_bricks = (gb.memory[0xFFC8] << 8) | gb.memory[0xFFC9]
                initial_timer = gb.memory[0xCA48]

                for f in range(150):
                    ball_x = gb.memory[0xFFB6]
                    pad_x = gb.memory[0xFFC0]
                    pad_w = gb.memory[0xFFC2]
                    target_x = ball_x - (pad_w // 2)
                    if pad_x < target_x - 1:
                        gb.button_press("right"); gb.button_release("left")
                    elif pad_x > target_x + 1:
                        gb.button_press("left"); gb.button_release("right")
                    else:
                        gb.button_release("left"); gb.button_release("right")
                    gb.tick()

                final_bricks = (gb.memory[0xFFC8] << 8) | gb.memory[0xFFC9]
                final_timer = gb.memory[0xCA48]
            finally:
                gb.stop()

            with open(tmp_trace, "w", encoding="utf-8") as fp:
                for r in records:
                    fp.write(json.dumps(r) + "\n")

            res = subprocess.run(
                [str(EXE_PATH), "--verify-trace", str(local_rom), str(tmp_trace)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 0, msg=res.stderr)
            report = json.loads(res.stdout.strip())
            self.assertEqual(report["mismatches"], 0)
            self.assertGreater(report["ticks_compared"], 140)

            # Assert coverage for Stage 4 Bonus
            self.assertTrue(any(r["stage"] == 3 for r in records), "Did not reach Stage 4 (stage == 3)")
            self.assertTrue(any(r["bonus_flag"] == 1 for r in records), "bonus_flag != 1 in Stage 4")
            self.assertLess(final_bricks, initial_bricks, "No bricks destroyed during bonus stage")
            self.assertLess(final_timer, initial_timer, "Bonus countdown timer did not decrement")
            # Verify ball penetrated through multiple bricks
            bricks_destroyed = initial_bricks - final_bricks
            self.assertGreaterEqual(bricks_destroyed, 10, f"Expected penetration through multiple bricks, got {bricks_destroyed}")

    def test_local_rom_stage4_bonus_expiration_if_present(self):
        local_rom = ROOT.parent / "Local-Privado/roms/Alleyway (USA).gb"
        if not local_rom.is_file():
            self.skipTest("User local ROM not present in ../Local-Privado/roms/Alleyway (USA).gb")
        from pyboy import PyBoy

        with tempfile.TemporaryDirectory() as tmp:
            tmp_trace = Path(tmp) / "stage4_expire.jsonl"
            gb = PyBoy(str(local_rom), window="null", sound_emulated=False)
            try:
                # Advance past title
                for f in range(346):
                    if f == 120: gb.button_press("start")
                    elif f == 125: gb.button_release("start")
                    gb.tick()

                # Fast-forward across stages 1..3 into Stage 4
                gb.memory[0xFFC8] = 0; gb.memory[0xFFC9] = 0; gb.memory[0xFFA4] = 8
                for f in range(346, 558):
                    if f >= 400 and f % 20 < 10: gb.button_press("a")
                    else: gb.button_release("a")
                    gb.tick()

                gb.memory[0xFFC8] = 0; gb.memory[0xFFC9] = 0; gb.memory[0xFFA4] = 8
                for f in range(558, 780):
                    if f >= 600 and f % 20 < 10: gb.button_press("a")
                    else: gb.button_release("a")
                    gb.tick()

                gb.memory[0xFFC8] = 0; gb.memory[0xFFC9] = 0; gb.memory[0xFFA4] = 8
                for f in range(780, 1100):
                    if f >= 820 and f % 20 < 10: gb.button_press("a")
                    else: gb.button_release("a")
                    gb.tick()
                    if gb.memory[0xCA45] == 3 and gb.memory[0xFFA4] == 5:
                        break

                start_frame = f
                # Serve ball
                gb.button_release("a")
                for _ in range(5): gb.tick()
                gb.button_press("a")
                for _ in range(5): gb.tick()
                gb.button_release("a")

                # Step 10 frames in state 6 so ball is flying
                for _ in range(10): gb.tick()

                # Documented intervention: fast-forward authentic bonus_timer (0xCA48) to 2
                # to measure countdown cadence, zero expiration, and State 7 transition with lives preserved
                gb.memory[0xCA48] = 2

                records = []
                tick_id = [0]
                latched = [0xFF, 0xFF]

                def _on_logic_start(context):
                    latched[0] = int(gb.memory[0xFF8C])
                    latched[1] = int(gb.memory[0xFF8D])

                def _on_logic_end(context):
                    rec = {
                        "tick": tick_id[0],
                        "state": gb.memory[0xFFA4],
                        "prng": gb.memory[0xFFA1],
                        "vblank_count": gb.memory[0xFFA2],
                        "paddle_x": gb.memory[0xFFC0],
                        "paddle_w": gb.memory[0xFFC2],
                        "ball_x": gb.memory[0xFFB6],
                        "ball_x_sub": gb.memory[0xFFB7],
                        "ball_y": gb.memory[0xFFB4],
                        "ball_y_sub": gb.memory[0xFFB5],
                        "ball_vx": (gb.memory[0xFFBA] << 8) | gb.memory[0xFFBB],
                        "ball_vy": (gb.memory[0xFFB8] << 8) | gb.memory[0xFFB9],
                        "speed_idx": gb.memory[0xFFBE],
                        "bricks_left": (gb.memory[0xFFC8] << 8) | gb.memory[0xFFC9],
                        "score_tens": gb.memory[0xFFCA] | (gb.memory[0xFFCB] << 8),
                        "lives": gb.memory[0xCA44],
                        "stage": gb.memory[0xCA45],
                        "scroll_flag": gb.memory[0xFFAB],
                        "bonus_flag": gb.memory[0xFFAA],
                        "bonus_timer": gb.memory[0xCA48],
                        "row_offset": gb.memory[0xFFA9],
                        "in_joyp_raw": latched[0],
                        "in_joyp_edge": latched[1],
                    }
                    records.append(rec)
                    tick_id[0] += 1

                gb.hook_register(0, 0x051F, _on_logic_start, None)
                gb.hook_register(0, 0x0525, _on_logic_end, None)

                lives_before_expire = gb.memory[0xCA44]

                for f in range(100):
                    ball_x = gb.memory[0xFFB6]
                    pad_x = gb.memory[0xFFC0]
                    pad_w = gb.memory[0xFFC2]
                    target_x = ball_x - (pad_w // 2)
                    if pad_x < target_x - 1:
                        gb.button_press("right"); gb.button_release("left")
                    elif pad_x > target_x + 1:
                        gb.button_press("left"); gb.button_release("right")
                    else:
                        gb.button_release("left"); gb.button_release("right")
                    gb.tick()
                    if gb.memory[0xFFA4] == 7:
                        break

                lives_after_expire = gb.memory[0xCA44]
            finally:
                gb.stop()

            with open(tmp_trace, "w", encoding="utf-8") as fp:
                for r in records:
                    fp.write(json.dumps(r) + "\n")

            res = subprocess.run(
                [str(EXE_PATH), "--verify-trace", str(local_rom), str(tmp_trace)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 0, msg=res.stderr)
            report = json.loads(res.stdout.strip())
            self.assertEqual(report["mismatches"], 0)
            self.assertGreater(report["ticks_compared"], 30)

            # Assert coverage for expiration
            self.assertEqual(records[-1]["bonus_timer"], 0, "Timer did not expire to 0")
            self.assertEqual(records[-1]["state"], 7, "Did not transition to State 7 upon expiration")
            self.assertEqual(lives_after_expire, lives_before_expire, "Lives were incorrectly lost during bonus stage expiration")

    def test_autonomous_clock_and_bonus_lifecycle_passes(self):
        """Verify native simulation runs on independent clock without per-tick oracle resets."""
        proc = subprocess.run([str(EXE_PATH), "--verify-autonomous-clock"], check=True, capture_output=True, text=True)
        self.assertIn("PASS: autonomous clock and bonus lifecycle verified", proc.stdout)

    def test_game_progression_lifecycle_synthetic(self):
        """Verify progression transitions via synthetic multi-stage verifier."""
        proc = subprocess.run([str(EXE_PATH), "--verify-multi-stage-rules"], check=True, capture_output=True, text=True)
        self.assertIn("PASS: multi-stage rules verified", proc.stdout)
        self.assertIn("ending sequence", proc.stdout)
        self.assertIn("game over reset", proc.stdout)
        self.assertIn("bonus reward", proc.stdout)

    def test_local_rom_progression_scenarios_if_present(self):
        """Verify full progression lifecycle against PyBoy: Game Over, Ending, Bonus reward, Stage wrap."""
        local_rom = ROOT.parent / "Local-Privado/roms/Alleyway (USA).gb"
        if not local_rom.is_file():
            self.skipTest("User local ROM not present in ../Local-Privado/roms/Alleyway (USA).gb")
        from pyboy import PyBoy

        def _spawn_past_title():
            gb = PyBoy(str(local_rom), window="null", sound_emulated=False)
            for f in range(346):
                if f == 120: gb.button_press("start")
                elif f == 125: gb.button_release("start")
                gb.tick()
            return gb

        # 1. Test Game Over: lives = 0, trigger State 7 -> verify State 11 (0x0B)
        gb1 = _spawn_past_title()
        try:
            gb1.memory[0xCA44] = 0
            gb1.memory[0xFFAA] = 0
            gb1.memory[0xFFA4] = 7
            for _ in range(120): gb1.tick()
            self.assertEqual(gb1.memory[0xFFA4], 11, "Did not transition to State 11 (Game Over) on last life loss")
        finally:
            gb1.stop()

        # 2. Test Bonus Stage clear and authentic reward (500 tens = 5,000 pts)
        gb2 = _spawn_past_title()
        try:
            # Advance to Stage 4 (stage = 3)
            gb2.memory[0xCA45] = 2; gb2.memory[0xFFAA] = 0
            gb2.memory[0xFFC8] = 0; gb2.memory[0xFFC9] = 0; gb2.memory[0xFFA4] = 8
            for _ in range(500):
                if gb2.memory[0xCA45] == 3 and gb2.memory[0xFFA4] == 5:
                    break
                gb2.tick()
            self.assertEqual(gb2.memory[0xCA45], 3, "Failed to enter Stage 4 (bonus)")
            self.assertEqual(gb2.memory[0xFFAA], 1, "bonus_flag != 1 in Stage 4")

            # Clear all bonus bricks and trigger State 8
            score_before = gb2.memory[0xFFCA] | (gb2.memory[0xFFCB] << 8)
            gb2.memory[0xFFC8] = 0; gb2.memory[0xFFC9] = 0; gb2.memory[0xFFA4] = 8
            for _ in range(600):
                if gb2.memory[0xCA45] == 4:
                    break
                gb2.tick()
            score_after = gb2.memory[0xFFCA] | (gb2.memory[0xFFCB] << 8)
            delta_tens = score_after - score_before
            self.assertEqual(delta_tens, 500, f"Expected 500 tens (+5,000 pts) bonus reward, got {delta_tens}")
            self.assertEqual(gb2.memory[0xCA45], 4, "Did not advance to Stage 5 (stage = 4)")
        finally:
            gb2.stop()

        # 3. Test Stage 32 clear -> State 9 (Ending sequence) -> Stage 0 wraparound
        gb3 = _spawn_past_title()
        try:
            gb3.memory[0xCA45] = 31 # Stage 32
            gb3.memory[0xFFAA] = 0
            gb3.memory[0xFFC8] = 0; gb3.memory[0xFFC9] = 0
            gb3.memory[0xFFA4] = 8
            states_seen = []
            for _ in range(300):
                st = gb3.memory[0xFFA4]
                if not states_seen or states_seen[-1] != st:
                    states_seen.append(st)
                gb3.tick()
            self.assertIn(9, states_seen, "Did not enter State 9 (Ending sequence) on Stage 32 clear")
            self.assertEqual(gb3.memory[0xCA45], 0, "Stage counter did not wrap from 31 to 0")
        finally:
            gb3.stop()

        # 4. Test Bonus Stage ball drop preserves lives and advances to next stage
        gb4 = _spawn_past_title()
        try:
            # Advance to Stage 4 (bonus)
            gb4.memory[0xCA45] = 2; gb4.memory[0xFFAA] = 0
            gb4.memory[0xFFC8] = 0; gb4.memory[0xFFC9] = 0; gb4.memory[0xFFA4] = 8
            for _ in range(500):
                if gb4.memory[0xCA45] == 3 and gb4.memory[0xFFA4] == 5:
                    break
                gb4.tick()
            self.assertEqual(gb4.memory[0xCA45], 3, "Failed to reach Stage 4")
            lives_before = gb4.memory[0xCA44]
            # Trigger ball drop in bonus stage (State 7)
            gb4.memory[0xFFA4] = 7
            for _ in range(300):
                if gb4.memory[0xCA45] == 4 and gb4.memory[0xFFA4] == 5:
                    break
                gb4.tick()
            self.assertEqual(gb4.memory[0xCA45], 4, "Did not advance to Stage 5 after bonus ball drop")
            self.assertEqual(gb4.memory[0xCA44], lives_before, "Lives were decremented after bonus ball drop")
        finally:
            gb4.stop()

    def test_scheduler_timing_and_fractional_audio(self):
        """Verify monotonic scheduler timing, fractional zero-drift audio, and bonus conversions."""
        proc = subprocess.run([str(EXE_PATH), "--verify-scheduler-timing"], check=True, capture_output=True, text=True)
        self.assertIn("PASS: scheduler monotonic timing and fractional audio verified", proc.stdout)

    def test_all_32_stage_descriptors_and_advanced_rounds_regression(self):
        """Verify ROM descriptors for all 32 stages: 8 rounds x 4 types (A, B, C, D) and advanced layouts."""
        local_rom = ROOT.parent / "Local-Privado/roms/Alleyway (USA).gb"
        if not local_rom.is_file():
            self.skipTest("User local ROM not present in ../Local-Privado/roms/Alleyway (USA).gb")
        rom = local_rom.read_bytes()

        # Expected stage type cycle: A, B, C, D repeated for 8 rounds
        # Stage descriptors start at 0x1BE1, 3 bytes each:
        # byte 0: flags (bit 7 = bonus_flag, bit 6 = scroll_flag, bits 0..5 = pattern index)
        # bytes 1..2: little-endian pointer to brick map
        stage_types = []
        for s in range(32):
            desc = 0x1BE1 + s * 3
            b0 = rom[desc]
            m_ptr = rom[desc + 1] | (rom[desc + 2] << 8)
            is_bonus = (b0 & 0x80) != 0
            is_scroll = (b0 & 0x40) != 0

            # Count rows and destructible bricks in ROM map
            ptr = m_ptr
            rows = 0
            brick_count = 0
            while ptr < len(rom) and rom[ptr] != 0xFF:
                row = rom[ptr:ptr + 14]
                brick_count += sum(1 for x in row if x != 0)
                rows += 1
                ptr += 14

            stype = 'D' if is_bonus else ('B' if is_scroll else ('C' if rows >= 25 else 'A'))
            stage_types.append(stype)

            # Assert valid map properties for each stage
            self.assertGreater(rows, 0, f"Stage {s+1} has 0 rows")
            self.assertGreater(brick_count, 0, f"Stage {s+1} has 0 bricks")

            # Check authentic structure:
            round_idx = s // 4
            stage_in_round = s % 4
            if stage_in_round == 0:
                self.assertEqual(stype, 'A', f"Stage {s+1} should be Type A (static)")
            elif stage_in_round == 1:
                self.assertEqual(stype, 'B', f"Stage {s+1} should be Type B (horizontal scroll)")
                pattern_idx = b0 & 0x3F
                self.assertLess(pattern_idx, 8, f"Stage {s+1} scroll pattern index out of range")
            elif stage_in_round == 2:
                self.assertEqual(stype, 'C', f"Stage {s+1} should be Type C (vertical drop, 40 rows)")
                self.assertEqual(rows, 40, f"Stage {s+1} should have 40 rows for vertical drop")
            elif stage_in_round == 3:
                self.assertEqual(stype, 'D', f"Stage {s+1} should be Type D (bonus stage)")
                self.assertEqual(rows, 20, f"Stage {s+1} bonus should have 20 rows")

        # Verify exact round breakdown (8 rounds x 4 stages = 32)
        self.assertEqual(len(stage_types), 32)
        self.assertEqual(stage_types.count('A'), 8)
        self.assertEqual(stage_types.count('B'), 8)
        self.assertEqual(stage_types.count('C'), 8)
        self.assertEqual(stage_types.count('D'), 8)

        # Regressions for specific advanced rounds:
        # Stage 8 (Round 2 Bonus, Heart shape): map_ptr 0x3695, 162 bricks
        desc8 = 0x1BE1 + 7 * 3
        map8 = rom[desc8 + 1] | (rom[desc8 + 2] << 8)
        self.assertEqual(map8, 0x3695)

        # Stage 14 (Round 4 Scroll, 108 bricks): map_ptr 0x1EE7, pattern index 3
        desc14 = 0x1BE1 + 13 * 3
        self.assertEqual(rom[desc14] & 0x3F, 3)
        map14 = rom[desc14 + 1] | (rom[desc14 + 2] << 8)
        self.assertEqual(map14, 0x1EE7)

        # Stage 23 (Round 6 Drop, 300 bricks, 40 rows): map_ptr 0x2EE9
        desc23 = 0x1BE1 + 22 * 3
        map23 = rom[desc23 + 1] | (rom[desc23 + 2] << 8)
        self.assertEqual(map23, 0x2EE9)


if __name__ == "__main__":
    unittest.main()
