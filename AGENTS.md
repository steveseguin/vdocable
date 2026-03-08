# Repository Guidelines

## Project Structure & Module Organization
`game-capture/native-qt/` contains the Windows Qt app, with code in `src/`, headers in `include/versus/`, and verification in `tests/`, `e2e/`, and `qa/`. `ninja-plugin/` is the OBS plugin, with native sources, PowerShell helpers in `scripts/`, and coverage in `tests/`. `ninjasdk/` ships the JavaScript SDK plus `MCP/` tools. `vst/` contains the VST3 bridge (`webrtc_vst/`), CLI tools, and integration tests. `vdoninja/` is the browser app plus services like `vdo-auth-service/` and `tests/playwright/`. `vst3sdk/` is vendored upstream code; avoid editing it unless updating the Steinberg SDK.

## Build, Test, and Development Commands
There is no single root build. Run commands from the module you change:

- `cd game-capture/native-qt && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`
- `cd game-capture/native-qt && powershell -File .\qa\run-fast-gate.ps1 -BuildDir build -Configuration Release`
- `cd ninja-plugin && cmake -B build -DBUILD_TESTS=ON -DBUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target vdoninja-tests && ctest --test-dir build --output-on-failure`
- `cd ninjasdk && npm test`
- `cd vst && npm run test:integration`
- `cd vdoninja/vdo-auth-service && npm run dev` or `npm test`
- `cd vdoninja/tests/playwright && npm test`

## Coding Style & Naming Conventions
Follow the local style of the module you are touching. JavaScript under `vdoninja/` uses Prettier with tabs, semicolons, and double quotes (`vdoninja/.prettierrc`). Native code follows 4-column indentation and the `clang-format` rules in `ninja-plugin/.clang-format`. Keep C++ filenames in `snake_case` such as `dual_stream_policy.cpp`, GoogleTest files as `test_*.cpp`, Node or Vitest files as `*.test.js`, and Playwright specs as `*.spec.js`. Do not hand-edit generated artifacts such as `vdoninja/web/release/` or `*.min.js` unless the source change also regenerates them.

## Testing Guidelines
Add a regression test for each behavior change. Prefer module-local unit tests first, then E2E coverage when the change affects signaling, browser behavior, or OBS integration. Live and soak scripts exist in several modules, but they are opt-in verification, not the default gate; mention them explicitly in PRs when you use them.

## Commit & Pull Request Guidelines
This workspace snapshot does not include a root `.git`, so there is no history to mine. Use short, imperative, scoped commit subjects such as `fix(vst): ignore video tracks in play mode` or `test(ninja-plugin): cover reload flow`. PRs should name the affected module, summarize user-visible impact, list the commands you ran, and attach screenshots or log excerpts for UI, OBS, or browser-streaming changes. Never commit secrets; use Wrangler secrets and local environment configuration instead.
