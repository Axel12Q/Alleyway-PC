ALLEYWAY PC — 0.1.0 PREVIEW — WINDOWS x64

INICIO
1. Extrae el ZIP completo en una carpeta.
2. Abre Alleyway-PC.exe.
3. Selecciona tu archivo local Alleyway (USA).gb (32 KiB).
4. Pulsa Enter y luego Espacio para sacar.

No necesitas Python, Zig, un IDE ni instalar dependencias para jugar.
No se incluye ROM. Solo está admitida la versión USA verificada (CRC32
5CC01586; SHA-256 ed8070e011713527bdc03e2b9cec9f9c4a7e3aaa00e57e8786a186b265da1bb2).
Los archivos con otra identidad se rechazan. No se guardan ni modifican ROMs.

CONTROLES
Enter: iniciar / pausar / reanudar.
Flechas izquierda y derecha, o A/D: mover pala.
Espacio o Z: sacar; mantener junto con dirección para mover más rápido.
Shift o X junto con dirección: movimiento lento.
M: silenciar / activar audio.
R: reinicio directo a saque de fase 1. T: volver al título.
Esc: salir. Cambiar de ventana congela el juego y apaga el sonido pendiente;
al volver, continúa sin teclas atascadas.

ALCANCE
Reimplementación nativa de investigación, NO conversor automático ROM->EXE.
Esta es una versión preliminar pública, no una réplica audiovisual completa.
La lógica se compara con el original en escenarios de juego y transiciones.
Los 12 efectos primarios tienen secuencia de registros de canal 1 contrastada.
Solo está sintetizado el canal 1: faltan canales 2, 3, 4 y música completa.
Algunas animaciones intermedias y escenas conservan adaptaciones visuales.
No se ha validado una partida manual continua que complete las 32 fases.
Las pruebas de registros no afirman igualdad de fase PCM o fidelidad ciclo a ciclo.

PROBLEMAS
Si no inicia, ejecuta desde una consola para ver mensajes y comprueba la ROM.
Puedes usar: Alleyway-PC.exe --rom "ruta\Alleyway (USA).gb"
Si usas rutas con caracteres no ASCII, selecciona el archivo desde el diálogo.
El programa no escribe partidas guardadas. Para desinstalar, elimina su carpeta.

CODIGO Y REPORTES
https://github.com/Axel12Q/Alleyway-PC
Incluye versión, fase, acción y forma de reproducir el problema; no adjuntes ROMs.
Desarrollo asistido por OpenAI Codex y Google Antigravity (modelos Gemini/Claude).
Herramientas de comprobación: PyBoy; compilación: Zig/Clang; Win32 GDI/waveOut.
Software no oficial, sin afiliación con Nintendo. Licencia de nuestro código: MIT.
