<p align="center">
  <img src="resources/icons/WobblePaint.png" width="128" alt="WagleWaglePaint app icon">
</p>

# WagleWaglePaint

[![Latest Release](https://img.shields.io/github/v/release/nyattic/WagleWaglePaint?style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e&color=ffc94a)](https://github.com/nyattic/WagleWaglePaint/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/nyattic/WagleWaglePaint/total?style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e&color=ffc94a)](https://github.com/nyattic/WagleWaglePaint/releases)
![License](https://img.shields.io/badge/license-MIT-ffc94a?style=for-the-badge&logo=opensourceinitiative&logoColor=white&labelColor=1e1b2e)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-ffc94a?style=for-the-badge&logo=qt&logoColor=white&labelColor=1e1b2e)

A native drawing app where every line wobbles. Draw once and your
sketch comes alive as a crisp, boiling-line animation ready to export
as a looping GIF.

Inspired by Shake Art DELUXE and PS1-style vertex jitter, with layers,
tablet pressure, selections, project files, and automatic updates.

## Download

Get the newest version from
[GitHub Releases](https://github.com/nyattic/WagleWaglePaint/releases/latest).

| Platform | Supported systems | Download this file |
| --- | --- | --- |
| Windows | Windows 10 or later, x64 | `WagleWaglePaint-<version>-Windows-x64-Setup.exe` |
| macOS | macOS 13 or later, Apple Silicon | `WagleWaglePaint-<version>-macOS-arm64.dmg` |

The `.zip`, `.nupkg`, `appcast.xml`, and `.json` files on the release
page are used by the automatic updaters. They are not needed for a
normal installation.

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

- Brush and eraser with drawing-tablet pressure support
- 17 built-in pen, marker, airbrush, and spray presets
- Crisp pixel-edged strokes inspired by hand-drawn animation tools
- Boiling-line wobble with independently redrawn, fully loopable frames
- Lasso and auto selection with move, delete, and undo support
- Paint bucket fills that wobble together with the line art
- Layers with thumbnails, visibility, opacity, and drag reordering
- Adjustable wobble strength, frame count, and FPS
- Looping GIF export and current-frame PNG export
- `.wagle` project files with undo and redo
- Automatic recovery of unsaved work after an interrupted session
- Persistent recent colors and a configurable default save folder
- Customizable shortcuts and English or Korean interface
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
| `Space` + drag | Pan the canvas |
| Scroll | Zoom |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo on Windows |
| `Cmd+Z` / `Cmd+Shift+Z` | Undo / Redo on macOS |
| `Ctrl/Cmd+E` | Export an animated GIF |
| `Ctrl/Cmd+0` | Fit the canvas to the window |
| `Esc` | Cancel the current stroke or selection |

## Settings

Open Settings with the gear button in the toolbar. It is also available
from **Edit → Settings** on Windows and **WagleWaglePaint → Settings**
on macOS.

- **General:** choose the interface language and drawing animation
  behavior. Restart the app after changing the language.
- **Files:** choose the default folder used by save and export dialogs.
- **Shortcuts:** replace any application shortcut and restore the
  defaults when needed.

## Automatic updates

WagleWaglePaint checks for updates after launch. You can also use
**Help → Check for Updates** at any time. Updates are downloaded and
installed through Sparkle on macOS and Velopack on Windows.

## For developers

See [BUILDING.md](BUILDING.md) for source-build and test instructions.

## License

[MIT](LICENSE)
