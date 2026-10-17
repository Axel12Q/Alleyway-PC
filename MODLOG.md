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

## Hito 5 (2026-10-17): Bloques moviles de Fase 2, fases de 40 filas, bonus y progresion de 32 fases
### 1. Desplazamiento horizontal por fila (`0x0B21..0x0B66`)
- Decodificacion de descriptores en `0x1BE1`: el byte 0 contiene flags de bonificacion (`0x80`), desplazamiento (`0x40`) y patron de velocidad (`0x3F`).
- Punteros en tabla `0x4075` cargan los 8 patrones oficiales de 20 bytes en los buffers de fila (`row_pattern` y `row_timer`).
- Cada fotograma en juego activo (`state == 6`), `step_horizontal_scroll` decrementa los temporizadores de fila y avanza el desplazamiento circular `row_shift[r]` (0..111 px).
- La funcion de colision `check_brick_point` ajusta las sondas restando/sumando el desplazamiento modular de 112 px (14 columnas), y el renderizador `render_stage1` dibuja las celdas desplazadas con recorte exacto en los bordes izquierdo y derecho (`X=8..119`).

### 2. Mecanica de bonificacion (`0xFFAA = 1`) y penetracion de bola
- Descriptores con bit 7 en alto (fases 4, 8, 12, 16, 20, 24, 28, 32) configuran `bonus_flag = 1`.
- Al colisionar con un bloque en fase de bonus, la bola destruye el bloque sin rebotar (`0x0E76`), permitiendo atravesar formaciones enteras en un unico pase.

### 3. Progresion continua de fases (0..31) y vidas extra (1-UP)
- Al completar la destruccion de bloques destructibles (`bricks_left == 0`), el estado pasa a 8 (Clear).
- Al pulsar A o Start, el juego avanza ciclicamente a la siguiente fase (`(stage + 1) & 0x1F`), resetea la paleta y carga el nuevo mapa desde la ROM local.
- Se implemento la verificacion de vidas extra (1-UP) contra la tabla en `0x1B5D` (umbrales de 10k a 90k puntos).

## Hito 6 (2026-10-17): Correccion de dispatch de audio (NR10=0), seguridad de cola waveOut, reglas multi-fase y temporizadores
### 1. Despachador de efectos de sonido y tablas con NR10=0
- Corregida la condicion en `apu_dispatch_sfx` de `src/main.c`: la condicion `rom[addr] != 0` descartaba tablas validas donde `NR10 = 0x00` (sin barrido de frecuencia por hardware). Los efectos autenticos de bloques 1, 2 y 3 (`0x6FDE`, `0x6FBB`, `0x6FCA`) tienen `NR10 = 0` y caian erroneamente al sonido generico.
- Ahora el despachador comprueba limites de memoria de la ROM y asigna fielmente los 5 registros de Canal 1.
- Efectos de rebote en techo (`SFX 11`) y pared (`SFX 12`): documentados como aproximaciones estaticas parciales hasta incorporar la emulacion de las rutinas de software que modulan `NR13`/`NR14` en VBlank (`0x6B76`/`0x6B8B`).

### 2. Seguridad en la cola waveOut de Win32
- Eliminada la sobreescritura de flags y buffers en vuelo: `audio_backend_init` prepara los 4 encabezados una unica vez con `waveOutPrepareHeader`.
- En `audio_step_frame`, solo se copian muestras y se envia a la tarjeta si el encabezado tiene `WHDR_DONE` activo. Si el driver de sonido sigue reproduciendo un buffer, se omite el envio a hardware sin alterar flags ni retrasar el bucle de simulacion del juego.
- Cierre ordenado y defensivo en `audio_backend_shutdown` llamando a `waveOutReset`, `waveOutUnprepareHeader` (comprobando `WHDR_PREPARED`) y `waveOutClose`.

### 3. Ampliacion de pruebas unitarias en C real (`tests/test_c_modules.c`)
- Flip vertical (Y) y centinelas de memoria: comprobacion de que `gb_tile_blit` con volteo Y ubica correctamente los pixeles en la base del tile y no desborda buffers contiguos marcados con centinelas `0xDEADBEEF`.
- Despacho de audio con `NR10 = 0`: comprobacion con tabla sintetica de que se activa el canal, se respeta `NR10 = 0` y `SFX 1` genera una frecuencia y forma de onda PCM distinta a `SFX 2`.
- Simulacion de maquina de estados de la cola `waveOut`: valida que la proteccion contra sobreescritura en vuelo previene la corrupcion de audio y que los buffers se reutilizan en cuanto el controlador de audio los marca como completados.

### 4. Temporizadores y verificador de reglas multi-fase (`src/main.c`)
- Anadido modo `--verify-multi-stage-rules` a `alleyway-lab.exe` integrado en `tests/test_stage1_native.py`, verificando de forma aislada y determinista:
  - Envolvente y rotacion ciclica modular de 112 px en desplazamiento de filas horizontales.
  - Penetracion de bloques sin rebote en fases de bonificacion (`bonus_flag == 1`).
  - Umbrales de 1-UP y acreditacion de vidas extra segun la tabla `0x1B5D`.
  - Descenso vertical del mural de bloques (`drop_timer` a `0x3C`, decremento de `row_offset`).
  - Cuenta atras y finalizacion de fases de bonus (`bonus_timer`).
  - Wraparound ciclico de fases de 31 a 0 en pantalla de Stage Clear.
- Todas las pruebas de regresion (21 pruebas en total) pasan en ~1.0s.

## Hito 7 (2026-10-17): Pruebas de integracion de audio de produccion y primer informe de paridad real de Fase 2
### 1. Pruebas de integracion del audio de produccion (`src/gb_audio_backend.h`)
- Desacoplado el despachador de sonido (`gb_audio_dispatch_sfx`) y el avance de cola (`gb_audio_step_frame`) a una cabecera compartida sin dependencias directas de Win32.
- Introducida la interfaz de operaciones de dispositivo `GbAudioDeviceOps` para permitir el puente transparente tanto hacia Win32 `waveOut` (en `src/main.c`) como hacia controladores mock de prueba (en `tests/test_c_modules.c`).
- Las pruebas en C (`test_apu_dispatcher_production_nr10_zero` y `test_production_audio_backend_integration`) invocan directamente el codigo productivo usando fixtures de tablas de onda con datos inventados (sin copiar tablas comerciales):
  - Verifica despacho con `NR10 = 0`, asignacion exacta de registros y sintesis de formas de onda diferenciadas.
  - Simula cola ocupada (`buffer_done_status = false`): verifica que no se sobreescribe el buffer, no se avanza el indice y el estado de la APU avanza exactamente una vez.
  - Verifica reanudacion de envio al liberar buffer, fallo de apertura de dispositivo, fallo de escritura y reseteo ordenado en el cierre.
  - Comprobado mediante mutaciones forzadas que reintroducir `NR10 != 0` o eludir la proteccion de cola ocupada produce un fallo inmediato de la prueba C.

### 2. Primer informe de paridad real de Fase 2 frente al cartucho original
- Ampliado el arnes de captura (`tools/alleyway_watchpoints.json`) y el verificador de trazas nativo (`src/main.c`) con los 20 registros de desplazamiento (`0xCA00..0xCA13`) y los 20 temporizadores de fila (`0xCA28..0xCA3B`).
- Descubrimiento de desensamblado: la rutina en `0x0773` (Estado 5: saque) llama a `0x0B21` (desplazamiento de filas), por lo que las filas se desplazan antes de lanzar la bola.
- Verificacion bit a bit de la Fase 2 real ejecutada en PyBoy contra `build/alleyway-lab.exe --verify-trace`:
  - **Ticks comparados**: **739 ticks consecutivos** (`mismatches = 0`).
  - **Campos validados**: `state`, `prng`, `paddle_x`, `paddle_w`, `ball_x`, `ball_x_sub`, `ball_y`, `ball_y_sub`, `ball_vx`, `ball_vy`, `speed_idx`, `bricks_left`, `score_tens`, `lives`, `stage`, `scroll_flag`, `bonus_flag`, 20 `row_shift` y 20 `row_timer`.
  - **Eventos observados y validados**: desplazamiento continuo hacia la derecha (`row_pattern = 0x04`), avance de `row_shift` a traves de los 112 valores posibles (0 a 111 con wraparound modular a 0), y colision real con bloque en fila desplazada (`bricks_left: 84 -> 82`, `score_tens: 0 -> 2` [20 puntos], `speed_idx: 0 -> 3`).
  - Verificacion multi-direccional inyectada: ejecucion de 939 ticks con filas alternadas izquierda (`0x83`) y derecha (`0x02`), alcanzando el rango completo de 0 a 111 en ambas direcciones sin divergencias (`mismatches = 0`).

## Hito 8 (2026-10-17): Paridad real de Fase 3 (descenso vertical autentico por rebotes) y Primera Fase Bonus (Fase 4)
### 1. Paridad real y desensamblado del descenso vertical de Fase 3
- Se comprobo mediante desensamblado de `0x1165..0x1195` y `0x0BC7..0x0C00` que el descenso vertical no se rige por tiempo/fotogramas, sino por el contador `scroll_timer` (`0xFFC4`) decrementado en cada rebote de la pala (`0x1167`).
- En el saque (`0x0788`), `0x1177` carga `scroll_timer = 8` de `ROM[0x1B7D]` y fija `scroll_step = 1`.
- Al agotarse `scroll_timer` tras 8 rebotes, `CALL 0x0BC7` decrementa `row_offset` (40 -> 39 filas visibles desplazadas) y recarga de la tabla `0x1B7D + scroll_step` (`[8, 8, 5, 5, 3, 3, 2, 2, 2, 2]`).
- Se anadio la prueba automatica `test_local_rom_stage3_parity_if_present` comparando **2,138 ticks consecutivos** frente a PyBoy:
  - Cero discrepancias (`mismatches = 0`).
  - Se observaron y validaron 2 descensos completos (`row_offset: 20 -> 19 -> 18`).
  - Colisiones con bloques desplazados y destruccion de 24 bloques (`bricks_left: 84 -> 60`, puntuacion `270`, `speed_idx: 3 -> 5`).

### 2. Primera Fase de Bonificacion (Fase 4, 161 bloques)
- Descriptor `0x1BEA` activa `bonus_flag = 1` y apunta a `map_ptr = 0x357C`.
- Temporizador oficial inicializado desde tabla `0x1B71`: 95 segundos (`0x5F`) para la Ronda 0.
- Cadencia de cuenta atras (`0x198C`): decrementa en Estado 6 unicamente en ticks donde el contador VBlank `vblank_count & 0x1F == 0` (cada 32 frames).
- Penetracion: en `0x0E43` la destruccion de bloques es inmediata y en `0x0E76` salta a `0x0DF3`, atravesando bloques sin rebotar.
- Expiracion y caida: al expirar o caer la bola (`0x0CED`), transiciona a Estado 7, el cual en bonus (`0x07DD`) preserva intactas las vidas y salta a Estado 8 (Clear).
- Pruebas automatizadas en `test_stage1_native.py`:
  - `test_local_rom_stage4_bonus_parity_if_present`: 140+ ticks verificados con `mismatches = 0`, 47 bloques destruidos por penetracion continua.
  - `test_local_rom_stage4_bonus_expiration_if_present`: con temporizador en 2, comprueba decremento, cruce por cero (`bonus_timer = 0`), paso a Estado 7 y conservacion de vidas con `mismatches = 0`.

### 3. Suite de pruebas
- Total de pruebas automatizadas: **25 pruebas pasando en 4.4s** (`unittest discover tests`).

## Siguiente paso recomendado para Codex / futuras sesiones
1. Canales adicionales APU (Canal 2 pulso, Canal 3 onda programable, Canal 4 ruido) y modulacion por software de barridos de tono (`0x6B76`/`0x6B8B`).
2. Secuencias de intro y telon de transicion (Estados 1..4).
3. Mecanica de division de bola / transformacion de pala en fases avanzadas.



