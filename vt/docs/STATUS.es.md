# Estado — Virtua Tennis 1 + 2 Recomp

Última actualización: 2026-11-07

## Funciona

- **Base de Virtua NBA - Recomp** (Flycast `1b63c8a` + ejecutor AOT), generalizada para los dos juegos en un
  solo programa con lanzador.
- **Distribución legal: motor + constructor** (igual que NBA). El ejecutable no lleva código de los juegos.
  `CONSTRUIR_JUEGO.bat` / `build_game.sh` descarga llvm-mingw (comprobado por SHA-256), arranca cada juego que
  encuentre en `roms/` hasta su entrada (`0x0C021000`), decodifica su programa (4 MB desde `0x0C020000`, estable
  desde la entrada en los dos cartuchos) y compila `vt_game.dll` / `libvt_game.so` (~170.000 bloques).
  `data/vt_games.txt` dice qué juegos están construidos; el lanzador pide ejecutar el constructor si falta alguno.
- **Sólo cartuchos**: `vtennis` (Virtua Tennis / Power Smash) y `vtenis2c` (Virtua Tennis 2 / Power Smash 2).
  El soporte de la versión GD-ROM de VT1 se ha quitado.
- **Arranque sin BIOS** desde instantáneas por juego y región (`data/<romset>_boot_<jp|us|ex|kr>.state`), con
  selector de región, botón Reiniciar, FXAA, interfaz en español y GGPO heredados de NBA.
- **Panorámico 16:9 nativo**: cámaras con aspecto 16/9 (parche de *literal pools*), HUD y menús comprimidos a 4:3.

## Novedades (2026-11-07)

- **Trucos** (menú → Trucos, desactivados por defecto), por *game ID* (` POWER SMASH` / ` POWER SMASH 2`):
  - *El rival no puede puntuar*: puntos del rival a 0 cada frame. Sirve para terminar el Arcade en Normal sin
    perder juegos ni continuar, que es la condición para que salgan Master (VT1) / King (VT2).
  - *Empieza cada juego con 40*: puntos del jugador a 3.
  - Direcciones encontradas con volcados de RAM cada 30 frames durante un partido y una línea de tiempo del
    marcador leída de las capturas: VT1 `0x0C305E1C`/`0x0C305E20`, VT2 `0x0C23D258`/`0x0C23D25C`
    (juegos en `0x0C23D250`/`254`, *deuce*/ventaja en `0x0C23D260`).
- **Selección de jugador en panorámico**: los marcos y paneles son polígonos planos a profundidad ~0,0025 que no
  se comprimían; ahora se comprime cualquier polígono plano, así que las cajas quedan centradas.
- **Título de VT1**: durante el fundido de entrada los *tiles* de fondo (profundidad 2,2222) llegan en la lista
  translúcida y después del logo, así que el logo salía comprimido ~1 s. Se aceptan también translúcidos y la
  decisión pasa al frame siguiente: siempre estirado.
- **Aviso parental de VT1**: los primeros frames se dibujan encima del título (capa negra opaca a profundidad 50),
  y el frame se trataba como título (estirado). Una capa opaca a pantalla completa delante del título cancela el
  estirado: siempre 4:3.
- **Efecto gris del attract de VT1 (DirectX 11)**: la lectura de la textura renderizada a la CPU reutiliza la
  textura de *staging* y el búfer en lugar de crearlos en cada pasada.
- **Primer arranque (BIOS) lento en el constructor**: sin instantánea, el constructor ejecutaba la BIOS de NAOMI
  en el intérprete del ejecutor AOT (todavía no hay código recompilado), varias veces más lento que el dynarec.
  Ahora, si no hay instantánea, el constructor usa el dynarec de Flycast para la BIOS y se detiene en el punto de
  entrada del juego (`aot_builder_entry_check` en `compilePC`): guarda la instantánea, decodifica el programa y
  sale. Prueba (Linux, 2 núcleos): de 69 s a 8 s por juego. Los bloques de la BIOS compilados por el camino se
  incluyen en el módulo, así que el primer arranque de otra región también va más rápido.
- Documentación en inglés (README, TECHNICAL, README.txt) y en español (README.es, LEEME).

## Pendiente

1. Silla de árbitro vacía: no la he podido reproducir (en partido, con las cámaras normales, el juez de silla
   está sentado en los dos juegos, en 4:3 y en 16:9). Hace falta una captura del momento.
2. Comprobar en Windows con GPU real el rendimiento del efecto gris del attract de VT1.
3. Verificar en una partida real que Master / King aparecen con el truco.
