# OBS Goal Bar

A native OBS Studio plugin that shows a live progress bar toward a goal, such as YouTube likes or subscribers, and runs actions when the goal is reached.

**Status:** work in progress. The current build only loads the plugin; no source is available yet.

## Requirements

- OBS Studio 33.0.1 or later
- macOS 13.0+ on Apple silicon (Windows and Linux are planned)
- CMake 3.28+ and Xcode

## Build

```sh
cmake --preset macos
cmake --build --preset macos
```

The plugin bundle is written to `build_macos/RelWithDebInfo/obs-goal-bar.plugin`.

## Core tests

The core library depends on neither OBS nor Qt, and its tests build with plain CMake:

```sh
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
