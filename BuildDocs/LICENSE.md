# License

This repository includes four main components, each governed by its own licensing terms:

1. **Unreal Engine 5.5 Project**  
2. **Qt Applications** (two separate apps)  
3. **Node.js Server**  
4. **Third-Party Libraries & Assets** (included under `Source/`)

### Licensing Overview

ProjectMobius's own source code (the Node.js server, Qt application sources, and all custom Unreal Engine modules and assets) is released under the MIT License. Unreal Engine, Qt runtime libraries, and other bundled third-party libraries or imported assets remain under their respective licenses as detailed below.

---

## 1. Unreal Engine 5.5 Project

**Path:** `UnrealFolder/ProjectMobius5.5/`

All content under `UnrealFolder/ProjectMobius5.5/` (C++ code, Blueprints, assets, plugins, etc.) is subject to the **Unreal Engine End User License Agreement (EULA)**. You do **not** have permission to redistribute Unreal Engine itself; you must comply with Epic Games’ terms.

- **Unreal Engine EULA (v5.5):**  
  https://www.unrealengine.com/en-US/eula

### Key Points

- You retain ownership of your own source files and assets (e.g., anything in `Source/`, `Content/`, `Plugins/`, etc.), but any redistribution or commercial usage of this project must comply with the Unreal Engine EULA.
- You may **not** remove or alter any Epic Games copyright/trademark notices.
- Recipients of this repository must have a valid Unreal Engine license and agree to the EULA above before building or distributing the UE5.5 project.

---

## 2. Qt Applications

Your two Qt-based applications live here:

- `UnrealFolder/ProjectMobius5.5/Tools/QT_Apps/OpenFileTCP/`  
- `UnrealFolder/ProjectMobius5.5/Tools/QT_Apps/PlotUE_Data/`

Each Qt app links against Qt framework modules. By default, most Qt modules are available under the **GNU Lesser General Public License v3.0 (LGPL-3.0)** (unless you hold a commercial Qt license). You must comply with whichever Qt license applies to the modules you used.

- **Qt Licensing Overview:**  
  https://doc.qt.io/qt-6/licensing.html

### 2.1 Your Qt App Source Code

All `.cpp`, `.h`, `.qml`, etc. files that you authored under the two folders above (for example, `OpenFileTCP/main.cpp`, `PlotUE_Data/AxisSettings.h`, `PlotUE_Data/AppWindow.qml`, etc.) are licensed under the **MIT License** (see Section 5 below).

### 2.2 Qt Framework Binaries & Libraries

Any Qt framework binaries (e.g. Qt Core, Qt Gui, Qt Quick, Qt Widgets, QtGraphs, etc.) that these apps depend on are governed by **LGPL-3.0** (or a commercial Qt license) according to the modules you used. You are responsible for complying with the LGPL’s conditions (e.g. dynamic linking, providing object files for relinking, including the “COPYING.LGPL” text in your distribution, etc.).

#### Example Notice (for each Qt executable/README)

Qt Framework Libraries Licensing Notice
--------------------------------------
This application (OpenFileTCP.exe / PlotUE_Data.exe) links against the following Qt modules under the GNU Lesser General Public License version 3.0 (LGPL-3.0):

  • Qt5Core (LGPL-3.0)
  • Qt5Gui (LGPL-3.0)
  • Qt5Widgets (LGPL-3.0)
  • QtGraphs (LGPL-3.0)
  • QtQuick (LGPL-3.0)

You must comply with the terms of the LGPL-3.0. In particular:

  1. All Qt libraries are dynamically linked, so you may replace or upgrade those libraries with your own.  
  2. If you modify any LGPL-licensed Qt modules, you must provide the corresponding object files to allow relinking.  
  3. The full text of the LGPL-3.0 license is included in this distribution under “COPYING.LGPL-3.0.txt.” You can also review the license online at:  
     https://www.gnu.org/licenses/lgpl-3.0.html

For more information on how to comply with Qt’s LGPL requirements, see:  
<a href="https://doc.qt.io/qt-6/lgpl.html">https://doc.qt.io/qt-6/lgpl.html</a>

---

## 3. Node.js Server

**Path:** `UnrealFolder/ProjectMobius5.5/Tools/NodeJS/`

All JavaScript source files in this folder (e.g. `MobiusServer.js`, `package.json`, any utility `.js` modules) that you authored are licensed under the **MIT License** (see Section 5 below). Any third-party npm packages you installed (inside `node_modules/`) must be used in accordance with their own respective licenses—refer to each package’s license inside `node_modules/<package>/LICENSE` when you redistribute.

---

## 4. Third-Party Libraries & Assets

Within your Unreal project’s `Source/` directory, you have included several third-party libraries and data. Each is governed by its own license. Below is a summary of where to find each, and the full license URL you must comply with:


### 4.1 ASSIMP (Open Asset Import Library)

- **Location (two copies):**  
  - `Source/ProjectMobius/ThirdParty/assimp/`  
  - `Source/MobiusWidgets/ThirdParty/assimp/`  
- **License:** BSD 3-Clause “Simplified” License  
- **Full License URL:**  
  https://github.com/assimp/assimp/blob/master/LICENSE

#### Key Points

- You may use, modify, and redistribute ASSIMP under the terms of its BSD 3-Clause license.  
- The `assimp/` folder should include a copy of `LICENSE` or `LICENSE.txt` from the original ASSIMP distribution.  

### 4.2 OpenCV

- **Location:**  
  `Source/Visualization/ThirdParty/OpenCV/` and `Source/Visualization/ThirdParty/OpenCV_Lib/`  
- **License:** BSD 3-Clause “New” or “Revised” License  
- **Full License URL:**  
  https://github.com/opencv/opencv/blob/master/LICENSE

#### Key Points

- You may use, modify, and redistribute OpenCV under its BSD 3-Clause license.  
- The `OpenCV/` folder should contain a copy of `LICENSE` or `LICENSE.txt` from the original OpenCV release.

### 4.3 HDF5

- **Location:**  
  You have an `HDF5/` folder at the root of the repository (external to Unreal) for any HDF5 scripts, binaries, or headers.  
- **License:** HDF Group BSD-style license (sometimes called the HDF5 license)  
- **Full License URL:**  
  https://support.hdfgroup.org/HDF5/doc/HDF5_philosophy/license.html

#### Key Points

- You may use HDF5 in your project under the terms of the HDF5 license (a permissive, BSD-style license).  
- Include a copy of the HDF5 license text (e.g., `LICENSE.txt`) in the `HDF5/` folder itself.

### 4.4 Blender Models / Assets

- **Location:**  
  - `ImportedOpenSourceAssets/Blender/` (if you imported any .blend or exported meshes)  
  - Possibly other subfolders such as `DemoProgressVideos/` or `DemoAssets/` (if they contain Blender-based assets)  
- **License:** Depends on each model’s origin.  
  - If you created your own geometry in Blender, you may relicense it under MIT (see Section 5).  
  - If you downloaded models from the Blender Foundation or other repositories, they are typically distributed under the **Creative Commons** (e.g. CC-BY or CC-0).  
- **Action Required:** For any Blender model you did not author yourself, verify and include the exact license file (e.g. `CC-BY.txt` or `CC0.txt`) inside that model’s folder.

#### Key Points

- For “self-authored” Blender exports, you may relicense them under MIT (Section 5).  
- For any third-party models obtained from “blender.org” or elsewhere, include their specific Creative Commons license and author credit. If you’re uncertain, do **not** relicense them under MIT—leave them under their original terms and include a `LICENSE` or `COPYING` file adjacent to each such asset.

---

## 5. MIT License (For Your Own Code and Self-Authored Assets)

Use the following text for any `.cpp/.h/.qml/.js` files or assets (e.g., your own textures/Blender exports) that you own outright (the “Software”). Replace the placeholders below with your own name (or organization) and year.

```text
MIT License

Copyright (c) 2025 ProjectMobius contributors
Nicholas R. Harding and Peter Thompson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights  
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell  
copies of the Software, and to permit persons to whom the Software is furnished  
to do so, subject to the following conditions:  

The above copyright notice and this permission notice shall be included in  
all copies or substantial portions of the Software.  

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL  
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR  
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING  
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS  
IN THE SOFTWARE.

