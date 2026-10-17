# Alleyway PC research

Laboratorio para investigar Alleyway (Game Boy, 1989) y construir una reimplementacion
nativa verificable en Windows.

Estado actual: **Fases 1, 2, 3 y Fase 4 Bonus nativas verificadas bit a bit frente a PyBoy en logica y secuencias de transicion de estados (`--verify-trace` y `--run-scenario`)**,
con scheduler monotonico Win32 (`QueryPerformanceCounter` a ~59.7275 Hz), audio fraccionario sin deriva (`738 + 23157/65536` muestras/frame a 44,100 Hz) y decodificacion de tiles 2bpp planar
cargando tablas, mapas y efectos de sonido en tiempo de ejecucion desde el volcado local del usuario,
sin recursos comerciales en el repositorio.
- **Probado diferencialmente contra el cartucho original (`tests/test_scenario_diff.py`)**: final normal (`8 -> 4 -> 5`), caida/expiracion en bonus sin perder vidas (`7 -> 4 -> 5`, sin estado 8 intermedio), bonus perfecto con conteo progresivo (+500 decenas exactamente una vez en 50 pasos de 10 decenas), perdida de vida normal (`7 -> 11 -> 5`, con divergencia conocida de +1 paso de PRNG documentada como `expectedFailure`) y Game Over (`7 -> 11 -> 1 -> 2`).
- **Alcance e hipotesis/limites**: los descriptores y mapas de las 32 fases en ROM estan inspeccionados estructuralmente, pero las fases 5..32 aun no tienen trazas diferenciales de ejecucion completa; la presentacion visual del telon y banners es una adaptacion grafica sobre el framebuffer nativo; canales APU 2..4 y pantalla de titulo/final animados siguen en progreso.

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
.\build\alleyway-lab.exe --run-scenario "../Local-Privado/roms/Alleyway (USA).gb" <scenario.jsonl>
```

- `tools/inspect_rom.py`: valida cabecera, alineacion de bancos y checksums.
- `tools/capture_reference.py`: graba `screen.ppm`, `trace.jsonl` (por fotograma) y opcionalmente `logic_trace.jsonl` (por tick atomico de logica `0x051F..0x0525`).
- `tools/compare_traces.py`: compara dos capturas o archivos `trace.jsonl` separando `logic_match` de `screen_match`.
- `tools/scenario_diff.py`: ejecuta escenarios diferenciales en paralelo (PyBoy original + `alleyway-lab.exe --run-scenario` autonomo recibiendo solo entradas de joypad) y reporta el primer mismatch de estado/fase/vidas/puntos/temporizador.
- `tools/scan_ram.py`: escaneo diferencial (`diff_timelines`) y correlacion temporal (`find_correlated_addresses`) sobre `WRAM`/`HRAM`/`OAM`.
- `tools/synthetic_rom.py`: genera cartuchos sinteticos interactivos para pruebas sin ROM comercial.

## Colaboracion

Lee `AGENTS.md`, `docs/STATUS.md`, `docs/RESEARCH_PROTOCOL.md`, `docs/COLLABORATION.md`
y `docs/HANDOFF_ANTIGRAVITY.md`.
Fuentes: https://github.com/LIJI32/SameBoy, https://github.com/Baekalfen/PyBoy,
https://gbdev.io/pandocs/, https://www.nintendo.com/es-es/Juegos/Game-Boy/Alleyway-275451.html.
El nombre Alleyway pertenece a sus titulares. Proyecto experimental no oficial;
sin ROMs ni recursos comerciales incluidos. Asistencia de IA documentada.
