<p align="center">
  <img src="resources/icons/Ugurugu.png" width="128" alt="Ugurugu app icon">
</p>

# Ugurugu

[![Latest Release](https://img.shields.io/github/v/release/nyattic/Ugurugu?style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e&color=ffc94a)](https://github.com/nyattic/Ugurugu/releases/latest)
[![Downloads](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fnyattic%2FUgurugu%2Fdownload-badge%2Fdownloads.json&style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e)](https://github.com/nyattic/Ugurugu/releases)
![License](https://img.shields.io/badge/license-GPL--3.0-ffc94a?style=for-the-badge&logo=gnu&logoColor=white&labelColor=1e1b2e)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-ffc94a?style=for-the-badge&logo=qt&logoColor=white&labelColor=1e1b2e)

<p align="center"><a href="README.md">KR</a> · <b>EN</b> · <a href="README.ja.md">JP</a></p>

Ugurugu is a drawing app where your pictures wiggle and move.
You do not need to learn traditional animation: draw a picture, press Play,
and watch it move.

Save your work as a looping GIF or WebP, or create a still image with a
transparent background. You can also continue working on `.wawa` drawings
made with WiggleWiggleTool.

> [!NOTE]
> If something goes wrong, please open a
> [GitHub Issue](https://github.com/nyattic/Ugurugu/issues) and tell
> us what you were doing and what happened. Attaching the `.ugu` file, when
> possible, makes the problem much easier to find.

## Download and install

| Platform | Requirements | Download |
| --- | --- | --- |
| Windows | Windows 10 or later, 64-bit | [Windows installer](https://github.com/nyattic/Ugurugu/releases/latest/download/Ugurugu-Windows-x64-Setup.exe) |
| macOS | macOS 14 or later, Apple Silicon | [macOS installer](https://github.com/nyattic/Ugurugu/releases/latest/download/Ugurugu-macOS-arm64.dmg) |

### Windows

Run the downloaded Setup file. If Windows shows an unrecognized-app warning,
choose **More info → Run anyway**. For your safety, only use files from the
official [Releases page](https://github.com/nyattic/Ugurugu/releases/latest).

### macOS

Open the DMG and drag Ugurugu into the Applications folder. The macOS
app is checked by Apple before release.

The other files on the release page are used by automatic updates. For a
normal installation, you only need the installer in the table above.

## Get started

1. Open the app and create a canvas, or open an existing drawing.
2. Choose a brush from the left side and draw a line.
3. Pick a movement from the Wobble button, then press `P` to play it.
4. Use the **File** menu to export a moving GIF or WebP, or a PNG or JPG image.

Press `F1` at any time to open the built-in help.

## What can you do?

### Make lines move your way

- Choose smooth movement, a stop-motion-like stepped movement, or the familiar
  classic movement.
- Adjust how far lines move, how detailed the movement feels, how closely the
  lines move together, and how much surprise is added.
- Create lines with playful gaps, or let erased areas move with the drawing.
- Turn movement off whenever you want to use the app like a regular paint app.

### Draw and color comfortably

- Use pressure-sensitive brushes and erasers, with 17 ready-made pens,
  markers, airbrushes, and sprays.
- Smooth out slow or long strokes with adjustable line stabilization.
- Tell the paint bucket how similar nearby colors should be and whether it
  should look at the current layer or other layers too.
- Select freehand, rectangular, or oval areas, then move, resize, rotate,
  flip, copy, and paste them.
- Switch the area tool from selecting to painting when you want to fill the
  shape you draw immediately.

### Work with layers and images

- Stack layers, organize them into groups, and change their transparency or
  how their colors mix.
- Merge with the layer below when the result can stay exactly as it looks.
- Place a photo or another drawing on a new layer, then move, resize, or rotate
  it. Repeated resizing always starts from the original image.
- Crop or expand the canvas, or resize the whole drawing.

### Save and share

- Save projects as `.ugu` files with layers and movement settings intact.
- Projects saved by earlier versions as `.wagle` or `.wobble` still open.
- Export moving GIF and WebP files, or still PNG and JPG images.
- Use a transparent canvas to make sticker-like pictures and animations with
  no background.
- Save your favorite brush, fill, and movement setup as a `.wwpreset` file and
  load it on another computer.
- Recover your work after an unexpected shutdown.

### Make the app yours

- Pick the highlight color used around the app.
- Change any shortcut and restore the defaults whenever you want.
- Use the app in English, Korean, or Japanese.
- Check for new versions and update from inside the app.

## Continue a WiggleWiggleTool drawing

Open a `.wawa` file saved by WiggleWiggleTool 10 with **File → Open**. It opens
as a new project without changing the original. The first time you save it,
Ugurugu suggests a `.ugu` file with the same name.

Because the two apps draw in different ways, some wobble, airbrush, and filled
shapes may look a little different. After opening the file, the app tells you
what was changed or could not be brought across.

## Handy shortcuts

These are the defaults. You can change all of them in **Settings → Shortcuts**.

| Key | Action |
| --- | --- |
| `B` / `E` | Brush / Eraser |
| `L` / `W` / `G` | Area select / Auto select / Paint bucket |
| `P` | Play or pause movement |
| `Space` + drag | Move around the canvas |
| Scroll | Zoom in or out |
| `Alt` + click | Pick a color from the drawing |
| `Ctrl/Cmd+C`, `X`, `V` | Copy / Cut / Paste |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo on Windows |
| `Cmd+Z` / `Cmd+Shift+Z` | Undo / Redo on macOS |
| `Ctrl/Cmd+0` | Fit the canvas to the window |
| `Ctrl/Cmd+1` | View at actual pixel size |
| `Enter` / `Esc` | Apply / Cancel a selection change |
| `F1` | Open the built-in help |

## Settings and updates

Use the gear button to change the language, app color, drawing behavior,
default save folder, and shortcuts. Settings are also under **Edit → Settings**
on Windows and **Ugurugu → Settings** on macOS.

The app checks for a new version when it starts. You can check at any time with
**Help → Check for Updates**.

## For developers

See [BUILDING.md](BUILDING.md) if you want to build the app from source.

## Credits

- Development support by seuppi
- App icon artwork by seuppi

## License

Ugurugu is distributed under the
[GNU General Public License v3.0](LICENSE). Copyright and license details for
the included font and libraries are in the
[third-party notices](THIRD_PARTY_NOTICES.md).

Copyright (C) 2026 Nyabi (nyattic)
