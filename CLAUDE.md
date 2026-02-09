# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Project Mobius is an Unreal Engine 5.5 crowd simulation and visualization suite (Windows verified, macOS partial). It combines real-time pedestrian simulation with GPU-driven Gaussian density heatmaps, in-engine ImPlot/ImGui data visualization, and an IPC subsystem for inter-process communication. The Unreal project lives in `UnrealFolder/ProjectMobius/`.

## Build Commands

### Superbuild (external dependencies: Assimp + HDF5)
```bash
cd UnrealFolder/ProjectMobius
cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64
cmake --build _superbuild --config Release --parallel
ctest -C Release --output-on-failure
```

### Unreal Editor Build
Open `UnrealFolder/ProjectMobius/ProjectMobius.uproject` in UE 5.5, generate Visual Studio project files, then build `ProjectMobiusEditor` (Development Editor) in VS 2022.

### Package via UAT
```bash
RunUAT.bat BuildCookRun -project="$(Resolve-Path ProjectMobius.uproject)" -noP4 -platform=Win64 -clientconfig=Development -cook -build -stage -pak -archive -archivedirectory=./Binaries/Release
```

### Qt Tools (per app in `Tools/QT_Apps/<App>`)
```bash
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
cmake --install build --config Release
```

### Node.js Server (legacy, deprecated — prefer IPC subsystem)
```bash
cd UnrealFolder/ProjectMobius/Tools/NodeJS
npm install
npx pkg . --out-path dist
```

## Testing

- Automation tests go alongside their module, named `*Tests.cpp`, using Unreal's Automation framework.
- Sample data lives in `UnrealFolder/ProjectMobius/UnitTestSampleData/` or root `TestData/`.
- Headless test run: `UnrealEditor-Cmd.exe ProjectMobius.uproject -run=Automation -Test=All -unattended -nop4 -log`
- Superbuild-driven UE automation: enable with `-DSUPERBUILD_ENABLE_UE_AUTOMATION=ON` (see `BuildDocs/UE-Automation-Guide.md`).

## Architecture

### Module Dependency Graph

```
MobiusLogging          (foundation — thread-safe logging)
    ↓
MobiusCore             (central hub — IPC, heatmap, loading, subsystems)
    ↓
HeatmapVisualization   (GPU heatmap rendering)
Visualization          (OpenCV integration, texture processing)
    ↓
ProjectMobius          (main gameplay — MASS ECS crowd AI, Niagara, UI)
MobiusWidgets          (UMG widgets — has known circular dep with ProjectMobius)
HIT_ThesisWork         (research module for crowd behavior)
```

### Key Subsystems (in MobiusCore)

| Subsystem | Purpose |
|-----------|---------|
| `IpcSubsystem` | Named pipes (Win) / Unix sockets (Mac) to Qt apps — **primary IPC** |
| `HeatmapSubsystem` | GPU heatmap rendering pipeline |
| `StatisticSubsystem` | Metrics collection |
| `MobiusUserFeedbackSubsystem` | Throttled error popups to user |
| `MobiusCustomLoggerSubsystem` | Thread-safe file logging |
| `NativeFileDialogSubsystem` | Platform-native file selection |
| `LoadingSubsystem` | Async data loading |
| `TimeDilationSubSystem` | Simulation speed control |
| `WebSocketSubsystem` | Legacy — connects to Node.js server (deprecated) |

### Key Subsystems (in MobiusWidgets)

| Subsystem | Purpose |
|-----------|---------|
| `ImPlotVisualizationSubsystem` | ImPlot overlay visibility and rendering state |
| `ImPlotDataSubsystem` | Plot data management and forwarding to overlay |

### Crowd Simulation (ProjectMobius module)

Uses Epic's **MASS Entity** framework (ECS pattern):
- **Fragments** in `Public/MassAI/Fragments/` define per-entity simulation state
- **Processors** in `Public/MassAI/Processors/` handle movement, collision, representation, heatmap updates
- **Niagara** particles for visual representation of agents

### Plugins

| Plugin | Purpose |
|--------|---------|
| `Hdf5DataPlugin` | HDF5 trajectory file reading (Mobius & Juelich formats) |
| `UE4_Assimp` | Multi-format 3D model importing (FBX, OBJ, GLTF, etc.) |
| `UE_OpenCV` | OpenCV 4.5.5 integration for image processing |

### External Tools

- **Qt Plotting App** — real-time metric visualization via IPC (sub-100ms intervals)
- **Qt File-Dialog Server** — REST/TCP native file dialog exposure
- **Node.js Server** — legacy WebSocket bridge (deprecated, prefer `IpcSubsystem`)

### Third-Party Libraries (bundled)

| Library | Location | License |
|---------|----------|---------|
| ASSIMP 5.4.3 | `ASSIMP_5.4.3/` | BSD-3-Clause |
| HDF5 2.0.0 | `HDF5/` | HDF5 License |
| OpenCV 4.5.5 | `Plugins/UE_OpenCV/` | BSD-3-Clause |
| earcut.hpp | `Source/MobiusCore/ThirdParty/` | ISC |
| Dear ImGui 1.92.5 | `Source/MobiusWidgets/ThirdParty/ImGui/` | MIT |
| ImPlot 0.17 | `Source/MobiusWidgets/ThirdParty/ImPlot/` | MIT |

## Coding Conventions

- **Unreal C++ style**: 4-space indent, brace-on-new-line for functions, `UE_LOG`/`ensure` for runtime checks.
- **Type prefixes**: `U` (UObject), `A` (Actor), `F` (struct), `I` (interface), `S` (Slate widget). Functions/methods: PascalCase. Locals: camelCase.
- **Module layout**: public headers in `Public/`, implementations in `Private/`.
- **Documentation**: Doxygen-style block comments for complex/new code (`/** @param ParamName description */`).
- **Blueprint renames**: when renaming `UFUNCTION`/`UCLASS` or moving files that affect Blueprints, add `[CoreRedirects]` entries.
- **Generated folders**: never edit `Intermediate/`, `Saved/`, `DerivedDataCache/`, `Binaries/`.
- **Third-party folders**: treat `ASSIMP_5.4.3/`, `HDF5/`, `ThirdParty/`, `ImportedOpenSourceAssets/` as vendored — avoid edits unless updating licenses or upstream drops.
- **Commits**: short, imperative messages (e.g., "Refactor SMoveableWindow"). PRs describe scope, list build/test commands, link issues, include screenshots for UI changes.
- **Assets**: organize under `Content/FeatureName/`. Update `LICENSE.md`, `ASSET_LICENSES.md`, and `BuildDocs/` when adding assets or tooling.

## Known Technical Debt

- **Circular dependency**: `MobiusWidgets` ↔ `ProjectMobius` (via `FloorStatsWidget`) — TODO: move mass components to MobiusCore.
- **OpenCV path**: hardcoded to UE_5.5 engine directory — needs path resolution fix.
- **WebSocket subsystem**: deprecated in favor of `IpcSubsystem` but still present.

## Prerequisites

- Unreal Engine 5.5, Visual Studio 2022 (C++17), CMake ≥ 3.21
