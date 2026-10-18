# Mapa de Reutilizacion para Futuros Ports de Game Boy (Guia para Codex)

Este documento define la separacion formal entre la **infraestructura generica reutilizable**
para futuros ports nativos de Game Boy y la **ingenieria inversa especifica** de Alleyway.
Esta disenado para orientar la evolucion hacia una herramienta `ROM -> C -> EXE` (como el prototipo
independiente en `Documents/GB-Native-Lab`), sin modificar dicho prototipo ni introducir dependencias cruzadas.

---

## 1. Mapa de Modulos y Funciones

| Modulo / Archivo | Funciones Clave | Naturaleza | Reutilizable en Otros Juegos? |
|---|---|---|---|
| **`src/gb_tile.h`** | `gb_tile_decode_plane`, `gb_tile_blit` | **Servicio Hardware DMG (PPU)** | **100% Reutilizable**. Decodifica el formato planar estándar 2bpp de Game Boy (16 bytes/tile), paletas DMG (`BGP`, `OBP0`, `OBP1`), flips X/Y, transparencia de color 0 y recorte de pantalla. |
| **`src/gb_apu.h`** | `gb_apu_ch1_init`, `gb_apu_ch1_step`, `gb_apu_ch1_write_reg` | **Servicio Hardware DMG (APU)** | **100% Reutilizable**. Sintetizador PCM mono de 16-bit para Canal 1 con avance en ciclos CPU (4.194304 MHz), Frame Sequencer de 512 Hz, longitud (NR11/NR14), barrido (NR10) y envolvente de volumen (NR12). |
| **`src/gb_audio_backend.h`** | `gb_audio_dispatch_sfx`, `gb_audio_step_frame`, `GbAudioDeviceOps` | **Infraestructura de Audio** | **Reutilizable**. Abstraccion de cola de buffers y driver inyectable (`waveOut`, SDL, mock de pruebas) que desacopla la salida de audio del bucle de simulacion. |
| **`src/gb_scheduler.h`** | `gb_scheduler_init`, `gb_scheduler_advance`, `gb_scheduler_get_frame_audio_samples` | **Servicio de Temporizacion DMG** | **100% Reutilizable**. Reloj monotónico de alta precision a 59.7275 Hz (70224 ciclos/frame a 4.194304 MHz) con acumulador de tiempo real y sintesis fraccionaria de muestras PCM a 44,100 Hz (`738 + 23157/65536` muestras/frame) con deriva acumulada nula. |
| **`tools/inspect_rom.py`** | `inspect_rom`, `parse_header` | **Herramienta de Cartucho** | **100% Reutilizable**. Parseo de cabecera DMG/CGB (0x0100..0x014F), tamaño, bancos de 16 KiB, tipo de cartucho/MBC y validacion de checksums. |
| **`tools/synthetic_rom.py`** | `generate_synthetic_rom` | **Herramienta de Pruebas** | **100% Reutilizable**. Generador de cartuchos sinteticos validos para pruebas unitarias publicas en CI sin depender de ROMs comerciales. |
| **`tools/capture_reference.py`** | `capture_reference`, hooks `0x051F..0x0525` | **Herramienta de Captura** | **Concepto reutilizable**. Registro determinista de inputs de joypad y watchpoints de memoria por tick de logica atomico contra PyBoy/SameBoy. |
| **`tools/compare_traces.py`** | `compare_traces`, `compare_snapshots` | **Herramienta de Verificacion** | **100% Reutilizable**. Comparador formal de secuencias temporales separando discrepancias de logica (`logic_match`) de diferencias de pantalla (`screen_match`). |
| **`tools/scan_ram.py`** | `diff_timelines`, `find_correlated_addresses` | **Herramienta de Analisis** | **100% Reutilizable**. Escaneo diferencial y correlacion temporal para aislar direcciones de memoria de variables en WRAM, HRAM y OAM. |
| **`tools/scenario_diff.py`** | `run_native`, `compare`, `state_runs` | **Arnes Diferencial** | **Reutilizable**. Ejecutor en paralelo de oraculo (PyBoy) vs ejecutable C headless (`--run-scenario`), extrayendo discrepancias per-frame. |

---

## 2. Distincion entre Servicios de Hardware y Reglas Especificas

### A. Servicios de Hardware Reutilizables (Independientes del Juego)
1. **PPU y Decodificacion de Graficos**:
   - Lectura de planos 2bpp planar (`VRAM 0x8000..0x97FF`).
   - Mapeo de paletas DMG de 4 tonos (`BGP = 0xE4`, `OBP0`, `OBP1`).
   - Flags de sprites en OAM (flip X, flip Y, prioridad).
2. **APU y Sintesis de Sonido**:
   - Registros de hardware de Canal 1 (`NR10 = 0xFF10`, `NR11 = 0xFF11`, `NR12 = 0xFF12`, `NR13 = 0xFF13`, `NR14 = 0xFF14`).
   - Frame Sequencer a 512 Hz y calculo de frecuencia: `f = 131072 / (2048 - raw_freq)`.
3. **Temporizacion y Reloj Monotónico**:
   - Cadencia de fotogramas DMG a ~59.7275 Hz (periodo ~16.7428 ms).
   - Acumulador de tiempo en punto fijo para evitar saltos o aceleraciones ante tirones de la ventana.
   - Generacion de muestras fraccionarias para mixers de audio estandar (44.1 kHz o 48 kHz).
4. **Inspeccion y Verificacion**:
   - Linea temporal determinista de joypad (Joyp Raw / Joyp Edge).
   - Harness diferencial para comprobar ejecuciones nativas contra emuladores de referencia.

### B. Reglas y Direcciones Especificas de Alleyway (Propias del Titulo)
1. **Direcciones de ROM**:
   - `0x1B87`: Propiedades de bloques (puntos, dureza, velocidad minima, color).
   - `0x1B41` / `0x1B51`: Zonas de rebote de paleta (normal de 24 px / encogida de 16 px).
   - `0x1BE1`: Punteros y flags de descriptores de las 32 fases.
   - `0x1B71`: Tabla de duracion y premios de bonus multi-ronda.
   - `0x1B5D`: Tabla de umbrales para vidas extra (1-UP: 10k a 90k).
   - `0x11EE`: Punteros a vectores de velocidad en punto fijo 8.8.
   - `0x4075`: Punteros a los 8 patrones de desplazamiento horizontal de 20 bytes.
   - `0x02C1`: Formato de lista de despliegue VRAM (vaddr, control/modo, datos).
   - `0x4B75`: Offset en ROM donde se ubica el banco contiguo de 384 tiles (6 KiB).
2. **Variables en HRAM / WRAM**:
   - `0xFFA4`: Registro de estado principal (1..12).
   - `0xFFA1`: Semilla del generador pseudoaleatorio (`prng = (prng + 0x41) & 0xFF`).
   - `0xFFA3`: Bandera `vram_brick_dirty` para coordinar VRAM con VBlank.
   - `0xFFA9`: `row_offset` para la matriz de 40 filas.
   - `0xFFB6` / `0xFFB8`: Coordenadas de bola (`ball_x`, `ball_y`).
   - `0xFFC0` / `0xFFC2`: Posicion y ancho de paleta (`paddle_x`, `paddle_w`).
   - `0xFFC8`: Contador de bloques restantes (`bricks_left`).
3. **Mecanicas de Juego Propias**:
   - Aritmetica en punto fijo 8.8 big-endian de la bola.
   - Doble pase de sondas de colision con snap de coordenadas.
   - Descenso vertical del mural gobernado por **rebotes de paleta** (`scroll_timer` decrementa en rebote, no por tiempo).
   - Espera `HALT 0x023D` de 1 VBlank en colisiones multi-bloque en esquina.

---

## 3. Dependencias de ROM y Datos Privados

- **Aislamiento en Git**: El repositorio **no contiene ni debe contener ningun byte comercial ni recurso extraido**.
- **Carga en Tiempo de Ejecucion**: `src/main.c` carga la ROM local proporcionada por el usuario (`fread` de 32 KiB) en `s->rom` y lee directamente las tablas de vectores, paletas, graficos y sonidos.
- **Fixtures Sinteticos**: Todas las pruebas publicas y flujos de CI se ejecutan sobre cartuchos sinteticos generados en memoria (`build_synthetic_stage1_rom_bytes`), preservando la legalidad y reproducibilidad publica.

---

## 4. Pasos Necesarios para Separar Hacia un Generador ROM -> C -> EXE

Para que una herramienta como `GB-Native-Lab` pueda generalizar este enfoque a otros juegos de Game Boy:

1. **Extraer una biblioteca estatica de runtime (`libgb_runtime`)**:
   - Mover `gb_tile.h`, `gb_apu.h`, `gb_audio_backend.h` y `gb_scheduler.h` a una carpeta de libreria compartida independiente.
   - Proporcionar una capa de abstraccion de plataforma (`platform_win32.c` / `platform_sdl.c`) con callbacks limpios para Joypad, Framebuffer y Audio.
2. **Desacoplar la extraccion de datos del cartucho**:
   - Crear un analizador/extractor estatico que lea la ROM de entrada, identifique los bancos de tiles y mapas de tiles, y genere estructuras C o archivos binarios de recursos empaquetados.
3. **Mapear las rutinas de logica**:
   - Traducir o transcribir las maquinas de estado y fisica del juego objetivo conectandolas a los servicios de la libreria de runtime (VBlank ISR, lectura de inputs, render de mapas y playback de sonido).
