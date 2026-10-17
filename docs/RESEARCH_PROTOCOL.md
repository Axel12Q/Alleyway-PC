# Protocolo de medicion

No se ha analizado la ROM todavia. Todas las direcciones estan por descubrir.

1. Identificar el volcado con SHA256, cabecera y revision.
2. Fijar version del emulador, configuracion y secuencia de arranque.
3. Localizar estado de partida y coordenadas mediante cambios observados.
4. Validar cada direccion con movimientos repetidos, pausas y reinicios.
5. Medir velocidad con diferencias de coordenadas por tick de logica; no asumir
   que cada fotograma visual equivale a un paso de simulacion.
6. Identificar rebotes en paredes, bloques y distintas zonas de la paleta.
7. Repetir entradas identicas desde el mismo estado y comparar las trazas.

Formato propuesto de trazas JSONL (requiere acuerdo antes de implementarlo):
frame, inputs, paddle_x, ball_x, ball_y, lives, score, state.
Cada coordenada debe documentar unidad, signo, origen, precision y momento de
muestreo. No suponer que la velocidad es flotante ni que un pixel equivale a
una unidad interna.

Registrar evidencia por hallazgo: observacion, hipotesis, experimento y resultado.
Validar controles/puerto serie al inicio por el reporte conocido:
https://github.com/visualboyadvance-m/visualboyadvance-m/issues/274

La primera aceptacion sera un conjunto de entradas reproducible que produzca
las mismas posiciones, colisiones y cambios de bloques en original y port.
Comparar imagenes por separado: un framebuffer igual no prueba logica correcta.
