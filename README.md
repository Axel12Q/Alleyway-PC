# Alleyway PC research

## Windows preview 0.1.0

Reimplementación nativa de investigación para Windows x64. Descarga el paquete
`Alleyway-PC-0.1.0-preview-windows-x64.zip` en [Releases](https://github.com/Axel12Q/Alleyway-PC/releases),
extráelo y abre **Alleyway-PC.exe**. Selecciona tu ROM local de Alleyway USA (32 KiB).
El paquete no necesita Python, Zig ni instalación y **no incluye ROM ni recursos del juego**.

Enter inicia/pausa, flechas mueven, Espacio saca, M silencia, R reinicia,
T vuelve al título y Esc sale. Cambiar de ventana suspende juego y audio.
La versión admite la ROM USA verificada, no cualquier juego de Game Boy.

Es una **preview**, con canales de audio 2..4/música y animaciones intermedias aún
incompletos. No es un conversor automático ni un port audiovisual completo.
Consulta [controles y límites](docs/RELEASE_README.txt) y [cambios](CHANGELOG.md).
Las pulsaciones rápidas y el tono de pared persistente están corregidos;
la secuencia de registros de los 12 efectos primarios se contrasta con PyBoy.

Nuestro código y herramientas son MIT; ROM, gráficos y música no están cubiertos
por esa licencia. Proyecto no oficial, sin afiliación con Nintendo.
Desarrollado con asistencia de OpenAI Codex y Google Antigravity (Gemini/Claude).

## Laboratorio y evidencia de investigación

Laboratorio para investigar Alleyway (Game Boy, 1989) y construir una reimplementacion
nativa verificable en Windows.

Estado actual: **Fases 1, 2, 3, 4, 8, 12, 14, 16, 23 y 32 verificadas bit a bit frente a PyBoy en logica y secuencias de transicion de estados (`--verify-trace` y `--run-scenario`, 44/44 pruebas OK sin `expectedFailure`)**,
con presentacion visual fiel desde ROM (100.00% paridad visual tono a tono frente a PyBoy en Pantalla de Titulo y Saque Fase 1 via `tests/test_visual_parity.py`),
scheduler monotonico Win32 (`QueryPerformanceCounter` a ~59.7275 Hz), sesion de ventana e2e (`--verify-window-session`, validando creacion HWND, procedimientos de ventana y despacho de mensajes, no teclas fisicas ni audio acustico),
audio fraccionario sin deriva (`738 + 23157/65536` muestras/frame a 44,100 Hz), decodificacion de tiles 2bpp planar
y resolucion autentica de la colision multi-bloque en Fase 17 (`0x09F9 -> 0x023D: LDH A, (0xFFA3)` wait for VBlank),
cargando tablas, mapas y efectos de sonido en tiempo de ejecucion desde el volcado local del usuario,
sin recursos comerciales en el repositorio.
- **Probado diferencialmente contra el cartucho original (`tests/test_scenario_diff.py`, 12 escenarios de partida + transiciones y telon en las 32 fases con `0 mismatches` incluyendo `prng`)**: final normal (`8 -> 4 -> 5`), caida/expiracion en bonus sin perder vidas (`7 -> 4 -> 5`, sin estado 8 intermedio), bonus perfecto multi-ronda desde `0x1B71` (Rondas 0..3: +500/+700/+1000/+1500 decenas a +10 decenas/frame y VBlank extra en 1-UP), perdida de vida normal con nuevo saque y juego activo (`7 -> 11 -> 5 -> 6`), Game Over con reinicio `Start` desde titulo (`State 2`), telon inicial de 221 frames, segunda partida y Pausa/Reanudacion (`State 12`), final tras Fase 32 (`8 -> 9 [1445f] -> 4 -> 5` a `stage = 0`), fases avanzadas horizontal (Fase 14) y vertical (Fase 23), y **las 32 transiciones de telon de fase (`0..31 -> 1..0`) con seguimiento activo de paleta y 16 alineaciones de VBlank (`0, 7, 16, 31`)**.
- **Alcance e hipotesis/limites**: las 32 transiciones de fase y telon estan comprobadas de forma integral exigiendo alcanzar el stage destino, `curtain > 0` y saque, pero las fases fuera de 1, 2, 3, 4, 8, 12, 14, 16, 23 y 32 aun no tienen trazas diferenciales de juego activo completo de principio a fin; canales APU 2..4 y animaciones intermedias de OAM (Mario entrando en la pala `0x43DC` y Mario saludando en el final `0x45BD`) siguen en progreso.

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
