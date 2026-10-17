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

### Hito 4: Componentes genericos reutilizables — decodificador 2bpp, sintesis APU y backend waveOut
- **Decodificador generico de tiles 2bpp (`src/gb_tile.h`)**:
  - Decodifica el formato planar de 16 bytes a indices 0..3 o a pixeles ARGB/XRGB aplicando paletas DMG (`BGP`/`OBP0`/`OBP1`) y transformaciones OAM (flip horizontal y vertical, transparencia de color 0 y clipping).
  - Validado en C mediante el ejecutable de pruebas `tests/test_c_modules.c` (indices de planos, paletas estándar e invertidas, flips, transparencia y clipping negativo) ejecutado de forma automatizada por `tests/test_c_modules.py`.
  - Conectado a `src/main.c`: cuando la ROM local esta cargada, la paleta (Tile 0 izquierda, Tile 1 centro, Tile 0 volteado derecha), la bola (Tile 5 en `0x4BC5`), el muro superior (Tile 179 en `0x56A5`) y los bloques 168..176 (en `0x55F5`, empaquetando 2 filas de 4 px por tile de 8x8) se renderizan directamente con los graficos autenticos del cartucho.
- **Sintesis APU Canal 1 (`src/gb_apu.h`) y Backend Win32 waveOut**:
  - **Sintetizador**: implementado con avance en ciclos de reloj de CPU (4.194304 MHz) y acumulador de fase en punto fijo 32.32 para evitar acumulacion de error fraccionario. Incorpora Frame Sequencer de 512 Hz, contador de longitud de 64 pasos (`NR11` bits 0..5 y `NR14` bit 6), apagado de DAC cuando `NR12` bits 7..3 son 0, barrido por hardware (`NR10`) con deteccion de desbordamiento en trigger/paso, y decaimiento/incremento de envolvente de volumen (`NR12`).
  - **Pruebas en C real**: `tests/test_c_modules.c` valida apagado de DAC (silencio absoluto), expiracion de contador de longitud, desbordamiento de barrido por hardware y alternancia de pulsos PCM en muestras reales, incluyendo deteccion de fallo provocado (`--deliberate-fail`).
  - **Integracion de audio de extremo a extremo**: `src/main.c` conecta los eventos del juego (`sfx_trigger`) y las tablas de ROM (`0x701A`, `0x6FDE`, `0x6FBB`, `0x6FCA`) al sintetizador APU y a una cola de 4 buffers waveOut en Win32 (`AUDIO_SAMPLE_RATE = 44100`, ~735 muestras por fotograma a 60 Hz). Si no hay dispositivo de audio disponible o se ejecuta en entorno headless/CI, avanza la simulacion APU de forma puramente determinista sin fallar.
- **Distincion de capas del sistema**:
  - *Logica y fisica*: determinista, bit-exacta verificada contra PyBoy (5,651 ticks).
  - *Graficos*: decodificacion planar 2bpp autentica desde ROM sobre framebuffer Win32/GDI.
  - *Sintesis PCM*: calculada en tiempo real por `src/gb_apu.h` en base a registros DMG reales y tablas de efectos extraidas de la ROM.
  - *Salida a dispositivo*: Win32 waveOut con reciclaje de buffers (`WHDR_PREPARED`/`WHDR_DONE`) y cierre seguro en `WM_DESTROY`.

### Hito 5: Bloques moviles de Fase 2, fases de 40 filas, bonus y progresion de 32 fases
- **Desplazamiento horizontal de filas (`0x0B21..0x0B66`)**:
  - Implementado el motor de desplazamiento horizontal por fila: lectura del indice de patron desde el byte 0 del descriptor de fase (`desc0 & 0x3F`), punteros en tabla `0x4075` (8 patrones de 20 bytes), direccion (bit 7) y recarga de temporizador (`0xCA28`).
  - Proyeccion ciclica en el espacio de juego (112 px = 14 columnas de 8 px) tanto en la sonda de colision `check_brick_point` como en el renderizado con recorte en los bordes laterales (`X=8..119`).
- **Fases de bonificacion (`0xFFAA = 1`) y penetracion de bola**:
  - Bit 7 de descriptor de fase (`0x1BE1[0] & 0x80`) activa `bonus_flag`.
  - La bola destruye bloques sin rebotar (`check_brick_point` retorna 0 al penetrar y no invierte `ball_vx`/`ball_vy` en `step_ball_and_collisions` segun `0x0E76`).
- **Progresion de fases (0..31) y umbral de vidas extra (1-UP)**:
  - Al destruir todos los bloques (`bricks_left == 0`), el juego entra en estado 8 (Clear) y avanza ciclicamente a la siguiente fase (`(stage + 1) & 0x1F`) cargando su respectivo descriptor y matriz desde la ROM local.
  - Verificacion de 1-UP implementada contra la tabla de umbrales en `0x1B5D` (`10,000` a `90,000` puntos) incrementando vidas hasta 99.

## Lo que falta para el juego completo (siguiente paso para continuar)
1. **Descenso vertical del mural (`0x0BC7..0x0C31`)**: desplazamiento paso a paso de filas de celdas hacia abajo en fases de 40 filas cuando expira el temporizador de descenso.
2. **Temporizador descendente de fases de bonificacion (`0x198C..0x1A50`)**: cuenta atras de segundos en HUD durante las fases de bonus y finalizacion por tiempo.
3. **Canales adicionales APU (Canal 2 pulso, Canal 3 onda programable, Canal 4 ruido)**.



