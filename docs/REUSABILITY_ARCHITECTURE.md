# Arquitectura de Reutilizacion para Ports de Game Boy

Este documento define la separacion formal entre la **infraestructura generica reutilizable**
para futuros ports de Game Boy y la **ingenieria inversa especifica** requerida por cada titulo.

## 1. Distincion de Enfoques Tecnicos

| Enfoque | Definicion | Rol en el Proyecto |
|---|---|---|
| **Reimplementacion Nativa Verificable** | Codigo nativo en C que implementa las formulas, maquinas de estado y fisica medidas, cargando recursos del usuario en tiempo de ejecucion. | **Enfoque central del proyecto.** Permite maximo rendimiento, portabilidad nativa y codigo limpio y mantenible. |
| **Emulacion Completa** | Virtualizacion del hardware completo (CPU SM83, PPU ciclo a ciclo, timers, interrupciones, APU). | **Oraculo de referencia.** Usado para medir y generar trazas de validacion (PyBoy, SameBoy), pero **no** embebido en el ejecutable final. |
| **Recompilacion Estatica** | Traduccion automatizada binario-a-binario o binario-a-C de la ROM completa. | No prometida ni asumida; requiere manejo complejo de automodificacion y timing de hardware. |

---

## 2. Componentes Genericos Reutilizables (Para cualquier juego de Game Boy)

1. **Inspeccion e integridad de cartucho (`tools/inspect_rom.py`, `src/gb_cart.h`)**:
   - Validacion de cabecera DMG/CGB, alineacion de bancos de 16 KiB, suma de comprobacion de cabecera y global, decodificacion de MBC y capacidades RAM.
2. **Linea temporal determinista y captura de trazas (`tools/capture_reference.py`, `tools/compare_traces.py`)**:
   - Registro frame-a-frame de entradas (`button:action`), hashes de framebuffer (`PPM`) y trazas JSONL de memoria/registros.
   - Comparador que aisla divergencias logicas de divergencias de renderizado.
3. **Escaneo diferencial de memoria (`tools/scan_ram.py`)**:
   - `diff_timelines` y `find_correlated_addresses` para aislar coordenadas y contadores en WRAM (`0xC000..0xDFFF`), HRAM (`0xFF80..0xFFFE`) y OAM (`0xFE00..0xFE9F`).
4. **Decodificador generico de tiles 2bpp (`src/gb_tile.h`, `tools/decode_tiles.py`)**:
   - Formato estandar Game Boy: 16 bytes por tile de 8x8 (2 planos entrelazados por fila de 8 pixeles).
   - Mapeo de indices 0..3 a paletas de 4 colores configurables.
5. **Capa de plataforma y tiempo (`src/platform_win32.c`, futura abstraccion SDL)**:
   - Framebuffer de 160x144, escalado entero arbitrario, reloj de 59.73 Hz / 60 Hz, mapeo de teclado a joypad DMG.
6. **Sintesis y eventos de audio APU**:
   - Mapeo de registros de los 4 canales DMG (Square 1 con barrido, Square 2, Wave programable, Noise) a sintesis PCM en tiempo real.

---

## 3. Componentes Especificos por Titulo (Requieren ingenieria inversa dedicada)

1. **Ubicacion y formato de tablas de datos**:
   - En Alleyway: tablas de velocidad a 8.8 (`0x11EE`), rebotes por zonas de paleta (`0x1B41`/`0x1B51`), propiedades de bloques (`0x1B87`) y descriptores de fase (`0x1BE1`). En otros juegos (ej. Solar Striker, Tetris) estas tablas tendran formatos y direcciones totalmente distintas.
2. **Maquina de estados de juego**:
   - Transiciones titulo -> demo -> saque -> juego activo -> muerte -> cambio de fase (`0xFFA4` en Alleyway).
3. **Fisica y colisiones internas**:
   - Integracion de coordenadas en punto fijo (8.8 big-endian en Alleyway) y sondas especificas contra la matriz de celdas.
4. **Driver de audio del cartucho**:
   - El formato de las secuencias musicales y efectos en la ROM especifica, antes de traducirse a escrituras a los registros hardware de la APU (`0xFF10..0xFF3F`).
