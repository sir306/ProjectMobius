# Project Mobius

[![CodeQL](https://github.com/sir306/ProjectMobius/actions/workflows/codeql.yml/badge.svg)](https://github.com/sir306/ProjectMobius/actions/workflows/codeql.yml)
[![C++ Style](https://github.com/sir306/ProjectMobius/actions/workflows/cpp-style-check.yml/badge.svg)](https://github.com/sir306/ProjectMobius/actions/workflows/cpp-style-check.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.5-black?logo=unrealengine)](https://www.unrealengine.com/)
[![Last Commit](https://img.shields.io/github/last-commit/sir306/ProjectMobius?label=Last%20Commit)](https://github.com/sir306/ProjectMobius/commits/main)
[![Contributors](https://img.shields.io/github/contributors/sir306/ProjectMobius?label=Contributors)](https://github.com/sir306/ProjectMobius/graphs/contributors)

**Crowd dataset playback and analysis in Unreal Engine 5.5**

<p align="center">
  <img src="docs/images/hero-crowd-simulation.png" alt="Project Mobius crowd simulation" width="720">
</p>

<p align="center">
  <em>1,000 pedestrian agents replayed in real time with MASS Entity and Niagara rendering</em>
</p>

Project Mobius is an Unreal Engine 5.5 application for loading pedestrian trajectory datasets, replaying them in 3D, and analyzing crowd movement with heatmaps, flow counters, and in-engine charts. Windows is the primary target; macOS support is partial.

<p align="center">
  <img src="docs/images/crowd-simulation-overview.png" alt="Crowd simulation overview" width="360">
  <img src="docs/images/vr-demo-loading.png" alt="Project Mobius viewport" width="180">
</p>

## What It Does

- Loads pedestrian trajectories from JSON and HDF5, including Mobius and Juelich HDF5 datasets
- Replays imported timesteps through Unreal's MASS framework with Niagara-based pedestrian rendering
- Visualizes crowd density with floor heatmaps, between-floor counts, and related analytics widgets
- Provides interactive playback controls for play/pause, stepping, scrubbing, and time dilation
- Displays in-engine charts and overlays through the ImPlot and Slate UI stack
- Imports environment geometry from common formats such as FBX, OBJ, UDatasmith, IFC, and WKT
- Supports flow counters, agent inspection, screenshots, and pedestrian appearance/scalability controls

## Why HDF5

For larger trajectory files, convert JSON to HDF5 with the bundled
[`json_to_hdf5_converter.py`](UnrealFolder/ProjectMobius/Plugins/Hdf5DataPlugin/Scripts/json_to_hdf5_converter.py)
script. In one 5,000-agent sample, HDF5 cut load time from `21.29 s` to `3.18 s`
and reduced memory use from `317,555 KB` to `55,283 KB`.

<p align="center">
  <img src="docs/images/SpedUpJsonHDF5Load5000Agents.gif" alt="Project Mobius loading a 5,000-agent sample converted from JSON to HDF5" width="900">
</p>

| Format | Load time | Memory use |
|--------|-----------|------------|
| JSON | `21.29 s` | `317,555 KB` |
| HDF5 | `3.18 s` | `55,283 KB` |

More on supported formats, the conversion command, and the benchmark:
[docs/DATA-PIPELINE.md](docs/DATA-PIPELINE.md)

## Quick Start

### Prerequisites

- Unreal Engine 5.5
- Visual Studio 2022
- CMake 3.21 or newer

### Build

```bash
git clone https://github.com/sir306/ProjectMobius.git
cd ProjectMobius/UnrealFolder/ProjectMobius
cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64
cmake --build _superbuild --config Release --parallel
```

Then open `UnrealFolder/ProjectMobius/ProjectMobius.uproject`, generate Visual
Studio project files, and build `ProjectMobiusEditor` in `Development Editor`.

Full build, packaging, and automation notes live in
[docs/BUILD-AND-RUN.md](docs/BUILD-AND-RUN.md).

## Repository Overview

- `UnrealFolder/ProjectMobius/`: the Unreal project, superbuild, config, content, and source
- `UnrealFolder/ProjectMobius/Source/`: runtime modules for playback, UI, analytics, and core systems
- `UnrealFolder/ProjectMobius/Plugins/`: bundled HDF5, Assimp, and OpenCV integrations
- `BuildDocs/`: Unreal-specific build and automation notes distributed with the project
- `docs/`: repo-level documentation and wiki seed pages

## Documentation

- [Docs Index](docs/README.md)
- [Build and Run](docs/BUILD-AND-RUN.md)
- [Controls](docs/CONTROLS.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Data Pipeline](docs/DATA-PIPELINE.md)
- [UE Automation Guide](UnrealFolder/ProjectMobius/BuildDocs/UE-Automation-Guide.md)
- [Third-Party Licenses](THIRD-PARTY-LICENSES.md)

## License

Project Mobius source code is released under the **MIT License**.

See [LICENSE](LICENSE) for the project license and
[THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md) for bundled dependencies,
assets, and other third-party notices.
