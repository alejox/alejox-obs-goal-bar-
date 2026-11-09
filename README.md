<div align="center">

# 🎯 OBS Goal Bar

**Una barra de progreso nativa para OBS que avanza en vivo hacia tu meta de me gusta, suscriptores o seguidores, y celebra cuando la alcanzas.**

**Español** | [English](README.en.md)

</div>

---

## ✨ Qué incluye

| | Función | Qué hace |
|:-:|---------|----------|
| 📊 | **Fuente «Barra de meta»** | Una fuente de OBS con título, valor actual / meta y una barra que se llena con una animación suave. Tres estilos (Minimalista, Intenso, Neón), color de acento y tamaño |
| ▶️ | **YouTube, modo público** | Lee los me gusta y los suscriptores del directo desde la página pública del canal (`@usuario`), sin iniciar sesión |
| 🟣 | **Twitch** | Seguidores y suscriptores con la API oficial. Conectas tu cuenta con un código en twitch.tv/activate |
| ✍️ | **Modo manual** | Escribes el valor actual a mano; siempre disponible |
| 🎉 | **Celebración** | Al cumplir la meta: brillo y confeti sobre la barra |
| 🎬 | **Mostrar una fuente** | Muestra una fuente de la escena (imagen, texto, audio o video). Audio y video se reproducen desde el inicio y se ocultan al terminar |
| 🔀 | **Más acciones (opcionales)** | Cambiar de escena (y volver tras N s), guardar un clip del búfer de repetición, tomar una captura. Todas desactivadas por defecto; la prueba nunca guarda clips |
| ⏭️ | **Siguiente meta automática** | Tras la celebración, la meta sube según el incremento que elijas |
| ⌨️ | **Atajos de teclado** | «Barra de meta: Probar celebración» y «Barra de meta: Reiniciar meta», por fuente |
| 🌍 | **Traducciones** | Interfaz en inglés y español |

> [!NOTE]
> **YouTube redondea los suscriptores** (por ejemplo, 12,3 K), incluso para el dueño del canal. Por eso una meta de suscriptores se ajusta al siguiente valor que YouTube puede mostrar. Los me gusta son exactos y son la métrica por defecto.

---

## 🧰 Instalación (macOS)

> [!IMPORTANT]
> **Cierra OBS antes de instalar.** OBS no puede reemplazar un plugin que ya cargó.

1. Consigue `obs-goal-bar.plugin` (o [compílalo](#️-compilar-desde-el-código)).
2. Cópialo en:

   ```text
   ~/Library/Application Support/obs-studio/plugins/
   ```

   En Finder, pulsa <kbd>⌘</kbd> <kbd>⇧</kbd> <kbd>G</kbd> y pega la ruta. Crea la carpeta `plugins` si no existe.
3. Abre OBS. En **Ayuda > Archivos de registro > Ver registro actual** deberías ver `[obs-goal-bar] loaded`.

Si macOS se niega a cargar un paquete descargado, quita la marca de cuarentena:

```bash
xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/obs-goal-bar.plugin
```

**Desinstalar:** borra `~/Library/Application Support/obs-studio/plugins/obs-goal-bar.plugin`.

## 🪟🐧 Instalación (Windows / Linux, desde GitHub Releases)

Cierra OBS primero y descarga el paquete de tu sistema desde la página de GitHub Releases. Cada paquete trae también sus pasos en `INSTALL.txt`.

| Sistema | Paquete | Extraer en | Resultado |
|---------|---------|------------|-----------|
| Windows x64 | `obs-goal-bar-<versión>-windows-x64.zip` | `C:\ProgramData\obs-studio\plugins\` | `...\plugins\obs-goal-bar\bin\64bit\obs-goal-bar.dll` |
| Linux x86_64 | `obs-goal-bar-<versión>-linux-x86_64.tar.gz` | `~/.config/obs-studio/plugins/` | `.../plugins/obs-goal-bar/bin/64bit/obs-goal-bar.so` |

En Linux:

```bash
mkdir -p ~/.config/obs-studio/plugins
tar -xzf obs-goal-bar-<versión>-linux-x86_64.tar.gz -C ~/.config/obs-studio/plugins
```

- Windows necesita OBS 33 o posterior. El token de renovación de Twitch se guarda en el Administrador de credenciales de Windows.
- Linux se compila en Ubuntu 24.04 contra la versión de OBS del PPA del OBS Project. El inicio de sesión de Twitch necesita `libsecret` y un Secret Service activo (GNOME Keyring o KWallet).
- Ambos están compilados y con pruebas unitarias en CI; todavía no se han ejecutado dentro de OBS.

**Desinstalar:** borra la carpeta `obs-goal-bar` de la carpeta de plugins indicada arriba.

---

## 🚀 Uso en 5 pasos

1. **Añade la fuente.** En una escena, pulsa **+** y elige **Barra de meta**.
2. **Elige la plataforma.** **Manual**, **YouTube** (escribe tu `@usuario`) o **Twitch** (pulsa **Conectar Twitch** e ingresa el código en twitch.tv/activate).
3. **Fija la meta.** Elige la métrica y escribe la **Meta**.
4. **Decide qué pasa al cumplirla.** En **Al cumplir la meta**: animación, una fuente para mostrar y la siguiente meta automática. Pulsa **Probar celebración** para verlo.
5. **Asigna atajos (opcional).** En **Ajustes > Atajos**, busca la fuente y asigna «Probar celebración» y «Reiniciar meta».

> [!TIP]
> **Reiniciar meta** empieza la barra de nuevo desde el valor actual. Nunca dispara la celebración, aunque el valor ya esté en la meta.

---

## 🔄 Actualizaciones

- **Automáticas.** Al abrir OBS, el plugin consulta la última versión publicada en GitHub, como mucho una vez al día. Si hay una nueva, te muestra un aviso con las notas de la versión y tres opciones: **Instalar al reiniciar**, **Más tarde** u **Omitir esta versión**.
- **Manual.** En **Herramientas > Goal Bar: Buscar actualizaciones**.
- **Cómo se aplica.** Al elegir **Instalar al reiniciar**, el paquete se descarga y se verifica (SHA-256 publicado por GitHub). Se instala cuando cierras OBS y queda activo al volver a abrirlo. Si algo falla, se restaura la versión anterior.

> [!NOTE]
> Solo se actualiza sola una instalación hecha como en las instrucciones de arriba (no dentro de la carpeta del programa OBS ni desde un paquete de la distribución), y tu usuario debe poder escribir en ella. Si no, el aviso enlaza a la página de la versión para descargarla a mano.

---

## 🛠️ Compilar desde el código

Requisitos: macOS 13 o posterior en Apple Silicon, Xcode y CMake 3.28 o posterior.

```sh
cmake --preset macos
cmake --build --preset macos
```

El paquete queda en `build_macos/RelWithDebInfo/obs-goal-bar.plugin`.

El núcleo no depende de OBS ni de Qt, y sus pruebas se compilan con CMake a secas:

```sh
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

---

## 🔐 Privacidad

| Dato | Dónde queda |
|------|-------------|
| Sesión de Twitch | El token de renovación vive en el Llavero de macOS; el token de acceso, solo en memoria |
| YouTube | Se lee la página pública del directo; no hay inicio de sesión ni claves |
| Todo lo demás | El plugin solo habla con youtube.com, twitch.tv y github.com (para buscar actualizaciones). No envía datos a ningún otro sitio |

---

## 🧭 Estado

macOS (Apple Silicon) está probado dentro de OBS. Windows x64 y Linux x86_64 se compilan, prueban y empaquetan solo en CI (GitHub Actions).

## Licencia

GPL-2.0-or-later. Consulta [LICENSE](LICENSE).
