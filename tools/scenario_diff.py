"""ORIGINAL (PyBoy) vs NATIVE (C) differential scenarios for Alleyway state/progression logic.

The original is driven with PRIVATE injections (documented per scenario below); the native
executable is started from a snapshot of the original taken at an atomic VBlank ISR boundary
(0x01EF, LY=144) and is then run AUTONOMOUSLY (`alleyway-lab.exe --run-scenario`): it receives
only the joypad inputs that the original latched, never expected states, clocks or timers.
The per-tick sequences of state/stage/lives/score/bonus/prng (plus ball/paddle/scroll fields
during active play) are then compared and the first mismatch is reported.

Requires a local ROM (kept outside Git). Nothing here embeds ROM data.
"""
import json
import subprocess
import tempfile
from pathlib import Path

HOOK_VBLANK = 0x01EF       # VBlank ISR entry (LY=144): 100% of frame CPU work & PRNG step done
HOOK_LOGIC_START = 0x051F  # latches joypad in play loop (0xFF8C/0xFF8D)

COMPARE_FIELDS = ("state", "stage", "lives", "score_tens", "bonus_flag")
PLAY_FIELDS = (
    "paddle_x", "paddle_w",
    "ball_x", "ball_x_sub", "ball_y", "ball_y_sub",
    "ball_vx", "ball_vy", "speed_idx", "bricks_left",
    "row_offset", "scroll_timer", "scroll_step", "row_shift_0", "row_shift_6",
)


def _boot_past_title(rom_path):
    from pyboy import PyBoy
    gb = PyBoy(str(rom_path), window="null", sound_emulated=False)
    for f in range(346):
        if f == 120:
            gb.button_press("start")
        elif f == 125:
            gb.button_release("start")
        gb.tick()
    return gb


def _fast_forward_to_stage(gb, stage_idx):
    """PRIVATE INJECTION: set stage = stage_idx-1, prior normal_stage_count (0xCA46) and
    bonus_stage_count (0xCA47) for stages 0..stage_idx-1, bricks_left = 0, state = 8 so the
    original itself plays state 8 -> 4 -> 5 into the target stage (no native involvement)."""
    m = gb.memory
    m[0xCA45] = stage_idx - 1
    prior_bonus = stage_idx // 4
    m[0xCA46] = stage_idx - prior_bonus
    m[0xCA47] = prior_bonus
    m[0xFFAA] = 0
    m[0xFFC8] = 0
    m[0xFFC9] = 0
    m[0xFFA4] = 8
    for _ in range(600):
        gb.tick()
        if m[0xCA45] == stage_idx and m[0xFFA4] == 5:
            return
    raise RuntimeError("original did not reach stage %d" % stage_idx)


def _serve(gb):
    for _ in range(3):
        gb.tick()
    gb.button_press("a")
    for _ in range(3):
        gb.tick()
    gb.button_release("a")
    for _ in range(5):
        gb.tick()


def _record(gb, latched):
    m = gb.memory
    rec = {
        "state": m[0xFFA4], "prng": m[0xFFA1], "vblank_count": m[0xFFA2],
        "paddle_x": m[0xFFC0], "paddle_w": m[0xFFC2],
        "ball_x": m[0xFFB6], "ball_x_sub": m[0xFFB7],
        "ball_y": m[0xFFB4], "ball_y_sub": m[0xFFB5],
        "ball_vx": (m[0xFFBA] << 8) | m[0xFFBB], "ball_vy": (m[0xFFB8] << 8) | m[0xFFB9],
        "speed_idx": m[0xFFBE], "bricks_left": (m[0xFFC8] << 8) | m[0xFFC9],
        "score_tens": m[0xFFCA] | (m[0xFFCB] << 8),
        "lives": m[0xCA44], "stage": m[0xCA45],
        "normal_stage_count": m[0xCA46], "bonus_stage_count": m[0xCA47],
        "scroll_flag": m[0xFFAB], "bonus_flag": m[0xFFAA],
        "bonus_timer": m[0xCA48], "row_offset": m[0xFFA9],
        "scroll_timer": m[0xFFC4], "scroll_step": m[0xFFC5],
        "in_joyp_raw": latched[0], "in_joyp_edge": latched[1],
        "logic_fired": 0,
    }
    for r in range(20):
        rec[f"row_pattern_{r}"] = m[0xCA14 + r]
        rec[f"row_shift_{r}"] = m[0xCA00 + r]
        rec[f"row_timer_{r}"] = m[0xCA28 + r]
    return rec


def _inject_noop(_mem, _snap):
    pass


def _inject_bricks_one(mem, snap):
    mem[0xFFC8] = 0
    mem[0xFFC9] = 1
    snap["bricks_left"] = 1


def _inject_ball_below_paddle(mem, snap):
    mem[0xFFB4] = 0xA0
    mem[0xFFB5] = 0
    snap["ball_y"] = 0xA0
    snap["ball_y_sub"] = 0


def _inject_lives(n):
    def f(mem, snap):
        mem[0xCA44] = n
        snap["lives"] = n
        _inject_ball_below_paddle(mem, snap)
    return f


def _inject_bonus_timer_one(mem, snap):
    mem[0xCA48] = 1
    snap["bonus_timer"] = 1


def _inject_scroll_timer_two(mem, snap):
    mem[0xFFC4] = 2
    snap["scroll_timer"] = 2


SCENARIOS = {
    # Normal stage 1 clear: last brick (bricks_left forced to 1) -> 8 -> 4 -> 5.
    "normal_clear": dict(
        stage=None, inject=_inject_bricks_one, frames=420,
        doc="inject bricks_left=1 at first logic tick after serve",
    ),
    # Bonus (stage 4) ball drop: ball_y forced below paddle -> 7 -> 4 -> 5, lives kept.
    "bonus_drop": dict(
        stage=3, inject=_inject_ball_below_paddle, frames=700,
        doc="fast-forward original to stage 4, serve, inject ball_y=0xA0",
    ),
    # Bonus timer expiry: bonus_timer forced to 1 -> 7 -> 4 -> 5, lives kept.
    "bonus_expire": dict(
        stage=3, inject=_inject_bonus_timer_one, frames=700,
        doc="fast-forward original to stage 4, serve, inject bonus_timer=1",
    ),
    # Bonus Round 0 (Stage 4) perfect clear: +500 tens in 50 steps of 10 (499 frames in State 8).
    "bonus_perfect": dict(
        stage=3, inject=_inject_bricks_one, frames=800,
        doc="fast-forward original to stage 4 (Round 0 bonus), serve, inject bricks_left=1",
    ),
    # Bonus Round 1 (Stage 8) perfect clear: +700 tens in 70 steps of 10 (519 frames in State 8).
    "bonus_perfect_r1": dict(
        stage=7, inject=_inject_bricks_one, frames=800,
        doc="fast-forward original to stage 8 (Round 1 bonus), serve, inject bricks_left=1",
    ),
    # Bonus Round 2 (Stage 12) perfect clear: +1000 tens in 100 steps + 1-UP (550 frames in State 8).
    "bonus_perfect_r2": dict(
        stage=11, inject=_inject_bricks_one, frames=800,
        doc="fast-forward original to stage 12 (Round 2 bonus), serve, inject bricks_left=1",
    ),
    # Bonus Round 3 (Stage 16) perfect clear: +1500 tens in 150 steps + 1-UP (600 frames in State 8).
    "bonus_perfect_r3": dict(
        stage=15, inject=_inject_bricks_one, frames=850,
        doc="fast-forward original to stage 16 (Round 3 bonus), serve, inject bricks_left=1",
    ),
    # Normal life lost with spare lives: 7 (101) -> 11 (1, lives--) -> 5 -> re-serve (A) & active play.
    "life_lost": dict(
        stage=None, inject=_inject_lives(4), frames=420, compare_play=True, track_ball=True,
        button_events=[
            (105, "press", "right"),
            (112, "release", "right"),
            (115, "press", "a"),
            (118, "release", "a"),
        ],
        doc="inject lives=4 and ball_y=0xA0, then move paddle, re-serve with A, track ball and break bricks",
    ),
    # Last life lost -> Game Over (377) -> Title (1 -> 2) -> Start restart -> 221f curtain -> serve (A) -> Pause/Resume.
    "game_over": dict(
        stage=None, inject=_inject_lives(0), frames=850, compare_play=True,
        button_events=[
            (500, "press", "start"),
            (503, "release", "start"),
            (730, "press", "a"),
            (733, "release", "a"),
            (740, "press", "right"),
            (750, "release", "right"),
            (760, "press", "start"),
            (763, "release", "start"),
            (780, "press", "start"),
            (783, "release", "start"),
            (790, "press", "left"),
            (810, "release", "left"),
        ],
        doc="inject lives=0 and ball_y=0xA0 -> Game Over -> Title -> Start restart -> serve -> Pause/Resume",
    ),
    # Stage 32 (stage=31) clear -> State 8 (600f) -> State 9 Ending (1445f, stage=0) -> State 4 (66f) -> State 5.
    "stage32_ending": dict(
        stage=31, inject=_inject_bricks_one, frames=2250,
        doc="fast-forward original to stage 32 (stage=31), serve, inject bricks_left=1 -> Ending State 9 -> Stage 1",
    ),
    # Advanced horizontal scrolling stage (Stage 14, stage=13, Round 4 Type B): active play, scroll & collisions.
    "stage14_horizontal": dict(
        stage=13, inject=_inject_noop, frames=350, compare_play=True, track_ball=True,
        doc="fast-forward original to stage 14 (stage=13), serve and track ball across scrolling rows",
    ),
    # Advanced vertical descending stage (Stage 23, stage=22, Round 6 Type C, 40 rows): paddle bounces & vertical drop.
    "stage23_vertical": dict(
        stage=22, inject=_inject_scroll_timer_two, frames=450, compare_play=True, track_ball=True,
        doc="fast-forward original to stage 23 (stage=22), inject scroll_timer=2, bounce and verify vertical drop",
    ),
}


def run_original(rom_path, name):
    """Run the ORIGINAL; return (snapshot, inputs, expected) sampled atomically at VBlank ISR (0x01EF)."""
    sc = SCENARIOS[name]
    gb = _boot_past_title(rom_path)
    try:
        if sc["stage"] is not None:
            _fast_forward_to_stage(gb, sc["stage"])
        _serve(gb)
        if gb.memory[0xFFA4] != 6:
            raise RuntimeError("original not in state 6 after serve")
        state = {"snap": None, "logic_fired": False, "vblank_rec": None}
        records = []

        def on_logic_start(_ctx):
            state["logic_fired"] = True

        def on_vblank(_ctx):
            # At 0x01EF (LY=144), the previous frame's CPU work & PRNG step (0x0522) are 100% done,
            # and 0xFF8C/0xFF8D/0xFFA2 still hold the exact joypad & vblank_count of that frame.
            latched = (int(gb.memory[0xFF8C]), int(gb.memory[0xFF8D]))
            if state["snap"] is None:
                if state["logic_fired"]:
                    snap = _record(gb, latched)
                    sc["inject"](gb.memory, snap)
                    state["snap"] = snap
            else:
                rec = _record(gb, latched)
                rec["logic_fired"] = 1 if state["logic_fired"] else 0
                state["vblank_rec"] = rec
            state["logic_fired"] = False

        gb.hook_register(0, HOOK_LOGIC_START, on_logic_start, None)
        gb.hook_register(0, HOOK_VBLANK, on_vblank, None)

        for _ in range(5):
            gb.tick()
            if state["snap"] is not None:
                break
        if state["snap"] is None:
            raise RuntimeError("vblank/logic hooks never fired for snapshot")

        events_by_tick = {}
        for t_idx, action, btn in sc.get("button_events", ()):
            events_by_tick.setdefault(t_idx, []).append((action, btn))
        track_ball = sc.get("track_ball", False)

        for t in range(sc["frames"]):
            for action, btn in events_by_tick.get(t, ()):
                if action == "press":
                    gb.button_press(btn)
                else:
                    gb.button_release(btn)
            if track_ball and gb.memory[0xFFA4] == 6:
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

            state["vblank_rec"] = None
            gb.tick()
            if state["vblank_rec"] is not None:
                records.append(state["vblank_rec"])
            else:
                # Fallback when LCD is briefly off during screen transitions (0x01EF not triggered)
                rec = _record(gb, (0xFF, 0xFF))
                rec["logic_fired"] = 0
                records.append(rec)
    finally:
        gb.stop()
    inputs = [(r["in_joyp_raw"], r["in_joyp_edge"]) for r in records]
    return state["snap"], inputs, records


def run_native(exe, rom_path, snap, inputs, workdir=None):
    """Run the native executable autonomously from the snapshot + inputs only."""
    with tempfile.TemporaryDirectory(dir=workdir) as tmp:
        path = Path(tmp) / "scenario.jsonl"
        with open(path, "w", encoding="utf-8") as fp:
            fp.write(json.dumps(snap) + "\n")
            for i, (raw, edge) in enumerate(inputs):
                fp.write(json.dumps({"tick": i, "in_joyp_raw": raw, "in_joyp_edge": edge}) + "\n")
        proc = subprocess.run([str(exe), "--run-scenario", str(rom_path), str(path)],
                              capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError("native --run-scenario failed rc=%d: %s" % (proc.returncode, proc.stderr))
    return [json.loads(l) for l in proc.stdout.splitlines() if l.strip()]


def _bonus_timer_comparable(exp):
    return exp["bonus_flag"] == 1 and exp["state"] in (5, 6, 7, 8)


def compare(expected, native, check_prng=True, compare_play=False):
    """Return None if identical, else a dict describing the FIRST mismatch."""
    if len(native) != len(expected):
        return {"tick": min(len(native), len(expected)), "field": "length",
                "expected": len(expected), "native": len(native)}
    for i, (e, n) in enumerate(zip(expected, native)):
        for f in COMPARE_FIELDS:
            if e[f] != n[f]:
                return {"tick": i, "field": f, "expected": e[f], "native": n[f],
                        "expected_rec": {k: e[k] for k in COMPARE_FIELDS}, "native_rec": n}
        if check_prng and e["prng"] != n["prng"]:
            return {"tick": i, "field": "prng", "expected": e["prng"], "native": n["prng"],
                    "state": e["state"]}
        if _bonus_timer_comparable(e) and e["bonus_timer"] != n["bonus_timer"]:
            return {"tick": i, "field": "bonus_timer", "expected": e["bonus_timer"],
                    "native": n["bonus_timer"]}
        if compare_play and e["state"] in (5, 6, 0x0C):
            for f in PLAY_FIELDS:
                if f in n and e[f] != n[f]:
                    return {"tick": i, "field": f, "expected": e[f], "native": n[f],
                            "state": e["state"]}
    return None


def state_runs(records):
    """Run-length encode the state sequence: [(state, frames), ...]."""
    runs = []
    for r in records:
        if runs and runs[-1][0] == r["state"]:
            runs[-1][1] += 1
        else:
            runs.append([r["state"], 1])
    return [(s, n) for s, n in runs]


def run_differential(exe, rom_path, name, workdir=None, check_prng=True):
    sc = SCENARIOS[name]
    snap, inputs, expected = run_original(rom_path, name)
    native = run_native(exe, rom_path, snap, inputs, workdir)
    return expected, native, compare(
        expected, native,
        check_prng=check_prng,
        compare_play=sc.get("compare_play", False),
    )


def run_stage_curtain_transition_diff(exe, rom_path, from_stage, vblank_offset=None, workdir=None):
    """Run ORIGINAL (PyBoy) and NATIVE (`--run-scenario`) for a single stage transition
    (`from_stage` -> `(from_stage + 1) & 0x1F`) triggered by breaking the last brick in State 6.
    Note: This tests the transition/curtain timing (`State 8 -> [State 9] -> State 4 -> State 5`),
    not a full active playthrough of all 32 stages.
    """
    gb = _boot_past_title(rom_path)
    try:
        if from_stage > 0:
            _fast_forward_to_stage(gb, from_stage)
        _serve(gb)
        if gb.memory[0xFFA4] != 6:
            raise RuntimeError(f"original not in state 6 at stage {from_stage}")

        state = {"snap": None, "logic_fired": False, "vblank_rec": None}
        records = []

        def on_logic_start(_ctx):
            state["logic_fired"] = True

        def on_vblank(_ctx):
            latched = (int(gb.memory[0xFF8C]), int(gb.memory[0xFF8D]))
            if state["snap"] is None:
                if state["logic_fired"]:
                    if vblank_offset is not None:
                        gb.memory[0xFFA2] = vblank_offset & 0xFF
                    snap = _record(gb, latched)
                    _inject_bricks_one(gb.memory, snap)
                    state["snap"] = snap
            else:
                rec = _record(gb, latched)
                rec["logic_fired"] = 1 if state["logic_fired"] else 0
                state["vblank_rec"] = rec
            state["logic_fired"] = False

        gb.hook_register(0, HOOK_LOGIC_START, on_logic_start, None)
        gb.hook_register(0, HOOK_VBLANK, on_vblank, None)

        for _ in range(5):
            gb.tick()
            if state["snap"] is not None:
                break
        if state["snap"] is None:
            raise RuntimeError(f"snapshot failed for stage {from_stage}")

        next_stage = (from_stage + 1) & 0x1F
        max_frames = 2250 if from_stage == 31 else 800
        for _ in range(max_frames):
            state["vblank_rec"] = None
            gb.tick()
            if state["vblank_rec"] is not None:
                records.append(state["vblank_rec"])
            else:
                rec = _record(gb, (0xFF, 0xFF))
                rec["logic_fired"] = 0
                records.append(rec)
            if records[-1]["state"] == 5 and records[-1]["stage"] == next_stage:
                break
    finally:
        gb.stop()

    inputs = [(r["in_joyp_raw"], r["in_joyp_edge"]) for r in records]
    native = run_native(exe, rom_path, state["snap"], inputs, workdir)
    mismatch = compare(records, native, check_prng=True, compare_play=False)
    exp_curtain = sum(1 for r in records if r["state"] == 4)
    nat_curtain = sum(1 for r in native if r["state"] == 4)
    return {
        "from_stage": from_stage,
        "next_stage": next_stage,
        "vblank_offset": vblank_offset,
        "expected_curtain_frames": exp_curtain,
        "native_curtain_frames": nat_curtain,
        "expected_runs": state_runs(records),
        "native_runs": state_runs(native),
        "mismatch": mismatch,
    }


def verify_all_32_curtain_transitions(exe, rom_path, workdir=None):
    """Validate `compute_stage_curtain_dur` across all 32 stages (`next_stage = 0..31` via
    `from_stage = 0..31`, including `from_stage = 31` Ending return to Stage 1), plus
    different initial VBlank alignments (`0, 7, 16, 31`) on normal and bonus curtain entries."""
    results = []
    for from_stage in range(32):
        res = run_stage_curtain_transition_diff(exe, rom_path, from_stage, vblank_offset=None, workdir=workdir)
        results.append(res)
    # Test explicit VBlank alignments (0, 7, 16, 31) entering a normal stage (from_stage=0 -> next_stage=1)
    # and entering a bonus stage (from_stage=2 -> next_stage=3)
    alignment_results = []
    for from_stage in (0, 2, 13, 22):
        for vb in (0, 7, 16, 31):
            res = run_stage_curtain_transition_diff(exe, rom_path, from_stage, vblank_offset=vb, workdir=workdir)
            alignment_results.append(res)
    return results, alignment_results


