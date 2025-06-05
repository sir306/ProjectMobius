# ProjectMobius

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
