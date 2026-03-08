# VDO Cable

VDO Cable is a Windows standalone app for routing multiple application audio sources into separate VDO.Ninja streams. It captures app audio with WASAPI process loopback, gives each route its own websocket + WebRTC publisher, and ships with fixed release asset names for GitHub Pages and `releases/latest/download/...` links.

Project entry points:

- Native app: `audio-router/native-qt/`
- Release scripts: `audio-router/native-qt/qa/`
- Marketing site: `docs/`
- Docs E2E checks: `tests/docs/`

Local workflows:

```powershell
cmake -S audio-router/native-qt -B audio-router/native-qt/build-local -G "Visual Studio 17 2022" -A x64 -DVDO_CABLE_BUILD_TESTS=ON
cmake --build audio-router/native-qt/build-local --config Release --target vdocable vdocable_logic_test
ctest --test-dir audio-router/native-qt/build-local -C Release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File .\audio-router\native-qt\qa\run-fast-gate.ps1 -BuildDir build-local -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File .\audio-router\native-qt\qa\run-stability-gate.ps1 -BuildDir build-local -Configuration Release -Iterations 3
```

Route safety and signaling behavior:

- The UI blocks duplicate local Stream IDs based on the effective published ID, including password/salt hashing behavior.
- Server collision alerts such as `streamid-already-published` stop the affected route and surface a warning.
- WebSocket pinging is disabled and reconnect attempts use capped backoff to avoid hammering the signaling server.

Docs and release operations:

- GitHub Pages entry page: `docs/index.html`
- Release playbook: `docs/RELEASES.md`
- Stable download aliases:
  - `https://github.com/steveseguin/vdocable/releases/latest/download/vdocable-setup.exe`
  - `https://github.com/steveseguin/vdocable/releases/latest/download/vdocable-portable.exe`
  - `https://github.com/steveseguin/vdocable/releases/latest/download/vdocable-win64.zip`
