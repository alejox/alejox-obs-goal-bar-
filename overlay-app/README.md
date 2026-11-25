# 🎯 AC Goal Bar Overlay — the Goal Bar for Meld Studio

**English** · [Español](README.es.md)

The goal bar of the **AC Goal Bar** OBS plugin, as a desktop app that works in **Meld Studio**. It also works in any app with a browser source (Streamlabs, TikTok LIVE Studio, OBS…).

It reads YouTube, Twitch, Kick and TikTok the same way the plugin does and has the same 15 styles (Glitch and GTA 6 included), the confetti celebration and the automatic next goal. Meld shows the bar as a **Browser** layer pointing to the app.

> The OBS plugin is still a separate product: you don't need it to use this app, and you don't need this app to use the plugin.

## Requirements

- **Windows 10 or 11** (64-bit) or **macOS 13 or later** on an Apple Silicon Mac.
- **Meld Studio** (or any app with a browser source).
- The app must be **open while you stream**: it is what reads the platforms and draws the bar.

## Install

### Windows

1. Download `ac-goal-bar-overlay-<version>-windows-x64.zip`.
2. Extract it anywhere (for example **Documents**). You get an **AC Goal Bar Overlay** folder.
3. Open **AC Goal Bar Overlay.exe**.
4. If Windows says "Windows protected your PC", click **More info → Run anyway**. It shows up because the app is not signed with a paid certificate.

### macOS

1. Download `ac-goal-bar-overlay-<version>-macos-arm64.zip` and unzip it.
2. Drag **AC Goal Bar Overlay.app** to **Applications**.
3. Open **Terminal** and run this command once. macOS blocks downloaded apps that Apple has not signed:

   ```bash
   xattr -dr com.apple.quarantine "/Applications/AC Goal Bar Overlay.app"
   ```

4. Open the app from Applications.

## Set up the goal

In the app window, **Goal** section:

1. **Platform**:
   - **YouTube**: your `@handle`.
   - **Twitch**: click **Connect Twitch** and enter the code on twitch.tv/activate.
   - **Kick**: the channel name.
   - **TikTok**: the `@user`.
   - **Manual**: you type the value yourself.
2. **Metric**: likes, followers, subscribers, viewers, comments, bits, paid subscriptions or diamonds. Only the ones that platform reads are listed.
3. **Title** and **Goal**.
4. **Celebration** and **Auto next goal** (with its step). **Test celebration** shows it without touching the goal.
5. Under **Style**: the style, colors, opacity and size.

The preview at the top shows the bar exactly as it will look on stream. Everything is saved automatically.

## Add it to Meld Studio

1. In the app, click **Copy URL**. It is `http://127.0.0.1:48722/goal-bar`.
2. In Meld Studio, click the canvas and paste with **Ctrl+V** (Windows) or **Cmd+V** (Mac). Meld creates a **Browser** layer with the bar.
   - Another way: in the **Layers** panel click **＋ → Browser** and paste the URL into the Inspector's **URL** field.
3. Adjust the layer's size and position. The bar scales without stretching. Its exact size is the one set in the app (800 × 120 by default).

In OBS, Streamlabs or other apps: add a **browser source** with the same URL.

## While you stream

- **Closing the window doesn't quit the app**: it stays in the system tray (the menu bar on a Mac) and the bar stays in Meld. To quit for good: tray icon → **Quit**.
- If the app is closed, the layer is empty. When you open the app again the bar comes back by itself, with nothing to touch in Meld.
- The app shows in how many places the bar is showing ("Showing in 1 place").

## Differences from the OBS plugin

| | OBS plugin | App for Meld |
|---|---|---|
| Platforms, metrics, styles, celebration, auto next goal | ✅ | ✅ |
| Show a source, switch scene, save a clip, screenshot when the goal is reached | ✅ | ❌ (for now) |
| Hotkeys | ✅ | ❌ |
| Works without another app open | ✅ (inside OBS) | ❌ (the app must be open) |

Meld Studio has goal widgets of its own that use the accounts you connect in Meld. This app can't use those connections, because Meld does not share them with outside apps. That is why it reads the platforms itself, like the plugin.

## Common problems

<details>
<summary><b>The Meld layer is empty</b></summary>

Check that the app is open (window or tray icon) and that the layer's URL is the one the app shows. Open the URL with **View in the browser**: if the bar shows there, it has to show in Meld too.
</details>

<details>
<summary><b>"Port 48722 is in use"</b></summary>

Another program uses that port. Change the **Port** in the app (for example to 48723), copy the new URL and paste it into the Meld layer.
</details>

<details>
<summary><b>The bar says "Offline", "Error" or doesn't move</b></summary>

Same as in the plugin: check the channel or `@user`. Some metrics only exist while live (viewers, and on TikTok likes, comments and diamonds). The app's log (`ac-goal-bar-overlay.log`, in the settings folder below) says what happened.
</details>

## Privacy

Same as the plugin: no telemetry and no account of its own. The app only connects to the platform you choose, and to **Euler Stream** for TikTok's chat (see the [plugin README](../README.md#-privacy)). The app's server only listens on `127.0.0.1`: nothing outside your computer can see it.

**Where it keeps your data**

- Settings and log:
  - Windows: `%LOCALAPPDATA%\alejoxcode\AC Goal Bar Overlay\`
  - macOS: `~/Library/Preferences/alejoxcode/AC Goal Bar Overlay/`
- The Twitch token goes to the system's secure store: Credential Manager on Windows, Keychain on macOS.

**Uninstall:** quit the app (tray → Quit) and delete its folder (Windows) or the app in Applications (Mac). If you want, delete the settings folder too.

## Build from source

```bash
cmake -S overlay-app -B build_overlay -DCMAKE_PREFIX_PATH=<path to Qt 6>
cmake --build build_overlay
```

On Linux, `qt6-base-dev`, `libcurl4-openssl-dev` and `libsecret-1-dev` are enough. The [`overlay-app.yml`](../.github/workflows/overlay-app.yml) workflow builds the Windows and macOS packages with the same Qt the plugin uses.
