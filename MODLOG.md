# Alleyway: estudio de viabilidad
Fecha: 2026-10-17
Estado: investigacion documental; ningun binario del juego analizado ni port construido.

## Candidato
Alleyway, Game Boy (1989). Nintendo describe 32 fases, bloques moviles y fases de bonificacion.
No se encontro port nativo de PC ni decompilacion en las busquedas realizadas. Esto no demuestra inexistencia absoluta. Hay conversiones comunitarias para otras plataformas y versiones emuladas; deben distinguirse de un port nativo.

## Ruta propuesta
Ingenieria inversa guiada por ejecucion y reimplementacion nativa en C/C++ con SDL. No inventar una fisica de Breakout y presentarla como port fiel.
1. Identificar version y hash del volcado propio del usuario.
2. Reproducir arranque y controles en emulador de referencia.
3. Localizar posiciones, velocidades, colisiones, puntuacion y datos de un nivel.
4. Registrar entradas y estados por fotograma.
5. Implementar una fase y comparar trazas; ampliar solo tras validar.
6. Distribuir codigo propio y herramientas de importacion; mantener ROM y datos extraidos fuera del repositorio.

## Herramientas evaluadas documentalmente
- SameBoy: referencia y depuracion. https://github.com/LIJI32/SameBoy
- PyBoy: automatizacion Python e inspeccion de estado. https://github.com/Baekalfen/PyBoy
- Pan Docs: documentacion del hardware. https://gbdev.io/pandocs/
- gb-recompiled: posible experimento de recompilacion a C con runtime SDL2. No se verifico compatibilidad con Alleyway ni se recomienda como ruta garantizada. https://github.com/arcanite24/gb-recompiled
- Disassembler: alternativa a evaluar si el depurador no basta. https://github.com/ericwoude/gameboy-disassembler

## Evidencia y fuentes
Nintendo, fuente primaria sobre plataformas y contenido:
https://www.nintendo.com/es-es/Juegos/Game-Boy/Alleyway-275451.html
Incidencia primaria de emulador: controles y puerto serie son un riesgo que conviene probar, no una explicacion del codigo verificada por nosotros:
https://github.com/visualboyadvance-m/visualboyadvance-m/issues/274
Base de conocimiento consultada:
https://github.com/rehan-remade/universal-modder/blob/main/knowledge/INDEX.md
No se identifico una nota especifica de Alleyway en la consulta.

## Consultas
Alleyway Game Boy native PC port remake github
Alleyway disassembly github gameboy
Alleyway decompilation
Alleyway remake Windows github
Solar Striker PC port / disassembly github
Mole Mania decompilation PC port github
Kwirk PC port github disassembly

## Limites y siguiente paso
No se instalaron herramientas, no se descargaron ROMs, no se ejecutaron juegos y no se capturaron pantallas.
Falta una copia propia del juego y su volcado para verificar viabilidad sobre el binario real.
Primera meta: una fase ejecutable en Windows con comportamiento medido contra el original.
