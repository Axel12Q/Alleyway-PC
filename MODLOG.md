# Alleyway: registro de investigacion y desarrollo
Fecha: 2026-10-17
Estado: Hito 12 completado — Fases 1, 2, 3, 4, 8, 12, 14, 16, 23 y 32 verificadas bit a bit frente a PyBoy (trazas de logica `--verify-trace` y 12 escenarios diferenciales completos `--run-scenario`, 40/40 pruebas OK sin `expectedFailure`) sobre el volcado local del usuario (`../Local-Privado/roms/Alleyway (USA).gb`, fuera de Git). Canales APU 2..4 y secuencias visuales animadas de titulo/final siguen en progreso.

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
- Temporizador oficial inicializado desde tabla `0x1B71`: 95 unidades HUD (1 unidad HUD = 32 VBlanks ~0.536s; 95 unidades = 3040 fotogramas = ~50.9s).
- Cadencia de cuenta atras (`0x198C`): decrementa en Estado 6 unicamente en ticks donde el contador VBlank `vblank_count & 0x1F == 0` (cada 32 frames).
- Penetracion: en `0x0E43` la destruccion de bloques es inmediata y en `0x0E76` salta a `0x0DF3`, atravesando bloques sin rebotar.
- Expiracion y caida: al expirar o caer la bola (`0x0CED`), transiciona a Estado 7, el cual en bonus (`0x07DD`) preserva intactas las vidas y salta a Estado 8 (Clear).
- Pruebas automatizadas en `test_stage1_native.py`:
  - `test_local_rom_stage4_bonus_parity_if_present`: 140+ ticks verificados con `mismatches = 0`, 47 bloques destruidos por penetracion continua.
  - `test_local_rom_stage4_bonus_expiration_if_present`: con temporizador en 2, comprueba decremento, cruce por cero (`bonus_timer = 0`), paso a Estado 7 y conservacion de vidas con `mismatches = 0`.

## Hito 9 (2026-10-17): Cierre de la brecha del reloj independiente, progresion de juego y matriz de 32 fases
### 1. Cierre de la brecha del reloj independiente
- En `run_verify_trace`, eliminada la asignacion oculta `g_sim.vblank_count = (uint8_t)(ref_vblank - 1u)`.
- Separacion formal entre el reloj autonomo (`s->vblank_count++` por tick) y el reloj suministrado por el scheduler (`stage1_step_tick_scheduler` con delta de fotogramas).
- Asercion explicita `g_sim.vblank_count == ref_vblank` en cada tick de la traza para garantizar paridad del reloj sin correcciones oraculares.
- Opcion CLI `--verify-autonomous-clock` en `src/main.c` probando el avance autonomo a traves de Saque (5), Juego activo (6), Pausa (12 / 0x0C), expiracion por cuenta atras a cero y transicion sin perdida de vidas en bonus.

### 2. Progresion completa y maquina de estados de Alleyway
- Implementada la transicion autentica de la maquina de estados (`0x054C`):
  - Limpieza normal: Estado 8 -> Estado 4 -> Estado 5 incrementando `stage`.
  - Bonus completado: Estado 8 suma decenas de bonificacion de `0x1B71` (`[500, 700, 1000, 1500]` decenas = `5,000, 7,000, 10,000, 15,000` puntos en HUD).
  - Caida de bola / fin de tiempo en bonus: Estado 7 -> Estado 8 sin restar vidas.
  - Fin de vidas: Estado 7 con `lives == 0` -> Estado 11 (`0x0B`, Game Over) -> reseteo con Start/A a Fase 1.
  - Fase 32 completada: `stage` incrementa de 31 a 32 -> reinicio modular a 0 y transicion a Estado 9 (`0x0839`, secuencia final de Mario) -> bucle a Fase 1.

### 3. Matriz de clasificacion formal de las 32 Fases
- 8 Rondas x 4 Fases por ronda:
  - Tipo A (Fases 1, 5, 9, 13, 17, 21, 25, 29): Estáticas, 20 filas.
  - Tipo B (Fases 2, 6, 10, 14, 18, 22, 26, 30): Desplazamiento horizontal de 20 filas.
  - Tipo C (Fases 3, 7, 11, 15, 19, 23, 27, 31): Descenso vertical de 40 filas por rebotes en paleta.
  - Tipo D (Fases 4, 8, 12, 16, 20, 24, 28, 32): Bonus sprites, penetracion continua, cuenta atras y puntuacion extra.
- Refutacion: el binario DMG carece de mecanicas tipo Arkanoid de division de bola o transformacion de pala; unicamente existe la reduccion normal a 16 px por colision con el techo.

### 4. Suite de pruebas
- Total de pruebas automatizadas: **28 pruebas aprobadas en ~2.2s** (`unittest discover tests`).

## Hito 10 (2026-10-17): Scheduler monotonico QPC y audio fraccionario sin deriva (`242706a`)
- Implementado `src/gb_scheduler.h` con reloj monotonico `QueryPerformanceCounter` a la frecuencia exacta de DMG (`70,224 / 4,194,304` s/frame, ~59.7275 Hz) y sintesis fraccionaria de audio a 44,100 Hz (`738 + 23157/65536` muestras/frame, 738,353 muestras en 1,000 fotogramas con cero deriva).
- Nota de auditoria: en `242706a`, `state_timer` no se inicializaba al entrar en estados y las pruebas de progresion solo inspeccionaban PyBoy/ROM sin ejecutar el binario C; corregido en Hito 11.

## Hito 11 (2026-10-17): Inicializacion productiva de `state_timer` y verificacion diferencial PyBoy vs C (`dcb47f0`)
- Implementada `stage1_enter_state` en `src/main.c` como unico punto de entrada a estados y anadido el modo `--run-scenario <rom> <scenario.jsonl>`.
- Creados `tools/scenario_diff.py` y `tests/test_scenario_diff.py` ejecutando en paralelo PyBoy y `alleyway-lab.exe --run-scenario` sobre 6 escenarios de transicion (`normal_clear`, `bonus_drop`, `bonus_expire`, `bonus_perfect`, `life_lost`, `game_over`) mas prueba de mutacion de `state_timer`.
- Suite en `dcb47f0`: **38 pruebas ejecutadas (37 OK + 1 `expectedFailure` en `test_life_lost_prng_matches_original`)**.

## Hito 12 (2026-10-17): Cierre de PRNG en `life_lost`, reinicio desde Titulo + 2.a partida + Pausa/Reanudacion, Bonus multi-ronda (`0x1B71`), Fase 32 -> Ending y Fases 14/23
### 1. Muestreo atomico en VBlank (`0x01EF`, `LY = 144`) y orden real de PRNG
- `tools/scenario_diff.py` registra un hook de PyBoy en la entrada de la ISR de VBlank (`0x01EF`, `LY = 144`), muestreando cada fotograma de forma 100% atomica tras `0x0522: CALL 0x0505` (mientras la CPU espera en `0x0221: HALT`).
- Desensamblado de `State 7` (`0x07D3..0x0804`): `0x07ED: RET Z` solo retorna a `0x0522` en `7 -> 11` cuando `lives == 0` (`game_over`). Cuando `lives > 0` (`life_lost`), espera 1 VBlank dentro de `State 11` (`0x07F3: CALL 0x47C7`) sin avanzar PRNG, restablece `paddle_shrunk = 0`, `paddle_w = 0x18`, `scroll_step = 2` (`0x07FC`), asigna `state = 5` y retorna a `0x0522` en `11 -> 5`.
- En `State 5` (`0x0773..0x07A5`), pulsar `A` llama a `0x101B` (`serve_ball`) y espera 1 VBlank en `0x0794: CALL 0x47C7` permaneciendo en `State 5` sin avanzar PRNG (`serve_pending = 1`), pasando a `State 6` en el siguiente VBlank.
- Eliminados `@unittest.expectedFailure` y `KNOWN_PRNG_DIVERGENCE`: `prng` coincide en todos los fotogramas de los 12 escenarios.

### 2. Reinicio desde Titulo (`State 2`), duracion exacta de `State 4`, Bonus multi-ronda (`0x1B71`), Ending (`State 9`) y limpieza vertical (`0x0BF2`)
- En `State 2` (`0x0582..0x0604`), pulsar `Start` reinicia `stage = 0, score_tens = 0, lives = 4, up_index = 0` (conservando `prng`, `vblank_count`, `scroll_timer`) y tras `3` fotogramas entra en `State 4` con `+155` fotogramas adicionales de la secuencia de entrada de Mario (`0x43DC`, total `221` fotogramas en Fase 1).
- En `State 4` (`0x06A2..0x0734`), la duracion del desempaquetado `0x092A` se calcula por descriptor (`(next_bonus ? 98 : 65) + (rows * 14 + nonempty * 5) / 390`), restableciendo paleta (`0x28`, `0x18`) y limpiando `scroll_step = 0` al salir.
- En `State 8` (`0x19F7..0x1A68`), el conteo de bonus perfecto espera `449` fotogramas, lee el premio de la ronda desde `0x1B71` (`500, 700, 1000, 1500` decenas = `50, 70, 100, 150` pasos de `+10` decenas/fotograma) y contabiliza el VBlank extra (`0x47C7`) cuando `0x0C71` otorga un 1-UP (`550` fotogramas en Ronda 2, `600` en Ronda 3 y Fase 32).
- Al completar la Fase 32 (`stage: 31 -> 0`), `State 8` pasa a `State 9` (`0x0839`, `DUR_ENDING = 1445` fotogramas) -> `State 4` (`66` fotogramas) -> `State 5` en `stage = 0` conservando puntos y vidas.
- En el descenso vertical (`0x0BC7 -> 0x0BF2`), al decrementar `row_offset` se limpia en memoria la fila `row_offset + 20` que sale por el fondo de las 20 filas visibles y se descuentan sus bloques de `bricks_left`.

### 3. Suite diferencial ampliada (12 escenarios PyBoy vs C, `40/40` tests OK)
- Escenarios verificados con `mismatch = None` en `tests/test_scenario_diff.py`:
  1. `normal_clear` (420f): `[(6, 103), (8, 145), (4, 66), (5, 106)]`
  2. `bonus_drop` (700f): `[(7, 229), (4, 66), (5, 405)]`
  3. `bonus_expire` (700f): `[(6, 29), (7, 229), (4, 66), (5, 376)]`
  4. `bonus_perfect` (800f, Ronda 0): `[(6, 61), (8, 499), (4, 66), (5, 174)]`
  5. `bonus_perfect_r1` (800f, Ronda 1): `[(6, 80), (8, 519), (4, 67), (5, 134)]`
  6. `bonus_perfect_r2` (800f, Ronda 2 + 1-UP): `[(6, 66), (8, 550), (4, 66), (5, 118)]`
  7. `bonus_perfect_r3` (850f, Ronda 3 + 1-UP): `[(6, 67), (8, 600), (4, 66), (5, 117)]`
  8. `life_lost` (420f, `compare_play=True`, re-saque + juego activo): `[(7, 101), (11, 1), (5, 15), (6, 303)]`
  9. `game_over` (850f, `compare_play=True`, Start en Titulo -> 2.a partida -> Pausa/Reanudacion): `[(7, 101), (11, 377), (1, 1), (2, 25), (4, 221), (5, 7), (6, 29), (12, 20), (6, 34), (7, 35)]`
  10. `stage32_ending` (2250f, Fase 32 -> State 9 Ending -> Fase 1): `[(6, 73), (8, 600), (9, 1445), (4, 66), (5, 66)]`
  11. `stage14_horizontal` (350f, `compare_play=True`, Fase 14): `[(6, 350)]`
  12. `stage23_vertical` (450f, `compare_play=True`, Fase 23): `[(6, 450)]`
- Total de la suite automatizada: **40 pruebas ejecutadas y aprobadas (`40 OK, 0 expectedFailure`)** en ~9.0s.

## Hito 13 (2026-10-17): Presentacion visual fiel desde ROM (Titulo y Saque), verificacion e2e de ventana Win32 y medicion de telon en 32 fases
- **Renderizado visual con listas de despliegue VRAM tipo Nintendo DMG (`0x02C1`) en `src/main.c`**:
  - Reemplazadas las barras grises y cajas inventadas por la reconstruccion fiel de los mapas BG y Window y sprites OAM desde la ROM local:
    - **Pantalla de Titulo (`State 2`, `0x41CD`)**: mapa BG de 20x18 baldosas decodificando baldosas 2bpp desde `ROM[0x4B75..0x6374]`, mas 5 sprites OAM (`OAM[10..14]`) en `(112, 104)` con el record formateado en BCD (`0200` para 2,000 pts).
    - **Tubo superior y Ventana lateral HUD (`0x4A3C`)**: baldosas de borde superior (`0xBD`, `0xB5`x20) y ventana derecha (`0x9C00` en `WX = 127`, screen `X = 120..159`): pared derecha (`0xBE`/`0xB4`), titulos `"TOP"` y `"1UP"`, marcador BCD de 5 digitos, letrero `"STAGE XX"` (`0xC0..0xC3` y numero de fase en fila 11), icono de pala (`0xB1`, `0xB2`) y vidas en fila 16.
    - **Pared izquierda (Sprites OAM `OAM[15..31]`)**: 17 sprites del tubo izquierdo (`0xB4`) situados en `oam_x = 8` (`screen_x = 0`) y `oam_y = 24..152`.
    - **Pala y Bola (`OAM[0..2]` y `OAM[3]`)**: pala de 24 px (`0x00, 0x01, 0x00` con flip horizontal) o 16 px encogida (`0x00, 0x00` flip), bola (`0x05`).
    - **Mapeo de baldosas de ladrillos (`0x0AC9..0x0B08` en `0x1B87`)**: renderizado de pares de filas de ladrillos de 8x8 segun tipo superior/inferior, desplazamiento horizontal de fila (`row_shift`) y mascara de combinacion.
  - La misma funcion de renderizado (`render_stage1`) alimenta tanto el volcado headless (`--dump-ppm`) como la ventana Win32 interactiva (`WM_PAINT` con `StretchDIBits`).
- **Verificacion de paridad visual automatizada tono a tono frente a PyBoy (`tests/test_visual_parity.py`)**:
  - **Pantalla de Titulo (`State 2`)**: **100.00% de coincidencia** (23,040 de 23,040 pixeles identicos, 0 diferencias).
  - **Pantalla de Saque Fase 1 (`State 5`)**: **100.00% de coincidencia** (23,040 de 23,040 pixeles identicos, 0 diferencias).
- **Sesion de ventana Win32 verificada de extremo a extremo (`--verify-window-session`)**:
  - Prueba automatizada que crea la ventana real `HWND`, procesa la bomba de mensajes Win32 (`PeekMessageW`/`TranslateMessage`/`DispatchMessageW`), despacha `WM_PAINT`, y despacha eventos sinteticos `WM_KEYDOWN`/`WM_KEYUP` para Start (`VK_RETURN`), movimiento (`VK_RIGHT`), saque (`VK_SPACE`), pausa/reanudacion (`VK_RETURN`), reinicio (`R`) y cierre (`Esc`), comprobando el cierre ordenado y el apagado seguro de audio `waveOut`.
  - *Alcance delimitado*: valida la integracion con la API de ventanas y la bomba de mensajes del SO; **NO valida pulsacion fisica en teclado ni captura acustica de audio por microfono**.

## Hito 14 (2026-10-17): Resolucion de colision multi-bloque en Fase 17 y suite automatizada de telones en 32 fases
- **Resolucion autentica de la colision multi-bloque (`0x09F9 -> 0x023D`) en `src/main.c`**:
  - Investigacion de la discrepancia de tick 85 en Fase 17 (`runs_exp = [(6, 85), (8, 146), (4, 68), (5, 1)]` vs `runs_nat = [(6, 85), (8, 145), (4, 68), (5, 2)]`, primer mismatch `tick 85 prng expected 104 native 169 state 8`).
  - Al destruir un segundo bloque en el mismo fotograma (colisiones de esquina con dos bloques), la subrutina `0x09F9` llama a `0x023D`, la cual comprueba `0xFFA3` (`vram_brick_dirty`). Si esta activo, ejecuta `JR 0x0221` (`HALT`), deteniendo la CPU en espera del siguiente VBlank antes de decrementar `bricks_left` (`0xFFC8`) en `0x0E68` y aplicar el rebote.
  - Durante este `HALT`, el VBlank del tick 85 muestrea `state = 8`, `prng = 104` (sin avanzar en `0x0522`), `bricks_left = 0` y `ball = (28, 91)` antes del rebote vertical. En tick 86, la CPU reanuda: decrementa `bricks_left` de `0` a `65535` (`0xFFFF`), ejecuta el rebote vertical a `ball_y = 92`, avanza `prng` a `169` y comienza la cuenta de 145 fotogramas de `State 8`, totalizando **146 fotogramas**.
  - Implementado en `src/main.c` mediante los campos `vram_brick_dirty` y `multi_brick_pending`, logrando paridad bit a bit estricta sin excepciones por numero de fase ni muletas en pruebas.
- **Suite automatizada de telones en las 32 fases y alineaciones de VBlank (`test_all_32_curtain_transitions_and_vblank_alignments`)**:
  - Integrada en `tests/test_scenario_diff.py` como prueba formal de regresion continua (44 pruebas en total).
  - En `tools/scenario_diff.py` (`run_stage_curtain_transition_diff`), se incorporo seguimiento activo de bola con la paleta durante `state == 6`, garantizando que la bola alcance los bloques objetivo sin caer prematuramente en fases complejas (como fases 28, 29 y 30 que anteriormente terminaban en perdida de vida con 0 frames de telon).
  - Condicion de fallo estricta: la prueba exige alcanzar `next_stage`, pasar por `state == 4` con duracion `curtain > 0`, y terminar en `state == 5` de saque. Cualquier escenario inconcluso se reporta como fallo de discrepancia (`mismatch`).
  - **Resultados de las 32 transiciones de fase (`0..31 -> 1..0`) y 16 alineaciones de VBlank (`0, 7, 16, 31`)**: **100% de coincidencia bit a bit frente a PyBoy (0 discrepancias)**.
- Total de la suite automatizada: **44 pruebas ejecutadas y aprobadas (`44 OK, 0 expectedFailure`)** en ~19.1s.

## Checkpoint Jugable y Mapa de Reutilizacion para Codex (2026-10-17)
- **Lanzador local (`tools/launch.py`, `launch.ps1`)**:
  - Lanzador limpio para abrir Alleyway con ROM local detectada automaticamente (`local/roms/` o `../Local-Privado/roms/`) o pasada por linea de comandos.
  - Hardening de `launch.ps1`: utiliza `$PSScriptRoot`, `Join-Path` y `-LiteralPath` para resolver Python y `tools/launch.py` de forma robusta ante ejecuciones desde otros directorios o rutas con espacios.
  - Hardening de `tools/launch.py`: si el usuario especifica una ruta de ROM inexistente, reporta un error explicito a `stderr` y termina con codigo de salida 1 de inmediato sin buscar otras ROMs silenciosamente. Establece `cwd=ROOT` para la compilacion y ejecucion.
  - Si no se especifica ROM y no existe ninguna en los directorios de busqueda, arranca en modo diagnostico fallback.
  - Imprime instrucciones completas de controles (Enter, Espacio/Z, Flechas/AD, Shift/X, R, Esc).
- **Documento formal de Checkpoint (`docs/CHECKPOINT.md`)**:
  - Detalla SHA, comandos de compilacion y prueba, controles, alcance probado rigurosamente (física 8.8, 44 tests, 32 transiciones de fase y 16 alineaciones de VBlank), adaptaciones (Win32, QPC a cadencia DMG ~59.7275 Hz desacoplada de presentacion de host, waveOut) y limites pendientes (APU 2..4, animaciones OAM intermedias, partidas largas manuales), evitando llamarlo port completo.
  - Enlaces ajustados a rutas relativas al repositorio (sin `file:///`).
- **Mapa de Reutilizacion para Codex (`docs/REUSABILITY_ARCHITECTURE.md`)**:
  - Inventario exhaustivo auditado contra las firmas reales del codigo: `inspect`/`inspect_file` en `tools/inspect_rom.py`, `build_synthetic_rom` en `tools/synthetic_rom.py`, y `gb_tile_decode_indices`/`gb_tile_decode_pixels`/`gb_tile_blit` en `src/gb_tile.h`.
  - Reemplazadas afirmaciones absolutas ("100% reutilizable") por clasificaciones rigurosas ("Candidato reutilizable", "Parcial", "Mixto").
  - Delimitacion entre servicios genericos y reglas especificas de Alleyway (tablas SFX embebidas en `gb_audio_backend.h`, direcciones WRAM en `scenario_diff.py`).
  - Documentados los requisitos reales para un compilador universal `ROM -> C -> EXE` (traductor de CPU SM83, bus de memoria, interrupciones, PPU por scanlines y APU completo).

## Siguiente paso recomendado para Codex / futuras sesiones
1. Canales adicionales APU (Canal 2 pulso, Canal 3 onda programable, Canal 4 ruido) y modulacion por software de barridos de tono (`0x6B76`/`0x6B8B`).
2. Animaciones intermedias completas de metasprites OAM (entrada de Mario `0x43DC` en Estado 4 y celebracion animada del final `0x45BD` en Estado 9).


