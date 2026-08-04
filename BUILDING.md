# Building Ugurugu

## Requirements

- CMake 3.25 or later, and 4.2 or later on Windows for the
  Visual Studio 18 2026 generator
- Qt 6.10 or later; releases are built against Qt 6.11.1
- An internet connection while configuring so CMake can fetch the
  pinned spdlog, Sparkle, or Velopack dependencies

## macOS

The macOS presets use Ninja. Install the dependencies with Homebrew:

```sh
brew install cmake ninja qt
```

Configure, build, and create a deployable app:

```sh
cmake --preset macos-release
cmake --build --preset macos-release
cmake --install out/build/macos-release --prefix out/install/macos-release
open out/install/macos-release/Ugurugu.app
```

The supported deployment target is macOS 14 or later. Use the official
Qt 6.11.1 binaries when creating a distributable build; Homebrew Qt may
have a newer deployment target.

## Windows

Install Visual Studio 2026 with the Desktop development with C++
workload and its C++ Clang tools for Windows component, plus
Qt 6.11.1 for MSVC 2022 x64.

The Windows presets use the Visual Studio 18 2026 generator with the
ClangCL toolset, so the build uses clang-cl against the MSVC ABI. The
Qt MSVC 2022 kit is the right one to install: MSVC v145 keeps binary
compatibility with v143, and the Velopack updater ships an MSVC import
library.

Configure, build, and create a deployable application directory:

```powershell
cmake --preset windows-release "-DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64"
cmake --build --preset windows-release
cmake --install out/build/windows-release --config Release --prefix out/install/windows-release
.\out\install\windows-release\Ugurugu.exe
```

## Tests

Use the debug preset for the current platform:

```sh
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
```

On Windows, replace `macos-debug` with `windows-debug`.
