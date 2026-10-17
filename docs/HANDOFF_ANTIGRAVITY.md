# Active handoff to Antigravity

The user requested that you take over development while Codex credits recover.
Work locally in the Antigravity worktree on antigravity/development, test changes,
and push coherent milestones to the specified GitHub repository. Continue doing
useful independent work with focused, budget-conscious steps.

## Overarching Project Objective

Understand and reproduce how Game Boy games work internally, using Alleyway as the
first concrete case study, to build reusable components (ROM/header parsing, deterministic
input/trace timelines, RAM differential scanners, 2bpp tile decoders, APU audio) to
accelerate future Game Boy native ports.

## Architectural Boundaries

1. Native Reimplementation (Chosen route): Native C logic faithfully executing the
   measured formulas, state machines, and fixed-point math, reading original assets
   from local ROM at runtime.
2. Emulation: Complete hardware virtualization (CPU, PPU, APU, timers). Used as reference
   oracle (PyBoy, SameBoy), not embedded in the port.
3. Static Recompilation: Automated binary translation. Not assumed or promised.

## What exists and was verified

- Stage 1, 2, 3, and Stage 4 Bonus Native C playable engine in `src/main.c` (Win32/GDI at 60 Hz).
- Bit-exact trace verification (`--verify-trace`) comparing native C simulation
  against PyBoy logic ticks (`0x051F..0x0525`):
  - Stage 1: 5,651 consecutive ticks (0 mismatches).
  - Stage 2: 739 consecutive ticks of horizontal scrolling (0 mismatches).
  - Stage 3: 2,138 consecutive ticks of vertical brick drops on paddle bounce (0 mismatches).
  - Stage 4: 140+ ticks of continuous brick penetration, authentic countdown cadence, and zero-expiration without life loss (0 mismatches).
- Independent autonomous clock verification (`--verify-autonomous-clock`) without per-tick oracle resets across Serve, Active play, Pause, and Expiration.
- Full 32-stage progression and Game Over state machine (`0x054C`), verified via **true differential execution** (`tools/scenario_diff.py` and `tests/test_scenario_diff.py`) sampling atomically at VBlank ISR entry (`0x01EF`, `LY = 144`) and comparing PyBoy against `build/alleyway-lab.exe --run-scenario` frame-by-frame (including `prng` on every frame of every state):
  - Normal clear (`normal_clear`, 420f): `[(6, 103), (8, 145), (4, 66), (5, 106)]` (`stage: 0 -> 1`).
  - Bonus ball drop (`bonus_drop`, 700f) and bonus timer expiration (`bonus_expire`, 700f): `state 7` (229 frames) -> **directly to `state 4` (66 frames, `stage: 3 -> 4`, lives preserved, never enters `state 8`)** -> `state 5`.
  - Multi-round perfect bonus clears reading runtime ROM table `0x1B71` and accounting for the extra VBlank frame inside `0x0C71` when a 1-UP is awarded:
    - Round 0 (`bonus_perfect`, Stage 4, 800f): `[(6, 61), (8, 499), (4, 66), (5, 174)]` (`+500` tens in 50 steps).
    - Round 1 (`bonus_perfect_r1`, Stage 8, 800f): `[(6, 80), (8, 519), (4, 67), (5, 134)]` (`+700` tens in 70 steps).
    - Round 2 (`bonus_perfect_r2`, Stage 12, 800f): `[(6, 66), (8, 550), (4, 66), (5, 118)]` (`+1000` tens in 100 steps + 1-UP `lives: 4 -> 5`).
    - Round 3 (`bonus_perfect_r3`, Stage 16, 850f): `[(6, 67), (8, 600), (4, 66), (5, 117)]` (`+1500` tens in 150 steps + 1-UP `lives: 4 -> 5`).
  - Normal life loss, re-serve, paddle movement, and active play (`life_lost`, 420f, `compare_play=True`): `[(7, 101), (11, 1), (5, 15), (6, 303)]` with exact PRNG parity across `7 -> 11 -> 5` and 2-VBlank serve delay (`0x0794: CALL 0x47C7`), plus ball/paddle/brick verification (`bricks_left = 72`, `score_tens = 12`, `lives = 3`).
  - Game Over, Title `Start` restart, 221-frame first-stage curtain (`66 + 155` Mario spacecraft entrance `0x43DC`), second game serve, and Pause/Resume (`game_over`, 850f, `compare_play=True`): `[(7, 101), (11, 377), (1, 1), (2, 25), (4, 221), (5, 7), (6, 29), (12, 20), (6, 34), (7, 35)]`.
  - Stage 32 clear -> Ending (`State 9`, 1445 frames) -> next game loop (`stage32_ending`, 2250f): `[(6, 73), (8, 600), (9, 1445), (4, 66), (5, 66)]` (`stage: 31 -> 0`, preserving `score_tens = 1502` and `lives = 5`).
  - Advanced horizontal scrolling stage (`stage14_horizontal`, Stage 14, 350f, `compare_play=True`): `[(6, 350)]`, verifying `row_shift_6 > 0` and brick collisions (`bricks_left: 69 -> 61`, `score_tens = 13`).
  - Advanced vertical descending stage (`stage23_vertical`, Stage 23, 450f, `compare_play=True`): `[(6, 450)]`, verifying paddle-bounce drop (`row_offset: 20 -> 19`), `0x0BF2` bottom-row clearing, and brick collisions (`bricks_left = 104`, `score_tens = 45`).
- Shared state-entry helper `stage1_enter_state` in `src/main.c` initializing `state_timer` once per transition with zero test backdoors (`test_mutation_without_timer_initialisation_fails_differential` proves removing timer initialization fails at tick 103).
- Reusable 2bpp planar tile decoder (`src/gb_tile.h`) with flips, palettes, transparency, and clipping.
- Reusable APU Channel 1 synthesizer (`src/gb_apu.h`) with cycle-accurate fixed-point phase accumulator, length/sweep/envelope, and decoupled injectable backend (`src/gb_audio_backend.h`).
- Reusable monotonic real-time DMG scheduler and fractional audio generator (`src/gb_scheduler.h`) with fixed-step accumulation, 59.7275 Hz exact timing, and zero-drift 44,100 Hz fractional sample synthesis (`738 + 23157/65536`).
- Presentation adaptation for Game Over and Stage Clear banners using cartridge font tiles (`0x5375`), plus opening curtain effect in State 4 (explicitly documented as visual adaptations rather than original OAM/BG display lists).
- Deterministic input timeline, hardware register/watchpoint tracer (`tools/capture_reference.py`).
- Trace and metadata comparator (`tools/compare_traces.py`).
- Differential RAM scanner and time-series correlation finder (`tools/scan_ram.py`).
- Differential scenario runner (`tools/scenario_diff.py`) comparing original ROM in PyBoy vs native C executable (`--run-scenario`).
- Synthetic interactive ROM fixture generator (`tools/synthetic_rom.py`).
- Cartridge header, size, bank alignment, and checksum inspector (`tools/inspect_rom.py`).
- GitHub Actions CI workflow running on Windows with synthetic fixtures only.
- Local user dump verified at `../Local-Privado/roms/Alleyway (USA).gb`.
- Test suite: **40 automated tests passing (`40 OK, 0 expectedFailure`)**.

## Evidence vs Limits

- **Verified by C vs PyBoy differential execution**:
  - Stages 1..4 active play traces (`--verify-trace`: 5,651 ticks Stage 1, 739 ticks Stage 2, 2,138 ticks Stage 3, 140+ ticks Stage 4).
  - 12 full differential scenarios (`--run-scenario` via `tests/test_scenario_diff.py` across Stages 1, 4, 8, 12, 14, 16, 23, and 32): `normal_clear` (420f), `bonus_drop` (700f), `bonus_expire` (700f), `bonus_perfect` (800f), `bonus_perfect_r1` (800f), `bonus_perfect_r2` (800f), `bonus_perfect_r3` (850f), `life_lost` (420f), `game_over` (850f), `stage32_ending` (2250f), `stage14_horizontal` (350f), and `stage23_vertical` (450f) — all with `0 mismatches` including `prng`.
  - Independent clock timing (`--verify-autonomous-clock`), monotonic QPC scheduler and zero-drift fractional audio (`--verify-scheduler-timing`), and multi-stage synthetic rules (`--verify-multi-stage-rules`).
- **Verified by ROM / PyBoy inspection only (not C execution)**:
  - `test_original_only_progression_ram_facts_if_present` (checks original ROM behavior in PyBoy) and `test_rom_stage_descriptor_layout_if_present` (checks the 32 stage descriptors and brick counts in ROM bytes; stages outside 1, 2, 3, 4, 8, 12, 14, 16, 23, 32 are covered structurally by this descriptor test rather than individual full-play traces).
- **Known limits & open items**:
  - State 4 curtain visuals and State 8/9/11 text banners are Win32 presentation adaptations, not tilemap/OAM-accurate reimplementations of the intro/intermission screens.
  - Intro sequences (States 0..3 title screen and Mario entrance visuals), ending sequence animation visuals (State 9), and APU channels 2, 3, 4 remain in progress.

## Roadmap & Budget Guidelines

- Follow user-authorized model selection; avoid spawning subagents unless explicitly requested.
- Focus on small, bounded milestones backed by unit and differential tests:
  1. Complete APU Channels 2 (Square 2), 3 (Wave), and 4 (Noise) and software frequency sweeps (`0x6B76`/`0x6B8B`).
  2. Frame-by-frame animated intro/title sequence (States 0..3) and ending sequence visuals (State 9).
- Update `docs/STATUS.md` and `docs/HANDOFF_ANTIGRAVITY.md` after each verified milestone.

