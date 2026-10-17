# Alleyway: registro de investigacion y desarrollo
Fecha: 2026-10-17
Estado: primera meta alcanzada — Fase 1 nativa en C implementada, jugable en Windows y verificada bit a bit frente a PyBoy sobre el volcado local del usuario (`../Local-Privado/roms/Alleyway (USA).gb`, fuera de Git). Fases 2..32, fases de bonificacion, tiles 2bpp y audio siguen pendientes.

## Copia local del usuario (fuera de Git)
- Ruta: `../Local-Privado/roms/Alleyway (USA).gb`
- Metadatos verificados con `tools/inspect_rom.py`:
  - `bytes = 32768`, `title = "ALLEY WAY"`, `cartridge_type = 0`, `version = 0`, `old_licensee_code = 1`.
  - `header_checksum_valid = true` (94), `global_checksum_valid = true` (53662).
  - `sha256 = ed8070e011713527bdc03e2b9cec9f9c4a7e3aaa00e57e8786a186b265da1bb2`.

## Hito 1 (2026-10-17): Linea temporal determinista, trazas y comparador (`df314f1`)
- Implementados `tools/synthetic_rom.py`, `tools/inspect_rom.py`, `tools/capture_reference.py`, `tools/compare_traces.py` y 8 pruebas unitarias en `tests/`.

## Hito 2 (2026-10-17): Escaneo de RAM y verificacion de fisica y Fase 1 (`8e1e9a8`)
- Implementados `tools/scan_ram.py`, `tools/alleyway_watchpoints.json`, `tests/test_scan_ram.py` y documentado el mapa de memoria e ingenieria inversa en `docs/RESEARCH_PROTOCOL.md`.

## Hito 3 (2026-10-17): Motor nativo en C de la Fase 1 y paridad bit a bit
### Descubrimiento clave sobre el momento de muestreo (`0x051F..0x0525`)
- **Observado**: Al muestrear la memoria justo al retorno de `gb.tick(1)` (`LY = 0`), existia un desfase aparente de 1 fotograma cuando se activaba el efecto de sonido de rebote en pared (`f=526`, `PC = 0x6F3F`).
- **Hipotesis**: La rutina de sonido llamada al final de la interrupcion VBlank (`0x0210: CALL 0x7FF0`) consume mas ciclos cuando hay un efecto de sonido activo, haciendo que el final de la ISR cruce la frontera de `LY = 0`.
- **Experimento y Verificacion**: Registrando hooks en PyBoy al inicio (`0x051F`) y al final (`0x0525`, justo antes de `CALL 0x0221`) del bucle principal de logica (`--logic-hooks 0x051F:0x0525` en `tools/capture_reference.py`), se capturo cada paso de simulacion de forma atomica. Al comparar **5,651 ticks consecutivos de logica** de la Fase 1 (con 59 bloques destruidos, cambios de velocidad `3 -> 5 -> 7` y `1,150` puntos), el numero de discrepancias entre PyBoy y nuestra implementacion fue exactamente **0 (`mismatches = 0`)**.

### Implementacion realizada
- `tools/capture_reference.py`: soporte para `--logic-hooks START_PC:END_PC` que genera `logic_trace.jsonl` con las entradas latcheadas (`in_joyp_raw`, `in_joyp_edge`) y los watchpoints al final de cada tick de logica.
- `src/main.c`:
  - Motor nativo en C11 (`AlleywayStage1`) que lee tablas y layout de la Fase 1 desde la ROM local del usuario en tiempo de ejecucion.
  - Modo `--smoke-test`: verifica el framebuffer de diagnostico y un paso de fisica sobre una tabla sintetica en memoria.
  - Modo `--verify-trace <rom> <logic_trace.jsonl>`: verifica que el ejecutable nativo en C reproduce exactamente cada tick de un `logic_trace.jsonl` en `state`, `prng`, `paddle_x`, `paddle_w`, `ball_x`, `ball_x_sub`, `ball_y`, `ball_y_sub`, `ball_vx`, `ball_vy`, `speed_idx`, `bricks_left`, `score_tens` y `lives`.
  - Modo interactivo Win32/GDI a 60 Hz (`.\build\alleyway-lab.exe`): permite jugar la Fase 1 nativa en Windows cuando la ROM local esta disponible (o muestra el patron de diagnostico si no hay ROM).
- `tests/test_stage1_native.py`: pruebas automatizadas del ejecutable C (`--smoke-test`, `--verify-trace` con ROM sintetica + fallo deliberado, y paridad real con `../Local-Privado/roms/Alleyway (USA).gb` cuando esta presente).
- `.github/workflows/ci.yml`: integracion continua en Windows usando exclusivamente fixtures sinteticos.

## Hito 4 (2026-10-17): Componentes reutilizables — decodificador 2bpp, sintesis APU y backend waveOut
### 1. Decodificador generico de tiles 2bpp (`src/gb_tile.h`)
- Implementada la conversion del formato planar 2bpp a matrices de color y a renderizado directo sobre framebuffer.
- Soporta paletas DMG (BGP, OBP0, OBP1), volteo en X/Y (DMG OAM flags bit 5 y 6), transparencia de color 0 para sprites y clipping de coordenadas fuera de pantalla.
- Conectado a `src/main.c`: la paleta (Tile 0 en `0x4B75`, Tile 1 en `0x4B85`), la bola (Tile 5 en `0x4BC5`), el muro superior (Tile 179 en `0x56A5`) y los bloques 168..176 (en `0x55F5`, empaquetando 2 filas de 4 px por tile de 8x8) se renderizan con los graficos del cartucho local.
- Pruebas en C real compiladas con Zig: `tests/test_c_modules.c` verifica indices de planos, paletas, volteo X/Y, transparencia y clipping negativo sobre el codigo C real.

### 2. Sintesis APU Canal 1 y backend Win32 waveOut (`src/gb_apu.h`)
- Sintetizador refactorizado con avance en ciclos CPU (4.194304 MHz) y acumulador fraccionario de fase en punto fijo 32.32 (eliminando acumulacion de error fraccionario en frecuencias).
- Incorpora Frame Sequencer a 512 Hz (longitud a 256 Hz, barrido a 128 Hz, envolvente a 64 Hz), contador de longitud de 64 pasos (`NR11`/`NR14`), verificacion de DAC apagado cuando bits 7..3 de `NR12` son 0, desbordamiento de barrido por hardware (`NR10`) y actualizacion de envolvente aun con volumen en 0.
- Pruebas en C real (`tests/test_c_modules.c` y runner `tests/test_c_modules.py`): verifican silencio en DAC apagado, expiracion y desactivacion de longitud, deteccion de desbordamiento de barrido, polaridad de pulso PCM y deteccion de fallo provocado (`--deliberate-fail`).
- Backend Win32 `waveOut` implementado en `src/main.c`: 4 buffers rotativos a 44.1 kHz (~735 muestras por fotograma a 60 Hz). Despacha las tablas reales de la ROM (`0x701A`, `0x6FDE`, etc.) ante los eventos `sfx_trigger` (saque/paleta, techo, pared, bloques). Limpieza segura con `waveOutReset`/`waveOutUnprepareHeader`/`waveOutClose` en `WM_DESTROY`.
- Modo determinista y headless: si `waveOutOpen` falla o se ejecuta sin dispositivo de audio, avanza el estado APU de forma identica sin fallar ni bloquearse.

## Siguiente paso recomendado para Codex / futuras sesiones
1. Implementar el desplazamiento horizontal de filas (`0x0B21..0x0B66`, patrones de la tabla `0x4075`) y el descenso vertical del mural (`0x0BC7..0x0C31`) para soportar las Fases 2 y 3.
2. Implementar la regla de bonificacion (`0xFFAA == 1`, penetracion de bola en `0x0E76`) y la progresion automatica de fases (`0xCA45 = 0..31`).
3. Avanzar en la sintesis de los canales adicionales APU (Canal 2 pulso, Canal 3 onda programable, Canal 4 ruido).


