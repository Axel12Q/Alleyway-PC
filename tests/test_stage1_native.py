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
    rom[0x1B87 + 4] = 0x00
    # Stage 0 descriptor at 0x1BE1 -> 0x1C44
    rom[0x1BE1] = 0x00
    rom[0x1BE2:0x1BE4] = (0x1C44).to_bytes(2, "little")
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
            self.assertEqual(report["bricks_left"], 83)
            self.assertEqual(report["score"], 10)


if __name__ == "__main__":
    unittest.main()
