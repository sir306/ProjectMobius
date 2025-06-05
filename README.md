# Project Mobius

## Introduction

Project Mobius is a comprehensive, cross-platform simulation and visualization suite that integrates:

1. **Unreal Engine 5.5** for real-time crowd simulation with GPU-driven heatmaps.  
2. **Qt Applications**—two separate Qt-based tools:  
   - A **Plotting App** for real-time data visualization (using QtGraphs/QML).  
   - A **File-Dialog Server** exposing native file dialogs over REST/TCP.  
3. **Node.js WebSocket Server** for low-latency, bidirectional messaging between Unreal Engine and the Qt GUIs.  
4. **Third-Party Libraries & Assets** (bundled under `ThirdParty/`): HDF5, Assimp, OpenCV, and Blender-exported models.

This repository provides everything needed to build, run, and package the complete system on Windows. (Support for Linux and macOS to come).

---

## Table of Contents

1. [Features](#features)  
2. [Repository Structure](#repository-structure)  
3. [Getting Started](#getting-started)  
   - [Prerequisites](#prerequisites)  
   - [Cloning the Repository](#cloning-the-repository)  
4. [Building & Running](#building--running)  
   - [Unreal Engine 5.5 Project](#unreal-engine-55-project)  
   - [Qt Applications](#qt-applications)  
   - [Node.js Server](#nodejs-server)  
   - [Third-Party Libraries](#third-party-libraries)  
5. [Repository Configuration](#repository-configuration)  
   - [.gitignore](#gitignore)  
   - [License Files](#license-files)  
6. [Usage & Workflow](#usage--workflow)  
7. [Contributing](#contributing)  
8. [License](#license-details)  
9. [Acknowledgments](#acknowledgments)  
10. [Contact](#contact)  

---

## Features

- **Crowd Simulation**  
  - Real-time pedestrian movement visualization with GPU-powered Gaussian Density Heatmap Generation (color-banded heatmaps).  
- **Qt-Based Tools**  
  1. **Plotting App**  
     - Real-time plotting of metrics (e.g., agent count over time) at sub-100 ms intervals.  
     - Scatter, line, and heatmap visualizations using QtGraphs/QML (with optional OpenGL/ImPlot integration).  
  2. **File-Dialog Server**  
     - Exposes a RESTful endpoint (e.g., `http://localhost:8080/selectFile`) and an optional TCP listener for remote clients to invoke native file dialogs.  
     - Returns selected file paths via JSON/TCP.  
- **Node.js WebSocket Server**  
  - Bridges Unreal Engine and the Qt GUIs with bidirectional JSON messaging over WebSocket (`ws://localhost:3000`).  
  - Handles reconnects, message buffering, and simple request/response semantics.  
- **Bundled Third-Party Libraries & Assets**  
  - **HDF5** for large-scale data logging.  
  - **Assimp** for importing Blender models into Unreal.  
  - **OpenCV** for texture processing (e.g., Gaussian blur).  
  - **Blender Models** (exported `.fbx` under `ThirdParty/BlenderModels/`) for environment and agent meshes.

---

## Repository Structure

ProjectMobius/
├── .gitignore
├── LICENSE.md
├── CONTRIBUTING.md
├── README.md
├── UnrealProject/
│ ├── ProjectMobius.uproject
│ ├── Source/
│ ├── Content/
│ ├── Plugins/
│ └── Config/
│
├── QtApps/
│ ├── PlotApp/
│ │ ├── PlotApp.pro
│ │ ├── src/
│ │ └── qml/
│ └── FileDialogServer/
│ ├── FileDialogServer.pro
│ ├── src/
│ └── resources/
│
├── NodeServer/
│ ├── package.json
│ ├── server.js
│ └── wsHandlers/
│
├── ThirdParty/
│ ├── HDF5/
│ ├── Assimp/
│ ├── OpenCV/
│ └── BlenderModels/
│
├── build_scripts/
│ ├── build_windows.bat
│ ├── build_linux.sh
│ └── build_mac.sh
└── docs/
└── (design docs, API references)

## Getting Started

### Prerequisites

Before building or running any component of Project Mobius, ensure the following dependencies are installed:

1. **Unreal Engine 5.5**  
   - Download/Install via Epic Games Launcher:  
     <a href="https://www.unrealengine.com/download">https://www.unrealengine.com/download</a>  
   - On **Windows**: Visual Studio 2022 with C++ toolchain.  
   - On **macOS**: Xcode 13+ (command-line tools).  
   - On **Linux**: `clang` 12+, `Make`, and required GL/Vulkan dependencies.

2. **Qt 6 (Qt Quick, QtGraphs)**  
   - Download/Install via the Qt Installer:  
     <a href="https://www.qt.io/download">https://www.qt.io/download</a>  
   - Required Qt modules: `QtCore`, `QtGui`, `QtQuick`, `QtWidgets`, `QtGraphs`, `QtNetwork` (for FileDialogServer).

3. **Node.js & npm** (≥ v16.x)  
   - Download/Install from:  
     <a href="https://nodejs.org/">https://nodejs.org/</a>  

4. **CMake** (≥ v3.20)  
   - **Windows**: Use the official installer:  
     <a href="https://cmake.org/download/">https://cmake.org/download</a>  
   - **Linux/macOS**: Install via package manager (e.g., `apt`, `brew`) or build from source.

5. **Build Toolchains**  
   - **Windows**: Visual Studio 2022 (C++17).  
   - **Linux**: `gcc` 10+ or `clang` 12+; `make` or `ninja`.  
   - **macOS**: Xcode 13+ (command-line tools); `clang`, `make`, or `ninja`.

6. **Third-Party Libraries**  
   - Bundled prebuilt binaries reside in `ThirdParty/`. If you prefer to rebuild from source, see [Third-Party Libraries](#third-party-libraries).

## License Details

ProjectMobius is a collection of tools and sample data built around an Unreal Engine 5.5 project. The repository includes:

- **Unreal content** under `UnrealFolder/ProjectMobius/` for real-time visualization.
- Two **Qt applications** in `UnrealFolder/ProjectMobius/Tools/QT_Apps/` used for data capture and plotting.
- A small **Node.js server** in `UnrealFolder/ProjectMobius/Tools/NodeJS/` for TCP communication.
- Various **third-party assets and libraries** such as ASSIMP, OpenCV, and HDF5.

See [LICENSE.md](LICENSE.md) for complete licensing details. In brief:

- Unreal Engine content is covered by the Unreal Engine EULA. You need your own Unreal license to build or distribute it.
- The Qt applications are MIT licensed for the source you wrote while the Qt framework is under the LGPL.
- The Node server code is MIT licensed and any npm packages remain under their own licenses.
- Third-party assets and libraries retain their original licenses (BSD for ASSIMP and OpenCV, HDF5's license, Creative Commons for imported Blender models, etc.).

This repository is intended as a reference for setting up data visualization workflows in Unreal. Check each subfolder for specific docs and see the license file for more information.

## Qt Licensing

The Qt-based tools under `UnrealFolder/ProjectMobius/Tools/QT_Apps/` use Qt modules that are available under the GNU Lesser General Public License version 3 (LGPL-3.0). The full text of the LGPL-3.0 license is provided in `COPYING.LGPL-3.0.txt`.

## Third-Party Licenses

This repository bundles several open source libraries.
Copies of their respective license texts are included in the following folders:

- `Source/ProjectMobius/ThirdParty/assimp/LICENSE`
- `Source/Visualization/ThirdParty/OpenCV/LICENSE`

Additional licensing details can be found in [LICENSE.md](LICENSE.md).
