# Third-Party Licenses

This document covers all third-party libraries, assets, and tools bundled in the Project Mobius repository. Project Mobius's own source code is released under the **MIT License** (see `LICENSE`).

---

## Licensing Overview

| Library | Version | License | Location |
|---------|---------|---------|----------|
| ASSIMP | 5.4.3 | BSD-3-Clause | `Plugins/UE4_Assimp/` |
| HDF5 | 2.0.0 | HDF5 License (BSD-style) | `Plugins/Hdf5DataPlugin/Source/ThirdParty/hdf5-2.0.0/` |
| OpenCV | 4.5.5 | BSD-3-Clause | `Source/Visualization/ThirdParty/OpenCV/` (runtime via Epic's built-in OpenCV engine plugin) |
| Dear ImGui | 1.92.5 | MIT | `Source/MobiusWidgets/ThirdParty/ImGui/` |
| ImPlot | 0.17 | MIT | `Source/MobiusWidgets/ThirdParty/ImPlot/` |
| earcut.hpp | — | ISC | `Source/MobiusCore/ThirdParty/earcut_hpp/` |
| portable-file-dialogs | — | WTFPL | `Source/MobiusCore/ThirdParty/PortableFileDialogs/` |
| MakeHuman meshes | — | CC0 1.0 | `Content/MakeHuman/` |

---

## 1. Unreal Engine 5.5

All content in this distribution (C++ code, Blueprints, assets, plugins, etc.) is subject to the **Unreal Engine End User License Agreement (EULA)**. You must comply with Epic Games' terms.

- **Unreal Engine EULA:** https://www.unrealengine.com/en-US/eula

You retain ownership of your own source files and assets (e.g., anything in `Source/`, `Content/`, `Plugins/`), but redistribution or commercial usage must comply with the EULA. Recipients must have a valid Unreal Engine license.

---

## 2. Third-Party Libraries

### 2.1 ASSIMP (Open Asset Import Library)

- **Location:** `Plugins/UE4_Assimp/`
- **License:** BSD-3-Clause
- **URL:** https://github.com/assimp/assimp/blob/master/LICENSE

### 2.2 OpenCV

- **Location:** `Source/Visualization/ThirdParty/OpenCV/` (runtime provided by Epic's built-in OpenCV engine plugin)
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

## 3. Third-Party Assets & Data

> **Note:** The upstream Assimp `test/` directory (which contained CC-BY-SA and other Creative Commons test models) has been removed from this repository. Those assets are part of Assimp's upstream test infrastructure and are not used by the UE4_Assimp plugin at runtime.

| Asset Path | License | Notes |
|------------|---------|-------|
| `Plugins/Hdf5DataPlugin/Hdf5TestData/JuelichTestCases/` | CC-BY 4.0 | Pedestrian trajectory data from Forschungszentrum Jülich |
| `Content/MakeHuman/` | CC0 1.0 | Character meshes exported from MakeHuman |
| `ImportedOpenSourceAssets/gui_pack_White/` | CC0 1.0 | GUI icon pack from [OpenGameArt](https://opengameart.org/content/gui-pack) |
| `ImportedOpenSourceAssets/click.wav` | CC0 1.0 | Click sound from [OpenGameArt](https://opengameart.org/content/click) |
| `ImportedOpenSourceAssets/StatButtonIcon.png` | CC BY 4.0 | Bar chart icon from [icon-icons.com](https://icon-icons.com/icon/chart-bar/194545), resized |
| `ImportedOpenSourceAssets/cross_icon.png` | Free for commercial use | Close icon from [icon-icons.com](https://icon-icons.com/icon/square-close-cross/128691) |
