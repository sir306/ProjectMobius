# Repository Guidelines

## Project Structure & Module Organization
- `Source/` contains Unreal C++ modules such as `ProjectMobius` (core gameplay), `MobiusWidgets`, `Visualization`, `HeatmapVisualization`, `HIT_ThesisWork`, and `ErrorHandling`. Keep public headers in `Public/` and implementation in `Private/`.
- `Content/` holds assets; `Config/` stores project .ini files; `Plugins/` carries third-party code (e.g., assimp/OpenCV) and should be kept lean.
- `BuildDocs/` tracks licensing artifacts for distribution; `_superbuild/` is a CMake build tree for Qt utilities; `Tools/QT_Apps/` contains the Qt apps, and `Tools/bin/Win64/` houses their packaged binaries.
- `Intermediate/`, `Saved/`, `DerivedDataCache/`, and `Binaries/` are generated; do not hand-edit or commit them.

## Build, Test, and Development Commands
- Unreal (editor build): open `ProjectMobius.uproject` in Unreal Engine 5.5, "Generate Visual Studio project files," then build the `ProjectMobiusEditor` target in Visual Studio (Development Editor).
- Package via UAT (example): `RunUAT.bat BuildCookRun -project="$(Resolve-Path ProjectMobius.uproject)" -noP4 -platform=Win64 -clientconfig=Development -cook -build -stage -pak -archive -archivedirectory=./Binaries/Release`.
- Qt utilities: `cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.9.2/msvc2022_64"` then `cmake --build _superbuild --config Release --parallel`.

## Coding Style & Naming Conventions
- Follow Unreal style: 4-space indent, brace-on-new-line for functions, `UE_LOG`/`ensure` for runtime checks, and minimal `#include` scope.
- Types use prefixes (`U` objects, `A` actors, `F` structs, `I` interfaces, `S` Slate widgets). Functions and methods are PascalCase; locals are camelCase. Align module API in `Public/` with matching `Private` implementations.

## Testing Guidelines
- Favor Unreal automation tests colocated with the feature module; name files `*Tests.cpp` and register with the Automation framework. Place sample data in `UnitTestSampleData/` when needed.
- Headless run example: `UnrealEditor-Cmd.exe ProjectMobius.uproject -run=Automation -Test=All -unattended -nop4 -log`.
- For Qt utilities, wire tests into CTest in `_superbuild` and run `ctest --test-dir _superbuild -C Release`.

## Commit & Pull Request Guidelines
- Commit messages mirror existing history: short, imperative, and scoped (e.g., "Improve mesh validation").
- PRs should describe intent and affected modules, list build/test commands executed, and attach screenshots or short clips for UI/visual changes. Link issues/tasks and call out asset or license updates in `BuildDocs/` when applicable.

## Asset & Config Tips
- Organize new assets in `Content/FeatureName/` and avoid committing oversized binaries unless required. Document any imported licenses.
- Keep `Config/*.ini` changes minimal; explain default changes in the PR. Never edit generated `Intermediate/`, `Saved/`, or `DerivedDataCache/` content.