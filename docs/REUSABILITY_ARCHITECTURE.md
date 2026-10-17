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

Estado de desarrollo de cada componente generico:

| Componente | Archivo / Ubicacion | Estado | Descripcion / Alcance real |
|---|---|---|---|
| **Inspeccion e integridad de cartucho** | `tools/inspect_rom.py` | **Implementado** | Valida cabecera DMG/CGB, alineacion de 16 KiB, checksums de cabecera/global y carga en PyBoy. (Nota: `src/gb_cart.h` es una cabecera en C propuesta a futuro). |
| **Linea temporal determinista y captura de trazas** | `tools/capture_reference.py`, `tools/compare_traces.py` | **Implementado** | Registro de entradas por fotograma, atomic logic ticks via hooks (`START:END`), hashes PPM de framebuffer y trazas JSONL. Comparador desacoplado. |
| **Escaneo diferencial de memoria** | `tools/scan_ram.py` | **Implementado** | `diff_timelines` y `find_correlated_addresses` para aislar variables correlacionadas en WRAM, HRAM y OAM. |
| **Decodificador generico de tiles 2bpp** | `src/gb_tile.h` | **Implementado** | Decodificador planar C11 de 16 bytes a indices 0..3 o ARGB/XRGB con paletas DMG, flips X/Y, transparencia y clipping. Probado con harness C (`tests/test_c_modules.c`). |
| **Sintesis APU Canal 1 (Onda cuadrada + sweep)** | `src/gb_apu.h` | **Implementado** | Sintesis PCM mono 16-bit con avance en ciclos CPU (4.19 MHz), Frame Sequencer de 512 Hz, longitud (NR11/NR14), barrido (NR10) y envolvente (NR12). |
| **Capa de plataforma y presentacion Win32** | `src/main.c` (Win32/GDI + waveOut) | **Parcial** | Framebuffer 160x144 con escalado entero, entradas de teclado a Joypad y backend waveOut de 4 buffers a 44.1 kHz. (Propuesto: desacoplar a `src/platform_win32.c` / SDL). |
| **Sintetizador APU Canales 2, 3 y 4** | Futuro `src/gb_apu.h` | **Propuesto** | Canal 2 (pulso sin barrido), Canal 3 (onda arbitraria/sampleada) y Canal 4 (ruido blanco pseudoaleatorio). |


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
