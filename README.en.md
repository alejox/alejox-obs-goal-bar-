<div align="center">

# 🎯 OBS Goal Bar

**A native OBS progress bar that moves live toward your likes, subscribers or followers goal, and celebrates when you reach it.**

[Español](README.md) | **English**

</div>

---

## ✨ Features

| | Feature | What it does |
|:-:|---------|--------------|
| 📊 | **"Goal Bar" source** | An OBS source with a title, current value / goal and a bar that fills with a smooth animation. Three styles (Minimal, Bold, Neon), accent color and size |
| ▶️ | **YouTube, public mode** | Reads the live stream's likes and subscribers from the channel's public page (`@handle`), no sign-in |
| 🟣 | **Twitch** | Followers and subscribers through the official API. Connect your account with a code on twitch.tv/activate |
| ✍️ | **Manual mode** | Type the current value yourself; always available |
| 🎉 | **Celebration** | When the goal is reached: a glow and confetti over the bar |
| 🎬 | **Show a source** | Shows a scene source (image, text, audio or video). Audio and video play from the start and hide when they end |
| 🔀 | **More actions (optional)** | Switch scene (and back after N s), save a Replay Buffer clip, take a screenshot. All off by default; tests never save clips |
| ⏭️ | **Auto next goal** | After the celebration, the goal moves up by the step you choose |
| ⌨️ | **Hotkeys** | "Goal Bar: Test celebration" and "Goal Bar: Reset goal", per source |
| 🌍 | **Translations** | English and Spanish UI |

> [!NOTE]
> **YouTube rounds subscriber counts** (for example, 12.3K), even for the channel owner. A subscribers goal is therefore aligned to the next value YouTube can show. Likes are exact and are the default metric.

---

## 🧰 Install (macOS)

> [!IMPORTANT]
> **Quit OBS before installing.** OBS cannot replace a plugin it already loaded.

1. Get `obs-goal-bar.plugin` (or [build it](#️-build-from-source)).
2. Copy it to:

   ```text
   ~/Library/Application Support/obs-studio/plugins/
   ```

   In Finder, press <kbd>⌘</kbd> <kbd>⇧</kbd> <kbd>G</kbd> and paste the path. Create the `plugins` folder if it does not exist.
3. Open OBS. **Help > Log Files > View Current Log** should show `[obs-goal-bar] loaded`.

If macOS refuses to load a downloaded bundle, clear the quarantine flag:

```bash
xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/obs-goal-bar.plugin
```

**Uninstall:** delete `~/Library/Application Support/obs-studio/plugins/obs-goal-bar.plugin`.

---

## 🚀 Usage in 5 steps

1. **Add the source.** In a scene, click **+** and pick **Goal Bar**.
2. **Pick the platform.** **Manual**, **YouTube** (type your `@handle`) or **Twitch** (click **Connect Twitch** and enter the code on twitch.tv/activate).
3. **Set the goal.** Choose the metric and type the **Goal**.
4. **Decide what happens when it is reached.** Under **When the goal is reached**: the animation, a source to show and the auto next goal. Click **Test celebration** to preview it.
5. **Assign hotkeys (optional).** In **Settings > Hotkeys**, find the source and bind "Test celebration" and "Reset goal".

> [!TIP]
> **Reset goal** starts the bar over from the current value. It never fires the celebration, even when the value is already at the goal.

---

## 🛠️ Build from source

Requirements: macOS 13 or later on Apple Silicon, Xcode and CMake 3.28 or later.

```sh
cmake --preset macos
cmake --build --preset macos
```

The bundle is written to `build_macos/RelWithDebInfo/obs-goal-bar.plugin`.

The core depends on neither OBS nor Qt, and its tests build with plain CMake:

```sh
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

---

## 🔐 Privacy

| Data | Where it stays |
|------|----------------|
| Twitch login | The refresh token lives in the macOS Keychain; the access token only in memory |
| YouTube | The public live page is read; no sign-in, no keys |
| Everything else | The plugin only talks to youtube.com and twitch.tv. It sends no data anywhere else |

---

## 🧭 Status

macOS (Apple Silicon) first. Windows and Linux are coming.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
