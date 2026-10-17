# Alleyway: registro de investigacion y desarrollo
Fecha: 2026-10-17
Estado: ingenieria inversa de la Fase 1 verificada sobre `../Local-Privado/roms/Alleyway (USA).gb` (fuera de Git); preparando implementacion nativa verificable de la Fase 1.

## Copia local del usuario (fuera de Git)
- Ruta: `../Local-Privado/roms/Alleyway (USA).gb`
- Metadatos verificados con `tools/inspect_rom.py`:
  - `bytes = 32768`, `title = "ALLEY WAY"`, `cartridge_type = 0`, `version = 0`, `old_licensee_code = 1`.
  - `header_checksum_valid = true` (94), `global_checksum_valid = true` (53662).
  - `sha256 = ed8070e011713527bdc03e2b9cec9f9c4a7e3aaa00e57e8786a186b265da1bb2`.

## Hito 1 (2026-10-17): Linea temporal determinista, trazas y comparador (`df314f1`)
- Implementados `tools/synthetic_rom.py`, `tools/inspect_rom.py`, `tools/capture_reference.py`, `tools/compare_traces.py` y 8 pruebas unitarias en `tests/`.

## Hito 2 (2026-10-17): Escaneo de RAM y verificacion de fisica y Fase 1
### Herramientas anadidas
- `tools/scan_ram.py`: escaneo diferencial entre dos lineas temporales (`diff_timelines`) y busqueda de correlacion temporal (`find_correlated_addresses`) sobre `WRAM` (`0xC000..0xDFFF`), `OAM` (`0xFE00..0xFE9F`) y `HRAM` (`0xFF80..0xFFFE`).
- `tests/test_scan_ram.py`: prueba automatizada sobre cartucho sintetico que verifica el descubrimiento automatico de coordenadas y contadores de botones.
- `tools/alleyway_watchpoints.json`: definicion simbolica de los watchpoints verificados de Alleyway (USA).

### Hallazgos verificados sobre `Alleyway (USA).gb`
1. **Puerto serie y controles (`0x01F0..0x0296`, `0x03C1..0x0415`, `0x10A4..0x10FA`) — VERIFICADO**:
   - **Observacion**: En VBlank (`0x01F6..0x01FC`) el juego escribe `0x02` en `0xFF90` y `0x81` en `0xFF02` (`SC`). La interrupcion serie (`0x0058 -> 0x0272`) lee dos bytes consecutivos de `0xFF01` (`SB`) hacia `0xFF91` y `0xFF92`/`0xFF93`.
   - **Verificacion**: En `0x10A4`, el juego compara `0xFF91 < 0xF1`. Si se cumple, interpreta `0xFF91 - 0x30` como posicion absoluta de un controlador rotatorio externo e ignora la cruceta; cuando no hay hardware externo conectado, `SB` recibe `0xFF` (`0xFF91 = 0xFF`), ejecutando la rama de cruceta en `0x10AA..0x10DC`.
2. **Movimiento de la paleta (`0x10AA..0x10DC`) — VERIFICADO**:
   - `0xFFC0`: posicion X base (`OAM[0].X = 0xFFC0 + 1`, `OAM[1].X = 0xFFC0 + 9`, `OAM[2].X = 0xFFC0 + 17`).
   - `0xFFBF`: posicion Y (`144 = 0x90`).
   - `0xFFC2`: ancho en pixeles (`24` normal; pasa a `16` con `0xFFC1 = 1` y `0xFFC0 += 4` cuando la bola toca el techo `Ball_Y < 24` en `0x0CF2..0x0D11`).
   - Velocidad por fotograma segun botones mantenidos en `0xFF8C`:
     - Solo cruceta: **3 px/fotograma**.
     - Cruceta + `A`: **5 px/fotograma**.
     - Cruceta + `B` (sin `A`): **1 px/fotograma**.
   - Limites de `0xFFC0`: minimo `15` (`0x0F`), maximo `127 - 0xFFC2` (`103` normal, `111` encogida).
3. **Fisica de la bola en punto fijo 8.8 big-endian (`0x0F7C`, `0x0CB0..0x116A`) — VERIFICADO**:
   - `0xFFB4:0xFFB5` (`Y`), `0xFFB6:0xFFB7` (`X`), `0xFFB8:0xFFB9` (`vy`), `0xFFBA:0xFFBB` (`vx`).
   - En cada fotograma activo (`state == 6`), `0x0F7C` copia el byte alto actual a `0xFFBC` (`prev_Y`) y `0xFFBD` (`prev_X`) y suma `(vy, vx)` en 16 bits.
   - Al sacar con `A` (`0x101B..0x108C`), la bola sale desde `Y = 116.0` (`0x7400`) con el vector de `Speed 1`, angulo 9 (`|vx| = 181, vy = +181`, es decir `0.70703125 px/frame` por eje), hacia la izquierda si `paddle_x + paddle_w/2 >= 72` o hacia la derecha si `< 72`, dejando `0xFFBE = 3`.
   - En el primer rebote con la paleta (`0x1113`), pasa a usar `0xFFBE = 3` (`Speed 3` en `0x12B8`, donde el angulo 9 tiene `|vx| = 226, |vy| = 226`) y selecciona el angulo segun la zona `B = (ball_x - (paddle_x - 3)) >> 1` en `0x1B41` (`[3,6,6,6,9,9,9,9,9,9,9,9,6,6,6]`).
4. **Geometria y bloques de la Fase 1 (`0x092A..0x0A95`, `0x0D27..0x0E8C`, `0x1BE1 -> 0x1C44`) — VERIFICADO**:
   - Cada celda de `0xC000` (14 columnas $\times$ 60 filas) representa un bloque de `8 px` de ancho por `4 px` de alto. Cada tile de fondo de `8x8` en VRAM (`0x9821..`) empaqueta verticalmente 2 filas de la matriz (`row` par e impar) usando las 3 variantes graficas de `0x1B87`.
   - Fase 1 tiene 84 bloques destructibles (`0xFFC8:0xFFC9 = 84`):
     - Filas `0..1`: 28 celdas tipo 4 (`hp = 0`, muro superior indestructible en `Y = 24..31`).
     - Filas `6..7`, cols `1..12`: 24 bloques tipo 3 (`30 pts`, `hp = 1`, `min_speed_idx = 7`).
     - Filas `8..9`, cols `1..12`: 24 bloques tipo 2 (`20 pts`, `hp = 1`, `min_speed_idx = 5`).
     - Filas `10..12`, cols `1..12`: 36 bloques tipo 1 (`10 pts`, `hp = 1`, `min_speed_idx = 0`).
   - Experimento de traza de 650 fotogramas (`tools/capture_reference.py` con `tools/alleyway_watchpoints.json`): verificado el saque en `f=476`, rebote en paleta en `f=514`, rebote en pared derecha y destruccion del primer bloque en `f=588` (`bricks_left: 84 -> 83`, `score: 0 -> 10`).

## Siguiente paso (Hito 3)
- Construir el simulador/ejecutable nativo en C de la Fase 1 (`src/`) que cargue las tablas y el mapa de la Fase 1 directamente desde la ROM local del usuario (sin empaquetar datos comerciales en el repositorio), permita jugarla en ventana Win32/GDI y verifique paridad exacta de trazas fotograma a fotograma frente a PyBoy.
