# Shared agent instructions

Read README.md, docs/STATUS.md, docs/COLLABORATION.md, docs/HANDOFF_ANTIGRAVITY.md,
docs/RESEARCH_PROTOCOL.md and docs/REUSABILITY_ARCHITECTURE.md.

Official Project Goal:
Understand and reproduce how Game Boy games work internally, using Alleyway as the
first concrete case study, to accelerate future native ports through reusable tools
and components. It is NOT enough to complete an isolated game; generic infrastructure
must be decoupled from title-specific logic.

Technical Boundaries:
- Distinguish emulation, native reimplementation, and static recompilation.
- This project is a verifiable native reimplementation reading user assets at runtime,
  not an emulator (no full CPU/PPU emulation pipeline) and not an automated recompiler.
- Current executable implements a verified, playable Stage 1 engine with bit-exact
  logic trace verification against PyBoy for 5651+ ticks. Fases 2..32, bonus mechanics,
  audio and intro sequences remain in progress. Never call it purely diagnostic now,
  nor claim that all 32 stages are complete.
- Never invent mechanics, RAM addresses, level data or claim unverified results.
- Mark findings observed / hypothesis / verified with source code addresses and experiments.
- Keep commercial ROMs, extracted data, audio artifacts and decompiler output outside
  tracked files, in ignored local/ or the shared Local-Privado folder.
- Do not download commercial games or publish original resources.
- Public tests MUST use synthetic fixtures. Keep third-party tools in ignored .tools/.
- Concurrent agents MUST use separate worktrees AND branches.
- Before each milestone: meaningful tests, inspect diff, update STATUS and MODLOG,
  commit with truthful author attribution, then push antigravity/development.
- Never force-push, overwrite another agent's changes, or merge to main without reviewed integration.
