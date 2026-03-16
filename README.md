# Project Mobius

[![CodeQL](https://github.com/sir306/ProjectMobius/actions/workflows/codeql.yml/badge.svg)](https://github.com/sir306/ProjectMobius/actions/workflows/codeql.yml)
[![C++ Style](https://github.com/sir306/ProjectMobius/actions/workflows/cpp-style-check.yml/badge.svg)](https://github.com/sir306/ProjectMobius/actions/workflows/cpp-style-check.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.5-black?logo=unrealengine)](https://www.unrealengine.com/)
[![Last Commit](https://img.shields.io/github/last-commit/sir306/ProjectMobius?label=Last%20Commit)](https://github.com/sir306/ProjectMobius/commits/main)
[![Human Contributors](https://img.shields.io/github/contributors/sir306/ProjectMobius?label=Human%20Contributors)](https://github.com/sir306/ProjectMobius/graphs/contributors)

**Real-time crowd simulation and visualization in Unreal Engine 5.5**

<p align="center">
  <img src="docs/images/hero-crowd-simulation.png" alt="Project Mobius — 1000-agent crowd simulation" width="720">
</p>

<p align="center">
  <em>1,000 pedestrian agents simulated in real time using MASS Entity ECS and Niagara particle rendering</em>
</p>

Project Mobius is an Unreal Engine 5.5 crowd simulation and visualization suite for Windows (macOS partial support). It combines real-time pedestrian simulation using Epic's MASS Entity framework with GPU-driven Gaussian density heatmaps, HDF5 trajectory data import, multi-format 3D model loading via Assimp, and an in-engine ImPlot/ImGui data visualization overlay.

<p align="center">
  <img src="docs/images/crowd-simulation-overview.png" alt="Crowd simulation overview" width="360">
  <img src="docs/images/vr-demo-loading.png" alt="VR demo mode" width="180">
</p>

---

## Table of Contents

1. [Features](#features)
2. [Repository Structure](#repository-structure)
3. [Architecture](#architecture)
   - [Module Dependency Graph](#module-dependency-graph)
   - [Key Subsystems](#key-subsystems)
   - [In-Engine Visualization (ImPlot/ImGui)](#in-engine-visualization-implotimgui)
   - [Crowd Simulation](#crowd-simulation)
   - [Plugins](#plugins)
4. [Getting Started](#getting-started)
   - [Prerequisites](#prerequisites)
   - [Cloning the Repository](#cloning-the-repository)
   - [Superbuild (External Dependencies)](#superbuild-external-dependencies)
   - [Unreal Editor Build](#unreal-editor-build)
   - [Packaging (UAT)](#packaging-uat)
5. [Key Bindings](#key-bindings)
6. [Testing](#testing)
7. [Deprecated Components](#deprecated-components)
8. [License](#license)
9. [CI Workflows](#ci-workflows)
10. [Further Documentation](#further-documentation)

---

## Features

- **Crowd Simulation** -- real-time pedestrian movement using MASS Entity ECS (Fragments, Processors) with Niagara particle rendering
- **GPU Heatmaps** -- Gaussian density heatmaps with color-banded rendering for crowd density analysis
- **HDF5 Data Import** -- trajectory file reading in Mobius and Juelich formats via the Hdf5DataPlugin
- **3D Model Import** -- multi-format model loading (FBX, OBJ, GLTF, etc.) through the UE4_Assimp plugin
- **In-Engine Data Visualization** -- real-time evacuation stats, agent metrics, and multi-chart overlays via ImPlot/ImGui rendered as a Slate overlay
- **OpenCV Integration** -- image and texture processing (Gaussian blur, etc.) via the UE_OpenCV plugin
- **IPC Communication** -- named pipes (Windows) / Unix sockets (macOS) for inter-process communication
- **Procedural Mesh Generation** -- polygon triangulation using earcut.hpp
- **VR Support** -- VR input bindings (in development)
- **Platform-Native File Dialogs** -- built-in file selection without external tools

---

## Repository Structure

```
ProjectMobius/
├── docs/images/                       # README screenshots and visuals
├── DemoProgressVideos/                # Development progress recordings
├── HelpfulTextDocs/                   # Reference documentation
├── ImportedOpenSourceAssets/           # CC-licensed Blender models (.fbx)
├── TestData/                          # Root-level test data
├── UserManual/                        # End-user documentation
├── UnrealFolder/
│   └── ProjectMobius/
│       ├── ProjectMobius.uproject
│       ├── CMakeLists.txt             # Superbuild (Assimp + HDF5)
│       ├── Config/                    # Unreal .ini configuration
│       ├── Content/                   # Unreal assets (Blueprints, maps, UI)
│       ├── UnitTestSampleData/        # Automation test fixtures
│       ├── Plugins/
│       │   ├── Hdf5DataPlugin/        # HDF5 trajectory reader
│       │   ├── UE4_Assimp/            # Assimp 3D import plugin
│       │   └── UE_OpenCV/             # OpenCV 4.5.5 integration
│       └── Source/
│           ├── MobiusLogging/         # Thread-safe logging foundation
│           ├── MobiusCore/            # Central hub (IPC, heatmap, subsystems)
│           ├── HeatmapVisualization/  # GPU heatmap rendering
│           ├── Visualization/         # OpenCV texture processing
│           ├── ProjectMobius/         # Main gameplay (MASS ECS, Niagara, UI)
│           ├── MobiusWidgets/         # UMG/Slate widgets, ImPlot overlay
│           └── HIT_ThesisWork/        # Research module for crowd behavior
├── LICENSE                            # MIT license
├── THIRD-PARTY-LICENSES.md           # Third-party and asset license details
└── CLAUDE.md                          # AI assistant context file
```

---

## Architecture

### Module Dependency Graph

```
MobiusLogging              (foundation -- thread-safe logging)
    |
MobiusCore                 (central hub -- IPC, heatmap, loading, subsystems)
    |
    +-- HeatmapVisualization   (GPU heatmap rendering)
    +-- Visualization          (OpenCV integration, texture processing)
    |
    +-- ProjectMobius          (main gameplay -- MASS ECS crowd AI, Niagara, UI)
    +-- MobiusWidgets          (UMG/Slate widgets, ImPlot overlay)
    +-- HIT_ThesisWork         (research module for crowd behavior)

Note: MobiusWidgets <-> ProjectMobius have a known circular dependency
      (via FloorStatsWidget). TODO: move shared mass components to MobiusCore.
```

### Key Subsystems

| Subsystem | Module | Purpose |
|-----------|--------|---------|
| `IpcSubsystem` | MobiusCore | Named pipes (Win) / Unix sockets (Mac) -- **primary IPC** |
| `HeatmapSubsystem` | MobiusCore | GPU heatmap rendering pipeline |
| `StatisticSubsystem` | MobiusCore | Metrics collection |
| `LoadingSubsystem` | MobiusCore | Async data loading |
| `TimeDilationSubSystem` | MobiusCore | Simulation speed control |
| `NativeFileDialogSubsystem` | MobiusCore | Platform-native file selection |
| `MobiusCustomLoggerSubsystem` | MobiusCore | Thread-safe file logging |
| `MobiusUserFeedbackSubsystem` | MobiusCore | Throttled error popups |
| `ImPlotVisualizationSubsystem` | MobiusWidgets | ImPlot overlay visibility and rendering |
| `ImPlotDataSubsystem` | MobiusWidgets | Plot data management and forwarding |

### In-Engine Visualization (ImPlot/ImGui)

Project Mobius uses **ImGui v1.92.5** and **ImPlot v0.17** (vendored in `Source/MobiusWidgets/ThirdParty/`) for real-time in-engine data visualization. This replaces the previously external Qt plotting application.

**Data flow:**

```
UFloorStatsWidget --> UImPlotDataSubsystem --> UImPlotVisualizationSubsystem --> SImPlotOverlay
```

- `UFloorStatsWidget` pushes chart titles, axis settings, data points, and live samples
- `UImPlotDataSubsystem` manages plot data and forwards to the visualization subsystem
- `UImPlotVisualizationSubsystem` manages overlay visibility and ImPlot rendering state
- `SImPlotOverlay` renders ImPlot output as a Slate overlay directly in the viewport

The overlay subsystem exposes a `BlueprintCallable` API (`ShowOverlay`, etc.) for custom chart integration.

### Crowd Simulation

Uses Epic's **MASS Entity** framework (ECS pattern):

- **Fragments** (`Source/ProjectMobius/Public/MassAI/Fragments/`) define per-entity simulation state
- **Processors** (`Source/ProjectMobius/Public/MassAI/Processors/`) handle movement, collision, representation, and heatmap updates
- **Niagara** particles provide visual representation of agents

### Plugins

All plugins are included in this repository -- no separate cloning required. Paths below are relative to `UnrealFolder/ProjectMobius/`.

| Plugin | Location | Purpose |
|--------|----------|---------|
| `Hdf5DataPlugin` | `Plugins/Hdf5DataPlugin/` | HDF5 trajectory file reading (Mobius & Juelich formats) |
| `UE4_Assimp` | `Plugins/UE4_Assimp/` | Multi-format 3D model importing (FBX, OBJ, GLTF, etc.) |
| `UE_OpenCV` | `Plugins/UE_OpenCV/` | OpenCV 4.5.5 integration for image processing |

---

## Getting Started

### Prerequisites

- **Unreal Engine 5.5** -- install via the [Epic Games Launcher](https://www.unrealengine.com/download)
- **Visual Studio 2022** with C++17 toolchain
- **CMake >= 3.21** -- install from [cmake.org](https://cmake.org/download/)

### Cloning the Repository

```bash
git clone https://github.com/sir306/ProjectMobius.git
```

No submodules are used -- a standard clone is sufficient. All third-party libraries and plugins are included in the repository.

### Superbuild (External Dependencies)

The superbuild compiles Assimp (shared) and HDF5 (static) from source and stages binaries into the plugin directories where Unreal expects them.

**Windows (Visual Studio):**

```bash
cd UnrealFolder/ProjectMobius
cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64
cmake --build _superbuild --config Release --parallel
ctest -C Release --output-on-failure
```

**macOS (Ninja):**

```bash
cd UnrealFolder/ProjectMobius
cmake -S . -B _superbuild -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build _superbuild --parallel
```

### Unreal Editor Build

1. Open `UnrealFolder/ProjectMobius/ProjectMobius.uproject` in Unreal Engine 5.5
2. Right-click the `.uproject` file and choose **Generate Visual Studio project files**
3. Build the `ProjectMobiusEditor` target (Development Editor) in Visual Studio 2022

### Packaging (UAT)

```bash
RunUAT.bat BuildCookRun ^
  -project="UnrealFolder/ProjectMobius/ProjectMobius.uproject" ^
  -noP4 -platform=Win64 -clientconfig=Development ^
  -cook -build -stage -pak -archive ^
  -archivedirectory=./Binaries/Release
```

---

## Key Bindings

### Camera and Movement

| Action | Input |
|--------|-------|
| Move Camera | `W` `A` `S` `D` (hold) |
| Zoom | Scroll wheel |
| Rotate (keyboard) | `Q` / `E` (hold) |
| Pan View | `MMB` + mouse move |
| Orbit View | `RMB` + mouse move |
| Reset Camera | `Ctrl+R` |
| Pivot Camera | `RMB` + `Q` / `E` |

### Simulation and Environment

| Action | Input |
|--------|-------|
| Play / Pause | `Space` |
| Time Step +/- | `Right Arrow` / `Left Arrow` |
| Rotate Sun | `Ctrl + Left Arrow` / `Ctrl + Right Arrow` |
| Inspect Pedestrian | `Ctrl + Hover` (view), `Ctrl + Click` (select/pin) |

### System

| Action | Input |
|--------|-------|
| Screenshot | `Ctrl+C` |
| Quit | `Esc` |

**Speed modifier:** Hold `Shift` with movement, rotation, or time-step controls for 5x speed.

See [BuildDocs/Project Mobius Key Bindings.md](UnrealFolder/ProjectMobius/BuildDocs/Project%20Mobius%20Key%20Bindings.md) for the full reference including Mac bindings.

---

## Testing

- **Automation tests** are located alongside their module, named `*Tests.cpp`, using Unreal's Automation framework
- **Sample data** lives in `UnrealFolder/ProjectMobius/UnitTestSampleData/` and root `TestData/`
- **Headless test run:**
  ```bash
  UnrealEditor-Cmd.exe ProjectMobius.uproject -run=Automation -Test=All -unattended -nop4 -log
  ```
- **Superbuild ctest:**
  ```bash
  ctest -C Release --output-on-failure
  ```
- **UE automation via superbuild:** enable with `-DSUPERBUILD_ENABLE_UE_AUTOMATION=ON` (see [BuildDocs/UE-Automation-Guide.md](UnrealFolder/ProjectMobius/BuildDocs/UE-Automation-Guide.md))

---

## Deprecated Components

### Node.js WebSocket Server

Removed in November 2025 due to security concerns with npm package dependencies. The `IpcSubsystem` (named pipes / Unix sockets) is the replacement for all inter-process communication.

### Qt Tools (OpenFileTCP, PlotUE_Data)

Deprecated. File dialogs are now handled by the built-in `NativeFileDialogSubsystem`. Real-time data plotting is handled by the in-engine ImPlot/ImGui overlay (`UImPlotVisualizationSubsystem` in MobiusWidgets).

---

## License

Project Mobius source code is released under the **MIT License**.

Copyright (c) 2025 ProjectMobius contributors -- Nicholas R. Harding and Peter Thompson.

### Third-Party Licenses

| Library | Version | Location | License |
|---------|---------|----------|---------|
| Assimp | 5.4.3 | `UnrealFolder/ProjectMobius/Plugins/UE4_Assimp/` | BSD-3-Clause |
| HDF5 | 2.0.0 | `UnrealFolder/ProjectMobius/Plugins/Hdf5DataPlugin/Source/ThirdParty/hdf5-2.0.0/` | HDF5 License |
| OpenCV | 4.5.5 | `UnrealFolder/ProjectMobius/Plugins/UE_OpenCV/` | BSD-3-Clause |
| Dear ImGui | 1.92.5 | `UnrealFolder/ProjectMobius/Source/MobiusWidgets/ThirdParty/ImGui/` | MIT |
| ImPlot | 0.17 | `UnrealFolder/ProjectMobius/Source/MobiusWidgets/ThirdParty/ImPlot/` | MIT |
| earcut.hpp | -- | `UnrealFolder/ProjectMobius/Source/MobiusCore/ThirdParty/earcut_hpp/` | ISC |
| portable-file-dialogs | -- | `UnrealFolder/ProjectMobius/Source/MobiusCore/ThirdParty/PortableFileDialogs/` | WTFPL |

- **Unreal Engine** content is subject to the [Epic Games EULA](https://www.unrealengine.com/eula)
- **Creative Commons assets** -- see [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md)
- **Full licensing details** -- see [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md)

---

## CI Workflows

| Badge | Workflow | What it checks |
|-------|----------|----------------|
| CodeQL | Security scanning | Buffer overflows, injection, null-deref patterns (C++, JS, Python) |
| C++ Style | Format & naming | clang-format consistency, UE naming conventions (U/A/F/I/S prefixes, bool `b` prefix), MIT license headers |
| TODO Tracker | Tech debt | Scans `TODO`/`FIXME`/`HACK`/`XXX`/`WORKAROUND` comments — results in [docs/TECH-DEBT.md](docs/TECH-DEBT.md) |
| Repository Activity | Weekly report | Commit stats, contributor counts, SLOC — results in [docs/ACTIVITY.md](docs/ACTIVITY.md) |

---

## Further Documentation

- [docs/ACTIVITY.md](docs/ACTIVITY.md) -- auto-generated weekly activity report (commits, contributors, SLOC)
- [docs/TECH-DEBT.md](docs/TECH-DEBT.md) -- auto-generated TODO/FIXME/HACK tracker by module
- [BuildDocs/](UnrealFolder/ProjectMobius/BuildDocs/) -- build guides, superbuild ctest guide, UE automation guide
- [BuildDocs/Visualization-ImPlot.md](UnrealFolder/ProjectMobius/BuildDocs/Visualization-ImPlot.md) -- ImPlot overlay integration details
- [BuildDocs/Project Mobius Key Bindings.md](UnrealFolder/ProjectMobius/BuildDocs/Project%20Mobius%20Key%20Bindings.md) -- complete input reference
- [CLAUDE.md](CLAUDE.md) -- AI assistant context and architecture reference
- [AGENTS.md](AGENTS.md) -- repository guidelines for AI agents
