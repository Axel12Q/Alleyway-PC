# Mapa de Reutilización para Futuros Ports de Game Boy (Guía para Codex)

Este documento define la separación entre la **infraestructura candidata a reutilización**
para futuros ports nativos de Game Boy y la **ingeniería inversa específica** de Alleyway.
Está diseñado para orientar la evaluación técnica hacia una herramienta `ROM -> C -> EXE` (como
el prototipo experimental en `Documents/GB-Native-Lab`), sin modificar dicho prototipo ni introducir
dependencias cruzadas.

---

## 1. Inventario Contrastado de Módulos y Funciones

| Módulo / Archivo | Funciones / Símbolos Reales | Clasificación | Alcance, Limitaciones y Pruebas Pendientes |
|---|---|---|---|
| **`src/gb_tile.h`** | `gb_tile_decode_indices`, `gb_tile_decode_pixels`, `gb_tile_blit` | **Candidato reutilizable** (Decodificador / Blitter 2bpp) | **NO es una PPU completa**. Decodifica baldosas individuales planares estándar 2bpp (16 bytes/tile), paletas DMG (`BGP`, `OBP0`, `OBP1`), flips X/Y, transparencia y clipping sobre buffer lineal. **Limitaciones**: No emula registros PPU (`LCDC`, `STAT`, `LY`, `LYC`, `SCX`, `SCY`, `WY`, `WX`), modos de renderizado 0..3, prioridades complejas BG/OBJ ni interrupciones STAT. |
| **`src/gb_apu.h`** | `gb_apu_ch1_init`, `gb_apu_ch1_step`, `gb_apu_ch1_write_reg`, `gb_apu_ch1_trigger` | **Candidato reutilizable** (Sintetizador APU Canal 1) | **Parcial**. Sintetizador PCM mono 16-bit para Canal 1 (onda cuadrada con barrido) con avance por ciclos CPU (4.194304 MHz), Frame Sequencer de 512 Hz, longitud (NR11/NR14), barrido (NR10) y envolvente (NR12). **Limitaciones**: No prueba compatibilidad universal con el catálogo DMG. Faltan por completo Canal 2 (pulso 2), Canal 3 (onda arbitraria/sampleada) y Canal 4 (ruido), así como el control global de canales (`NR50`, `NR51`, `NR52`). |
| **`src/gb_audio_backend.h`** | `gb_audio_step_frame`, `GbAudioDeviceOps`, `GbAudioContext`, `gb_audio_dispatch_sfx` | **Mixto** (Infraestructura de audio + reglas Alleyway) | **Requiere desacoplamiento**: La máquina de estados de cola (`GbAudioBufferState`) y la interfaz inyectable `GbAudioDeviceOps` (`open`, `write`, `is_buffer_done`, `reset`, `close`) son genéricas. Sin embargo, `gb_audio_dispatch_sfx` contiene **direcciones de ROM específicas de Alleyway** (`0x701A`, `0x6FDE`, `0x6FBB`, `0x6FCA`) y reglas de SFX que deben extraerse fuera del backend. |
| **`src/gb_scheduler.h`** | `gb_scheduler_init`, `gb_scheduler_advance`, `gb_scheduler_get_frame_audio_samples`, `gb_scheduler_bonus_units_to_ticks` | **Candidato reutilizable** (Temporización y Audio Fraccionario) | Reloj monotónico DMG a ~59.7275 Hz (70224 ciclos/frame a 4.194304 MHz) con acumulador de tiempo real desacoplado y sintetizador de muestras fraccionarias a 44,100 Hz (`738 + 23157/65536` muestras/frame). **Limitaciones**: La función `gb_scheduler_bonus_units_to_ticks` contiene la conversión de unidades HUD de Alleyway (1 unidad = 32 VBlanks) y debe ser separada. |
| **`tools/inspect_rom.py`** | `inspect`, `inspect_file` | **Candidato reutilizable** (Inspección de Cartuchos) | Lee y valida cabeceras DMG/CGB (0x0100..0x014F), tamaño, alineación en bancos de 16 KiB, tipo de cartucho/MBC y checksums de cabecera y global. **Limitaciones**: Solo inspecciona metadatos; no decodifica código SM83 ni analiza vectores de interrupción. |
| **`tools/synthetic_rom.py`** | `build_synthetic_rom`, `_interactive_program`, `SYNTHETIC_WATCH_SPEC` | **Candidato reutilizable** (Generador de Fixtures) | Genera cartuchos sintéticos de 32 KiB válidos con bucle VBlank interactivo para pruebas unitarias sin ROMs comerciales. **Limitaciones**: El programa SM83 inyectado es un fixture mínimo para pruebas de herramientas, no un juego real. |
| **`tools/compare_traces.py`** | `compare_traces`, `compare_snapshots`, `parse_timeline` | **Candidato reutilizable** (Comparador de Trazas) | Compara secuencias de eventos y watchpoints separando `logic_match` de `screen_match`. Totalmente genérico para cualquier traza en formato JSONL. |
| **`tools/scan_ram.py`** | `diff_timelines`, `find_correlated_addresses` | **Candidato reutilizable** (Escáner Diferencial) | Análisis de correlación temporal sobre dumps de memoria para localizar variables en WRAM, HRAM y OAM. Genérico para cualquier volcado de oráculo. |
| **`tools/scenario_diff.py`** | `run_native`, `compare`, `state_runs`, `SCENARIOS`, `run_stage_curtain_transition_diff` | **Mixto** (Motor diferencial + escenarios Alleyway) | El concepto de ejecución dual (PyBoy vs ejecutable nativo headless `--run-scenario`) es reutilizable. Sin embargo, el archivo actual tiene **direcciones y watchpoints específicos de Alleyway** (`0xFFA4`, `0xFF8C`, `0xFFB6`, `0xFFC8`, inyecciones de ladrillos y listas de escenarios de Alleyway). |

---

## 2. Distinción entre Servicios de Hardware y Reglas Específicas

### A. Servicios de Hardware Reutilizables (Candidatos a Biblioteca de Runtime)
1. **Decodificación de Tiles**: Conversión planar 2bpp a índices 0..3 o píxeles con paleta DMG.
2. **Síntesis APU Canal 1**: Emulación precisa de envolvente, barrido y longitud por ciclos de CPU.
3. **Temporización de Simulación**: Mantenimiento de la cadencia DMG (~59.7275 Hz) mediante acumulador monotónico en punto fijo, desacoplada de la tasa de refresco del monitor del anfitrión.
4. **Mixer de Audio Fraccionario**: Generación sin deriva acumulada para frecuencias estándar (44.1 kHz).
5. **Inspección de Cartucho y Fixtures Sintéticos**: Validación de ROMs y arnés de pruebas sin datos propietarios.

### B. Reglas, Direcciones y Formatos Propios de Alleyway
1. **Direcciones fijas de ROM de Alleyway**:
   - `0x1B87`: Propiedades de bloques (puntos, dureza, velocidad mínima, color).
   - `0x1B41` / `0x1B51`: Zonas de rebote de paleta (normal de 24 px / encogida de 16 px).
   - `0x1BE1`: Punteros y flags de descriptores de las 32 fases.
   - `0x1B71`: Tabla de duración y premios de bonus multi-ronda.
   - `0x1B5D`: Tabla de umbrales para vidas extra (1-UP: 10k a 90k).
   - `0x11EE`: Punteros a vectores de velocidad en punto fijo 8.8.
   - `0x4075`: Punteros a los 8 patrones de desplazamiento horizontal de 20 bytes.
   - `0x02C1`: Formato de lista de despliegue VRAM (vaddr, control/modo, datos).
   - `0x4B75`: Offset en ROM donde se ubica el banco contiguo de 384 tiles (6 KiB).
2. **Variables de Memoria (HRAM / WRAM)**:
   - `0xFFA4` (estado 1..12), `0xFFA1` (PRNG), `0xFFA3` (vram_brick_dirty), `0xFFA9` (row_offset), `0xFFB6`/`0xFFB8` (coordenadas de bola), `0xFFC0`/`0xFFC2` (posición y ancho de paleta), `0xFFC8` (bricks_left).
3. **Lógica de Juego no Generalizable**:
   - Aritmética de bola en punto fijo 8.8 big-endian.
   - Sondas de colisión específicas contra matriz de celdas de 8x4 px.
   - Descenso vertical gobernado por **rebotes de paleta** (`scroll_timer` decrementa en rebote, no por fotogramas).
   - Espera `HALT 0x023D` de 1 VBlank en colisiones multi-bloque en esquina.

---

## 3. Dependencias de ROM y Datos Privados

- **Principio de Aislamiento**: El repositorio no contiene ni debe contener ningún byte comercial ni dato extraído de Alleyway.
- **Carga Dinámica en Tiempo de Ejecución**: `src/main.c` lee la ROM local mediante `fread` (32 KiB) y accede a las tablas mediante punteros directos.
- **Pruebas Públicas**: Todas las pruebas públicas de regresión en CI utilizan cartuchos sintéticos generados en memoria (`build_synthetic_stage1_rom_bytes`), preservando la legalidad del proyecto.

---

## 4. Retos y Requisitos Reales para un Generador `ROM -> C -> EXE`

La transcripción manual de física y máquinas de estado (como la realizada en este laboratorio para Alleyway) **NO constituye una conversión automática ni un compilador estático**. Para que una herramienta automatizada (como el prototipo `GB-Native-Lab` de Codex) pueda procesar ROMs comerciales completas, se requeriría resolver arquitectónicamente:

1. **Traducción o Recompilación de Instrucciones SM83**:
   - Traducción de opcodes binarios a funciones o bloques C equivalentes, modelando flags (`Z`, `N`, `H`, `C`) y saltos indirectos (`JP HL`, `CALL`).
2. **Bus de Memoria y Manejo de Hardware**:
   - Simulación del mapa de memoria (ROM banking MBC1/3/5, WRAM banking, HRAM, puertos de E/S).
   - Disparo y sincronización de interrupciones (VBlank en `0x0040`, LCD STAT en `0x0048`, Timer en `0x0050`).
3. **PPU y APU Completas**:
   - Una PPU completa que maneje la línea de escaneo (`LY`), ventanas, scroll fino y prioridades de sprites en tiempo real (más allá del decodificador estático de tiles de `gb_tile.h`).
   - Una APU completa con los 4 canales y mezcla de audio.
4. **Separación de la Biblioteca de Runtime**:
   - Extraer los componentes genéricos (`gb_tile.h`, `gb_apu.h`, `gb_scheduler.h`) en una biblioteca base independiente (`libgb_runtime`), separando estrictamente cualquier tabla o regla de juego específica.
