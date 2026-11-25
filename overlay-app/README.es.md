# 🎯 AC Goal Bar Overlay — la Goal Bar para Meld Studio

[English](README.md) · **Español**

La misma barra de metas del plugin **AC Goal Bar** para OBS, como una app de escritorio que funciona en **Meld Studio**. También sirve en cualquier programa con fuente de navegador (Streamlabs, TikTok LIVE Studio, OBS…).

Lee YouTube, Twitch, Kick y TikTok igual que el plugin y tiene los mismos 15 estilos (con Glitch y GTA 6), la celebración con confeti y la siguiente meta automática. La barra se muestra en Meld como una capa **Browser** que apunta a la app.

> El plugin de OBS sigue siendo un producto aparte: no hace falta para usar esta app, y esta app no hace falta para usar el plugin.

## Requisitos

- **Windows 10 u 11** (64 bits) o **macOS 13 o posterior** en Mac con Apple Silicon.
- **Meld Studio** (o cualquier programa con fuente de navegador).
- La app tiene que estar **abierta mientras transmites**: es la que lee las plataformas y dibuja la barra.

## Instalar

### Windows

1. Descarga `ac-goal-bar-overlay-<versión>-windows-x64.zip`.
2. Descomprímelo donde quieras (por ejemplo en **Documentos**). Queda una carpeta **AC Goal Bar Overlay**.
3. Abre **AC Goal Bar Overlay.exe**.
4. Si Windows muestra «Windows protegió su PC», pulsa **Más información → Ejecutar de todas formas**. Aparece porque la app no está firmada con un certificado de pago.

### macOS

1. Descarga `ac-goal-bar-overlay-<versión>-macos-arm64.zip` y descomprímelo.
2. Arrastra **AC Goal Bar Overlay.app** a **Aplicaciones**.
3. Abre la **Terminal** y ejecuta este comando una vez. macOS bloquea las apps descargadas que no están firmadas por Apple:

   ```bash
   xattr -dr com.apple.quarantine "/Applications/AC Goal Bar Overlay.app"
   ```

4. Abre la app desde Aplicaciones.

## Configurar la meta

En la ventana de la app, sección **Meta**:

1. **Plataforma**:
   - **YouTube**: tu `@usuario`.
   - **Twitch**: pulsa **Conectar Twitch** e ingresa el código en twitch.tv/activate.
   - **Kick**: el nombre del canal.
   - **TikTok**: el `@usuario`.
   - **Manual**: escribes el valor tú mismo.
2. **Métrica**: me gusta, seguidores, suscriptores, espectadores, comentarios, bits, suscripciones de pago o diamantes. Solo aparecen las que lee esa plataforma.
3. **Título** y **Meta**.
4. **Celebración** y **Siguiente meta automática** (con su incremento). **Probar celebración** la muestra sin tocar la meta.
5. En **Estilo**: el estilo, los colores, la opacidad y el tamaño.

La vista previa de arriba muestra la barra tal cual saldrá en el directo. Todo se guarda solo.

## Añadirla a Meld Studio

1. En la app, pulsa **Copiar URL**. Es `http://127.0.0.1:48722/goal-bar`.
2. En Meld Studio, haz clic en el lienzo y pega con **Ctrl+V** (Windows) o **Cmd+V** (Mac). Meld crea una capa **Browser** con la barra.
   - Otra forma: en el panel **Layers** pulsa **＋ → Browser** y pega la URL en el campo **URL** del Inspector.
3. Ajusta el tamaño y la posición de la capa. La barra se escala sin deformarse. El tamaño exacto es el que pusiste en la app (800 × 120 por defecto).

En OBS, Streamlabs u otros programas: añade una **fuente de navegador** con la misma URL.

## Mientras transmites

- **Cerrar la ventana no cierra la app**: sigue en la bandeja del sistema (en Mac, en la barra de menú) y la barra sigue en Meld. Para cerrarla del todo: icono de la bandeja → **Salir**.
- Si la app está cerrada, la capa queda vacía. Al volver a abrirla, la barra vuelve sola, sin tocar nada en Meld.
- La app dice en cuántos sitios se está mostrando la barra («Mostrándose en 1 sitio»).

## Diferencias con el plugin de OBS

| | Plugin de OBS | App para Meld |
|---|---|---|
| Plataformas, métricas, estilos, celebración, siguiente meta | ✅ | ✅ |
| Mostrar una fuente, cambiar de escena, guardar un clip, captura al cumplir la meta | ✅ | ❌ (por ahora) |
| Atajos de teclado | ✅ | ❌ |
| Funciona sin otra app abierta | ✅ (dentro de OBS) | ❌ (la app debe estar abierta) |

Meld Studio trae sus propios widgets de metas, que usan las cuentas que conectas en Meld. Esta app no puede usar esas conexiones, porque Meld no las comparte con programas externos. Por eso lee las plataformas por su cuenta, como el plugin.

## Problemas frecuentes

<details>
<summary><b>La capa de Meld está vacía</b></summary>

Comprueba que la app esté abierta (ventana o icono en la bandeja) y que la URL de la capa sea la que muestra la app. Abre la URL con **Ver en el navegador**: si ahí se ve la barra, en Meld también tiene que verse.
</details>

<details>
<summary><b>«El puerto 48722 está en uso»</b></summary>

Otro programa usa ese puerto. Cambia el **Puerto** en la app (por ejemplo a 48723), copia la URL nueva y pégala en la capa de Meld.
</details>

<details>
<summary><b>La barra dice «Sin directo», «Error» o no se mueve</b></summary>

Es lo mismo que en el plugin: revisa el canal o el `@usuario`. Algunas métricas solo existen durante el directo (espectadores, y en TikTok me gusta, comentarios y diamantes). El registro de la app (`ac-goal-bar-overlay.log`, en la carpeta de configuración de abajo) dice qué pasó.
</details>

## Privacidad

Igual que el plugin: no hay telemetría ni cuenta propia. La app se conecta solo a la plataforma que elijas, y a **Euler Stream** para el chat de TikTok (ver el [README del plugin](../README.es.md#-privacidad)). El servidor de la app escucha solo en `127.0.0.1`: nada fuera de tu computadora puede verlo.

**Dónde guarda tus datos**

- Ajustes y registro:
  - Windows: `%LOCALAPPDATA%\alejoxcode\AC Goal Bar Overlay\`
  - macOS: `~/Library/Preferences/alejoxcode/AC Goal Bar Overlay/`
- El token de Twitch va al almacén seguro del sistema: Administrador de credenciales en Windows, Llavero en macOS.

**Desinstalar:** cierra la app (bandeja → Salir) y borra su carpeta (Windows) o la app de Aplicaciones (Mac). Si quieres, borra también la carpeta de ajustes.

## Compilar desde el código

```bash
cmake -S overlay-app -B build_overlay -DCMAKE_PREFIX_PATH=<ruta a Qt 6>
cmake --build build_overlay
```

En Linux basta con `qt6-base-dev`, `libcurl4-openssl-dev` y `libsecret-1-dev`. El workflow [`overlay-app.yml`](../.github/workflows/overlay-app.yml) arma los paquetes de Windows y macOS con el mismo Qt que usa el plugin.
