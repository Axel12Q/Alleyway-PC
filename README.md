# Alleyway PC research

Laboratorio para investigar Alleyway (Game Boy, 1989) y construir una reimplementacion
nativa verificable en Windows.

Estado actual: **Fase 1 nativa implementada y verificada bit a bit frente a PyBoy**
(cargando tablas y mapa en tiempo de ejecucion desde el volcado local del usuario,
sin recursos comerciales en el repositorio). Fases 2..32, fases de bonificacion,
audio y animaciones de introduccion siguen pendientes.

## Empezar en Windows

Python 3.12 y uv deben estar disponibles. Desde esta carpeta:

```powershell
uv venv
uv pip sync requirements-dev.lock
.\.venv\Scripts\python.exe tools/build.py
.\build\alleyway-lab.exe --smoke-test
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
```

Para ejecutar la ventana nativa (autodecta la ROM local en `local/roms/` o
`../Local-Privado/roms/`, o muestra el patron de diagnostico si no hay ROM):

```powershell
.\build\alleyway-lab.exe
# O indicando explicitamente la ruta a tu volcado:
.\build\alleyway-lab.exe --rom "../Local-Privado/roms/Alleyway (USA).gb"
```

Controles en la ventana nativa (Fase 1):
- `Izquierda` / `Derecha` (o `A` / `D`): mover paleta (`3 px/frame`).
- `Espacio` / `Z`: boton `A` (sacar bola y acelerar paleta a `5 px/frame`).
- `Shift` / `X`: boton `B` (movimiento de precision a `1 px/frame`).
- `Enter`: `Start` (pausar / continuar / reiniciar al terminar).
- `R`: reiniciar Fase 1.
- `Esc`: cerrar ventana.

## Herramientas de inspeccion, captura, escaneo y verificacion

Coloca tu propio volcado en `local/roms/` o `../Local-Privado/roms/` (ignorados por Git):

```powershell
.\.venv\Scripts\python.exe tools/inspect_rom.py "../Local-Privado/roms/Alleyway (USA).gb"
.\.venv\Scripts\python.exe tools/capture_reference.py "../Local-Privado/roms/Alleyway (USA).gb" --frames 700 --watch-file tools/alleyway_watchpoints.json --logic-hooks 0x051F:0x0525
.\build\alleyway-lab.exe --verify-trace "../Local-Privado/roms/Alleyway (USA).gb" local/captures/<hash>/<run>/logic_trace.jsonl
```

- `tools/inspect_rom.py`: valida cabecera, alineacion de bancos y checksums.
- `tools/capture_reference.py`: graba `screen.ppm`, `trace.jsonl` (por fotograma) y opcionalmente `logic_trace.jsonl` (por tick atomico de logica `0x051F..0x0525`).
- `tools/compare_traces.py`: compara dos capturas o archivos `trace.jsonl` separando `logic_match` de `screen_match`.
- `tools/scan_ram.py`: escaneo diferencial (`diff_timelines`) y correlacion temporal (`find_correlated_addresses`) sobre `WRAM`/`HRAM`/`OAM`.
- `tools/synthetic_rom.py`: genera cartuchos sinteticos interactivos para pruebas sin ROM comercial.

## Colaboracion

Lee `AGENTS.md`, `docs/STATUS.md`, `docs/RESEARCH_PROTOCOL.md`, `docs/COLLABORATION.md`
y `docs/HANDOFF_ANTIGRAVITY.md`.
Fuentes: https://github.com/LIJI32/SameBoy, https://github.com/Baekalfen/PyBoy,
https://gbdev.io/pandocs/, https://www.nintendo.com/es-es/Juegos/Game-Boy/Alleyway-275451.html.
El nombre Alleyway pertenece a sus titulares. Proyecto experimental no oficial;
sin ROMs ni recursos comerciales incluidos. Asistencia de IA documentada.
