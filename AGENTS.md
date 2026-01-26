# Repository Guidelines

## Project Structure & Module Organization
The Unreal project lives in `UnrealFolder/ProjectMobius/` with `Source/`, `Content/`, `Config/`, `Plugins/`, `Tools/`, `UnitTestSampleData/`, and `ProjectMobius.uproject`. Root-level vendor and asset drops live in `ASSIMP_5.4.3/`, `HDF5/`, `ThirdParty/`, `ImportedOpenSourceAssets/`, `TestData/`, and `DemoProgressVideos/`. License bundles for redistribution are mirrored in `UnrealFolder/ProjectMobius/BuildDocs/`. Follow the UE-specific rules in `UnrealFolder/ProjectMobius/AGENTS.md` when changing code inside the Unreal project.

## Build, Test, and Development Commands
Windows is the only verified platform; use UE 5.5 and Visual Studio 2022.
- Superbuild (external deps + optional UE hooks) from `UnrealFolder/ProjectMobius/`:
  - `cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64`
  - `cmake --build _superbuild --config Release --parallel`
  - `ctest -C Release --output-on-failure`
- Unreal editor build: open `UnrealFolder/ProjectMobius/ProjectMobius.uproject`, generate VS project files, then build `ProjectMobiusEditor` (Development Editor).
- Qt tools (if source is present under `UnrealFolder/ProjectMobius/Tools/QT_Apps/<App>`):
  - `cmake -S . -B build -G "Visual Studio 17 2022"`
  - `cmake --build build --config Release`
  - `cmake --install build --config Release`
- Node server (if `Tools/NodeJS` exists): `npm install`, then `npx pkg . --out-path dist`.

## Coding Style & Naming Conventions
Use Unreal C++ conventions: 4-space indent, braces on new lines, PascalCase types/functions, and prefixes (`U`, `A`, `F`, `I`, `S`). Keep headers in `Public/` and implementations in `Private/`. Avoid editing generated folders (`Intermediate/`, `Saved/`, `DerivedDataCache/`, `Binaries/`) unless explicitly required, and preserve existing formatting for QML and CMake files.

## Testing Guidelines
Place Unreal automation tests alongside their modules and name files `*Tests.cpp`. Put sample data under `UnrealFolder/ProjectMobius/UnitTestSampleData/` or root `TestData/`. Headless run example: `UnrealEditor-Cmd.exe ProjectMobius.uproject -run=Automation -Test=All -unattended -nop4 -log`. For superbuild-driven UE automation, see `UnrealFolder/ProjectMobius/BuildDocs/UE-Automation-Guide.md`.

## Commit & Pull Request Guidelines
Commit messages in this repo are short, imperative statements (e.g., "Refactor SMoveableWindow"). PRs should describe scope, list build/test commands run, link issues, and include screenshots or short clips for UI or visualization changes.

## Assets, Licensing, and Security Notes
Treat third-party folders (`ASSIMP_5.4.3/`, `HDF5/`, `ThirdParty/`, `ImportedOpenSourceAssets/`) as vendored; avoid edits unless you are updating licenses or replacing upstream drops. If you add assets or tooling, update `LICENSE.md`, `ASSET_LICENSES.md`, and `UnrealFolder/ProjectMobius/BuildDocs/`. The legacy WebSocket server path is deprecated; prefer the IPC subsystem in `UnrealFolder/ProjectMobius/Source/MobiusCore` for tool integration.
