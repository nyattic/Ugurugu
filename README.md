# WagleWaglePaint

![Last Commit](https://img.shields.io/github/last-commit/nyattic/WagleWaglePaint?style=for-the-badge&logo=git&logoColor=white&labelColor=1e1b2e&color=ffc94a)
[![Downloads](https://img.shields.io/github/downloads/nyattic/WagleWaglePaint/total?style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e&color=ffc94a)](https://github.com/nyattic/WagleWaglePaint/releases)
![License](https://img.shields.io/badge/license-MIT-ffc94a?style=for-the-badge&logo=opensourceinitiative&logoColor=white&labelColor=1e1b2e)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-ffc94a?style=for-the-badge&logo=qt&logoColor=white&labelColor=1e1b2e)

A drawing tool where every line wobbles. Draw once and your sketch
comes alive as a boiling-line animation, ready to export as a looping
GIF.

Inspired by Shake Art DELUXE and PS1-style vertex jitter, rebuilt as a
native desktop app with layers, pressure support, and project files.

## Features

- Brush and eraser with tablet pressure support
- Crisp pixel-edged strokes inspired by hand-drawn animation tools
- Boiling-line wobble: every frame is an independent redraw of your
  strokes, fully loopable
- Lasso select and auto select (click inside line art) to move or
  delete strokes
- Paint bucket that fills enclosed areas — fills wobble along with the
  lines
- Layers with thumbnails, visibility, opacity, and drag reordering
- Adjustable wobble strength, frame count, and FPS
- Looping GIF export and single-frame PNG export
- `.wobble` project files with full undo/redo
- Automatic updates through Sparkle on macOS and Velopack on Windows
- English and Korean UI, following the system or an in-app preference

## Controls

| Key | Action |
| --- | --- |
| `B` | Brush |
| `E` | Eraser |
| `L` | Lasso select |
| `W` | Auto select |
| `G` | Paint bucket |
| `P` | Play or pause the preview |
| `Space` + drag | Pan the canvas |
| Scroll | Zoom |
| `Ctrl/Cmd+Z` / `Ctrl/Cmd+Shift+Z` | Undo / Redo |
| `Ctrl/Cmd+E` | Export animated GIF |
| `Esc` | Cancel stroke or selection |

Open `examples/Wave.wobble` to try a ready-made document.

## Building from source

Requires CMake 3.25+, Ninja, Qt 6.10+, spdlog, and fmt.

### macOS

```sh
brew install cmake ninja qt spdlog fmt
cmake --preset macos-release
cmake --build --preset macos-release
open out/build/macos-release/WagleWaglePaint.app
```

### Windows

Requires Visual Studio 2022 and Qt 6 (MSVC 2022 x64). Install spdlog
with vcpkg.

```powershell
vcpkg install spdlog:x64-windows fmt:x64-windows
cmake --preset windows-release -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build --preset windows-release
```

### Tests

```sh
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
```

## License

[MIT](LICENSE)
