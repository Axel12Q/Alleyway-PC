# Alleyway PC research

Laboratorio para investigar Alleyway (Game Boy) y construir una reimplementacion
nativa verificable. Estado: infraestructura; aun no existe un port jugable.

## Empezar en Windows

Python 3.12 y uv deben estar disponibles. Desde esta carpeta:

```powershell
uv venv
uv pip sync requirements-dev.lock
.\.venv\Scripts\python.exe tools/build.py
.\build\alleyway-lab.exe --smoke-test
.\build\alleyway-lab.exe
```

La ventana muestra un patron sintetico de 160x144, escalado por enteros;
Escape la cierra. No contiene graficos, fisica ni niveles del original.
Usamos Win32/GDI para validar la compilacion sin depender todavia de SDL.
La futura capa de plataforma podra usar SDL para otras plataformas.

Para descargar SameBoy en otro equipo: `powershell -File tools/setup_sameboy.ps1`.
Pruebas del inspector: `python -m unittest discover -s tests -v`.

## Copia local del juego

Coloca tu propio volcado en `local/roms/` (ignorado por Git). No se descarga aqui.

```powershell
.\.venv\Scripts\python.exe tools/inspect_rom.py local/roms/alleyway.gb
.\.venv\Scripts\python.exe tools/capture_reference.py local/roms/alleyway.gb --frames 120
```

El inspector informa cabecera y checksums; no certifica autenticidad del juego.
La captura avanza sin pulsar botones: sirve para registrar arranque, no gameplay.
Los resultados quedan en `local/captures/`. No subirlos al repositorio.
SameBoy portable esta en `.tools/sameboy/` en la preparacion local.

## Colaboracion

Lee AGENTS.md, docs/STATUS.md y docs/COLLABORATION.md.
Primera meta: medir y reproducir una sola fase; nunca sustituir mediciones por
una fisica generica de Breakout.

Fuentes: https://github.com/LIJI32/SameBoy,
https://github.com/Baekalfen/PyBoy, https://gbdev.io/pandocs/,
https://www.nintendo.com/es-es/Juegos/Game-Boy/Alleyway-275451.html.
El nombre Alleyway pertenece a sus titulares. Proyecto experimental no oficial;
sin ROMs ni recursos comerciales incluidos. Asistencia de IA documentada.

## Trabajo activo
El proyecto compartido esta en Documents/Alleyway-PC-Trabajo. Antigravity trabaja
en su propia carpeta y rama antigravity/development. Ver docs/COLLABORATION.md
y docs/HANDOFF_ANTIGRAVITY.md antes de iniciar otro agente.
