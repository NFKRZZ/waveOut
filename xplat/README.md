# waveOut Cross-Platform Rewrite Scaffold

This folder is a clean rewrite path for a fast cross-platform GUI architecture:

- Window/input: `SDL3`
- GPU renderer: `bgfx`
- UI layer: `ImGui` hooks are scaffolded (`src/ui/ImGuiLayer.*`)
- Language/build: `C++20` + `CMake`

The existing Win32/GDI app remains unchanged. This target is intended for incremental migration.

## Current Status

- Creates a resizable SDL3 window.
- Initializes bgfx on the native platform backend.
- Runs a GPU render loop with:
  - spectrogram texture pass (uploaded once, UV-scrolled on GPU)
  - waveform layer quads
  - beat/bar/playhead line overlays
- CPU prepares scene data; GPU performs rasterization.
- Shows frame time and FPS in both bgfx debug text and window title.
- Includes no-op ImGui layer hooks for future integration.
- Can load a real audio file (`wav/mp3/flac/...`) via command line.

## Build

### 1) Install dependencies (example: vcpkg)

```powershell
vcpkg install sdl3 bgfx
```

### Visual Studio (Recommended on Windows)

1. Open `xplat/` as a CMake folder in Visual Studio 2022.
2. Select preset `VS2022 x64 Debug` or `VS2022 x64 Release`.
3. Build `waveout_xplat`.
4. In Debug > Debug and Launch Settings, set command args to your track path, e.g.
   `C:\music\track.wav`

Presets are defined in [`CMakePresets.json`](CMakePresets.json) and already point at
`C:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake`.

### 2) Configure

```powershell
cmake -S xplat -B build/xplat -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### 3) Build

```powershell
cmake --build build/xplat --config Release
```

### 4) Compile bgfx shader binaries

`waveout_xplat` loads precompiled shader binaries from `xplat/assets/shaders/<renderer>/`.
Compile them with bgfx `shaderc`:

```powershell
powershell -ExecutionPolicy Bypass -File xplat/tools/Compile-BgfxShaders.ps1 `
  -ShadercPath "C:\path\to\shadercRelease.exe" `
  -BgfxShaderIncludeDir "C:\path\to\bgfx\src"
```

### 5) Run

Run `waveout_xplat` from the build output directory.

```powershell
.\waveout_xplat.exe "C:\path\to\track.wav"
```

If no file path is provided, the app falls back to synthetic scene data.

### One-command setup (PowerShell)

```powershell
powershell -ExecutionPolicy Bypass -File xplat/tools/Setup-Xplat.ps1 -Config Release
```

## Controls

- `Space`: play/pause timeline motion
- `Left/Right`: nudge timeline by 1 second
- `Home`: seek to start
- `Mouse wheel`: zoom timeline in/out

## Migration Plan

1. Replace demo `WaveScene` with real waveform/spectrogram data from your audio engine.
2. Move waveform + grid geometry from transient to persistent dynamic buffers.
3. Add interaction model (zoom/pan/scrub/selection) in the new renderer.
4. Replace no-op ImGui layer with real SDL3/bgfx ImGui backend integration.
5. Port playback controls and piano roll UI into this pipeline.
