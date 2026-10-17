# Estado

2026-10-17: preparacion inicial en Windows y relevo activo en `antigravity/development`.

## Hito 1 completado (infraestructura determinista de trazas y verificacion de metadatos)
- Implementado generador de cartucho sintetico interactivo (`tools/synthetic_rom.py`) con bucle sincronizado a VBlank (`0xFF44 == 144`), lectura de cruceta/botones via `0xFF00` y desplazamiento `SCX`/`SCY` para pruebas deterministas sin datos comerciales.
- Ampliado `tools/inspect_rom.py` con validacion de alineacion de bancos (16 KiB), correspondencia de tamano declarado en cabecera, codigos de licenciatario/destino y comprobacion de carga en PyBoy (`pyboy_loadable`).
- Ampliado `tools/capture_reference.py` (esquema v2) con:
  - Linea temporal determinista de entradas (`--inputs`: `frame`, `button`, `action` `press`/`release`) validada y canonizada con hash `inputs_sha256`.
  - Grabacion por fotograma en `trace.jsonl` de registros hardware (`PC`, `SP`, `LY`, `LCDC`, `STAT`, `SCY`, `SCX`, `BGP`, `WY`, `WX`, `JOYP`, `SB`, `SC`, `bootrom_unmapped`) y watchpoints configurables (`--watch` / `--watch-file` con tipos `u8`, `i8`, `u16le`, `u16be`, `bcd8`).
- Implementado comparador de capturas y trazas (`tools/compare_traces.py`) que separa la paridad logica (`logic_match` y primer fotograma/campo divergente) de la comparacion de imagen (`screen_match`).
- Suite de pruebas ampliada a 8 tests (`tests/test_inspect_rom.py` y `tests/test_capture_and_compare.py`), incluyendo ejecuciones repetidas identicas y deteccion exacta de divergencia deliberada en el fotograma 69.

## Disponibilidad del volcado del usuario (fuera de Git)
- Verificado en `../Local-Privado/roms/Alleyway (USA).gb` (copia de `C:/Users/maric/Documents/ROMS/Alleyway (USA).gb`, mantenida estrictamente fuera del repositorio):
  - `bytes`: 32768 (ROM ONLY, `cartridge_type = 0`, sin MBC ni RAM externa).
  - `title`: `ALLEY WAY`, `version`: 0, `old_licensee_code`: 1.
  - `header_checksum_valid`: `true` (94), `global_checksum_valid`: `true` (53662).
  - `sha256`: `ed8070e011713527bdc03e2b9cec9f9c4a7e3aaa00e57e8786a186b265da1bb2`.
  - Esto confirma consistencia interna del archivo para iniciar la ingenieria inversa; no certifica procedencia comercial.

## Hallazgos verificados sobre el entorno
- **Observado / Verificado**: La bootrom DMG interna por defecto de PyBoy 2.8.1 tarda 65 fotogramas en desmapear `0xFF50` y saltar a `0x0100`. Cualquier captura de menos de 65 fotogramas permanece dentro de la bootrom (`PC < 0x0100`).

## Siguiente paso inmediato (Hito 2)
- Analizar `../Local-Privado/roms/Alleyway (USA).gb` con PyBoy y herramientas de escaneo de RAM/desensamblado local:
  1. Verificar el comportamiento de arranque, controles y puerto serie (`0xFF00`, `0xFF01`, `0xFF02`).
  2. Localizar y verificar direcciones reales de estado de partida, posicion/velocidad de paleta y bola, vidas, puntuacion y mapa de bloques de la Fase 1.
