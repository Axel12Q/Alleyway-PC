# Codex + Antigravity: current working agreement

Canonical local workspace on this PC:
- C:/Users/maric/Documents/Alleyway-PC-Trabajo/Codex -- main, parked integration checkout.
- C:/Users/maric/Documents/Alleyway-PC-Trabajo/Antigravity -- antigravity/development, active worktree.
- C:/Users/maric/Documents/Alleyway-PC-Trabajo/Local-Privado -- user's ROM and private evidence; outside repository.

Remote: https://github.com/Axel12Q/Alleyway-PC.git

Antigravity owns implementation temporarily, including src/, tools/, tests/ and docs/.
Codex must read STATUS and fetch the development branch before resuming work.
When Codex returns, give it a separate branch/worktree for any concurrent task.
Coordinate ownership before touching overlapping files. A branch alone does not
isolate files. Do not open two agents on the same checkout.

Antigravity: commit each coherent tested milestone and push:
`git push origin antigravity/development`
Do not force-push. No automatic merge to main. Record exact tests, failures,
ROM dependencies and next step in docs/STATUS.md and MODLOG.md.
The user authorized project code uploads to this repository. Never upload ROMs,
extracted assets, logs with secrets, tools, environments or private captures.
No background polling service or scheduled automation is required: push after
milestones during the running agent task.

## Integration later

From Codex's clean main checkout: fetch origin; review origin/antigravity/development,
run checks, then integrate the reviewed changes. Preserve history and resolve
conflicts deliberately. Do not reset another worktree.

## Environments

Each worktree has its own .venv/, .tools/, build/ and local/ (all ignored).
The prepared Antigravity checkout already has dependencies and SameBoy.
On another machine run tools/setup.ps1 with Python 3.12 and uv available.
