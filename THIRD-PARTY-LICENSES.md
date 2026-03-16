# Third-Party Licenses

This document covers all third-party libraries, assets, and tools bundled in the Project Mobius repository. Project Mobius's own source code is released under the **MIT License** (see `LICENSE`).

---

## Licensing Overview

| Library | Version | License | Location |
|---------|---------|---------|----------|
| ASSIMP | 5.4.3 | BSD-3-Clause | `Plugins/UE4_Assimp/` |
| HDF5 | 2.0.0 | HDF5 License (BSD-style) | `Plugins/Hdf5DataPlugin/Source/ThirdParty/hdf5-2.0.0/` |
| OpenCV | 4.5.5 | BSD-3-Clause | `Plugins/UE_OpenCV/`, `Source/Visualization/ThirdParty/OpenCV/` |
| Dear ImGui | 1.92.5 | MIT | `Source/MobiusWidgets/ThirdParty/ImGui/` |
| ImPlot | 0.17 | MIT | `Source/MobiusWidgets/ThirdParty/ImPlot/` |
| earcut.hpp | — | ISC | `Source/MobiusCore/ThirdParty/earcut_hpp/` |
| portable-file-dialogs | — | WTFPL | `Source/MobiusCore/ThirdParty/PortableFileDialogs/` |

---

## 1. Unreal Engine 5.5

All content under `UnrealFolder/ProjectMobius/` (C++ code, Blueprints, assets, plugins, etc.) is subject to the **Unreal Engine End User License Agreement (EULA)**. You must comply with Epic Games' terms.

- **Unreal Engine EULA:** https://www.unrealengine.com/en-US/eula

You retain ownership of your own source files and assets (e.g., anything in `Source/`, `Content/`, `Plugins/`), but redistribution or commercial usage must comply with the EULA. Recipients of this repository must have a valid Unreal Engine license.

---

## 2. Third-Party Libraries

### 2.1 ASSIMP (Open Asset Import Library)

- **Location:** `Plugins/UE4_Assimp/`
- **License:** BSD-3-Clause
- **URL:** https://github.com/assimp/assimp/blob/master/LICENSE

### 2.2 OpenCV

- **Location:** `Plugins/UE_OpenCV/`, `Source/Visualization/ThirdParty/OpenCV/`, `Source/Visualization/ThirdParty/OpenCV_Lib/`
- **License:** BSD-3-Clause
- **URL:** https://github.com/opencv/opencv/blob/master/LICENSE

### 2.3 HDF5

- **Location:** `Plugins/Hdf5DataPlugin/Source/ThirdParty/hdf5-2.0.0/`
- **License:** HDF5 License (BSD-style)
- **URL:** https://github.com/HDFGroup/hdf5/blob/develop/LICENSE

### 2.4 Dear ImGui

- **Location:** `Source/MobiusWidgets/ThirdParty/ImGui/`
- **License:** MIT
- **URL:** https://github.com/ocornut/imgui/blob/master/LICENSE.txt

### 2.5 ImPlot

- **Location:** `Source/MobiusWidgets/ThirdParty/ImPlot/`
- **License:** MIT
- **URL:** https://github.com/epezent/implot/blob/master/LICENSE

### 2.6 earcut.hpp

- **Location:** `Source/MobiusCore/ThirdParty/earcut_hpp/`
- **License:** ISC
- **URL:** https://github.com/mapbox/earcut.hpp/blob/master/LICENSE

### 2.7 portable-file-dialogs

- **Location:** `Source/MobiusCore/ThirdParty/PortableFileDialogs/`
- **License:** WTFPL
- **URL:** https://github.com/nickvanheer/portable-file-dialogs

---

## 3. Third-Party Assets (Creative Commons)

| Asset Path | License | Notes |
|------------|---------|-------|
| `Plugins/UE4_Assimp/.../test/models-nonbsd/BLEND/` | CC-BY 2.0 | Model by Tiziana (TiZeta) |
| `Plugins/UE4_Assimp/.../test/models-nonbsd/MD5/` | CC-BY-SA | "BoarMan" by zphr (Christian Lenke) |
| `Plugins/UE4_Assimp/.../test/models/glTF2/issue_3269/` | CC-BY 4.0 | `texcoord_crash.gltf` by Ed Mackey, Analytical Graphics, Inc. |

Assets in `ImportedOpenSourceAssets/` are third-party resources with their own licenses — see `ImportedOpenSourceAssets/LICENSE.txt` for details.

---

## 4. Repository Folder Structure

```
LICENSE                    ← MIT License (this project's own code)
THIRD-PARTY-LICENSES.md   ← This file
ImportedOpenSourceAssets/  ← Third-party assets (see per-asset licenses)
HelpfulTextDocs/          ← Additional documentation
TestData/                 ← Sample datasets for unit tests
DemoProgressVideos/       ← Progress recordings
UnrealFolder/
  └─ ProjectMobius/       ← Unreal Engine 5.5 project (EULA)
    ├─ Config/            ← Engine/project .ini files
    ├─ Content/           ← UAssets, materials, etc.
    ├─ Plugins/
    │   ├─ Hdf5DataPlugin/        ← HDF5 trajectory reading
    │   │   └─ Source/ThirdParty/
    │   │       └─ hdf5-2.0.0/    ← HDF5 (HDF5 License)
    │   ├─ UE4_Assimp/            ← Multi-format 3D model importing
    │   │   └─ Source/ThirdParty/
    │   │       └─ assimp/        ← ASSIMP (BSD-3-Clause)
    │   └─ UE_OpenCV/             ← OpenCV integration
    │       └─ Source/ThirdParty/
    │           └─ opencv/        ← OpenCV (BSD-3-Clause)
    ├─ Source/
    │   ├─ MobiusLogging/         ← Logging module (MIT)
    │   ├─ MobiusCore/            ← Central hub module (MIT)
    │   │   └─ ThirdParty/
    │   │       ├─ earcut_hpp/    ← Polygon triangulation (ISC)
    │   │       └─ PortableFileDialogs/ ← File dialogs (WTFPL)
    │   ├─ HeatmapVisualization/  ← GPU heatmap rendering (MIT)
    │   ├─ Visualization/         ← Texture processing (MIT)
    │   │   └─ ThirdParty/
    │   │       └─ OpenCV/        ← OpenCV headers (BSD-3-Clause)
    │   ├─ ProjectMobius/         ← Main gameplay module (MIT)
    │   ├─ MobiusWidgets/         ← UI widgets & ImPlot overlay (MIT)
    │   │   └─ ThirdParty/
    │   │       ├─ ImGui/         ← Dear ImGui (MIT)
    │   │       └─ ImPlot/        ← ImPlot (MIT)
    │   └─ HIT_ThesisWork/        ← Research module (MIT)
    ├─ BuildDocs/             ← License and build documentation for redistribution
    └─ UnitTestSampleData/    ← Test data
```
