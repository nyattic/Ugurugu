# Building WagleWaglePaint

## Requirements

- CMake 3.25 or later
- Qt 6.10 or later
- spdlog and fmt
- An internet connection while configuring so CMake can fetch the
  pinned Sparkle or Velopack dependency

## macOS

The macOS presets use Ninja. Install the dependencies with Homebrew:

```sh
brew install cmake ninja qt spdlog fmt
```

Configure, build, and create a deployable app:

```sh
cmake --preset macos-release
cmake --build --preset macos-release
cmake --install out/build/macos-release --prefix out/install/macos-release
open out/install/macos-release/WagleWaglePaint.app
```

The supported deployment target is macOS 13 or later.

## Windows

Install Visual Studio 2022 with the Desktop development with C++
workload and Qt 6.10 for MSVC 2022 x64. Install spdlog and fmt with
vcpkg:

```powershell
vcpkg install spdlog:x64-windows fmt:x64-windows
```

Configure, build, and create a deployable application directory:

```powershell
cmake --preset windows-release "-DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64" "-DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build --preset windows-release
cmake --install out/build/windows-release --config Release --prefix out/install/windows-release
.\out\install\windows-release\WagleWaglePaint.exe
```

## Tests

Use the debug preset for the current platform:

```sh
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
```

On Windows, replace `macos-debug` with `windows-debug`.
