# Release Playbook

Use this flow so the GitHub Pages download buttons and `releases/latest/download/...` aliases stay stable.

## Stable filenames

- `vdocable-setup.exe`
- `vdocable-portable.exe`
- `vdocable-win64.zip`

Every tagged release should upload both the versioned assets and these three stable aliases.

## Preferred local release flow

From repo root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\audio-router\native-qt\qa\release-and-publish.ps1 -Version <version> -BuildDir build-release
```

This runs:

- native fast gate
- native stability gate
- release packaging
- code signing
- VirusTotal submission
- GitHub release create or update

The native fast gate must pass the packaged-app launch check from `dist/` with a stripped-down `PATH`; this prevents shipping builds that only work on the developer machine because Qt or VC runtimes are installed globally.

## Local build + package

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\audio-router\native-qt\qa\run-fast-gate.ps1 -BuildDir build-release -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File .\audio-router\native-qt\qa\build-release.ps1 -BuildDir build-release -Configuration Release -Version <version>
```

Expected outputs in `audio-router/native-qt/dist/`:

- `vdocable-<version>-setup.exe`
- `vdocable-<version>-portable.exe`
- `vdocable-<version>-win64.zip`
- `vdocable-setup.exe`
- `vdocable-portable.exe`
- `vdocable-win64.zip`

## Signing and VirusTotal

Local scripts support the same two modes used by the other apps:

- Local signing bundle at `C:\Users\Steve\code\code-signing`
- CI secrets:
  - `WINDOWS_SIGN_PFX_BASE64`
  - `WINDOWS_SIGN_PFX_PASSWORD`
  - `VIRUSTOTAL_API_KEY`

Optional local VirusTotal key locations:

- `audio-router/native-qt/.vt-apikey`
- repo root `.vt-apikey`

The release packaging step also copies the VC++ runtime and required Qt support DLLs into the stage directory, so a fresh Windows machine does not depend on the local Qt SDK or Visual Studio runtime being preinstalled.

## Dependency checkout

Root-repo CI and release jobs clone `steveseguin/game-capture` into `game-capture/` before building. Local builds still work with a sibling checkout or with `-DVDO_CABLE_GAME_CAPTURE_ROOT=<path>`.

## Route safety checks

- The app prevents local duplicate published Stream IDs across routes before start.
- Server `alert` collisions such as `streamid-already-published` stop the route and show a UI warning.
- Signaling recovery uses capped backoff and leaves websocket pinging disabled.

## Fixed download links

- `https://github.com/steveseguin/vdocable/releases/latest/download/vdocable-setup.exe`
- `https://github.com/steveseguin/vdocable/releases/latest/download/vdocable-portable.exe`
- `https://github.com/steveseguin/vdocable/releases/latest/download/vdocable-win64.zip`
