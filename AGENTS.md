# Shared agent instructions

Read README.md, docs/STATUS.md, docs/COLLABORATION.md and docs/HANDOFF_ANTIGRAVITY.md.
Goal: a native, verifiable PC implementation of Alleyway from the user's own dump.
Current executable is a diagnostic, NOT a playable port.
Never invent mechanics, RAM addresses, level data or claim unrun verification.
Mark findings observed / hypothesis / verified and include source or experiment.
Keep commercial ROMs, extracted data, save states and decompiler output outside
tracked files, in ignored local/ or the shared Local-Privado folder.
Do not download commercial games or publish original resources.
Use synthetic fixtures in public tests. Keep third-party tools in ignored .tools/.
Concurrent agents MUST use separate worktrees AND branches.
Antigravity is currently the primary implementer on antigravity/development and
may modify runtime, tools, tests and docs. Codex's main worktree is parked.
Before each milestone: meaningful tests, inspect diff, update STATUS and journal,
commit with truthful author attribution, then push the development branch.
Never force-push, overwrite another agent's changes, publish ROMs, merge to main
without a reviewed integration, or alter project/global security settings.
If the ROM is missing, finish useful tool/protocol work and explicitly report
that actual reverse engineering and gameplay cannot be validated yet.
