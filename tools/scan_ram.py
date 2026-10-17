"""Differential WRAM/HRAM/OAM scanner for locating state variables in a Game Boy ROM.

Supports two complementary discovery modes without assuming any hardcoded addresses:
1. diff_timelines: runs two input timelines to the same frame and lists all addresses
   in WRAM (0xC000-0xDFFF), OAM (0xFE00-0xFE9F), and HRAM (0xFF80-0xFFFE) that differ.
2. find_correlated_addresses: records memory snapshots at specified sample frames and
   identifies all WRAM/HRAM addresses that track a target series (e.g. an OAM sprite
   coordinate or frame delta) with a constant offset.
"""
import argparse
import json
from pathlib import Path
from typing import Any
from pyboy import PyBoy

try:
    from tools.capture_reference import normalize_input_timeline
except ImportError:
    from capture_reference import normalize_input_timeline  # type: ignore

MEMORY_REGIONS: dict[str, tuple[int, int]] = {
    "wram": (0xC000, 0xDFFF),
    "oam": (0xFE00, 0xFE9F),
    "io": (0xFF00, 0xFF7F),
    "hram": (0xFF80, 0xFFFE),
}


def capture_snapshots(
    rom_path: Path,
    sample_frames: list[int],
    inputs: list[dict[str, Any]] | None = None,
    regions: tuple[str, ...] = ("wram", "oam", "hram"),
) -> dict[int, dict[int, int]]:
    """Run PyBoy and return {frame: {address: byte_val}} at each requested 0-indexed frame."""
    if not sample_frames:
        raise ValueError("sample_frames must not be empty")
    if any(f < 0 for f in sample_frames):
        raise ValueError("sample_frames must contain non-negative frame indices")
    for reg in regions:
        if reg not in MEMORY_REGIONS:
            raise ValueError(f"Unknown memory region {reg!r} (valid: {sorted(MEMORY_REGIONS)})")

    total_frames = max(sample_frames) + 1
    normalized_inputs = normalize_input_timeline(inputs, total_frames)
    events_by_frame: dict[int, list[dict[str, Any]]] = {}
    for ev in normalized_inputs:
        events_by_frame.setdefault(ev["frame"], []).append(ev)

    sample_set = set(sample_frames)
    snapshots: dict[int, dict[int, int]] = {}

    gb = PyBoy(str(rom_path), window="null", sound_emulated=False)
    try:
        gb.set_emulation_speed(0)
        for frame_idx in range(total_frames):
            for ev in events_by_frame.get(frame_idx, []):
                if ev["action"] == "press":
                    gb.button_press(ev["button"])
                else:
                    gb.button_release(ev["button"])
            if not gb.tick(1, False):
                raise RuntimeError(f"Emulator stopped early at frame {frame_idx}")
            if frame_idx in sample_set:
                mem_map: dict[int, int] = {}
                for reg in regions:
                    start, end = MEMORY_REGIONS[reg]
                    for addr in range(start, end + 1):
                        mem_map[addr] = int(gb.memory[addr])
                snapshots[frame_idx] = mem_map
    finally:
        gb.stop(save=False)

    return snapshots


def diff_timelines(
    rom_path: Path,
    frames: int,
    inputs_a: list[dict[str, Any]] | None = None,
    inputs_b: list[dict[str, Any]] | None = None,
    regions: tuple[str, ...] = ("wram", "oam", "hram"),
) -> list[dict[str, Any]]:
    """Compare final memory state at (frames - 1) between two input timelines."""
    if frames < 1:
        raise ValueError("frames must be >= 1")
    target_frame = frames - 1
    snap_a = capture_snapshots(rom_path, [target_frame], inputs=inputs_a, regions=regions)[target_frame]
    snap_b = capture_snapshots(rom_path, [target_frame], inputs=inputs_b, regions=regions)[target_frame]

    diffs: list[dict[str, Any]] = []
    for addr in sorted(snap_a.keys()):
        val_a = snap_a[addr]
        val_b = snap_b[addr]
        if val_a != val_b:
            diffs.append({
                "address": addr,
                "address_hex": f"0x{addr:04X}",
                "value_a": val_a,
                "value_b": val_b,
                "delta": val_b - val_a,
            })
    return diffs


def find_correlated_addresses(
    snapshots: dict[int, dict[int, int]],
    sample_frames: list[int],
    target_values: list[int],
    search_regions: tuple[str, ...] = ("wram", "hram"),
) -> list[dict[str, Any]]:
    """Find addresses in search_regions whose value minus target_values[i] is constant across sample_frames."""
    if len(sample_frames) != len(target_values) or not sample_frames:
        raise ValueError("sample_frames and target_values must have equal non-zero length")

    allowed_ranges = [MEMORY_REGIONS[r] for r in search_regions]
    first_snap = snapshots[sample_frames[0]]
    candidates: list[dict[str, Any]] = []

    for addr, first_val in sorted(first_snap.items()):
        if not any(start <= addr <= end for start, end in allowed_ranges):
            continue
        offset = first_val - target_values[0]
        matches_all = True
        series: list[int] = [first_val]
        for f_idx, t_val in zip(sample_frames[1:], target_values[1:]):
            val = snapshots[f_idx][addr]
            if (val - t_val) != offset:
                matches_all = False
                break
            series.append(val)
        if matches_all:
            candidates.append({
                "address": addr,
                "address_hex": f"0x{addr:04X}",
                "offset_from_target": offset,
                "values": series,
            })
    return candidates


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--inputs-a", type=Path, default=None)
    parser.add_argument("--inputs-b", type=Path, default=None)
    parser.add_argument(
        "--regions",
        nargs="+",
        default=["wram", "oam", "hram"],
        choices=sorted(MEMORY_REGIONS.keys()),
    )
    args = parser.parse_args()

    inputs_a = json.loads(args.inputs_a.read_text(encoding="utf-8")) if args.inputs_a else None
    inputs_b = json.loads(args.inputs_b.read_text(encoding="utf-8")) if args.inputs_b else None
    diffs = diff_timelines(
        rom_path=args.rom,
        frames=args.frames,
        inputs_a=inputs_a,
        inputs_b=inputs_b,
        regions=tuple(args.regions),
    )
    print(json.dumps({"diff_count": len(diffs), "diffs": diffs}, indent=2))


if __name__ == "__main__":
    main()
