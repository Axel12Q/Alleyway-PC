"""Compare two capture directories or trace.jsonl files for deterministic parity.

Separates logical/register trace comparison from final framebuffer (screen_sha256)
comparison, as required by docs/RESEARCH_PROTOCOL.md.
"""
import argparse
import json
from pathlib import Path
from typing import Any


DEFAULT_TRACE_FIELDS = (
    "inputs_applied",
    "held_buttons",
    "pc",
    "sp",
    "ly",
    "lcdc",
    "stat",
    "scy",
    "scx",
    "bgp",
    "wy",
    "wx",
    "joyp",
    "sb",
    "sc",
    "bootrom_unmapped",
    "watch",
)


def load_trace_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        raise FileNotFoundError(f"Trace file not found: {path}")
    records: list[dict[str, Any]] = []
    for line_no, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = raw_line.strip()
        if not stripped:
            continue
        try:
            obj = json.loads(stripped)
        except json.JSONDecodeError as exc:
            raise ValueError(f"Invalid JSON at {path}:{line_no}: {exc}") from exc
        if not isinstance(obj, dict) or "frame" not in obj:
            raise ValueError(f"Invalid trace record at {path}:{line_no}: missing 'frame'")
        records.append(obj)
    return records


def compare_trace_records(
    expected_records: list[dict[str, Any]],
    actual_records: list[dict[str, Any]],
    fields: tuple[str, ...] = DEFAULT_TRACE_FIELDS,
    max_divergences: int = 20,
) -> dict[str, Any]:
    """Compare two lists of frame records and report exact frame/field divergences."""
    divergences: list[dict[str, Any]] = []
    min_len = min(len(expected_records), len(actual_records))

    for idx in range(min_len):
        exp = expected_records[idx]
        act = actual_records[idx]
        frame_exp = exp.get("frame", idx)
        frame_act = act.get("frame", idx)
        if frame_exp != frame_act:
            divergences.append({
                "frame": idx,
                "field": "frame",
                "expected": frame_exp,
                "actual": frame_act,
            })
            if len(divergences) >= max_divergences:
                break
            continue

        for field in fields:
            if field == "watch":
                exp_watch = exp.get("watch") or {}
                act_watch = act.get("watch") or {}
                all_keys = sorted(set(exp_watch.keys()) | set(act_watch.keys()))
                for wkey in all_keys:
                    if exp_watch.get(wkey) != act_watch.get(wkey):
                        divergences.append({
                            "frame": frame_exp,
                            "field": f"watch.{wkey}",
                            "expected": exp_watch.get(wkey),
                            "actual": act_watch.get(wkey),
                        })
                        if len(divergences) >= max_divergences:
                            break
                if len(divergences) >= max_divergences:
                    break
            else:
                if exp.get(field) != act.get(field):
                    divergences.append({
                        "frame": frame_exp,
                        "field": field,
                        "expected": exp.get(field),
                        "actual": act.get(field),
                    })
                    if len(divergences) >= max_divergences:
                        break
        if len(divergences) >= max_divergences:
            break

    frame_count_match = len(expected_records) == len(actual_records)
    if not frame_count_match and len(divergences) < max_divergences:
        divergences.append({
            "frame": min_len,
            "field": "frame_count",
            "expected": len(expected_records),
            "actual": len(actual_records),
        })

    return {
        "logic_match": len(divergences) == 0 and frame_count_match,
        "expected_frames": len(expected_records),
        "actual_frames": len(actual_records),
        "divergence_count": len(divergences),
        "first_divergence": divergences[0] if divergences else None,
        "divergences": divergences,
    }


def resolve_capture_paths(target: Path) -> tuple[Path | None, Path]:
    """Return (capture.json path or None, trace.jsonl path) for a directory or file."""
    if target.is_dir():
        cap_json = target / "capture.json"
        trace_jsonl = target / "trace.jsonl"
        return (cap_json if cap_json.is_file() else None, trace_jsonl)
    return (None, target)


def compare_captures(
    expected_path: Path,
    actual_path: Path,
    fields: tuple[str, ...] = DEFAULT_TRACE_FIELDS,
    max_divergences: int = 20,
) -> dict[str, Any]:
    exp_meta_path, exp_trace_path = resolve_capture_paths(expected_path)
    act_meta_path, act_trace_path = resolve_capture_paths(actual_path)

    exp_records = load_trace_jsonl(exp_trace_path)
    act_records = load_trace_jsonl(act_trace_path)
    trace_report = compare_trace_records(
        exp_records, act_records, fields=fields, max_divergences=max_divergences
    )

    screen_match: bool | None = None
    rom_match: bool | None = None
    inputs_match: bool | None = None
    if exp_meta_path is not None and act_meta_path is not None:
        exp_meta = json.loads(exp_meta_path.read_text(encoding="utf-8"))
        act_meta = json.loads(act_meta_path.read_text(encoding="utf-8"))
        screen_match = exp_meta.get("screen_sha256") == act_meta.get("screen_sha256")
        rom_match = exp_meta.get("rom_sha256") == act_meta.get("rom_sha256")
        inputs_match = exp_meta.get("inputs_sha256") == act_meta.get("inputs_sha256")

    trace_report["screen_match"] = screen_match
    trace_report["rom_match"] = rom_match
    trace_report["inputs_match"] = inputs_match
    return trace_report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expected", type=Path, help="Expected capture directory or trace.jsonl")
    parser.add_argument("actual", type=Path, help="Actual capture directory or trace.jsonl")
    parser.add_argument(
        "--fields",
        nargs="+",
        default=list(DEFAULT_TRACE_FIELDS),
        help="Trace fields to compare (use 'watch' to compare all watchpoints)",
    )
    parser.add_argument("--max-divergences", type=int, default=20)
    args = parser.parse_args()

    try:
        report = compare_captures(
            args.expected,
            args.actual,
            fields=tuple(args.fields),
            max_divergences=args.max_divergences,
        )
    except (OSError, ValueError) as error:
        parser.exit(2, f"Error: {error}\n")

    print(json.dumps(report, indent=2))
    if not report["logic_match"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
