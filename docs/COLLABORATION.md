# Codex + Antigravity

## Reglas

- main: base integrada. No ejecutar dos agentes en la misma carpeta.
- Codex: src/ y tools/build.py, rama codex/runtime.
- Antigravity: investigacion en docs/research/ y herramientas de referencia,
  rama antigravity/research. Acuerdos de formatos antes de cambios cruzados.
- Un responsable integra cada cambio, ejecuta validaciones y actualiza STATUS.
- Las conversaciones no se sincronizan: registrar decisiones aqui.
- Una rama no aisla archivos: usar worktrees o clones distintos.

## Crear carpeta independiente para Antigravity

Desde este repositorio, una vez exista el commit inicial:

```powershell
git worktree add ../alleyway-antigravity -b antigravity/research main
```

Crear un entorno Python propio dentro de esa carpeta. `.venv`, `.tools` y
`local` no se copian al crear worktrees. La carpeta adicional puede leer el
volcado por ruta absoluta; no hace falta duplicarlo.

## Primer mensaje sugerido para Antigravity

Lee AGENTS.md, README.md y docs/STATUS.md. Tu rama es antigravity/research y
tu area es docs/research/ y herramientas de referencia. Primero comprueba las
herramientas existentes y documenta un procedimiento reproducible para medir
entrada, posicion de paleta y pelota por fotograma. Sin ROM disponible, prepara
el procedimiento y separa hipotesis de hechos. No inventes direcciones de RAM,
no descargues juegos y no modifiques src/. Coordina cualquier cambio de formato.

## Remoto

El usuario esta creando el repositorio. Todavia no hay origin ni publicaciones.
Cuando entregue la URL, verificar el remoto y acordar el primer push.
