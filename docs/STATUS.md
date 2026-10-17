# Estado

2026-10-17: preparacion inicial en Windows.

Implementado: ventana nativa de diagnostico, inspector de cabecera de cartucho,
captura de referencia sin entradas, instrucciones compartidas.

Pendiente: volcado propio y version, analizar codigo y RAM, medir movimiento,
extraer localmente datos del primer nivel y comparar implementacion nativa.
No hay gameplay implementado ni compatibilidad con el juego demostrada.

Validacion local: compilacion C con Zig 0.16.0 y warnings como errores; prueba
del framebuffer aprobada; dos pruebas de cabecera/checksums aprobadas.
PyBoy 2.8.1 ejecuto 10 frames de una ROM sintetica propia y genero PPM/JSON.
SameBoy 1.0.4 descargado y SHA256 contrastado con SHA2-256SUMS oficial;
su interfaz no se ha probado con Alleyway. PyBoy avisa que sus capturas PNG
opcionales requieren Pillow; nuestro exportador PPM funciona sin esa libreria.
Herramientas y dependencias son locales; sin cambios al PATH ni instalaciones
globales. Git inicializado en main, sin remoto.

Primera meta: una fase, con trazas de referencia reproducibles y comparacion
de posiciones, rebotes, bloques, puntuacion y vidas.
