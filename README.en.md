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
| 🟢 | **Kick** | Followers and viewers of any channel, no Kick account or app needed (undocumented public endpoint, see below) |
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

## 🪟🐧 Install (Windows / Linux, via GitHub Releases)

Quit OBS first, then download the package for your system from the GitHub Releases page. Each package also contains its steps as `INSTALL.txt`.

| System | Package | Extract into | Result |
|--------|---------|--------------|--------|
| Windows x64 | `obs-goal-bar-<version>-windows-x64.zip` | `C:\ProgramData\obs-studio\plugins\` | `...\plugins\obs-goal-bar\bin\64bit\obs-goal-bar.dll` |
| Linux x86_64 | `obs-goal-bar-<version>-linux-x86_64.tar.gz` | `~/.config/obs-studio/plugins/` | `.../plugins/obs-goal-bar/bin/64bit/obs-goal-bar.so` |

On Linux:

```bash
mkdir -p ~/.config/obs-studio/plugins
tar -xzf obs-goal-bar-<version>-linux-x86_64.tar.gz -C ~/.config/obs-studio/plugins
```

- Windows needs OBS 33 or later. The Twitch refresh token is kept in the Windows Credential Manager.
- Linux is built on Ubuntu 24.04 against the OBS version of the OBS Project PPA. Twitch login needs `libsecret` and a running Secret Service (GNOME Keyring or KWallet).
- Both are compile-tested and unit-tested in CI only; they have not been run inside OBS yet.

**Uninstall:** delete the `obs-goal-bar` folder from the plugins folder above.

---

## 🚀 Usage in 5 steps

1. **Add the source.** In a scene, click **+** and pick **Goal Bar**.
2. **Pick the platform.** **Manual**, **YouTube** (type your `@handle`), **Twitch** (click **Connect Twitch** and enter the code on twitch.tv/activate) or **Kick** (type the channel name, see below).
3. **Set the goal.** Choose the metric and type the **Goal**.
4. **Decide what happens when it is reached.** Under **When the goal is reached**: the animation, a source to show and the auto next goal. Click **Test celebration** to preview it.
5. **Assign hotkeys (optional).** In **Settings > Hotkeys**, find the source and bind "Test celebration" and "Reset goal".

> [!TIP]
> **Reset goal** starts the bar over from the current value. It never fires the celebration, even when the value is already at the goal.

### 🟢 Kick

Pick **Kick** and type the channel name (or paste its kick.com URL). No Kick account, app or credentials are needed.

Metrics: **Followers** (readable at any time) and **Viewers** (shown while the channel is live; "Offline" otherwise). Paid subscribers are not offered: Kick does not publish them on this endpoint. The bar refreshes about every 20 seconds and waits longer (up to 5 minutes) if Kick limits or blocks requests.

> ⚠️ Kick data comes from an **undocumented** public endpoint (`kick.com/api/v2/channels/<channel>`), the one Kick's own website uses. It is not part of Kick's official API, sits behind Cloudflare and may change or stop working at any time. If Kick blocks the request, the bar shows "Kick blocked the request" instead of a made-up value.

---

## 🔄 Updates

- **Automatic.** When OBS starts, the plugin checks the latest release on GitHub, at most once a day. If there is a newer one, it shows a notice with the release notes and three choices: **Install on next restart**, **Later** or **Skip this version**.
- **Manual.** Use **Tools > Goal Bar: Check for updates**.
- **How it applies.** After **Install on next restart**, the package is downloaded and verified (against the SHA-256 GitHub publishes). It is installed when you close OBS and is active the next time you open it. If any step fails, the previous version is put back.

> [!NOTE]
> Only an install made as in the instructions above (not inside the OBS program folder or from a distribution package) updates itself, and your user must be able to write to it. Otherwise the notice links to the release page for a manual download.

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
| Everything else | The plugin only talks to youtube.com, twitch.tv, kick.com and github.com (to check for updates). It sends no data anywhere else |

---

## 🧭 Status

macOS (Apple Silicon) is tested inside OBS. Windows x64 and Linux x86_64 are built, unit-tested and packaged in CI (GitHub Actions) only.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
