# Checkpoint de Desarrollo: Alleyway PC Research

**Fecha**: 2026-10-17  
**Rama activa**: `antigravity/development`  
**Último commit base**: `61d8cb8` (Hito 14)  
**Estado de pruebas**: **44 pruebas automatizadas ejecutadas y aprobadas (`44 OK, 0 expectedFailure`)**  

---

## 1. Instrucciones de Ejecución Local

### Prerrequisitos y Compilación
Desde la raíz del repositorio, con Python 3.12 y el entorno virtual activo:

```powershell
# Sincronizar dependencias del entorno
uv pip sync requirements-dev.lock

# Compilar el ejecutable nativo en C (utiliza zig cc C11 con -O2)
.\.venv\Scripts\python.exe tools/build.py

# Comprobación rápida de integridad (headless)
.\build\alleyway-lab.exe --smoke-test

# Ejecución completa de la suite de 44 pruebas
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
```

### Lanzador Local (con o sin ROM comercial)
Para jugar o inspeccionar visualmente el ejecutable interactivo en Windows:

```powershell
# Mediante el script de PowerShell:
.\launch.ps1

# O directamente con Python:
.\.venv\Scripts\python.exe tools/launch.py

# O especificando una ruta arbitraria a una ROM:
.\.venv\Scripts\python.exe tools/launch.py "C:\ruta\a\tu\Alleyway (USA).gb"
```

El lanzador:
- Busca automáticamente en `local/roms/Alleyway (USA).gb` y `../Local-Privado/roms/Alleyway (USA).gb` sin copiar, mover, publicar ni modificar el archivo de ROM ni archivos de guardado.
- Si no encuentra la ROM, arranca de forma segura en **modo diagnóstico fallback** (160x144, patrón de prueba, indicándolo en la barra de título de la ventana y en la consola).
- Si encuentra la ROM, inicia en la **Pantalla de Título auténtica** (`State 2`, *"PUSH START"*), con gráficos decodificados directamente desde el cartucho local.

### Controles de la Ventana Interactiva
| Tecla | Acción en el Juego |
|---|---|
| **Enter** | `Start`: Iniciar partida desde Título / Pausar / Reanudar / Reiniciar tras Game Over |
| **Espacio** / **Z** | Botón `A`: Sacar la bola / Acelerar la paleta a 5 px/fotograma |
| **Flecha Izquierda / Derecha** (o **A** / **D**) | Mover la paleta (velocidad normal: 3 px/fotograma) |
| **Shift** / **X** | Botón `B`: Movimiento de precisión lenta (1 px/fotograma) |
| **R** | Reiniciar directamente a la pantalla de saque de Fase 1 |
| **Esc** | Cerrar la ventana y finalizar el audio de forma limpia |

---

## 2. Qué está Probado Rigurosamente (Bit a Bit)

1. **Física y Colisiones (Punto fijo 8.8)**:
   - Movimiento subpíxel y velocidad en punto fijo 8.8 big-endian.
   - Tres velocidades de paleta (`1`, `3` y `5` px/tick).
   - Encogimiento de paleta en el techo (`0x0CE1..0x0D14`).
   - Sondas dobles contra la matriz de 8x4 px (`0x0D27..0x0E8C`).
   - Resolución de colisión multi-bloque (`0x09F9 -> 0x023D: LDH A, (0xFFA3)`) con detención `HALT` de 1 VBlank antes de decrementar `bricks_left` y aplicar el rebote vertical, verificado bit a bit en Fase 17 tick 85/86 (`(8, 146)` fotogramas en `State 8`).
2. **Mecánicas Multi-Fase Verificadas**:
   - **Fase 1**: 5,651 ticks consecutivos de juego activo frente a PyBoy (`mismatches = 0`).
   - **Fase 2**: Desplazamiento horizontal por fila (`mod 112`), lectura de patrones en `0x4075` y colisión con filas desplazadas (739 ticks bit a bit).
   - **Fase 3**: Descenso vertical del mural por rebotes de paleta (`0x0BC7`), decremento de `row_offset` y limpieza de fila inferior (`0x0BF2`, 2,138 ticks bit a bit).
   - **Fase 4 (Bonus)**: Destrucción de bloques sin reflejar vector de velocidad (`bonus_flag = 1`, penetración continua) y cadencia del temporizador de bonificación (1 unidad HUD = 32 VBlanks).
3. **Ciclo de Vida de Partida y Transiciones (12 escenarios diferenciales en `tests/test_scenario_diff.py`)**:
   - Final normal de fase (`8 -> 4 -> 5`).
   - Caída de bola y expiración de tiempo en bonus sin pérdida de vida (`7 -> 4 -> 5`, sin estado 8).
   - Conteo de bonus perfecto multi-ronda desde tabla `0x1B71` (+500, +700, +1000, +1500 decenas a +10 decenas/frame y frame extra por 1-UP en `0x0C71`).
   - Pérdida de vida normal, re-saque y juego activo (`7 -> 11 -> 5 -> 6`) con paridad exacta de PRNG.
   - Game Over con reinicio en título (`State 2`), telón inicial de Mario de 221 fotogramas, segunda partida y pausa/reanudación (`State 12`).
   - Final de Fase 32 -> Ending (`State 9`, 1445 fotogramas) -> retorno a Fase 1 conservando vidas y puntos.
4. **Telones en las 32 Fases y Alineaciones de VBlank**:
   - Las 32 transiciones de fase (`0..31 -> 1..0`) y 16 alineaciones de VBlank (`0, 7, 16, 31`) automatizadas en [`test_all_32_curtain_transitions_and_vblank_alignments`](file:///c:/Users/maric/Documents/Alleyway-PC-Trabajo/Antigravity/tests/test_scenario_diff.py#L171), exigiendo alcanzar el stage destino, `curtain_frames > 0` y saque (0 discrepancias).
5. **Paridad Visual**:
   - 100.00% de coincidencia tono a tono (23,040 de 23,040 píxeles idénticos) frente a PyBoy en Pantalla de Título y Saque de Fase 1.

---

## 3. Qué es Adaptación de Plataforma

- **Presentación**: Ventana Win32 GDI con escalado entero (`StretchDIBits`) a 60 Hz en lugar de pantalla LCD DMG física.
- **Entrada**: Teclado de PC mapeado a la máscara de bits de Joypad de Game Boy (`P14`/`P15`).
- **Temporización**: Monotónica de alta precisión mediante `QueryPerformanceCounter` a ~59.7275 Hz DMG con acumulador de tiempo fraccionario en lugar de reloj de cuarzo de hardware.
- **Audio**: Síntesis PCM mono a 44,100 Hz de 16 bits en buffer circular `waveOut` con generador fraccionario sin deriva (`738 + 23157/65536` muestras/frame).
- **Modelo de Telón**: Aproximación lineal de presupuesto de ciclos (`compute_stage_curtain_dur`) que reproduce los fotogramas exactos del bucle de desempaquetado de mapa.

---

## 4. Qué Falta (Límites Explícitos — NO es un Port Completo)

1. **Audio APU incompleto**:
   - Solo está implementado el Canal 1 (onda cuadrada con barrido). Faltan Canal 2 (pulso 2), Canal 3 (onda arbitraria) y Canal 4 (ruido blanco).
   - Los barridos de tono por software (`0x6B76`/`0x6B8B` en VBlank) no están modelados dinámicamente.
2. **Animaciones intermedias de OAM**:
   - La secuencia donde Mario corre y entra a la pala (`0x43DC` en Estado 4) y la celebración con Mario saludando en el final (`0x45BD` en Estado 9) están temporizadas de forma exacta en fotogramas, pero aún no dibujan los fotogramas de animación intermedia de los sprites.
3. **Partidas largas manuales**:
   - Aunque las 32 fases y sus transiciones están verificadas matemáticamente y de forma diferencial, no se ha jugado una partida manual continua de principio a fin durante 32 fases en la ventana interactiva.
