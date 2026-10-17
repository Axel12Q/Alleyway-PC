# Alleyway: registro de investigacion y desarrollo
Fecha: 2026-10-17
Estado: infraestructura de captura/comparacion verificada y volcado del usuario disponible en `../Local-Privado/roms/Alleyway (USA).gb`; iniciando ingenieria inversa de la Fase 1.

## Candidato y copia local
- Juego objetivo: Alleyway, Game Boy (1989).
- Copia local del usuario ubicada en `../Local-Privado/roms/Alleyway (USA).gb` (fuera de Git):
  - Tamano: 32768 bytes (`rom_size_code = 0`, `cartridge_type = 0`, `ram_size_code = 0`).
  - Titulo en cabecera: `ALLEY WAY`, `version = 0`, `old_licensee_code = 1`.
  - Checksums: `header_checksum_valid = true` (94), `global_checksum_valid = true` (53662).
  - SHA256: `ed8070e011713527bdc03e2b9cec9f9c4a7e3aaa00e57e8786a186b265da1bb2`.

## Hito 1 (2026-10-17): Linea temporal determinista, trazas y comparador
### Hechos verificados
- **Observado / Verificado (experimento PyBoy `0xFF50` y `PC`)**: La bootrom DMG integrada de PyBoy 2.8.1 mantiene `PC < 0x0100` durante los primeros 64 fotogramas (animacion de 60 fotogramas en `0x0043..0x0070`) y transfiere la ejecucion a `0x0100` en el fotograma 65.
- **Implementado y probado**:
  - `tools/synthetic_rom.py`: construye en memoria un cartucho sintetico de 32 KiB con codigo SM83 sincronizado a VBlank que responde a `0xFF00` (cruceta y boton A) y actualiza variables de prueba en `0xC000..0xC004` y `SCX`/`SCY`.
  - `tools/inspect_rom.py`: valida alineacion de bancos de 16 KiB, tamano esperado, checksums e indicador `pyboy_loadable`.
  - `tools/capture_reference.py`: soporta lineas temporales deterministas (`frame`, `button`, `action`), muestreo de registros hardware (`PC`, `SP`, `LY`, `LCDC`, `STAT`, `SCY`, `SCX`, `BGP`, `WY`, `WX`, `JOYP`, `SB`, `SC`) y watchpoints tipados (`u8`, `i8`, `u16le`, `u16be`, `bcd8`) hacia `trace.jsonl`.
  - `tools/compare_traces.py`: compara `trace.jsonl` y `capture.json`, separando `logic_match` (y reportando el primer fotograma/campo divergente) de `screen_match`.
  - Pruebas automatizadas: `python -m unittest discover -s tests -v` ejecuta 8 pruebas en ~0.12s (incluyendo paridad exacta en corridas repetidas y deteccion de divergencia deliberada en el fotograma 69).

## Herramientas y fuentes de referencia
- SameBoy 1.0.4: `.tools/sameboy/` (SHA256 verificado). https://github.com/LIJI32/SameBoy
- PyBoy 2.8.1: automatizacion e inspeccion determinista. https://github.com/Baekalfen/PyBoy
- Pan Docs: documentacion de hardware DMG. https://gbdev.io/pandocs/
- Incidencia documentada de controles/puerto serie en Alleyway: https://github.com/visualboyadvance-m/visualboyadvance-m/issues/274

## Siguiente paso (Hito 2)
- Ejecutar analisis estatico/dinamico sobre `../Local-Privado/roms/Alleyway (USA).gb` para:
  1. Documentar la secuencia exacta de fotogramas desde el arranque hasta la pantalla de titulo y el inicio de la Fase 1 al pulsar `Start`.
  2. Comprobar el uso de `0xFF00` (`P1/JOYP`), `0xFF01` (`SB`) y `0xFF02` (`SC`) durante la lectura de controles.
  3. Identificar y verificar con experimentos en PyBoy las direcciones de HRAM/WRAM/OAM de: estado de juego, posicion X y velocidad de la paleta, posicion X/Y y velocidad de la bola, vidas, puntuacion y matriz de bloques de la Fase 1.
