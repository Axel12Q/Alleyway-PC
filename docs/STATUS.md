# Estado

2026-10-17: desarrollo activo en `antigravity/development` con volcado del usuario verificado en `../Local-Privado/roms/Alleyway (USA).gb` (fuera de Git).

## Hitos completados
### Hito 1: Infraestructura determinista de entradas, trazas y comparador (`df314f1`)
- `tools/synthetic_rom.py`, `tools/inspect_rom.py`, `tools/capture_reference.py`, `tools/compare_traces.py` y pruebas en `tests/`.

### Hito 2: Escaneo diferencial de RAM e ingenieria inversa verificada de la Fase 1
- Implementado `tools/scan_ram.py` (`diff_timelines` y `find_correlated_addresses`) con pruebas unitarias sobre cartucho sintetico (`tests/test_scan_ram.py`; 9 tests en total pasando).
- Creado `tools/alleyway_watchpoints.json` con los watchpoints verificados de Alleyway (USA) para `tools/capture_reference.py`.
- **Verificado en `../Local-Privado/roms/Alleyway (USA).gb` (`ed8070e01171...`)**:
  - **Protocolo de controles y puerto serie (`0x01F0..0x0296`, `0x03C1..0x0415`, `0x10A4..0x10FA`)**: Cada VBlank lanza 2 transferencias serie con reloj interno (`0xFF90 = 2`, `0xFF02 = 0x81`). La interrupcion serie (`0x0058 -> 0x0272`) guarda `SB` en `0xFF91` (dial rotatorio) y `0xFF92`/`0xFF93` (boton serie). Sin periferico externo, `0xFF91 = 0xFF` (`>= 0xF1`), habilitando la lectura normal de la cruceta en `0xFF8C`.
  - **Movimiento de paleta (`0x10A4..0x10DC`)**: Posicion X en `0xFFC0` (`OAM[0].X = 0xFFC0 + 1`), Y fija en `0xFFBF = 144`, ancho en `0xFFC2` (`24 px` normal, `16 px` encogida cuando `0xFFC1 = 1`). Velocidades exactas: **3 px/frame** normal, **5 px/frame** manteniendo `A`, **1 px/frame** manteniendo `B`. Limites: `[15, 127 - ancho]`.
  - **Fisica de bola en punto fijo 8.8 big-endian (`0x0F7C`, `0x0CB0..0x116A`)**: Posicion `Y` en `0xFFB4:0xFFB5`, `X` en `0xFFB6:0xFFB7`, velocidad `vy` en `0xFFB8:0xFFB9`, `vx` en `0xFFBA:0xFFBB`, indice de velocidad en `0xFFBE` (`1..25`, tabla de punteros en `0x11EE`), tabla de 15 zonas de rebote en paleta normal en `0x1B41` (`[3,6,6,6,9,9,9,9,9,9,9,9,6,6,6]`) y 11 zonas en paleta encogida en `0x1B51`.
  - **Estructura de la Fase 1 (`0x1BE1 -> 0x1C44`, cargada en `0xC000` y durabilidad en `0xC400`)**: Matriz de 14 columnas (`8 px` de ancho, `X = 16..127`) $\times$ 14 filas (`4 px` de alto, `Y = 24..79`). Filas `0..1`: muro superior indestructible (tipo 4, `hp = 0`). Filas `6..7`: 24 bloques tipo 3 (`30 pts`, `min_speed = 7`). Filas `8..9`: 24 bloques tipo 2 (`20 pts`, `min_speed = 5`). Filas `10..12`: 36 bloques tipo 1 (`10 pts`, `min_speed = 0`). Total destructibles en `0xFFC8:0xFFC9` = **84**.
  - **Traza de referencia verificada (650 fotogramas)**: Arranque -> `Start` en fotograma 120 -> movimiento de paleta a la derecha (`40 -> 100`) en fotogramas `450..470` -> saque con `A` en fotograma 475 (`ball = (88.0, 116.0)`, `v = (+181, +181)`) -> rebote en paleta en fotograma 514 (`v = (+226, -226)`) -> rebote en pared derecha -> rotura del primer bloque en fotograma 588 (`bricks_left = 84 -> 83`, `score = 0 -> 10`).

## Siguiente paso inmediato (Hito 3)
- Implementar la simulacion nativa verificable de la Fase 1 en C (`src/`) y su verificador/comparador de trazas frente a PyBoy, cargando los datos de la ROM local del usuario en tiempo de ejecucion sin incluir recursos comerciales en Git.
