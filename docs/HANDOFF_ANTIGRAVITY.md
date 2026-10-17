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
- Full 32-stage progression and Game Over state machine (`0x054C`), verified via **true differential execution** (`tools/scenario_diff.py` and `tests/test_scenario_diff.py`) comparing PyBoy against `build/alleyway-lab.exe --run-scenario` frame-by-frame:
  - Normal clear (`normal_clear`): `state 6` -> `state 8` (144 frames) -> `state 4` (67 frames, `stage++`, grid reloaded 4 frames into state 4 at remaining timer 63) -> `state 5` (serve).
  - Bonus ball drop (`bonus_drop`) and bonus timer expiration (`bonus_expire`): `state 6` -> `state 7` (228 frames) -> **directly to `state 4` (67 frames, `stage++`, lives preserved, never enters `state 8`)** -> `state 5`.
  - Perfect bonus clear (`bonus_perfect`): `state 6` -> `state 8` (498 frames, awarding `+500` tens = `+5,000` pts in 50 progressive steps of `+10` tens per frame during the final 50 frames) -> `state 4` (67 frames) -> `state 5`.
  - Normal life loss (`life_lost`): `state 6` -> `state 7` (100 frames) -> `state 11` (`0x0B`, 1 transient frame where `lives` decrements) -> `state 5`.
  - Last life lost (`game_over`): `state 6` -> `state 7` (100 frames) -> `state 11` (`0x0B`, 377 frames) -> `state 1` (1 frame) -> `state 2` (title wait).
  - Stage 32 clear: `state 8` -> `state 9` (Ending sequence) -> Stage 1 loop.
- Shared state-entry helper `stage1_enter_state` in `src/main.c` initializing `state_timer` once per transition with zero test backdoors (`test_mutation_without_timer_initialisation_fails_differential` proves removing timer initialization fails at tick 105).
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
- Test suite: **38 automated tests passing (including 1 documented `expectedFailure`)**.

## Evidence vs Limits

- **Verified by C vs PyBoy differential execution**:
  - Stages 1..4 active play traces (`--verify-trace`: 5,651 ticks Stage 1, 739 ticks Stage 2, 2,138 ticks Stage 3, 140+ ticks Stage 4).
  - Intermission and progression scenarios (`--run-scenario` via `tests/test_scenario_diff.py`): `normal_clear` (360 ticks), `bonus_drop` (430 ticks), `bonus_expire` (410 ticks), `bonus_perfect` (620 ticks), `life_lost` (220 ticks), and `game_over` (580 ticks).
  - Independent clock timing (`--verify-autonomous-clock`), monotonic QPC scheduler and zero-drift fractional audio (`--verify-scheduler-timing`), and multi-stage synthetic rules (`--verify-multi-stage-rules`).
- **Verified by ROM / PyBoy inspection only (not C execution)**:
  - `test_original_only_progression_ram_facts_if_present` (checks original ROM behavior in PyBoy) and `test_rom_stage_descriptor_layout_if_present` (checks the 32 stage descriptors and brick counts in ROM bytes).
- **Known limits & open items**:
  - After a normal life loss (`7 -> 11 -> 5`), the native C PRNG (`0xFFA1`) is 1 LFSR step ahead of PyBoy (`tick 102: expected 81, native 146`), tracked as `@unittest.expectedFailure` in `test_life_lost_prng_matches_original`.
  - State 4 curtain visuals and State 8/9/11 text banners are Win32 presentation adaptations, not tilemap/OAM-accurate reimplementations of the intro/intermission screens.
  - Intro sequences (States 0..3 title screen and Mario entrance), ending sequence animation (State 9), and APU channels 2, 3, 4 remain in progress.

## Roadmap & Budget Guidelines

- Follow user-authorized model selection; avoid spawning subagents unless explicitly requested.
- Focus on small, bounded milestones backed by unit and differential tests:
  1. Complete APU Channels 2 (Square 2), 3 (Wave), and 4 (Noise) and software frequency sweeps (`0x6B76`/`0x6B8B`).
  2. Frame-by-frame animated intro/title sequence (States 0..3) and ending sequence (State 9).
- Update `docs/STATUS.md` and `docs/HANDOFF_ANTIGRAVITY.md` after each verified milestone.
