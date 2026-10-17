# Estado

2026-10-17: desarrollo activo en `antigravity/development` con volcado del usuario verificado en `../Local-Privado/roms/Alleyway (USA).gb` (fuera de Git).

## Hitos completados y subidos a `origin/antigravity/development`

### Hito 1: Infraestructura determinista de entradas, trazas y comparador (`df314f1`)
- `tools/synthetic_rom.py`, `tools/inspect_rom.py`, `tools/capture_reference.py` y `tools/compare_traces.py` con pruebas automatizadas sobre fixtures sinteticos.

### Hito 2: Escaneo diferencial de RAM e ingenieria inversa verificada de la Fase 1 (`8e1e9a8`)
- `tools/scan_ram.py`, `tools/alleyway_watchpoints.json`, `tests/test_scan_ram.py` y documentacion completa en `docs/RESEARCH_PROTOCOL.md`.
- Descubierto y verificado un hallazgo critico de sincronizacion: la rutina de sonido en VBlank (`CALL 0x7FF0`) desplaza el final de la ISR entre `LY = 153` (sin efecto de sonido) y `LY = 1` (con efecto de sonido activo), por lo que el muestreo al final de `gb.tick(1)` (`LY = 0`) cae a mitad del bucle principal. Usando hooks de PyBoy en `0x051F` (inicio del tick de logica) y `0x0525` (fin del tick tras `0x052B` y `0x0505`), cada tick de simulacion se mide de forma atomica.

### Hito 3: Implementacion nativa en C de la Fase 1 y verificacion bit a bit frente a PyBoy
- **Motor nativo C de la Fase 1 (`src/main.c`)**:
  - Carga en tiempo de ejecucion las tablas de velocidades (`0x11EE`), angulos aleatorios (`0x100B`), zonas de paleta (`0x1B41`/`0x1B51`), tipos de bloque (`0x1B87`) y el mapa de la Fase 1 (`0x1BE1 -> 0x1C44`) desde la ROM local del usuario (sin incluir bytes comerciales en el codigo fuente ni en Git).
  - Implementa la aritmetica exacta en punto fijo 8.8 big-endian (`ball_x`, `ball_y`, `ball_vx`, `ball_vy`), las 3 velocidades de paleta (`1`, `3` y `5` px/tick), el saque (`0x101B`), el rebote por zonas de paleta (`0x0CB1..0x0CE0`, `0x1113`), el encogimiento de paleta en el techo (`0x0CE1..0x0D14`), los rebotes laterales (`0x0D15..0x0D26`) y el doble pase de 4 sondas contra la matriz de bloques de `8x4 px` (`0x0D27..0x0E8C`).
  - Modo interactivo Win32/GDI a 60 Hz (`.\build\alleyway-lab.exe`) con renderizado del campo de juego, matriz de 84 bloques de la Fase 1, paleta, bola y marcador numerico (`HIGH`, `SCORE`, `BRICKS`, `LIVES`, `STAGE`). Si no encuentra la ROM local, permanece en modo diagnostico.
  - Modo verificador de trazas (`.\build\alleyway-lab.exe --verify-trace <rom> <logic_trace.jsonl>`): compara tick a tick la ejecucion nativa en C contra `logic_trace.jsonl` generado por PyBoy.
- **Validacion ejecutada**:
  - Compilacion C11 con Zig 0.16.0 (`-Wall -Wextra -Werror -O2`) sin advertencias.
  - `.\build\alleyway-lab.exe --smoke-test`: aprobado.
  - `.\.venv\Scripts\python.exe -m unittest discover -s tests -v`: **12 pruebas aprobadas** (incluyendo prueba sintetica de `--verify-trace`, deteccion de divergencia deliberada y prueba de paridad real contra `../Local-Privado/roms/Alleyway (USA).gb`).
  - Prueba extendida de paridad sobre la Fase 1 real: **5,651 ticks consecutivos de logica comparados (`mismatches = 0`, 59 bloques rotos de 84 a 25, puntuacion `1150`, aceleracion `speed_idx: 3 -> 5 -> 7`)**.
  - Flujo de CI en GitHub Actions (`.github/workflows/ci.yml`) configurado sobre `windows-latest` usando exclusivamente datos sinteticos.

## Lo que falta para el juego completo (siguiente paso para continuar)
1. **Desplazamiento vertical y horizontal de bloques (`0x0B21..0x0C31`)**: necesario para las fases con bloques moviles (`0xFFAB = 1`, tabla `0x4075`) y fases de mas de 20 filas (`0xFFA9 > 0`, como la Fase 3).
2. **Fases de bonificacion (`0xFFAA = 1`, `0x198C..0x1A50`)**: donde la bola atraviesa bloques sin rebotar (`0x0E76`).
3. **Progresion completa de las 32 fases (`0xCA45 = 0..31`), umbral de vidas extra (`1-UP` en `0x0C71..0x0CA5`, tabla `0x1B5D`), decodificacion de tiles 2bpp de la ROM y audio APU**.
