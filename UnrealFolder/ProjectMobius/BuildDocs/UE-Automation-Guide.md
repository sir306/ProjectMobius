# Unreal Automation Hooks in the Superbuild

## Overview
The superbuild now has optional ctests to:
- Run Unreal automation (UAT/`UnrealEditor-Cmd`) if you supply the executable and arguments.
- Generate project files and build the editor target if you supply the UE batch/script paths and args.
These are OFF by default to avoid surprise engine invocations.

## CMake Options (all cache variables)
- `SUPERBUILD_ENABLE_UE_AUTOMATION` (OFF): add a ctest to run Unreal automation.
  - `UE_AUTOMATION_EXE`: path to `UnrealEditor-Cmd.exe` (or platform equivalent).
  - `UE_AUTOMATION_ARGS`: args such as `-run=Automation -Test=All -unattended -nop4 -log=Automation.log`.
- `SUPERBUILD_ENABLE_UE_GENBUILD` (OFF): add ctests to GenerateProjectFiles and build the editor target.
  - `UE_GENERATE_FILES_EXE`: path to `GenerateProjectFiles.bat` (or `.sh`).
  - `UE_GENERATE_FILES_ARGS`: e.g. `-project=ProjectMobius.uproject -game -engine`.
  - `UE_BUILD_EXE`: path to UE build helper (e.g. `Engine/Build/BatchFiles/Build.bat`).
  - `UE_BUILD_ARGS`: e.g. `ProjectMobiusEditor Win64 Development -project=ProjectMobius.uproject -game -engine -progress`.

## Adding the Tests
Pass the options/paths on configure, e.g. (PowerShell):
```powershell
cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.9.2/msvc2022_64" `
  -DSUPERBUILD_ENABLE_TESTS=ON `
  -DSUPERBUILD_RUN_TESTS=ON `
  -DSUPERBUILD_ENABLE_UE_AUTOMATION=ON `
  -DUE_AUTOMATION_EXE="C:/Program Files/Epic/UE_5.5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" `
  -DUE_AUTOMATION_ARGS="-run=Automation;-Test=All;-unattended;-nop4;-log=Automation.log" `
  -DSUPERBUILD_ENABLE_UE_GENBUILD=ON `
  -DUE_GENERATE_FILES_EXE="C:/Program Files/Epic/UE_5.5/Engine/Build/BatchFiles/GenerateProjectFiles.bat" `
  -DUE_GENERATE_FILES_ARGS="-project=ProjectMobius.uproject;-game;-engine" `
  -DUE_BUILD_EXE="C:/Program Files/Epic/UE_5.5/Engine/Build/BatchFiles/Build.bat" `
  -DUE_BUILD_ARGS="ProjectMobiusEditor;Win64;Development;-project=ProjectMobius.uproject;-game;-engine;-progress"
```
> Note: For Windows, semicolons split args in CMake; avoid quoting the entire list.

macOS/Linux (Ninja example):
```sh
cmake -S . -B _superbuild -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSUPERBUILD_ENABLE_TESTS=ON \
  -DSUPERBUILD_RUN_TESTS=ON \
  -DSUPERBUILD_ENABLE_UE_AUTOMATION=ON \
  -DUE_AUTOMATION_EXE="/Applications/UE_5.5/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  -DUE_AUTOMATION_ARGS="-run=Automation;-Test=All;-unattended;-nop4;-log=Automation.log" \
  -DSUPERBUILD_ENABLE_UE_GENBUILD=ON \
  -DUE_GENERATE_FILES_EXE="/Applications/UE_5.5/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh" \
  -DUE_GENERATE_FILES_ARGS="-project=ProjectMobius.uproject;-game;-engine" \
  -DUE_BUILD_EXE="/Applications/UE_5.5/Engine/Build/BatchFiles/Mac/Build.sh" \
  -DUE_BUILD_ARGS="ProjectMobiusEditor;Mac;Development;-project=ProjectMobius.uproject;-game;-engine;-progress"
cmake --build _superbuild --parallel
```

## What ctest Will Do (when enabled)
- `ue_automation`: runs `UE_AUTOMATION_EXE UE_AUTOMATION_ARGS` (timeout 15 min).
- `ue_generate_project_files`: runs GenerateProjectFiles with your args (timeout 15 min).
- `ue_build_editor`: runs the UE build helper with your args (timeout 30 min).
All run from `${UE_ROOT}` (the superbuild source root) so relative `ProjectMobius.uproject` paths work.

## UAT Automation Examples
- All tests: `UnrealEditor-Cmd.exe ProjectMobius.uproject -run=Automation -Test=All -unattended -nop4 -log=Automation.log`
- A subset: `-Test=ProjectMobius.Functional` or any specific test name.

## Notes
- These tests are opt-in; by default they are not added to avoid heavy UE invocations.
- On Windows, use semicolons to separate arguments passed via CMake cache (they become spaces when invoked).
- Ensure your UE install matches the project version (UE 5.5 per repo guide).
- Consider setting a longer ctest timeout via `ctest -T test --timeout <seconds>` if your builds are slow.
