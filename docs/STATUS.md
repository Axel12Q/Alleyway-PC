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

### Hito 6: Correccion de dispatch de audio (NR10=0), seguridad de cola waveOut, reglas multi-fase y temporizadores
- **Correccion de `apu_dispatch_sfx` (`src/main.c`)**:
  - Eliminada la condicion erronea `s->rom[addr] != 0` que descartaba tablas de sonido con `NR10 = 0x00` (sin barrido de frecuencia por hardware). `NR10 = 0` es valido en los efectos autenticos de bloques 1, 2 y 3 (`0x6FDE`, `0x6FBB`, `0x6FCA`).
  - El despachador ahora valida los limites de la ROM cargada y programa con exactitud los 5 registros de Canal 1.
  - Efectos de techo (`SFX 11`) y rebote en pared (`SFX 12`): documentados como implementacion estatica parcial hasta modelar las rutinas por software que barren `NR13`/`NR14` en VBlank (`0x6B76`/`0x6B8B`).
- **Seguridad en la cola Win32 waveOut**:
  - Los buffers de audio se preparan una sola vez en `audio_backend_init` con `waveOutPrepareHeader`.
  - En `audio_step_frame`, nunca se borran los flags de un buffer en vuelo y solo se transfieren datos al dispositivo si el driver ha devuelto el buffer con `WHDR_DONE`. Si la cola de hardware esta ocupada, el frame se descarta limpiamente para el altavoz sin bloquear el bucle de juego ni interferir con la simulacion determinista de la APU.
  - Cierre ordenado y defensivo en `audio_backend_shutdown` con `waveOutReset`, despreparacion de buffers con `WHDR_PREPARED` y liberacion de manija `waveOutClose`.
- **Validacion en ejecutable de pruebas C (`tests/test_c_modules.c`)**:
  - `test_tile_blit_flips_and_transparency`: anadida prueba de flip vertical (Y), mapeo a fila 7 y verificacion de centinelas de memoria alrededor del buffer para confirmar que no hay sobreescritura de bordes.
  - `test_apu_dispatcher_nr10_zero`: prueba sintetica del despachador con `NR10 = 0`, verificando que activa el canal, programa la frecuencia exacta de la tabla y sintetiza un flujo PCM distinto entre `SFX 1` y `SFX 2`.
  - `test_audio_queue_safety_simulation`: simulacion de la maquina de estados de la cola `waveOut`, comprobando que un buffer sin `WHDR_DONE` no se sobreescribe ni avanza indices y se reutiliza correctamente cuando el driver lo libera.
- **Temporizadores y verificador de reglas multi-fase (`src/main.c`)**:
  - Implementada funcion `--verify-multi-stage-rules` en `alleyway-lab.exe` y anadida prueba automatica en `tests/test_stage1_native.py` que verifica:
    1. Desplazamiento horizontal con recorte y desbordamiento modular (`mod 112`), direccion izquierda/derecha y recarga de temporizadores de fila.
    2. Regla de penetracion de bonus (`bonus_flag == 1`), destruccion de bloques sin reflejar vector de velocidad.
    3. Umbrales de 1-UP segun tabla de ROM `0x1B5D` e incremento controlado de vidas.
    4. Descenso vertical de bloques (`drop_timer` a `0x3C` y decremento de `row_offset`).
    5. Cuenta atras y expiracion de temporizador de bonificacion (`bonus_timer`).
    6. Progresion y transicion ciclica de fases (31 -> 0) al pulsar A / Start en estado Clear.
- **Ejecucion de pruebas**: 21 pruebas aprobadas en ~1.0s.

### Hito 7: Pruebas de integracion del audio de produccion y primer informe de paridad real de Fase 2
- **Desacoplamiento y pruebas de integracion de audio (`src/gb_audio_backend.h`)**:
  - Extraido el despachador de sonido (`gb_audio_dispatch_sfx`) y la logica de gestion de cola (`gb_audio_step_frame`) a `src/gb_audio_backend.h`, consumido de forma identica por `src/main.c` y por el arnes de pruebas `tests/test_c_modules.c`.
  - Definida la interfaz inyectable `GbAudioDeviceOps` para desacoplar las llamadas Win32 del bucle de simulacion y permitir pruebas con controladores simulados.
  - La prueba `test_apu_dispatcher_production_nr10_zero` ejecuta directamente `gb_audio_dispatch_sfx` sobre un fixture de ROM con valores inventados/sinteticos (`NR10 = 0`), verificando registros y generacion de PCM disimil.
  - La prueba `test_production_audio_backend_integration` inyecta un mock con seguimiento de llamadas y simula inicio, envio exitoso, cola ocupada (`buffer_done_status = false`), error de escritura en dispositivo, cierre seguro y apertura fallida. Se verifico que el estado APU avanza exactamente una vez independientemente de si el audio se envio o se omitio por cola ocupada.
  - Se comprobo mediante mutaciones temporales provocadas que reintroducir el error `NR10 != 0` o suprimir la proteccion contra sobreescritura de buffers ocupados hace fallar el test de integracion de inmediato.
- **Primer informe de paridad real de Fase 2 frente al cartucho (`tests/test_stage1_native.py`)**:
  - Ampliado `tools/alleyway_watchpoints.json` con los 20 registros de desplazamiento (`0xCA00..0xCA13`) y los 20 temporizadores de fila (`0xCA28..0xCA3B`).
  - Ampliado el verificador de trazas nativo en `src/main.c` (`--verify-trace`) para comparar tick a tick `stage`, `scroll_flag`, `bonus_flag`, los 20 `row_shift` y los 20 `row_timer`.
  - Se descubrio que la rutina DMG en `0x0773` (Estado 5: saque) invoca `CALL 0x0B21` (desplazamiento de filas), de modo que los bloques se desplazan horizontalmente incluso antes de lanzar la bola; `src/main.c` se alineo con este comportamiento real.
  - **Resultado de paridad Fase 2 (Alleyway USA.gb en PyBoy)**:
    - Escenario: transicion desde Stage 1 Clear (`state = 8`) a Stage 2 (`stage = 1`, `state = 5`), saque con boton A y movimiento de paleta.
    - **Ticks comparados**: **739 ticks consecutivos** de simulacion de logica sin una sola discrepancia (`mismatches = 0`).
    - **Campos verificados bit a bit en cada tick**: `state`, `prng`, `paddle_x`, `paddle_w`, `ball_x`, `ball_x_sub`, `ball_y`, `ball_y_sub`, `ball_vx`, `ball_vy`, `speed_idx`, `bricks_left`, `score_tens`, `lives`, `stage`, `scroll_flag`, `bonus_flag`, 20 `row_shift` y 20 `row_timer`.
    - **Eventos observados y validados**: desplazamiento continuo hacia la derecha (`row_pattern = 0x04`), avance de `row_shift` a traves de los 112 valores posibles (0 a 111 con wraparound modular a 0), y colision real con bloque en fila desplazada (`bricks_left: 84 -> 82`, `score_tens: 0 -> 2` [20 puntos], `speed_idx: 0 -> 3`).
    - Verificacion multi-direccional inyectada: ejecucion de 939 ticks con filas alternadas izquierda (`0x83`) y derecha (`0x02`), alcanzando el rango completo de 0 a 111 en ambas direcciones sin divergencias (`mismatches = 0`).
- **Ejecucion de pruebas**: **22 pruebas aprobadas** en ~1.2s.

### Hito 8: Paridad real de Fase 3 (descenso vertical autentico por rebotes de paleta) y Primera Fase Bonus (Fase 4)
- **Descenso vertical autentico de Fase 3 (`0x0BC7..0x0C31` y `0x1165..0x1195`)**:
  - Se descubrio mediante desensamblado e inspeccion dinamica que el descenso vertical **NO esta gobernado por fotogramas**, sino por **rebotes en la paleta**.
  - Al sacar la bola, la rutina en `0x0788` llama a `0x1177`, inicializando `scroll_timer = 8` (leido de `ROM[0x1B7D]`) y `scroll_step = 1`.
  - En cada rebote de paleta en juego activo (`0x1167`), la rutina `0x116D` decrementa `scroll_timer`. Cuando llega a 0:
    - Invoca `CALL 0x0BC7`, que decrementa `row_offset` (`0xFFA9`) en 1 (desplazando la matriz de 40 filas hacia abajo) hasta llegar a 0.
    - Incrementa `scroll_step` y recarga `scroll_timer` desde la tabla oficial en `0x1B7D + scroll_step`: `[8, 8, 5, 5, 3, 3, 2, 2, 2, 2]`. A partir de `scroll_step >= 10`, se recarga a 1 (descenso por cada rebote).
  - **Validacion de paridad bit a bit frente al cartucho (`test_local_rom_stage3_parity_if_present`)**:
    - **Ticks comparados**: **2,138 ticks consecutivos** de logica atomica con **cero discrepancias (`mismatches = 0`)**.
    - **Eventos observados y verificados**: 2 descensos completos (`row_offset: 20 -> 19 -> 18`), colisiones continuas de la bola con las filas desplazadas y destruccion de 24 bloques (`bricks_left: 84 -> 60`, puntuacion 270, aceleracion `speed_idx: 3 -> 5`).
- **Mecanica autentica de la Primera Fase de Bonificacion (Fase 4, `0x1BEA`)**:
  - Descriptor en `0x1BEA`: `desc0 = 0x80` (`bonus_flag = 1`), `map_ptr = 0x357C` (161 bloques con el sprite de Mario).
  - Temporizador oficial de bonificacion: leido de tabla `0x1B71` segun la ronda (`stage >> 2`). En Ronda 0 (Fase 4): 95 segundos (`0x5F`).
  - Cadencia del temporizador (`0x198C`): solo avanza en Estado 6 (juego activo). En cada tick donde el contador de cuadros VBlank `vblank_count & 0x1F == 0` (cada 32 fotogramas, ~1.86 Hz), `bonus_timer` decrementa en 1.
  - Penetracion completa de bloques: en `0x0E43`, `bonus_flag != 0` destruye el bloque de inmediato sin decrementar HP; en `0x0E76` salta a `0x0DF3`, suprimiendo la reflexion de velocidad para que la bola atraviese formaciones densas sin rebotar.
  - Caida de bola y expiracion: cuando la bola cae (`0x0CED`) o el tiempo se agota (`0x1996: bonus_timer == 0`), se activa el Estado 7. En Estado 7 (`0x07DD`), las fases de bonus saltan la rutina de decremento de vidas y transicionan a Estado 8 (Clear) sin perder vidas.
  - Recompensa y transicion (`0x0805`): si `bricks_left == 0`, se otorgan 500 puntos de bonificacion (`0x01F4` decenas) de la tabla `0x1B71`. Se incrementa `stage` (3 -> 4) y se pasa al telon de intro (Estado 4).
  - **Validacion de paridad bit a bit frente al cartucho (`test_local_rom_stage4_bonus_parity_if_present` y `test_local_rom_stage4_bonus_expiration_if_present`)**:
    - Prueba 1 (penetracion y cadencia): 140+ ticks verificados con `mismatches = 0`, 47 bloques penetrados y destruidos continuamente (`bricks_left: 161 -> 114`), puntuacion 1,040 y decremento de temporizador observado.
    - Prueba 2 (expiracion y conservacion de vidas): simulacion con temporizador preparado cerca de cero (`bonus_timer = 2`), verificando 62 ticks de logica, cruce por cero (`bonus_timer = 0`), transicion a Estado 7 y preservacion intacta de vidas (`lives = 4`).
- **Aserciones de cobertura y suite de pruebas**:
  - Anadidas aserciones explicitas en `test_local_rom_stage2_parity_if_present` (`stage == 1`, `scroll_flag == 1`, avance y rotacion modular de `row_shift`).
  - Actualizado `--verify-multi-stage-rules` en `src/main.c` para verificar de forma aislada el descenso vertical por rebote y la cadencia de cuenta atras y transicion sin perdida de vidas en bonus.
  - Total de pruebas automatizadas: **25 pruebas aprobadas** en ~4.4s.

## Lo que falta para el juego completo (siguiente paso para continuar)
1. **Canales adicionales APU (Canal 2 pulso, Canal 3 onda programable, Canal 4 ruido)** y modulacion por software de barridos de tono (`0x6B76`/`0x6B8B`).
2. **Secuencias de intro y telon de transicion (Estados 1..4)**.
3. **Mecanica de division de bola / transformacion de pala en fases avanzadas**.



