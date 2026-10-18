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
  - Temporizador oficial de bonificacion: leido de tabla `0x1B71` segun la ronda (`stage >> 2`). En Ronda 0 (Fase 4): 95 unidades HUD. Unidades exactas: 1 unidad de HUD = 32 VBlanks (~0.536 s; 95 unidades = 3040 fotogramas = ~50.9 s).
  - Cadencia del temporizador (`0x198C`): solo avanza en Estado 6 (juego activo). En cada tick donde el contador de cuadros VBlank `vblank_count & 0x1F == 0` (cada 32 fotogramas, ~1.86 Hz), `bonus_timer` decrementa en 1. En Saque (Estado 5) y Pausa (Estado 12 / 0x0C) el temporizador permanece congelado.
  - Penetracion completa de bloques: en `0x0E43`, `bonus_flag != 0` destruye el bloque de inmediato sin decrementar HP; en `0x0E76` salta a `0x0DF3`, suprimiendo la reflexion de velocidad para que la bola atraviese formaciones densas sin rebotar.
  - Caida de bola y expiracion: cuando la bola cae (`0x0CED`) o el tiempo se agota (`0x1996: bonus_timer == 0`), se activa el Estado 7. En Estado 7 (`0x07DD`), las fases de bonus saltan la rutina de decremento de vidas y transicionan a Estado 8 (Clear) sin perder vidas.
  - Recompensa y transicion (`0x0805`): si `bricks_left == 0`, se otorgan 500 decenas de bonificacion (`0x01F4` decenas big-endian = 5,000 puntos en HUD) de la tabla `0x1B71`. Se incrementa `stage` (3 -> 4) y se pasa al telon de intro (Estado 4).
  - **Validacion de paridad bit a bit frente al cartucho (`test_local_rom_stage4_bonus_parity_if_present` y `test_local_rom_stage4_bonus_expiration_if_present`)**:
    - Prueba 1 (penetracion y cadencia): 140+ ticks verificados con `mismatches = 0`, 47 bloques penetrados y destruidos continuamente (`bricks_left: 161 -> 114`), puntuacion 1,040 y decremento de temporizador observado.
    - Prueba 2 (expiracion y conservacion de vidas): simulacion con temporizador preparado cerca de cero (`bonus_timer = 2`), verificando 62 ticks de logica, cruce por cero (`bonus_timer = 0`), transicion a Estado 7 y preservacion intacta de vidas (`lives = 4`).

### Hito 9: Cierre de la brecha del reloj independiente, progresion completa (Game Over, Bonus, Final) y matriz de 32 fases
- **Cierre de la brecha del reloj independiente (`src/main.c`, `run_verify_trace` y `--verify-autonomous-clock`)**:
  - Eliminada en `run_verify_trace` la sobreescritura oculta `g_sim.vblank_count = (uint8_t)(ref_vblank - 1u)` que corregia el reloj nativo antes de cada tick.
  - Separacion estricta y documentada entre reloj suministrado por el scheduler (`stage1_step_tick_scheduler`) y avance autonomo (`stage1_step_tick`): el simulador nativo acumula el delta de VBlanks del planificador y valida `(int)g_sim.vblank_count == ref_vblank` como asercion en cada tick.
  - Implementado `--verify-autonomous-clock` que inicializa el reloj nativo una sola vez (`vblank_count = 0`) y ejecuta el ciclo de vida integro sin oraculo:
    1. Saque (Estado 5): 50 ticks -> reloj avanza a 50, temporizador bonus congelado en 95.
    2. Saque con boton A -> Estado 6, reloj avanza a 51.
    3. Juego activo (Estado 6): 64 ticks -> reloj avanza a 115, temporizador bonus decrementa exactamente 2 unidades (32 ticks por unidad: 95 -> 93).
    4. Pausa (Estado 12 / 0x0C): 64 ticks -> reloj avanza a 179, temporizador bonus y fisica completamente congelados.
    5. Reanudacion a Estado 6.
    6. Expiracion exacta en frontera de 32 ticks (cruce por cero) -> transicion a Estado 7 sin perdida de vidas.
    7. Estado 7 transiciona a Estado 8 con vidas preservadas.
    8. Estado 8 avanza fase de 3 a 4 y prepara Estado 5.
- **Progresion real de fases y cierre de partida (maquina de estados DMG `0x054C`)**:
  - Estado 7 (`0x07D3`): si `bonus_flag != 0`, transiciona a Estado 8 sin restar vidas; si `bonus_flag == 0` y `lives == 0`, transiciona a Estado 11 (`0x0B`, Game Over); si `lives > 0`, resta 1 vida y vuelve a Estado 5.
  - Estado 8 (`0x0805`): si `bonus_flag != 0` y `bricks_left == 0`, suma las decenas de bonificacion de `0x1B71` (`[500, 700, 1000, 1500]` decenas = `5,000, 7,000, 10,000, 15,000` puntos); incrementa `stage`; si `stage >= 32`, reinicia a 0 y transiciona a Estado 9 (`0x0839`, secuencia de final de juego); en caso contrario carga la matriz y pasa a Estado 4 (telon) -> Estado 5 (saque).
  - Estado 9 (`0x0839`): celebracion de fin de juego (Fase 32 completada); con entrada o fin de secuencia pasa a Estado 4/5 con `stage = 0` (bucle de partida conservando puntuacion).
  - Estado 11 (`0x0B`, `0x08D1`): Game Over; reproduce jingle y tras tiempo/entrada reinicia partida (`lives = 3, score = 0, stage = 0, state = 5`).
- **Matriz de clasificacion formal de las 32 Fases (8 Rondas x 4 Tipos)**:
  - **Tipo A (Fases 1, 5, 9, 13, 17, 21, 25, 29)**: Estáticas, 20 filas (`bonus_flag = 0`, `scroll_flag = 0`).
  - **Tipo B (Fases 2, 6, 10, 14, 18, 22, 26, 30)**: Desplazamiento horizontal de 20 filas (`bonus_flag = 0`, `scroll_flag = 1`, patrones 0..7 en `0x4075`).
  - **Tipo C (Fases 3, 7, 11, 15, 19, 23, 27, 31)**: Descenso vertical de 40 filas (`bonus_flag = 0`, `scroll_flag = 0`, `row_offset = 20` decrementado por rebotes de paleta segun tabla `0x1B7D`).
  - **Tipo D (Fases 4, 8, 12, 16, 20, 24, 28, 32)**: Fases de bonificacion (`bonus_flag = 1`, `scroll_flag = 0`, bola penetra bloques sin rebotar, cuenta atras desde tabla `0x1B71` [95, 90, 85, 80 unidades de 32 VBlanks], premio por destruccion total `[500, 700, 1000, 1500]` decenas [5,000, 7,000, 10,000, 15,000 puntos], caida o expiracion sin perdida de vidas).
  - *Aclaracion sobre mecanicas*: Se refuta formalmente cualquier especulacion sobre "division de bola" o "transformaciones de paleta" al estilo Arkanoid; el binario oficial DMG de Alleyway carece por completo de tales rutinas (unicamente existen paleta normal de 24 px y paleta reducida a 16 px al tocar el techo).
- **Ejecucion de pruebas**: **28 pruebas aprobadas** en ~2.2s.

### Hito 10: Scheduler monotónico de alta precisión, audio fraccionario sin deriva, temporizadores auténticos de estado y verificación de las 32 fases
- **Scheduler monotónico DMG y audio fraccionario desacoplado (`src/gb_scheduler.h`, `src/gb_audio_backend.h`, `src/main.c`)**:
  - Reemplazado el temporizador `SetTimer(window, 1, 16, NULL)` por un reloj monotónico Win32 de alta precisión (`QueryPerformanceCounter`/`QueryPerformanceFrequency`), acumulador de tiempo fijo de fotogramas DMG (`70,224 / 4,194,304 = 0.0167427063` s, ~59.7275 Hz) y desacoplamiento de los ticks de simulación frente a la tasa de refresco o mensajes de ventana (`PeekMessageW`).
  - Generador fraccionario exacto de muestras de audio: a 44,100 Hz, cada fotograma DMG requiere exactamente `44100 * 70224 / 4194304 = 738 + 23157 / 65536` muestras. Con un acumulador entero de 16 bits (`audio_frac_acc`), se alternan 738 y 739 muestras por fotograma, logrando exactamente 738,353 muestras en 1,000 fotogramas sin pérdida ni desincronización de audio con el juego.
  - Implementado límite de retardo/arrastre (clamp a 200 ms) para evitar ráfagas descontroladas al mover la ventana o pausar el proceso.
  - Opcion de línea de comandos `--verify-scheduler-timing` en `alleyway-lab.exe` validando:
    1. Constantes físicas de DMG (`GB_CYCLES_PER_FRAME = 70224`, `GB_CPU_CLOCK_HZ = 4194304`, ~59.7275 Hz).
    2. Actualizaciones uniformes a 60 Hz (597..598 ticks por cada 10.0 s de reloj de pared).
    3. Secuencias con delta-t irregular y jitter de fotogramas (597 ticks en 10.0 s desacoplados).
    4. Congelación en pausa y reanudación limpia sin ráfaga de fotogramas.
    5. Corte de seguridad ante bloqueos de 5 segundos (máximo 12 ticks acumulados).
    6. Muestreo fraccionario exacto: 738,353 muestras acumuladas en 1,000 fotogramas (cero deriva acumulada).
    7. Conversiones de temporizador bonus HUD: 95 unidades = 3040 ticks = 50.90 segundos.
- **Temporizadores de retardo entre estados (`state_timer` en `AlleywayStage1`) — Corrección en Hito 11**:
  - *Rectificación de auditoría sobre Hito 10 (`242706a`)*: En `242706a`, `state_timer` fue declarado y decrementado pero nunca se inicializaba al entrar a los estados (`4`, `7`, `8`, `0x0B`), y el paso `state 8 -> state 4` se sobrescribía con `state 5` en el mismo tick; además, las dos pruebas añadidas en `tests/test_stage1_native.py` (`test_original_only_progression_ram_facts_if_present` y `test_rom_stage_descriptor_layout_if_present`, antes nombradas `test_local_rom_progression_scenarios_if_present` y `test_all_32_stage_descriptors_and_advanced_rounds_regression`) solo inspeccionaban la RAM de PyBoy y los descriptores de la ROM respectivamente, sin ejecutar el binario C nativo. Todo ello quedó corregido y verificado diferencialmente en el Hito 11.
- **Presentación visual adaptada (banners y telón)**:
  - *Distinción explícita entre medición del cartucho y adaptación visual*: Los conteos de fotogramas de los estados (`4`, `7`, `8`, `11`) provienen de mediciones empíricas por fotograma en el cartucho original. La representación visual en el ejecutable Win32 (bandas laterales que se abren en Estado 4 y recuadros centrados "GAME OVER" / "STAGE CLEAR" usando los tiles de fuente de `0x5375`) es una **adaptación de presentación**, no una reproducción de las listas OAM/BG internas de las rutinas de intermedio del original.

### Hito 11: Inicialización compartida de `state_timer`, ejecutor nativo `--run-scenario` y verificación diferencial real PyBoy vs C (`tools/scenario_diff.py`)
- **Entrada unificada a estados y temporizadores reales de producción (`src/main.c`)**:
  - Implementada la función compartida `stage1_enter_state(AlleywayStage1 *s, uint8_t new_state)` como único punto de entrada a cualquier estado (`1`, `2`, `4`, `5`, `6`, `7`, `8`, `9`, `0x0B`, `0x0C`), inicializando `state_timer` una sola vez al entrar y eliminando cualquier ruta o backdoor `state_timer == 0` en pruebas o producción.
  - Eliminada la sobrescritura inmediata `state 8 -> state 4 -> state 5`: al terminar `state 8`, el motor entra en `state 4` durante `DUR_CURTAIN = 67` fotogramas, recarga la matriz de bloques cuando restan `CURTAIN_GRID_LOAD_REMAINING = 63` fotogramas (4 fotogramas después de entrar a `state 4`, igualando el momento exacto en que el cartucho limpia `0xFFAA` y carga el mapa siguiente), y solo al agotar los 67 fotogramas invoca `stage1_begin_serve` (`state 5`).
  - Eliminado el atajo interactivo que permitía saltar estados temporizados (`4`, `7`, `8`, `11`) pulsando `A` o `Start` mientras `state_timer > 0`.
  - Actualizados `--verify-autonomous-clock` y `--verify-multi-stage-rules` para usar `stage1_enter_state` y recorrer los temporizadores reales mediante `ticks_until_state_change` sin poner `state_timer = 0`.
- **Mediciones empíricas por fotograma en el cartucho original (PyBoy) vs motor nativo C**:
  - **Normal Stage Clear (`normal_clear`)**: `state 6` -> al romper el último bloque entra en `state 8` durante **144 fotogramas** (`DUR_CLEAR = 144`, no 145) -> `state 4` (telón, `stage: 0 -> 1`) durante **67 fotogramas** (`DUR_CURTAIN = 67`) -> `state 5` (saque).
  - **Caída de bola en fase de bonus (`bonus_drop`)**: `state 6` -> al caer la bola bajo el fondo entra en `state 7` durante **228 fotogramas** (`DUR_BONUS_END = 228`) -> **transiciona directamente a `state 4` durante 67 fotogramas (`stage: 3 -> 4`, vidas intactas `lives = 4`) -> `state 5`**. *Corrección clave*: un bonus incompleto (por caída o tiempo agotado) **NUNCA pasa por `state 8`** en el cartucho original; `bonus_flag` (`0xFFAA`) permanece en `1` durante los primeros 4 fotogramas de `state 4` hasta que se carga la siguiente fase (`CURTAIN_GRID_LOAD_REMAINING = 63`).
  - **Expiración de temporizador de bonus (`bonus_expire`)**: cuando `bonus_timer` cruza a `0` en `state 6`, entra en `state 7` durante **228 fotogramas** -> **directamente a `state 4` durante 67 fotogramas (`stage: 3 -> 4`, `lives = 4`) -> `state 5`** (sin pasar por `state 8`).
  - **Bonus perfecto (`bonus_perfect`)**: al romper el último ladrillo de una fase de bonus (`bricks_left == 0`), entra en `state 8` durante **498 fotogramas** (`DUR_BONUS_TALLY = 498`, no 499). Durante los primeros 448 fotogramas el marcador conserva los puntos del bloque roto (`score_tens = 2`); durante los últimos **50 fotogramas** (`state_timer < 50`), el cartucho suma progresivamente **`+10` decenas por fotograma** (`50 * 10 = +500` decenas = `+5,000` puntos en Ronda 0, otorgados una única vez) -> `state 4` (**67 fotogramas**, `stage: 3 -> 4`, `bonus_flag` se limpia en el 5.º fotograma de `state 4`) -> `state 5`.
  - **Pérdida de vida normal con vidas restantes (`life_lost`)**: al caer la bola en fase normal con `lives = 4`, entra en `state 7` durante **100 fotogramas** (`DUR_DEATH = 100`, no 101) -> entra por **1 fotograma transitorio en `state 11` (`0x0B`, `DUR_LIFE_LOST_TRANSIENT = 1`)** donde decrementa `lives: 4 -> 3` -> pasa a `state 5` (saque).
  - **Game Over sin vidas restantes (`game_over`)**: al caer la bola con `lives == 0`, entra en `state 7` durante **100 fotogramas** -> `state 11` (`0x0B`) durante **377 fotogramas** (`DUR_GAME_OVER = 377`) -> `state 1` durante **1 fotograma** (`DUR_TITLE_ENTRY = 1`) -> `state 2` (espera en pantalla de título).
  - **Vidas iniciales (`0xCA44`)**: al iniciar partida desde título, `0xCA44` vale **4** en el primer saque (corregido en `stage1_reset`).
- **Herramienta y suite diferencial real PyBoy vs ejecutable C (`--run-scenario`, `tools/scenario_diff.py`, `tests/test_scenario_diff.py`)**:
  - Añadido modo `--run-scenario <rom> <scenario.jsonl>` en `src/main.c`: inicializa el estado desde la instantánea de la línea 1 (en estado 5 o 6) y ejecuta `stage1_step_tick` autónomamente alimentando únicamente `in_joyp_raw` e `in_joyp_edge` por fotograma, emitiendo el flujo JSONL de salida.
  - `tools/scenario_diff.py` prepara el escenario en PyBoy (documentando cada dirección de RAM inyectada en `injections`), graba la traza de PyBoy fotograma a fotograma, invoca `build/alleyway-lab.exe --run-scenario` con el mismo archivo de entrada y compara tick a tick `state`, `stage`, `lives`, `score_tens`, `bonus_flag`, `bonus_timer` y `prng`, además de las longitudes exactas de cada racha de estados (`state_runs`).
  - **Resultados de los 6 escenarios diferenciales (`tests/test_scenario_diff.py`)**:
    1. `normal_clear` (360 ticks): `6x104 -> 8x144 -> 4x67 -> 5x45`, `0 mismatches` (incluyendo `prng`).
    2. `bonus_drop` (430 ticks): `6x67 -> 7x228 -> 4x67 -> 5x68`, `0 mismatches` (incluyendo `prng`).
    3. `bonus_expire` (410 ticks): `6x62 -> 7x228 -> 4x67 -> 5x53`, `0 mismatches` (incluyendo `prng`).
    4. `bonus_perfect` (620 ticks): `5x2 -> 6x12 -> 8x498 -> 4x67 -> 5x41`, `0 mismatches` (incluyendo conteo progresivo de 50 pasos de `+10` decenas y `prng`).
    5. `life_lost` (220 ticks): `6x67 -> 7x100 -> 11x1 -> 5x52`, `0 mismatches` en `state`, `stage`, `lives`, `score_tens`, `bonus_flag`, `bonus_timer`. Divergencia conocida y documentada en `prng`: tras `7 -> 11 -> 5` el motor C queda 1 paso de LFSR por delante del original (`tick 102: expected 81, native 146`), registrada explícitamente con `@unittest.expectedFailure` en `test_life_lost_prng_matches_original`.
    6. `game_over` (580 ticks): `6x67 -> 7x100 -> 11x377 -> 1x1 -> 2x35`, `0 mismatches` (incluyendo `prng`).
  - **Prueba de mutación (`test_mutation_without_timer_initialisation_fails_differential`)**: corrompe `state_timer = 0` tras la transición en el flujo nativo y demuestra que el comparador diferencial detecta inmediatamente el fallo en el `tick 105` (`state: expected=8, native=4`), impidiendo que regresiones de temporizador pasen desapercibidas.
  - **Estado de la suite de pruebas en Hito 11 (`dcb47f0`)**: **38 pruebas ejecutadas (37 OK + 1 `expectedFailure` documentado sobre el paso extra de `prng` tras `life_lost`)**. *(Nota: en `dcb47f0`, `tools/scenario_diff.py` muestreaba al retorno de `gb.tick(1)` en `LY = 0` en lugar del inicio atómico de la ISR de VBlank `0x01EF` en `LY = 144`, produciendo un desplazamiento de 1 fotograma en las fronteras `6->8`/`8->4`/`6->7`/`7->11`; esto y la divergencia de `prng` quedaron resueltos en el Hito 12).*

### Hito 12: Cierre del desfase PRNG en `life_lost`, reinicio desde Título + 2.ª partida + Pausa/Reanudación, Bonus multi-ronda (`0x1B71`) + 1-UP, Fase 32 -> Ending (`State 9`) y Fases avanzadas (14 y 23)
- **Muestreo atómico en el punto de entrada de la ISR de VBlank (`0x01EF`, `LY = 144`) en `tools/scenario_diff.py`**:
  - Al retornar `gb.tick(1)` (`LY = 0`), la rutina de sonido de final de VBlank (`0x0210: CALL 0x7FF0`) o el bucle principal pueden haber cruzado la línea 0. Registrando un hook de PyBoy en `0x01EF` (`LY = 144`, inicio de la ISR de VBlank), cada fotograma se muestrea de forma 100% atómica después de que el bucle principal ejecutó el manejador de estado y el avance `0x0522: CALL 0x0505` (PRNG) y se detuvo en `0x0221` (`HALT`).
  - Con ello, `prng` (`0xFFA1`) se compara ahora en **todos los fotogramas de todos los estados** sin excepciones, eliminando `KNOWN_PRNG_DIVERGENCE` y `@unittest.expectedFailure`.
- **Causas raíz medidas en el desensamblado DMG y corregidas en `src/main.c`**:
  1. **Orden real de PRNG en `State 7 -> 11 -> 5` (`0x07D3..0x0804`)**:
     - En `State 7` (`0x07D3`), tras 101 VBlanks de espera (`DUR_DEATH = 101`), `0x07EA` comprueba `lives` (`0xCA44`).
     - Solo cuando `lives == 0` (`game_over`), `0x07ED: RET Z` retorna al bucle principal (`0x0522: CALL 0x0505`), avanzando el PRNG en la transición `7 -> 11`.
     - Cuando `lives > 0` (`life_lost`), la CPU **no retorna** en `7 -> 11`: decrementa `lives`, entra en `State 11` y espera 1 VBlank interno (`0x07F3: CALL 0x47C7`) **sin** llamar a `0x0505`; en el siguiente VBlank (`0x07F6..0x0804`), restablece la paleta (`paddle_shrunk = 0`, `paddle_w = 0x18`), fija `scroll_step = 2` (`0x07FC: LD A, 2; LDH (0xFFC5), A`), asigna `state = 5` y ejecuta `RET` hacia `0x0522: CALL 0x0505` en la transición `11 -> 5`. (En Hito 11 se suprimía el PRNG en `11 -> 5` en lugar de `7 -> 11`).
  2. **Secuencia de 2 VBlanks al sacar en `State 5` (`0x0773..0x07A5`)**:
     - Al pulsar `A` en `State 5`, `0x078E` llama a `0x101B` (`serve_ball`, que consume `prng` para elegir el ángulo inicial) y luego ejecuta `0x0794: CALL 0x47C7`, esperando 1 VBlank dentro de `State 5` **sin** retornar a `0x0522` (`serve_pending = 1`, sin paso de PRNG en ese fotograma). En el segundo VBlank (`0x0797..0x07A5`), dispara `SFX 7`, cambia a `State 6` y retorna a `0x0522` (1 paso de PRNG).
  3. **Restablecimiento de paleta/scroll y duración exacta de `State 4` (`0x06A2..0x0734`, `0x092A`, `0x43DC`)**:
     - Al cargar la fase en `State 4` (`0x06AE..0x06D8`), el cartucho restablece `paddle_shrunk = 0`, `paddle_w = 0x18`, `paddle_x = 0x28`, `paddle_y = 0x90`, y al salir a `State 5` (`0x0730`) limpia `scroll_step = 0` (`0xFFC5`).
     - El desempaquetado del mapa (`0x092A`) consume un número determinista de VBlanks según las filas y celdas no vacías del descriptor: `(next_bonus ? 98 : 65) + (rows * 14 + nonempty * 5) / 390` (`66..70` fotogramas en fases normales, `99..100` en fases de bonus).
     - Además, cuando se entra a `State 4` desde la pantalla de título (`State 2`, `first_curtain == 1`), `0x06F7` ejecuta la animación de entrada de la nave de Mario (`0x43DC`), añadiendo exactamente **`+155` fotogramas** (`66 + 155 = 221` fotogramas en Fase 1).
  4. **Reinicio desde Pantalla de Título (`State 2`, `0x0582..0x0604`)**:
     - Al pulsar `Start` en `State 2`, el original espera `DUR_TITLE_START_DELAY = 3` fotogramas antes de cambiar a `State 4` (`221` fotogramas), reiniciando `stage = 0`, `score_tens = 0`, `lives = 4`, `up_index = 0` pero **preservando intactos `prng`, `vblank_count` y `scroll_timer`**.
  5. **Conteo de Bonus multi-ronda desde ROM (`0x19F7..0x1A68`, `0x1B71`) y VBlank extra por 1-UP (`0x0C71`)**:
     - Eliminada la constante fija de 50 pasos: `State 8` espera `DUR_BONUS_PRE_TALLY = 449` fotogramas previos al conteo y lee en tiempo de ejecución el premio de la ronda desde `0x1B71 + round_idx * 3 + 1` (`500, 700, 1000, 1500` decenas para Rondas 0, 1, 2 y >=3), sumando `+10` decenas por fotograma durante `reward_tens / 10` pasos (`50, 70, 100, 150` fotogramas).
     - En cada paso de `+10` decenas, `0x1A59` invoca `CALL 0x0C71` (`check_1up_threshold`). Cuando se cruza un umbral de 1-UP (`1,000` decenas = `10,000` puntos), `0x0C9F` llama a `0x47C7` (`JP 0x0221`), consumiendo **1 VBlank adicional** dentro de `State 8` (`449 + 100 + 1 = 550` fotogramas en Ronda 2; `449 + 150 + 1 = 600` fotogramas en Ronda 3 y Fase 32).
  6. **Secuencia de Final tras Fase 32 (`State 9`, `0x0839`)**:
     - Al completar la Fase 32 (`stage == 31 -> 0`), tras `State 8` (`600` fotogramas) el juego entra en `State 9` durante exactamente **`DUR_ENDING = 1445` fotogramas**, tras lo cual pasa a `State 4` (`66` fotogramas) -> `State 5` en `stage = 0` conservando la puntuación y vidas acumuladas.
  7. **Limpieza de la fila inferior al descender el mural vertical (`0x0BC7 -> 0x0BF2`)**:
     - Cuando `row_offset` (`0xFFA9`) decrementa tras alcanzar el umbral de rebotes de paleta, `0x0BF2` borra en memoria (`grid_type = 0`) la fila `row_offset + 20` que acaba de salir por la parte inferior de las 20 filas visibles y resta de `bricks_left` (`0xCA46`) los bloques destructibles presentes en esa fila.
- **Resultados verificados de los 12 escenarios diferenciales PyBoy vs C (`tests/test_scenario_diff.py`, `mismatch = None` en los 12)**:
  1. `normal_clear` (420f): `state_runs = [(6, 103), (8, 145), (4, 66), (5, 106)]`, `stage: 0 -> 1`, `score_tens = 1`, `lives = 4`.
  2. `bonus_drop` (700f): `state_runs = [(7, 229), (4, 66), (5, 405)]`, `stage: 3 -> 4`, `lives = 4` (sin pasar por `state 8`).
  3. `bonus_expire` (700f): `state_runs = [(6, 29), (7, 229), (4, 66), (5, 376)]`, `stage: 3 -> 4`, `lives = 4` (sin pasar por `state 8`).
  4. `bonus_perfect` (800f, Fase 4 / Ronda 0, `+500` decenas en 50 pasos): `state_runs = [(6, 61), (8, 499), (4, 66), (5, 174)]`, `score_tens: 2 -> 502`, `stage: 3 -> 4`.
  5. `bonus_perfect_r1` (800f, Fase 8 / Ronda 1, `+700` decenas en 70 pasos): `state_runs = [(6, 80), (8, 519), (4, 67), (5, 134)]`, `score_tens: 2 -> 702`, `stage: 7 -> 8`.
  6. `bonus_perfect_r2` (800f, Fase 12 / Ronda 2, `+1000` decenas en 100 pasos + 1-UP): `state_runs = [(6, 66), (8, 550), (4, 66), (5, 118)]`, `score_tens: 2 -> 1002`, `lives: 4 -> 5`, `stage: 11 -> 12`.
  7. `bonus_perfect_r3` (850f, Fase 16 / Ronda 3, `+1500` decenas en 150 pasos + 1-UP): `state_runs = [(6, 67), (8, 600), (4, 66), (5, 117)]`, `score_tens: 2 -> 1502`, `lives: 4 -> 5`, `stage: 15 -> 16`.
  8. `life_lost` (420f, `compare_play = True`): `state_runs = [(7, 101), (11, 1), (5, 15), (6, 303)]`, verifica `prng` exacto tras `7 -> 11 -> 5`, nuevo saque con `A`, seguimiento de paleta y rotura de bloques (`bricks_left = 72`, `score_tens = 12`, `lives = 3`).
  9. `game_over` (850f, `compare_play = True`): `state_runs = [(7, 101), (11, 377), (1, 1), (2, 25), (4, 221), (5, 7), (6, 29), (12, 20), (6, 34), (7, 35)]`, verifica llegada a Título (`State 2`), reinicio real con `Start`, telón inicial de `221` fotogramas, llegada a `State 5` con `score_tens = 0, lives = 4, stage = 0`, saque a segunda partida (`State 6`), Pausa (`State 12` durante 20 fotogramas) y Reanudación (`State 6`).
  10. `stage32_ending` (2250f, Fase 32 -> Ending -> bucle a Fase 1): `state_runs = [(6, 73), (8, 600), (9, 1445), (4, 66), (5, 66)]`, `stage: 31 -> 0`, `score_tens = 1502`, `lives: 4 -> 5`.
  11. `stage14_horizontal` (350f, `compare_play = True`, Fase 14 desplazamiento horizontal avanzado): `state_runs = [(6, 350)]`, verifica desplazamiento `row_shift_6 > 0`, física de bola/paleta y rotura de bloques (`bricks_left: 69 -> 61`, `score_tens = 13`).
  12. `stage23_vertical` (450f, `compare_play = True`, Fase 23 descenso vertical avanzado): `state_runs = [(6, 450)]`, verifica descenso por rebotes de paleta (`row_offset: 20 -> 19`), limpieza de la fila inferior (`0x0BF2`) y colisiones (`bricks_left = 104`, `score_tens = 45`).
- **Alcance verificado vs límites**:
  - Tienen verificación diferencial de ejecución real PyBoy vs C las **Fases 1, 2, 3, 4, 8, 12, 14, 16, 23 y 32**. Las restantes fases de las 32 están cubiertas por inspección estructural de sus descriptores y mapas en ROM (`test_rom_stage_descriptor_layout_if_present`), sin afirmar que las 32 fases individuales tengan trazas de juego completas.
- **Estado de la suite de pruebas**: **40 pruebas automatizadas ejecutadas y aprobadas (`40 OK, 0 expectedFailure`)** en ~9.0s.

### Hito 13: Presentación visual 100% fiel desde ROM (Título y Saque Fase 1), verificación de ventana Win32 e2e y medición de telón en las 32 fases
- **Renderizado visual auténtico desde ROM local (`render_stage1` en `src/main.c`)**:
  - Reconstrucción de la presentación visual original leyendo directamente los 6 KiB de VRAM (`0x8000..0x97FF`, 384 patrones de 8x8) decodificados desde `ROM[0x4B75..0x6374]`.
  - Intérprete nativo de listas de despliegue VRAM tipo Nintendo DMG (`0x02C1`):
    - **Pantalla de Título (`State 1 / State 2`, `0x41CD`)**: mapa BG de 20x18 baldosas (`LCDC = 0x83`, Window desactivada), más 5 sprites OAM (`OAM[10..14]`) en `(112, 104)` formateando el récord de puntuación en BCD (`0200` para `high_score_tens = 200`).
    - **Tubo superior y Ventana lateral HUD (`0x4A3C`)**: baldosas de borde superior (`0xBD`, `0xB5`x20) y ventana derecha (`0x9C00` en `WX = 127`, screen `X = 120..159`): pared derecha (`0xBE`/`0xB4`), títulos `"TOP"` y `"1UP"`, marcador BCD de 5 dígitos (con corona `0xBF`/`0xBC`/`0xC9` para >= 10,000 pts), letrero `"STAGE XX"` (`0xC0..0xC3` y número de fase en fila 11), icono de pala (`0xB1`, `0xB2`) y vidas en fila 16.
    - **Pared izquierda (Sprites OAM `OAM[15..31]`)**: 17 sprites del tubo izquierdo (`0xB4`) situados en `oam_x = 8` (`screen_x = 0`) y `oam_y = 24..152` (`screen_y = 8..136`).
    - **Pala y Bola (`OAM[0..2]` y `OAM[3]`)**: pala de 24 px (`0x00, 0x01, 0x00` con flip horizontal) o 16 px encogida (`0x00, 0x00` flip), bola (`0x05`) visible en estados 6, 8, 12 y durante `serve_pending` de estado 5.
    - **Mapeo de baldosas de ladrillos (`0x0AC9..0x0B08` en `0x1B87`)**: renderizado de pares de filas de ladrillos de 8x8 según tipo superior/inferior, desplazamiento horizontal de fila (`row_shift`) y máscara de combinación.
  - El mismo renderizador productivo alimenta tanto el volcado headless (`--dump-ppm`) como la ventana Win32 interactiva (`WM_PAINT` con `StretchDIBits`).
- **Verificación de paridad visual bit-a-bit contra PyBoy (`tests/test_visual_parity.py`)**:
  - **Pantalla de Título (`State 2`, `test_title_screen_bit_exact_visual_parity`)**: **100.00% de coincidencia** (23,040 de 23,040 píxeles idénticos tono a tono, 0 diferencias).
  - **Pantalla de Saque Fase 1 (`State 5`, `test_stage1_serve_bit_exact_visual_parity`)**: **100.00% de coincidencia** (23,040 de 23,040 píxeles idénticos tono a tono, 0 diferencias).
- **Sesión de ventana Win32 verificada de extremo a extremo (`--verify-window-session`)**:
  - Valida la integración de extremo a extremo con el subsistema de ventanas del sistema operativo Windows:
    - Creación de ventana HWND nativa (`CreateWindowW`, `ShowWindow`).
    - Procedimiento de ventana (`WndProc`), bucle y bomba de mensajes de Win32 (`PeekMessageW`/`TranslateMessage`/`DispatchMessageW`).
    - Despacho y procesamiento de mensajes de repintado (`WM_PAINT` con `StretchDIBits`) y eventos sintéticos de entrada (`WM_KEYDOWN`/`WM_KEYUP`) inyectados a la cola de mensajes para:
      1. Enter (`VK_RETURN`, Start): delay de 3 fotogramas -> telón de entrada de Mario (`221` fotogramas) -> Saque (`State 5`, vidas 4, stage 0).
      2. Flecha derecha (`VK_RIGHT` durante 5 fotogramas): movimiento de pala (`paddle_x: 40 -> 55`).
      3. Espacio (`VK_SPACE`): saque a juego activo (`State 6`) disparando efecto de sonido SFX 7.
      4. Enter (`VK_RETURN`): pausa a `State 0x0C` (verifica que la bola queda congelada 5 fotogramas) y reanudación a `State 6`.
      5. Tecla `R`: reinicio directo a saque de Fase 1 (`paddle_x = 40`).
      6. Tecla `Esc`: cierre ordenado (`DestroyWindow`) y apagado seguro de audio (`audio_backend_shutdown`).
  - *Aclaración explícita de límites*: esta prueba valida la bomba de mensajes, los procedimientos de ventana y el despacho de eventos de la API de Win32; **NO valida pulsación física de teclas en un teclado por un usuario ni captura acústica de ondas sonoras mediante micrófono**.

### Hito 14: Verificación automatizada de telones en las 32 fases, alineaciones de VBlank y resolución de colisión multi-bloque en Fase 17
- **Resolución auténtica de la colisión multi-bloque (`0x09F9 -> 0x023D`) en `src/main.c`**:
  - En el cartucho DMG (`0x0E5F`), destruir un bloque llama a `0x09F9`. Al destruir un segundo bloque dentro del mismo fotograma (colisión en esquina con dos bloques en un solo paso de física), la subrutina `0x023D` comprueba `0xFFA3` (`vram_brick_dirty`). Al estar activo por el primer bloque, ejecuta `JR 0x0221` (`HALT`), deteniendo la CPU en espera del siguiente VBlank antes de decrementar `bricks_left` (`0xFFC8`) en `0x0E68` y rebotar.
  - Esto explicaba la divergencia observada en Fase 17 tick 85: en el original, el tick 85 se muestreó durante el `HALT` con `state = 8`, `prng = 104` (sin avanzar en `0x0522`), `bricks_left = 0` y `ball = (28, 91)` antes del rebote vertical. En tick 86, la CPU reanuda: decrementa `bricks_left` de `0` a `65535` (`0xFFFF`), ejecuta el rebote vertical a `ball_y = 92`, avanza `prng` a `169` y comienza la cuenta de 145 fotogramas de `State 8`, totalizando **146 fotogramas** (`(8, 146)`).
  - Implementado en `src/main.c` mediante los campos `vram_brick_dirty` y `multi_brick_pending`, logrando paridad bit a bit estricta (`runs_exp = runs_nat = [(6, 85), (8, 146), (4, 68), (5, 1)]`) sin excepciones por número de fase ni muletas en pruebas.
- **Suite automatizada de telones en las 32 fases y alineaciones de VBlank (`test_all_32_curtain_transitions_and_vblank_alignments`)**:
  - Integrada en `tests/test_scenario_diff.py` como prueba formal de regresión continua (elevando la suite a 44 pruebas).
  - En `tools/scenario_diff.py` (`run_stage_curtain_transition_diff`), se incorporó seguimiento activo de bola con la paleta durante `state == 6`, garantizando que la bola alcance los bloques objetivo sin caer prematuramente en fases complejas (como fases 28, 29 y 30 que anteriormente terminaban en pérdida de vida con 0 frames de telón).
  - Condición de fallo estricta: la prueba exige alcanzar `next_stage`, pasar por `state == 4` con duración `curtain > 0`, y terminar en `state == 5` de saque. Cualquier escenario inconcluso se reporta como fallo de discrepancia (`mismatch`).
  - **Resultados de las 32 transiciones de fase (`0..31 -> 1..0`) y 16 alineaciones de VBlank (`0, 7, 16, 31`)**: **100% de coincidencia bit a bit frente a PyBoy (0 discrepancias)**.
- **Estado de la suite de pruebas**: **44 pruebas automatizadas ejecutadas y aprobadas (`44 OK, 0 expectedFailure`)** en ~19.1s.

## Lo que falta para el juego completo (siguiente paso para continuar)
1. **Canales adicionales APU (Canal 2 pulso, Canal 3 onda programable, Canal 4 ruido)** y modulación por software de barridos de tono (`0x6B76`/`0x6B8B`).
2. **Animaciones intermedias completas de OAM en el telón (caminata y salto de Mario 0x43DC en State 4) y celebración animada del final (Mario saludando 0x45BD en State 9)**.


