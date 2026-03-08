# VDO Cable Native Qt

Windows standalone app for routing multiple application audio sources to separate VDO.Ninja stream IDs.

## Scope

- Audio-only v1
- One selected Windows process per route
- One websocket signaling connection per route
- One WebRTC publisher with one audio track per route
- Multiple routes can run at the same time

This is not a virtual audio driver. It uses WASAPI process loopback to target the "publish app audio as separate VDO.Ninja feeds" workflow directly.

## Build

Prerequisites:

- Visual Studio 2022 with C++
- CMake 3.24+
- Qt 6 Widgets

```powershell
cmake -S . -B build-local -G "Visual Studio 17 2022" -A x64 -DVDO_CABLE_BUILD_TESTS=ON
cmake --build build-local --config Release --target vdocable vdocable_logic_test
ctest --test-dir build-local -C Release --output-on-failure
```

## QA and packaging

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\qa\run-fast-gate.ps1 -BuildDir build-local -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File .\qa\build-release.ps1 -BuildDir build-local -Configuration Release -Version 0.1.0
```

Release outputs land in `dist/` with these stable aliases:

- `vdocable-setup.exe`
- `vdocable-portable.exe`
- `vdocable-win64.zip`
