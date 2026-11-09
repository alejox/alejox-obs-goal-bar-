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
| Todo lo demás | El plugin solo habla con youtube.com y twitch.tv. No envía datos a ningún otro sitio |

---

## 🧭 Estado

macOS (Apple Silicon) primero. Windows y Linux llegan pronto.

## Licencia

GPL-2.0-or-later. Consulta [LICENSE](LICENSE).
