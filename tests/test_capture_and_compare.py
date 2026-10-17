"""Deterministic input timeline, reference capture, and trace comparator tests on synthetic fixtures."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def _load_tool(name: str, rel_path: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


synth_mod = _load_tool("synthetic_rom", "tools/synthetic_rom.py")
capture_mod = _load_tool("capture_reference", "tools/capture_reference.py")
compare_mod = _load_tool("compare_traces", "tools/compare_traces.py")


class TimelineAndWatchValidationTests(unittest.TestCase):
    def test_validates_and_sorts_input_timeline(self):
        raw = [
            {"frame": 10, "button": "RIGHT", "action": "Release"},
            {"frame": 5, "button": "right", "action": "press"},
            {"frame": 5, "button": "a", "action": "press"},
        ]
        norm = capture_mod.normalize_input_timeline(raw, total_frames=20)
        self.assertEqual(
            norm,
            [
                {"frame": 5, "button": "a", "action": "press"},
                {"frame": 5, "button": "right", "action": "press"},
                {"frame": 10, "button": "right", "action": "release"},
            ],
        )

    def test_rejects_invalid_timeline_events(self):
        with self.assertRaises(ValueError):
            capture_mod.normalize_input_timeline([{"frame": -1, "button": "a", "action": "press"}], 10)
        with self.assertRaises(ValueError):
            capture_mod.normalize_input_timeline([{"frame": 10, "button": "a", "action": "press"}], 10)
        with self.assertRaises(ValueError):
            capture_mod.normalize_input_timeline([{"frame": 1, "button": "turbo", "action": "press"}], 10)
        with self.assertRaises(ValueError):
            capture_mod.normalize_input_timeline([{"frame": 1, "button": "a", "action": "hold"}], 10)
        with self.assertRaises(ValueError):
            capture_mod.normalize_input_timeline(
                [
                    {"frame": 3, "button": "a", "action": "press"},
                    {"frame": 3, "button": "a", "action": "release"},
                ],
                10,
            )

    def test_parses_and_validates_watch_specs(self):
        parsed = capture_mod.parse_watch_spec({
            "x": "0xC001:u8",
            "vel": "0xC002:i8",
            "word": "0xC010:u16le",
            "score": "0xC020:bcd8",
        })
        self.assertEqual(parsed["x"]["address"], 0xC001)
        self.assertEqual(parsed["word"]["type"], "u16le")
        with self.assertRaises(ValueError):
            capture_mod.parse_watch_spec({"bad_addr": "0x10000:u8"})
        with self.assertRaises(ValueError):
            capture_mod.parse_watch_spec({"bad_word": "0xFFFF:u16le"})
        with self.assertRaises(ValueError):
            capture_mod.parse_watch_spec({"bad_type": "0xC000:f32"})


class SyntheticCaptureAndCompareTests(unittest.TestCase):
    def test_repeat_runs_match_and_deliberate_mismatch_detected(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)
            rom_path = tmp_dir / "synthetic_interactive.gb"
            rom_path.write_bytes(synth_mod.build_synthetic_rom(interactive=True))

            timeline_a = [
                {"frame": 66, "button": "right", "action": "press"},
                {"frame": 67, "button": "a", "action": "press"},
                {"frame": 70, "button": "a", "action": "release"},
                {"frame": 71, "button": "right", "action": "release"},
            ]
            # Deliberate mismatch: release 'right' at frame 69 instead of 71
            timeline_b = [
                {"frame": 66, "button": "right", "action": "press"},
                {"frame": 67, "button": "a", "action": "press"},
                {"frame": 69, "button": "right", "action": "release"},
                {"frame": 70, "button": "a", "action": "release"},
            ]

            run1_dir = tmp_dir / "run1"
            run2_dir = tmp_dir / "run2"
            run3_dir = tmp_dir / "run3_mismatch"

            meta1 = capture_mod.run_capture(
                rom_path=rom_path,
                frames=74,
                inputs=timeline_a,
                watch_spec=synth_mod.SYNTHETIC_WATCH_SPEC,
                out_dir=run1_dir,
            )
            meta2 = capture_mod.run_capture(
                rom_path=rom_path,
                frames=74,
                inputs=timeline_a,
                watch_spec=synth_mod.SYNTHETIC_WATCH_SPEC,
                out_dir=run2_dir,
            )
            meta3 = capture_mod.run_capture(
                rom_path=rom_path,
                frames=74,
                inputs=timeline_b,
                watch_spec=synth_mod.SYNTHETIC_WATCH_SPEC,
                out_dir=run3_dir,
            )

            self.assertEqual(meta1["trace_sha256"], meta2["trace_sha256"])
            self.assertEqual(meta1["screen_sha256"], meta2["screen_sha256"])

            parity = compare_mod.compare_captures(run1_dir, run2_dir)
            self.assertTrue(parity["logic_match"])
            self.assertTrue(parity["screen_match"])
            self.assertEqual(parity["divergence_count"], 0)

            # Verify that synthetic X moved by +5 (from 80 to 85) and button_a_ticks is 3 in run1
            records1 = compare_mod.load_trace_jsonl(run1_dir / "trace.jsonl")
            self.assertEqual(records1[-1]["watch"]["synth_x"], 85)
            self.assertEqual(records1[-1]["watch"]["button_a_ticks"], 3)

            # Compare run1 vs deliberate mismatch run3
            diff_report = compare_mod.compare_captures(run1_dir, run3_dir)
            self.assertFalse(diff_report["logic_match"])
            self.assertFalse(diff_report["screen_match"])
            self.assertIsNotNone(diff_report["first_divergence"])
            self.assertEqual(diff_report["first_divergence"]["frame"], 69)


if __name__ == "__main__":
    unittest.main()
