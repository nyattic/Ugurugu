<p align="center">
  <img src="resources/icons/WobblePaint.png" width="128" alt="WagleWaglePaint app icon">
</p>

# WagleWaglePaint

[![Latest Release](https://img.shields.io/github/v/release/nyattic/WagleWaglePaint?style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e&color=ffc94a)](https://github.com/nyattic/WagleWaglePaint/releases/latest)
[![Downloads](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fnyattic%2FWagleWaglePaint%2Fdownload-badge%2Fdownloads.json&style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e)](https://github.com/nyattic/WagleWaglePaint/releases)
![License](https://img.shields.io/badge/license-GPL--3.0-ffc94a?style=for-the-badge&logo=gnu&logoColor=white&labelColor=1e1b2e)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-ffc94a?style=for-the-badge&logo=qt&logoColor=white&labelColor=1e1b2e)

<p align="center"><a href="README.md">KR</a> · <b>EN</b> · <a href="README.ja.md">JP</a></p>

A wobbly drawing app where every line wiggles. Draw once and your
sketch comes alive as a crisp, boiling-line animation ready to export
as a looping GIF.

Inspired by Shake Art DELUXE and PS1-style vertex jitter, with layers,
tablet pressure, selections, project files, and automatic updates.

> [!WARNING]
> WagleWaglePaint is currently in beta. You may run into bugs or rough
> edges. Bug reports are very welcome — please open a
> [GitHub Issue](https://github.com/nyattic/WagleWaglePaint/issues) with
> what you did, what you expected, and what happened instead. Attaching
> the `.wagle` project file helps a lot.

## Download

Click a file name below to download the newest version directly. The
full list is on
[GitHub Releases](https://github.com/nyattic/WagleWaglePaint/releases/latest).

| Platform | Supported systems | Download |
| --- | --- | --- |
| Windows | Windows 10 or later, x64 | [WagleWaglePaint-Windows-x64-Setup.exe](https://github.com/nyattic/WagleWaglePaint/releases/latest/download/WagleWaglePaint-Windows-x64-Setup.exe) |
| macOS | macOS 14 or later, Apple Silicon | [WagleWaglePaint-macOS-arm64.dmg](https://github.com/nyattic/WagleWaglePaint/releases/latest/download/WagleWaglePaint-macOS-arm64.dmg) |

The `.zip`, `.nupkg`, `appcast.xml`, and `.json` files on the release
page are used by the automatic updaters. They are not needed for a
normal installation.
The download badge counts installers and actual update packages while
excluding metadata requests made only to check for updates.

## Install

### Windows

Run the downloaded Setup file. WagleWaglePaint installs for the current
user and opens when installation is complete.

### macOS

Open the DMG and drag WagleWaglePaint into the Applications folder.

The current builds are not signed with a trusted developer certificate
or notarized, so Windows SmartScreen or macOS Gatekeeper may show a
warning. Only download the app from the official Releases page. On
macOS, Control-click the app, choose **Open**, and confirm the prompt if
necessary.

## Features

- Pressure-sensitive brush and eraser with 17 pen, marker, airbrush,
  and spray presets
- Loopable boiling-line wobble, redrawn every frame — or turn it off and
  draw like a regular paint app
- Crisp pixel-edged strokes with per-stroke anti-aliasing and roughness
- Responsive drawing, panning, and zooming on high-resolution canvases,
  even with very long strokes
- Lasso and auto selection with previews for moving, scaling, rotating,
  and flipping; deleting selected content can be undone and redone
- Canvas resizing that crops or expands without scaling the artwork, and
  image resizing that scales it
- Layers with thumbnails, visibility, opacity, and drag reordering
- A timeline with a live preview and adjustable wobble strength, frame
  count, and FPS
- 1–1600% zoom, true 100% actual-pixel view, and fit-to-window
- Looping GIF and still-image PNG/JPG export with progress and cancellation,
  plus `.wagle` project files
- Crash recovery and restored tool, color, and brush settings on restart
- Customizable shortcuts and an English, Korean, or Japanese interface
- Automatic updates through Sparkle on macOS and Velopack on Windows

## Controls

These are the default shortcuts. They can be changed in **Settings →
Shortcuts**.

| Key | Action |
| --- | --- |
| `B` | Brush |
| `E` | Eraser |
| `L` | Lasso select |
| `W` | Auto select |
| `G` | Paint bucket |
| `P` | Play or pause the preview |
| `M` | Flip the canvas horizontally (view only) |
| `Space` + drag | Pan the canvas |
| Scroll | Zoom |
| `Ctrl/Cmd++` / `Ctrl/Cmd+-` | Zoom in / out |
| `Ctrl/Cmd+Space` + drag | Zoom with the pen or mouse (drag right to zoom in) |
| Status-bar zoom slider | Zoom from 1–1600% |
| `Alt` + click | Pick a color from the canvas (brush, eraser, and bucket tools) |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo on Windows |
| `Cmd+Z` / `Cmd+Shift+Z` | Undo / Redo on macOS |
| `Ctrl/Cmd+E` | Export an animated GIF |
| `Ctrl/Cmd+D` | Duplicate the selection |
| `Ctrl/Cmd+0` | Fit the canvas to the window |
| `Ctrl/Cmd+1` | View at true 100% actual-pixel size |
| `Enter` | Apply the pending selection transform |
| `Esc` | Cancel the current stroke or selection transform; otherwise deselect |

## Settings

Open Settings with the gear button in the toolbar. It is also available
from **Edit → Settings** on Windows and **WagleWaglePaint → Settings**
on macOS.

- **General:** choose the interface language. Restart the app after
  changing the language.
- **Drawing:** turn the wobble animation on or off and choose how it
  behaves while drawing.
- **Files:** choose the default folder used by save and export dialogs.
- **Shortcuts:** replace any application shortcut and restore the
  defaults when needed.
- **About:** view the currently installed WagleWaglePaint version.

## Automatic updates

WagleWaglePaint checks for updates after launch. You can also use
**Help → Check for Updates** at any time. Updates are downloaded and
installed through Sparkle on macOS and Velopack on Windows, and the
update alert shows the release notes for the new version.

## For developers

See [BUILDING.md](BUILDING.md) for source-build and test instructions.

## License

WagleWaglePaint is distributed under the
[GNU General Public License v3.0](LICENSE). The bundled Pretendard JP
font is licensed under the
[SIL Open Font License 1.1](resources/fonts/OFL.txt).

Copyright (C) 2026 Nyabi (nyattic)
