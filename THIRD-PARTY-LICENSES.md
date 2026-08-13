# Third-Party Licenses

This document covers all third-party libraries, assets, and tools bundled in the Project Mobius repository. Project Mobius's own source code is released under the **MIT License** (see `LICENSE`).

---

## Licensing Overview

| Library | Version | License | Location |
|---------|---------|---------|----------|
| ASSIMP | 5.4.3 | BSD-3-Clause | `Plugins/UE4_Assimp/` |
| HDF5 | 2.0.0 | HDF5 License (BSD-style) | `Plugins/Hdf5DataPlugin/Source/ThirdParty/hdf5-2.0.0/` |
| OpenCV | 4.5.5 | BSD-3-Clause | `Source/Visualization/ThirdParty/OpenCV/` (runtime via Epic's built-in `OpenCV` engine plugin) |
| Dear ImGui | 1.92.5 | MIT | `Source/MobiusWidgets/ThirdParty/ImGui/` |
| ImPlot | 0.17 | MIT | `Source/MobiusWidgets/ThirdParty/ImPlot/` |
| earcut.hpp | — | ISC | `Source/MobiusCore/ThirdParty/earcut_hpp/` |
| portable-file-dialogs | — | WTFPL | `Source/MobiusCore/ThirdParty/PortableFileDialogs/` |
| IFC++ (IfcPlusPlus) | recorded as `7b80900` | MIT | `UnrealFolder/ProjectMobius/Source/ThirdParty/IfcBridgeSource/IfcPlusPlus/` |
| ↳ Carve, glm, earcut, RapidJSON, nowide, utf8, zippy, miniz, zip | bundled by IFC++ | MIT / ISC / Boost-1.0 / Unlicense | see §2.8 |

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

- **Location:** `Source/Visualization/ThirdParty/OpenCV/` (runtime provided by Epic's built-in `OpenCV` engine plugin)
- **License:** BSD-3-Clause
- **URL:** https://github.com/opencv/opencv/blob/master/LICENSE

The repository no longer ships a standalone `UE_OpenCV` plugin. Project Mobius
uses Unreal's built-in `OpenCV` engine plugin, enabled in
`UnrealFolder/ProjectMobius/ProjectMobius.uproject`, with local headers and
license files kept under `Source/Visualization/ThirdParty/OpenCV/`.

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

### 2.8 IFC++ (IfcPlusPlus) and its bundled dependencies

Runtime `.ifc` import. Vendored source is compiled into `MobiusIfcBridge.dll` and redistributed in
binary form, so every bundled dependency below ships with the product and is credited here.

- **Location:** `UnrealFolder/ProjectMobius/Source/ThirdParty/IfcBridgeSource/IfcPlusPlus/`
- **License:** MIT — Copyright Fabian Gerold
- **URL:** https://github.com/ifcquery/IfcPlusPlus

IFC++ bundles the following, all permissive. Full licence texts and per-component evidence paths are in
`Source/ThirdParty/IfcBridgeSource/THIRD_PARTY_NOTICES.md`.

| Bundled component | License | Copyright | Path under `.../IfcPlusPlus/` |
|---|---|---|---|
| Carve (CSG kernel) | MIT | Tobias Sargeant, 2006–2015 | `IfcPlusPlus/src/external/Carve/` |
| glm | MIT / "Happy Bunny" | G-Truc Creation, 2005 | `IfcPlusPlus/src/external/glm/` |
| earcut.hpp | ISC | Mapbox, 2015 | `IfcPlusPlus/src/external/earcut/` |
| RapidJSON | MIT | THL A29 Limited (Tencent) / Milo Yip, 2015 | `IfcPlusPlus/src/external/RapidJSON/` |
| nowide | Boost Software License 1.0 | Artyom Beilis, 2012 | `IfcPlusPlus/src/external/nowide/` |
| utf8 (utfcpp) | Boost Software License 1.0 | Nemanja Trifunovic, 2006–2016 | `IfcPlusPlus/src/external/utf8/` |
| zippy | Unlicense (public domain) | RAD Game Tools / Valve / R. Geldreich / Tenacious Software / M. Raiber | `IfcPlusPlus/src/external/zippy/` |
| miniz | Unlicense (public domain) | Rich Geldreich, 2013; Martin Raiber, 2016 | `IfcPlusPlus/src/external/zip-master/miniz.h` |
| zip (wrapper) | MIT — see note | see note | `IfcPlusPlus/src/external/zip-master/zip.{c,h}` |

**Carve is MIT, not GPL.** Carve was GPL-2.0 before its 2015 relicense, and old forks (e.g. `VTREEM/Carve`)
still carry the old text, which is why an outdated "Carve is GPL" claim circulates. The copy vendored here
carries the MIT header — verified in `IfcPlusPlus/src/external/Carve/src/include/carve/carve.hpp`.

**Note on `zip-master/zip.{c,h}`:** the vendored copy retains only the MIT *warranty disclaimer* paragraph;
the copyright line and permission grant were trimmed by whoever vendored it upstream. Upstream is MIT. This
is an attribution gap, not an incompatible licence. The wrapper is also **dead code** — it is compiled via
`IfcPlusPlus/CMakeLists.txt:74` but nothing in IFC++ calls any `zip_*` function (its `.ifcZIP` support goes
through zippy), so removing it from the source list would drop the obligation entirely.

---

## 3. Third-Party Assets & Data

> **Note:** The upstream Assimp `test/` directory (which contained CC-BY-SA and other Creative Commons test models) has been removed from this repository. Those assets are part of Assimp's upstream test infrastructure and are not used by the UE4_Assimp plugin at runtime.

| Asset Path | License | Notes |
|------------|---------|-------|
| `Plugins/Hdf5DataPlugin/Hdf5TestData/JuelichTestCases/` | CC-BY 4.0 | Pedestrian trajectory data from Forschungszentrum Jülich |

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

Epic's built-in `OpenCV` engine plugin is an external Unreal Engine dependency
and is not stored under this repository's `Plugins/` directory.
