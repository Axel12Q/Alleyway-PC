# Protocolo de medicion y mapa de memoria verificado

Volcado analizado localmente (fuera de Git): `../Local-Privado/roms/Alleyway (USA).gb`
- SHA256: `ed8070e011713527bdc03e2b9cec9f9c4a7e3aaa00e57e8786a186b265da1bb2`
- Cabecera: `ALLEY WAY`, 32768 bytes, `cartridge_type = 0`, `version = 0`, `header_checksum = 94` (valido), `global_checksum = 53662` (valido).

## 1. Controles y puerto serie (`0xFF00`, `0xFF01`, `0xFF02`) — VERIFICADO
- **Evidencia**: rutina de VBlank en `0x01F0..0x0220`, rutina de interrupcion serie en `0x0272..0x0296`, lectura de cruceta/botones en `0x03C1..0x0415` y control de paleta en `0x10A4..0x10FA`.
- En cada VBlank, el juego lee `0xFF00` (`P1/JOYP`) hacia `0xFF8C` (estado activo en bajo), `0xFF8D` (flanco de pulsacion activo en bajo) y `0xFF8E`, e inicia una transferencia serie de 2 bytes con reloj interno (`0xFF90 = 2`, `0xFF02 = 0x81`).
- La interrupcion serie (`0x0058 -> 0x0272`) guarda el primer byte recibido de `0xFF01` (`SB`) en `0xFF91` (posicion de control rotatorio/dial externo) y el segundo en `0xFF92`/`0xFF93` (boton serie).
- Sin dispositivo conectado al puerto link, `SB` recibe `0xFF` (`0xFF91 = 0xFF`, `0xFF93 = 0xFF`). En `0x10A4`, si `0xFF91 < 0xF1`, el juego usa `0xFF91 - 0x30` como posicion absoluta de la paleta e ignora la cruceta; si `0xFF91 >= 0xF1`, usa la cruceta (`0xFF8C`). Esto explica exactamente el fallo de controles en emuladores que no devuelven `0xFF` en `SB` con reloj interno.

## 2. Mapa de variables de estado (HRAM / WRAM) — VERIFICADO
Todas las direcciones siguientes fueron localizadas mediante `tools/scan_ram.py` y verificadas contra el codigo SM83 (`0x0500..0x1250`) y trazas de ejecucion en PyBoy:

| Variable | Direccion | Formato | Unidad / Significado verificado |
|---|---|---|---|
| `state` | `0xFFA4` | `u8` | Estado principal (`0`: init, `1`: prep titulo, `2`: pantalla titulo, `3`: demo, `4`: carga de fase, `5`: saque en paleta, `6`: juego activo, `7`: vida perdida, `8`: fase superada, `0x0C`: pausa) |
| `prng` | `0xFFA1` | `u8` | Generador pseudoaleatorio (`0x0505`: suma `0x41` en cada llamada) |
| `paddle_y` | `0xFFBF` | `u8` | Coordenada Y OAM de la paleta (`144 = 0x90`, pantalla `Y = 128`) |
| `paddle_x` | `0xFFC0` | `u8` | Coordenada X base de la paleta (`OAM[0].X = 0xFFC0 + 1`). Limites: `[15, 127 - paddle_w]` (`[15, 103]` normal, `[15, 111]` encogida) |
| `paddle_shrunk` | `0xFFC1` | `u8` | `0`: normal (`24 px`), `1`: encogida (`16 px`, al tocar el techo `Ball_Y < 24`) |
| `paddle_w` | `0xFFC2` | `u8` | Ancho de paleta en pixeles (`24 = 0x18` normal, `16 = 0x10` encogida) |
| `ball_y` | `0xFFB4:0xFFB5` | `u16be` (`8.8`) | Posicion Y de la bola en punto fijo 8.8 big-endian (`0xFFB4` entero OAM, `0xFFB5` subpixel `/256`) |
| `ball_x` | `0xFFB6:0xFFB7` | `u16be` (`8.8`) | Posicion X de la bola en punto fijo 8.8 big-endian (`0xFFB6` entero OAM, `0xFFB7` subpixel `/256`) |
| `ball_vy` | `0xFFB8:0xFFB9` | `i16be` (`8.8`) | Velocidad vertical con signo en punto fijo 8.8 big-endian (`0x0F7C`) |
| `ball_vx` | `0xFFBA:0xFFBB` | `i16be` (`8.8`) | Velocidad horizontal con signo en punto fijo 8.8 big-endian (`0x0F7C`) |
| `ball_prev_y` | `0xFFBC` | `u8` | Entero Y del fotograma previo |
| `ball_prev_x` | `0xFFBD` | `u8` | Entero X del fotograma previo |
| `speed_idx` | `0xFFBE` | `u8` | Indice de velocidad (`1..25`) en la tabla de punteros `0x11EE` |
| `bricks_left` | `0xFFC8:0xFFC9` | `u16be` | Contador de bloques destructibles restantes (`84` al iniciar la Fase 1) |
| `score_tens` | `0xFFCA:0xFFCB` | `u16le` | Puntuacion dividida entre 10 (`score = valor * 10`) |
| `high_score_tens` | `0xFFCC:0xFFCD` | `u16le` | Record dividido entre 10 (`200` inicial = `2000` puntos) |
| `lives` | `0xCA44` | `u8` | Vidas restantes (`4` al pulsar Start, `3` tras la entrada de Mario o al perder la primera bola) |
| `stage` | `0xCA45` | `u8` | Indice de fase (`0..31`; `0` = Fase 1) |
| `brick_type_grid` | `0xC000..0xC347` | `u8[60][14]` | Matriz de tipos de bloque (14 columnas de `8 px` $\times$ hasta 60 filas de `4 px`) |
| `brick_hp_grid` | `0xC400..0xC747` | `u8[60][14]` | Durabilidad por celda (`0` = indestructible, `>=1` = destructible) |

## 3. Reglas fisicas medidas y verificadas en la Fase 1
1. **Velocidad de la paleta (`0x10AA..0x10DC`)**:
   - Cruceta sola (`Left`/`Right`): **3 px/fotograma**.
   - Cruceta + boton `A` mantenido: **5 px/fotograma**.
   - Cruceta + boton `B` mantenido (sin `A`): **1 px/fotograma**.
   - Limite izquierdo: `0xFFC0 >= 15` (`0x0F`). Limite derecho: `0xFFC0 <= 127 - paddle_w` (`103` con paleta de 24 px; `111` con paleta de 16 px).
2. **Saque de la bola (`0x101B..0x108C`)**:
   - `ball_y = 116.0` (`0x74`), `speed_idx = 3` (o `7` si quedan `< 40` bloques).
   - Si el centro de la paleta `paddle_x + (paddle_w >> 1) < 72` (`0x48`): `ball_x = paddle_x + 24 + (paddle_w >> 1)` y sale hacia abajo-izquierda `v = (-181, +181)` (`Speed 1`, angulo 9).
   - Si `paddle_x + (paddle_w >> 1) >= 72`: `ball_x = paddle_x - 24 + (paddle_w >> 1)` y sale hacia abajo-derecha `v = (+181, +181)`.
3. **Paredes y colision con paleta (`0x0CB1..0x0D26`)**:
   - Pared izquierda: `ball_x < 16` (`0x10`); pared derecha: `ball_x >= 124` (`0x7C`). Ajusta X al borde (`0x0F08`/`0x0F20`) e invierte `ball_vx`.
   - Techo: `ball_y < 24` (`0x18`). Encoge la paleta a `16 px` (`0xFFC1 = 1, 0xFFC2 = 16, 0xFFC0 += 4`) la primera vez, ajusta Y e invierte `ball_vy`.
   - Fondo: `ball_y >= 160` (`0xA0`) -> `state = 7` (pierde vida).
   - Paleta (`vy >= 0`, `141 <= ball_y <= 148`, `0 <= ball_x - (paddle_x - 3) < paddle_w + 5`):
     - `zone = (ball_x - (paddle_x - 3)) >> 1`.
     - Tabla de angulos en `0x1B41` (normal, 15 zonas: `[3,6,6,6,9,9,9,9,9,9,9,9,6,6,6]`) o `0x1B51` (encogida, 11 zonas: `[3,6,6,9,9,9,9,9,9,6,6]`).
     - Mitad izquierda (`zone < 8` normal o `zone < 6` encogida) dirige `ball_vx < 0`; mitad derecha dirige `ball_vx > 0`.
4. **Matriz de bloques de la Fase 1 (`0x1BE1 -> 0x1C44`, colision en `0x0D27..0x0E8C`)**:
   - 14 filas $\times$ 14 columnas; cada celda mide `8 px` de ancho (`X = 16 + col * 8`) por `4 px` de alto (`Y = 24 + row * 4`).
   - Filas `0..1`: tipo `4` (muro superior indestructible, `hp = 0`, `14 x 2` celdas).
   - Filas `6..7`: columnas `1..12` tipo `3` (`30 pts`, `hp = 1`, `min_speed_idx = 7`, 24 bloques).
   - Filas `8..9`: columnas `1..12` tipo `2` (`20 pts`, `hp = 1`, `min_speed_idx = 5`, 24 bloques).
   - Filas `10..12`: columnas `1..12` tipo `1` (`10 pts`, `hp = 1`, `min_speed_idx = 0`, 36 bloques).
   - Total destructibles: **84 bloques**.
