# Third-Party Licenses

This document covers third-party libraries, assets, and tools included in the Project Mobius source repository. Original ProjectMobius-authored source code and original ProjectMobius-authored assets contained in the repository are released under the **MIT License** (see `LICENSE`), except where a file or directory states another license.

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

## 1. Unreal Engine dependency

The source repository does not include Unreal Engine itself, Epic Starter Content, Epic template/sample content, or Twinmotion-generated content. Those materials, if added locally by a user to build or run Project Mobius, are obtained or generated separately under the user's own Epic license and are not part of the repository's MIT-licensed contents.

- **Unreal Engine EULA:** https://www.unrealengine.com/en-US/eula/unreal
- **Epic Content License Agreement:** https://www.unrealengine.com/en-US/eula/content

As between Project Mobius contributors and Epic, contributors retain copyright in their original repository-authored source files and assets.

Users must not commit or upload Epic-provided content to this repository. Any Epic content added or generated locally to build or run the project, including VRTemplate files or Twinmotion-generated files, must remain untracked by Git and continue to be governed by Epic's terms rather than this repository's MIT License.

This means:
- Original ProjectMobius-authored code, Blueprints, and assets in the repository are MIT-licensed unless stated otherwise.
- Unreal Engine and any Epic-provided content added locally by a user remain under Epic's license terms and are not included in the repository's MIT License.
- Third-party libraries and third-party assets included in the repository remain under their own licenses listed below.

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

This distribution does not ship a standalone `UE_OpenCV` plugin. Project
Mobius uses Unreal's built-in `OpenCV` engine plugin, enabled in
`ProjectMobius.uproject`, with local headers and license files kept under
`Source/Visualization/ThirdParty/OpenCV/`.

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
