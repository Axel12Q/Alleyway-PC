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
- Full 32-stage progression and Game Over state machine (`0x054C`):
  - Normal clear -> Stage clear -> Curtain intro -> Serve advancing stage.
  - Bonus ball drop / timer expiration -> Stage 8 preserving lives intact.
  - Perfect bonus clear -> 500 tens (+5,000 pts) from table `0x1B71`.
  - Last life lost -> State 0x0B (Game Over) -> reset.
  - Stage 32 clear -> State 9 (Ending celebration) -> Stage 1 loop.
- Reusable 2bpp planar tile decoder (`src/gb_tile.h`) with flips, palettes, transparency, and clipping.
- Reusable APU Channel 1 synthesizer (`src/gb_apu.h`) with cycle-accurate fixed-point phase accumulator, length/sweep/envelope, and decoupled injectable backend (`src/gb_audio_backend.h`).
- Reusable monotonic real-time DMG scheduler and fractional audio generator (`src/gb_scheduler.h`) with fixed-step accumulation, 59.7275 Hz exact timing, and zero-drift 44,100 Hz fractional sample synthesis (`738 + 23157/65536`).
- Authentic DMG state delay counters (`state_timer`) measured from cartridge (Curtains: 67 frames, Death: 101 frames, Bonus drop: 228 frames, Normal clear: 145 frames, Bonus tally: 499 frames, Game Over: 358 frames).
- Authentic presentation banners for Game Over and Stage Clear using cartridge font tiles (`0x5375`), plus opening curtain effect in State 4.
- Deterministic input timeline, hardware register/watchpoint tracer (`tools/capture_reference.py`).
- Trace and metadata comparator (`tools/compare_traces.py`).
- Differential RAM scanner and time-series correlation finder (`tools/scan_ram.py`).
- Synthetic interactive ROM fixture generator (`tools/synthetic_rom.py`).
- Cartridge header, size, bank alignment, and checksum inspector (`tools/inspect_rom.py`).
- GitHub Actions CI workflow running on Windows with synthetic fixtures only.
- Local user dump verified at `../Local-Privado/roms/Alleyway (USA).gb`.
- Test suite: **30 automated tests passing in ~2.4s**.

## Evidence vs Limits

- Verified: Stages 1..4 mechanics (static, horizontal scrolling, vertical paddle-bounce drop, bonus penetration and timer), independent clock timing, monotonic QPC scheduler, zero-drift fractional audio, 32-stage classification matrix and ROM descriptor validation, Game Over and Stage 32 wrap transitions.
- Limits: Intro sequences (States 0..3 title screen and Mario entrance) and ending sequence animation (State 9) are currently handled as direct state transitions rather than full frame-by-frame sprite animations. APU channels 2, 3, 4 remain in progress.

## Roadmap & Budget Guidelines

- Maintain Gemini 3.8 Flash (Medium) as active model; avoid heavy multi-agent spawns.
- Focus on small, bounded milestones backed by synthetic unit tests:
  1. Complete APU Channels 2 (Square 2), 3 (Wave), and 4 (Noise) for polyphonic music and jingles.
  2. Frame-by-frame animated intro sequence (curtains opening, Mario entry) and ending sequence.
- Update `docs/STATUS.md` and `HANDOFF_ANTIGRAVITY.md` after each verified milestone.
