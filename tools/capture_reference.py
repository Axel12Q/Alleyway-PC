"""Capture deterministic reference frames and per-frame/per-logic-tick traces from a ROM."""
import argparse
import hashlib
import json
from importlib.metadata import version
from pathlib import Path
from typing import Any
from pyboy import PyBoy

try:
    from tools.inspect_rom import inspect as inspect_rom_bytes
except ImportError:
    from inspect_rom import inspect as inspect_rom_bytes  # type: ignore

VALID_BUTTONS = frozenset({"a", "b", "start", "select", "up", "down", "left", "right"})
VALID_ACTIONS = frozenset({"press", "release"})
VALID_WATCH_TYPES = frozenset({"u8", "i8", "u16le", "u16be", "bcd8"})


def normalize_input_timeline(inputs: list[dict[str, Any]] | None, total_frames: int) -> list[dict[str, Any]]:
    """Validate and canonicalize a frame-indexed input timeline (0 <= frame < total_frames)."""
    if total_frames < 1:
        raise ValueError("total_frames must be >= 1")
    if not inputs:
        return []
    if not isinstance(inputs, list):
        raise ValueError("Input timeline must be a JSON list of event objects")

    normalized: list[dict[str, Any]] = []
    seen_per_frame_button: set[tuple[int, str]] = set()

    for idx, raw_event in enumerate(inputs):
        if not isinstance(raw_event, dict):
            raise ValueError(f"Input event at index {idx} must be an object")
        frame = raw_event.get("frame")
        button = raw_event.get("button")
        action = raw_event.get("action")

        if isinstance(frame, bool) or not isinstance(frame, int):
            raise ValueError(f"Input event {idx}: 'frame' must be an integer")
        if frame < 0 or frame >= total_frames:
            raise ValueError(f"Input event {idx}: frame {frame} out of range [0, {total_frames - 1}]")
        if not isinstance(button, str) or button.lower() not in VALID_BUTTONS:
            raise ValueError(f"Input event {idx}: invalid button {button!r} (valid: {sorted(VALID_BUTTONS)})")
        if not isinstance(action, str) or action.lower() not in VALID_ACTIONS:
            raise ValueError(f"Input event {idx}: invalid action {action!r} (valid: {sorted(VALID_ACTIONS)})")

        btn_norm = button.lower()
        act_norm = action.lower()
        key = (frame, btn_norm)
        if key in seen_per_frame_button:
            raise ValueError(f"Duplicate input event for button {btn_norm!r} on frame {frame}")
        seen_per_frame_button.add(key)

        normalized.append({"frame": frame, "button": btn_norm, "action": act_norm})

    normalized.sort(key=lambda item: (item["frame"], item["button"], item["action"]))
    return normalized


def parse_watch_spec(watch_spec: dict[str, str] | None) -> dict[str, dict[str, Any]]:
    """Parse watchpoint definitions like {'paddle_x': '0xC000:u8'} into validated descriptors."""
    if not watch_spec:
        return {}
    if not isinstance(watch_spec, dict):
        raise ValueError("watch_spec must be a mapping of name -> 'address[:type]'")

    parsed: dict[str, dict[str, Any]] = {}
    for name, spec in sorted(watch_spec.items()):
        if not name or not isinstance(name, str):
            raise ValueError("Watchpoint name must be a non-empty string")
        if not isinstance(spec, str):
            raise ValueError(f"Watchpoint {name!r} specification must be a string")
        parts = spec.split(":")
        if len(parts) == 1:
            addr_str, fmt = parts[0], "u8"
        elif len(parts) == 2:
            addr_str, fmt = parts[0], parts[1].lower()
        else:
            raise ValueError(f"Invalid watchpoint specification for {name!r}: {spec!r}")

        if fmt not in VALID_WATCH_TYPES:
            raise ValueError(f"Watchpoint {name!r}: unsupported type {fmt!r} (valid: {sorted(VALID_WATCH_TYPES)})")
        try:
            address = int(addr_str, 0)
        except ValueError as exc:
            raise ValueError(f"Watchpoint {name!r}: invalid address {addr_str!r}") from exc

        width = 2 if fmt in ("u16le", "u16be") else 1
        if address < 0 or (address + width - 1) > 0xFFFF:
            raise ValueError(f"Watchpoint {name!r}: address {hex(address)} out of 16-bit memory range")

        parsed[name] = {
            "address": address,
            "address_hex": f"0x{address:04X}",
            "type": fmt,
            "spec": f"0x{address:04X}:{fmt}",
        }
    return parsed


def read_watch_value(memory: Any, address: int, fmt: str) -> int:
    b0 = int(memory[address])
    if fmt == "u8":
        return b0
    if fmt == "i8":
        return b0 - 256 if b0 >= 128 else b0
    if fmt == "bcd8":
        return ((b0 >> 4) & 0x0F) * 10 + (b0 & 0x0F)
    b1 = int(memory[address + 1])
    if fmt == "u16le":
        return b0 | (b1 << 8)
    if fmt == "u16be":
        return (b0 << 8) | b1
    raise ValueError(f"Unsupported watch format: {fmt}")


def parse_cli_watch_args(watch_items: list[str] | None, watch_file: Path | None) -> dict[str, str]:
    combined: dict[str, str] = {}
    if watch_file is not None:
        raw = json.loads(watch_file.read_text(encoding="utf-8"))
        if not isinstance(raw, dict):
            raise ValueError("Watch file must contain a JSON object mapping name -> '0xADDR[:type]'")
        for k, v in raw.items():
            combined[str(k)] = str(v)
    for item in watch_items or []:
        if "=" not in item:
            raise ValueError(f"Invalid --watch argument {item!r}; expected NAME=0xADDR[:TYPE]")
        name, spec = item.split("=", 1)
        combined[name.strip()] = spec.strip()
    return combined


def parse_logic_hooks_arg(raw: str | None) -> tuple[int, int] | None:
    if not raw:
        return None
    parts = raw.split(":")
    if len(parts) != 2:
        raise ValueError("Expected --logic-hooks START_ADDR:END_ADDR (e.g. 0x051F:0x0525)")
    start_addr = int(parts[0], 0)
    end_addr = int(parts[1], 0)
    if not (0 <= start_addr <= 0x7FFF and 0 <= end_addr <= 0x7FFF):
        raise ValueError("Logic hook addresses must be in ROM range 0x0000..0x7FFF")
    return (start_addr, end_addr)


def run_capture(
    rom_path: Path,
    frames: int = 120,
    inputs: list[dict[str, Any]] | None = None,
    watch_spec: dict[str, str] | None = None,
    out_dir: Path | None = None,
    logic_hooks: tuple[int, int] | None = None,
) -> dict[str, Any]:
    """Run a deterministic PyBoy capture and write screen.ppm, trace.jsonl, and capture.json."""
    if not rom_path.is_file():
        raise FileNotFoundError(f"ROM file does not exist: {rom_path}")
    if frames < 1:
        raise ValueError("Frame count must be a positive integer")

    rom_bytes = rom_path.read_bytes()
    rom_info = inspect_rom_bytes(rom_bytes)
    if not rom_info["pyboy_loadable"]:
        raise ValueError(
            "ROM is not loadable by PyBoy (requires 16 KiB bank alignment and valid header checksum)"
        )

    normalized_inputs = normalize_input_timeline(inputs, frames)
    parsed_watches = parse_watch_spec(watch_spec)

    inputs_canonical_json = json.dumps(normalized_inputs, sort_keys=True, separators=(",", ":"))
    inputs_sha256 = hashlib.sha256(inputs_canonical_json.encode("utf-8")).hexdigest()
    digest = rom_info["sha256"]

    if out_dir is None:
        root = Path(__file__).resolve().parents[1]
        folder_name = f"frame-{frames}" if not normalized_inputs else f"frame-{frames}-in-{inputs_sha256[:8]}"
        out_dir = root / "local/captures" / digest[:12] / folder_name
    out_dir.mkdir(parents=True, exist_ok=True)

    events_by_frame: dict[int, list[dict[str, Any]]] = {}
    for ev in normalized_inputs:
        events_by_frame.setdefault(ev["frame"], []).append(ev)

    held_buttons: set[str] = set()
    trace_lines: list[str] = []
    logic_lines: list[str] = []
    current_frame_ref = [0]
    latched_inputs_ref = [0xFF, 0xFF]

    gb = PyBoy(str(rom_path), window="null", sound_emulated=False)
    try:
        gb.set_emulation_speed(0)
        if logic_hooks is not None:
            start_pc, end_pc = logic_hooks

            def _on_logic_start(ctx: Any) -> None:
                latched_inputs_ref[0] = int(ctx.memory[0xFF8C])
                latched_inputs_ref[1] = int(ctx.memory[0xFF8D])

            def _on_logic_end(ctx: Any) -> None:
                w_vals = {
                    wname: read_watch_value(ctx.memory, wdesc["address"], wdesc["type"])
                    for wname, wdesc in parsed_watches.items()
                }
                tick_rec = {
                    "tick": len(logic_lines),
                    "frame": current_frame_ref[0],
                    "in_joyp_raw": latched_inputs_ref[0],
                    "in_joyp_edge": latched_inputs_ref[1],
                    "watch": w_vals,
                }
                logic_lines.append(json.dumps(tick_rec, sort_keys=True, separators=(",", ":")))

            gb.hook_register(0, start_pc, _on_logic_start, gb)
            gb.hook_register(0, end_pc, _on_logic_end, gb)

        for frame_idx in range(frames):
            current_frame_ref[0] = frame_idx
            frame_events = events_by_frame.get(frame_idx, [])
            applied_labels: list[str] = []
            for ev in frame_events:
                btn = ev["button"]
                act = ev["action"]
                if act == "press":
                    gb.button_press(btn)
                    held_buttons.add(btn)
                else:
                    gb.button_release(btn)
                    held_buttons.discard(btn)
                applied_labels.append(f"{btn}:{act}")

            is_last_frame = frame_idx == (frames - 1)
            if not gb.tick(1, is_last_frame):
                raise RuntimeError(f"Emulator stopped before capture completed at frame {frame_idx}")

            watch_values = {
                wname: read_watch_value(gb.memory, wdesc["address"], wdesc["type"])
                for wname, wdesc in parsed_watches.items()
            }

            frame_record = {
                "frame": frame_idx,
                "inputs_applied": applied_labels,
                "held_buttons": sorted(held_buttons),
                "pc": int(gb.register_file.PC),
                "sp": int(gb.register_file.SP),
                "ly": int(gb.memory[0xFF44]),
                "lcdc": int(gb.memory[0xFF40]),
                "stat": int(gb.memory[0xFF41]),
                "scy": int(gb.memory[0xFF42]),
                "scx": int(gb.memory[0xFF43]),
                "bgp": int(gb.memory[0xFF47]),
                "wy": int(gb.memory[0xFF4A]),
                "wx": int(gb.memory[0xFF4B]),
                "joyp": int(gb.memory[0xFF00]),
                "sb": int(gb.memory[0xFF01]),
                "sc": int(gb.memory[0xFF02]),
                "bootrom_unmapped": int(gb.memory[0xFF50]) != 0,
                "watch": watch_values,
            }
            trace_lines.append(json.dumps(frame_record, sort_keys=True, separators=(",", ":")))

        rgba = gb.screen.ndarray.copy()
    finally:
        gb.stop(save=False)

    # PPM avoids adding Pillow solely for image export.
    ppm_bytes = b"P6\n160 144\n255\n" + rgba[:, :, :3].tobytes()
    (out_dir / "screen.ppm").write_bytes(ppm_bytes)

    trace_payload = "\n".join(trace_lines) + "\n"
    (out_dir / "trace.jsonl").write_text(trace_payload, encoding="utf-8")
    trace_sha256 = hashlib.sha256(trace_payload.encode("utf-8")).hexdigest()
    screen_sha256 = hashlib.sha256(rgba.tobytes()).hexdigest()

    logic_trace_sha256 = None
    if logic_hooks is not None:
        logic_payload = "\n".join(logic_lines) + ("\n" if logic_lines else "")
        (out_dir / "logic_trace.jsonl").write_text(logic_payload, encoding="utf-8")
        logic_trace_sha256 = hashlib.sha256(logic_payload.encode("utf-8")).hexdigest()

    capture_metadata = {
        "schema_version": 2,
        "rom_sha256": digest,
        "rom_title": rom_info["title"],
        "rom_version": rom_info["version"],
        "frames": frames,
        "inputs": normalized_inputs,
        "inputs_sha256": inputs_sha256,
        "watch_spec": {k: v["spec"] for k, v in parsed_watches.items()},
        "logic_hooks": [f"0x{logic_hooks[0]:04X}", f"0x{logic_hooks[1]:04X}"] if logic_hooks else None,
        "logic_ticks": len(logic_lines) if logic_hooks else None,
        "logic_trace_sha256": logic_trace_sha256,
        "pyboy_version": version("pyboy"),
        "screen_sha256": screen_sha256,
        "ppm_sha256": hashlib.sha256(ppm_bytes).hexdigest(),
        "trace_sha256": trace_sha256,
        "trace_file": "trace.jsonl",
        "screen_file": "screen.ppm",
        "sound_emulated": False,
        "window": "null",
        "bootrom": "PyBoy default (unmaps 0xFF50 at frame ~65)",
        "note": "Reference capture and hardware/watchpoint trace. No Alleyway RAM addresses assumed.",
        "out_dir": str(out_dir),
    }
    (out_dir / "capture.json").write_text(
        json.dumps(capture_metadata, indent=2) + "\n",
        encoding="utf-8",
    )
    return capture_metadata


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument(
        "--inputs",
        type=Path,
        default=None,
        help="Optional JSON file containing a list of {frame, button, action} input events",
    )
    parser.add_argument(
        "--watch",
        action="append",
        default=[],
        help="Optional watchpoint NAME=0xADDR[:u8|i8|u16le|u16be|bcd8] (may be repeated)",
    )
    parser.add_argument(
        "--watch-file",
        type=Path,
        default=None,
        help="Optional JSON file mapping watchpoint names to '0xADDR[:type]'",
    )
    parser.add_argument(
        "--logic-hooks",
        type=str,
        default=None,
        help="Optional START_PC:END_PC hooks to record atomic logic ticks into logic_trace.jsonl (e.g. 0x051F:0x0525)",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Optional explicit output directory (defaults inside ignored local/captures/)",
    )
    args = parser.parse_args()
    if not args.rom.is_file() or args.frames < 1:
        parser.error("Provide an existing ROM and a positive frame count")

    try:
        inputs_data = None
        if args.inputs is not None:
            inputs_data = json.loads(args.inputs.read_text(encoding="utf-8"))
        watch_spec = parse_cli_watch_args(args.watch, args.watch_file)
        logic_hooks = parse_logic_hooks_arg(args.logic_hooks)
        result = run_capture(
            rom_path=args.rom,
            frames=args.frames,
            inputs=inputs_data,
            watch_spec=watch_spec,
            out_dir=args.out_dir,
            logic_hooks=logic_hooks,
        )
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        parser.exit(1, f"Error: {error}\n")
    print(result["out_dir"])


if __name__ == "__main__":
    main()
