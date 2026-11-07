# VIRTUA TENNIS - RECOMP

Recompilación estática de **Virtua Tennis** (Sega NAOMI, 1999) y **Virtua Tennis 2** (Sega NAOMI, 2001)
para Windows y Linux. Los dos juegos funcionan desde el mismo programa y se eligen en un menú.
Hecho por **Pocket_lucho** con Claude. [Read in English](README.md)

El código máquina SH-4 de los juegos se traduce por adelantado a C++ (una función por bloque básico)
y se compila a código nativo. El resto del hardware NAOMI (gráficos PowerVR2, sonido AICA, entrada
Maple/JVS, cartucho) lo pone un fork de [Flycast](https://github.com/flyinghead/flycast).
Comparte base con [VIRTUA NBA - RECOMP](https://github.com/PocketLucho/Virtua-NBA---Recompilation).

Si quieres apoyarme: https://buymeacoffee.com/pocket_lucho

## Qué necesitas

* Tus propios romsets de cartucho (nombres de MAME), uno o los dos:
  * **Virtua Tennis** / Power Smash: `vtennis.zip`
  * **Virtua Tennis 2** / Power Smash 2: `vtenis2c.zip`
* La **BIOS de NAOMI** (`naomi.zip`): sólo para el primer arranque de cada juego y región, para crear una
  instantánea de arranque. Después los juegos arrancan directamente en el logo de SEGA sin ella.
* Windows 10/11 x64, o Linux x86_64 (glibc 2.35+, p. ej. Ubuntu 22.04 o posterior).

**Ni este repositorio ni las releases incluyen código, gráficos, sonido ni BIOS de los juegos.**
El código recompilado se genera en tu PC a partir de tus propias ROMs (ver abajo).

## Primeros pasos (Windows)

1. Descarga el zip de la release y descomprímelo.
2. Copia `vtennis.zip` y/o `vtenis2c.zip` en `roms\` y `naomi.zip` en `data\`.
3. Ejecuta **`CONSTRUIR_JUEGO.bat`** (una vez). Este
   * descarga el compilador libre [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (180 MB, sólo la primera vez,
     comprobado por SHA-256),
   * ejecuta cada juego que tengas hasta su punto de entrada y decodifica su programa a C++ (unos segundos por juego),
   * lo compila todo en `vt_game.dll` (de 5 a 20 minutos según la CPU),
   * borra las fuentes generadas.
4. Ejecuta **`VirtuaTennis-Recompiled.exe`**, elige un juego y pulsa JUGAR. Tab / el menú Ajustes abre las opciones.

Si más adelante añades el otro juego, vuelve a ejecutar `CONSTRUIR_JUEGO.bat`.

## Primeros pasos (Linux x86_64)

Descarga `VirtuaTennis-Recomp-linux-x64.tar.gz`, descomprímelo, copia las ROMs y la BIOS como arriba y ejecuta
`./build_game.sh` una vez (necesita `g++` y `make`: `sudo apt install build-essential`) y luego `./VirtuaTennis-Recompiled`.

## Características

* Rendimiento nativo (código recompilado, sin JIT), con respaldo exacto al ciclo para lo no precompilado.
* Lanzador: Virtua Tennis y Virtua Tennis 2 en un solo programa.
* Arranque sin BIOS desde una instantánea, una por región (Japón, USA, Export, Corea), selector de región.
* **16:9 nativo**: la cámara del juego genera la vista panorámica real (red, jugadores, jueces y público siguen
  visibles en los bordes) y el marcador y los menús mantienen sus proporciones 4:3. La pantalla de título se
  estira; el resto de pantallas 2D se muestran con bandas rellenas con los píxeles del borde.
* Antialiasing FXAA (DirectX 11 / OpenGL) además de supermuestreo (resolución interna).
* Trucos integrados (menú del juego → Trucos, desactivados por defecto):
  * *El rival no puede puntuar*: los rivales secretos (Master en Virtua Tennis, King en Virtua Tennis 2)
    aparecen al terminar el modo Arcade en Normal sin perder ningún juego y sin continuar; este truco lo hace fácil.
  * *Empieza cada juego con 40*.
* Interfaz en español e inglés (más los otros idiomas de Flycast), menús reducidos, botón Reiniciar.
* Juego online con el netcode de rollback GGPO de Flycast (experimental; los dos jugadores necesitan la misma
  versión, el mismo romset y la misma opción de panorámico).

## Cómo funciona la distribución legal

Las releases sólo contienen el motor (un Flycast modificado, GPL v2) y el **constructor**. Las partes que vienen
del juego se generan en el PC de cada usuario a partir de sus propios archivos:

| Parte | De dónde sale |
| --- | --- |
| Código del juego recompilado (`vt_game.dll` / `libvt_game.so`) | Lo genera `CONSTRUIR_JUEGO.bat` / `build_game.sh` a partir de tus ROMs |
| Instantáneas de arranque (`data/<romset>_boot_<región>.state`) | Se crean en el primer arranque con tu ROM y tu `naomi.zip` |
| Gráficos, sonido, música, textos | Se leen de tu ROM durante la ejecución |

`vt/builder/seeds_*.txt` sólo contienen direcciones de puntos de entrada de código (números, nada de código),
que guían al decodificador.

## Compilar desde el código fuente

```bash
git clone --recursive https://github.com/PocketLucho/Virtua-Tennis---Recompilation
cd Virtua-Tennis---Recompilation
LLVM_MINGW=/ruta/a/llvm-mingw-20251104-ucrt-ubuntu-22.04-x86_64 vt/tools/package_windows.sh   # paquete Windows
vt/tools/package_linux.sh                                                                     # paquete Linux
```

Documentación técnica (inglés): [vt/docs/TECHNICAL.md](vt/docs/TECHNICAL.md). Diario de desarrollo:
[vt/docs/STATUS.es.md](vt/docs/STATUS.es.md).

## Legal

* Licencia **GNU GPL v2** (heredada de Flycast, ver `LICENSE`).
* **No contiene material con copyright de Sega**: ni ROM, ni BIOS, ni gráficos, sonidos, música o código del
  juego traducido. El código recompilado y las instantáneas se generan localmente a partir de los archivos del
  usuario y **no deben redistribuirse**.
* "Virtua Tennis", "Power Smash", "SEGA" y "NAOMI" son marcas de sus propietarios; se usan sólo para describir
  con qué es compatible este software. Este proyecto no está afiliado ni respaldado por ellos.
* Esto no es asesoramiento legal. Volcar tu propio cartucho es la única forma legítima de obtener la ROM.
