# Build and Run

This page keeps the longer setup and testing notes out of the root README.

## Prerequisites

- Git
- Windows 10 or 11 is the verified platform
- Unreal Engine 5.5
- Visual Studio 2022 with C++ tooling on Windows
- Xcode Command Line Tools on macOS (Apple Silicon)
- CMake 3.21 or newer
- Ninja for the macOS superbuild flow shown below
- Internet access during the first superbuild so HDF5 can fetch `zlib-1.3.1`
  for compression support
- Python if you plan to use the JSON-to-HDF5 conversion script

macOS support exists in parts of the codebase, but Windows is the primary
development and validation target.

## Clone the Repository

```bash
git clone https://github.com/sir306/ProjectMobius.git
cd ProjectMobius/UnrealFolder/ProjectMobius
```

No submodules are required for the standard source checkout.

## Build External Dependencies

The superbuild compiles the vendored HDF5 and Assimp dependencies into the
locations expected by the Unreal project. No separate HDF5 installation is
required, but the first HDF5 build downloads `zlib-1.3.1` unless that archive
is already cached or redirected locally.

### Windows (Visual Studio generator)

```bash
cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64
cmake --build _superbuild --config Release --parallel
```

### macOS (Ninja generator, Apple Silicon)

```bash
cmake -S . -B _superbuild -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build _superbuild --parallel
```

## Build the Unreal Editor Target

1. Open `UnrealFolder/ProjectMobius/ProjectMobius.uproject` in Unreal Engine 5.5.
2. Generate IDE project files from the `.uproject` if you need them for your platform.
3. Build `ProjectMobiusEditor` in `Development Editor`.

## Run Tests

### Superbuild tests

```bash
ctest -C Release --output-on-failure
```

### Unreal automation tests

```bash
UnrealEditor-Cmd.exe ProjectMobius.uproject -run=Automation -Test=All -unattended -nop4 -log
```

## Datasmith Override Materials

When the Unreal editor opens the project for the first time, the `MobiusEditor`
module automatically generates the Datasmith override materials that
`RuntimeMeshBuilder` needs at runtime. No manual steps are required — the
generation runs once and the results are saved into the Content directory.

### What gets generated

Two families of overrides are produced:

- **RuntimeDatasmithOverrides** (9 assets) — duplicated from the engine
  `DatasmithRuntime` plugin materials (`M_Opaque`, `M_Transparent`, etc.). A
  project-owned material function (`MF_ControlDatasmithMaterial` or
  `MF_ControlDatasmithMaterialTransparency`) is injected into each material
  graph so that the runtime can control visibility and masking.

- **DatasmithMasterMaterials** (5 assets) — duplicated from the Twinmotion
  plugin materials (`M_TMStdOpaque`, `M_StdTranslucentNEW`). These are only
  generated when the `Content/Twinmotion/` directory is present. The same
  material functions are injected, but the wiring strategy differs because
  Twinmotion materials chain `MaterialFunctionCall` nodes directly to the root
  `MaterialAttributes` pin rather than using `MakeMaterialAttributes` nodes.

### How it works

1. On editor startup `FMobiusEditorModule` waits for the asset registry to
   finish scanning, then checks whether the expected material instances already
   exist (`MI_Opaque` for runtime overrides, `MI_DatasmithOpaqueMasked` for
   Twinmotion overrides).
2. If any are missing it instantiates `UGenerateDatasmithMaterialsCommandlet`
   and calls `Main()` in-process (no subprocess needed).
3. The commandlet duplicates each source material, injects the control material
   function between the existing graph output and the root node, overrides the
   blend mode where needed, recompiles, and saves the `.uasset` to disk.
4. A validation pass confirms every expected asset exists before reporting
   success.

### Manual regeneration

To regenerate all overrides from scratch (e.g. after upgrading the engine or
Twinmotion plugin), delete the output folders and reopen the editor:

```
Content/01_Dev/RuntimeMeshGenerator/RuntimeDatasmithOverrides/
Content/01_Dev/RuntimeMeshGenerator/DatasmithMasterMaterials/
```

Alternatively, run the commandlet from the command line:

```bash
UnrealEditor-Cmd.exe ProjectMobius.uproject -run=GenerateDatasmithMaterials -unattended -nop4 -nosplash -nullrhi
```

Or use the provided scripts: `Scripts/GenerateDatasmithMaterials.bat` (Windows)
/ `Scripts/GenerateDatasmithMaterials.sh` (macOS/Linux).

## Package Example

```bash
RunUAT.bat BuildCookRun ^
  -project="UnrealFolder/ProjectMobius/ProjectMobius.uproject" ^
  -noP4 -platform=Win64 -clientconfig=Development ^
  -cook -build -stage -pak -archive ^
  -archivedirectory=./Binaries/Release
```

## Related References

- [UE Automation Guide](../UnrealFolder/ProjectMobius/BuildDocs/UE-Automation-Guide.md)
- [Superbuild CTest Guide](../UnrealFolder/ProjectMobius/BuildDocs/Superbuild-CTest-Guide.md)
- [BuildDocs README](../UnrealFolder/ProjectMobius/BuildDocs/README.md)
