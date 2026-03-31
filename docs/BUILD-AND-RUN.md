# Build and Run

This page keeps the longer setup and testing notes out of the root README.

## Video Walkthrough

This walkthrough covers the full developer setup pipeline for Project Mobius:

- Clone the repository
- Configure and build with CMake
- Open the Unreal project and let Unreal rebuild missing modules when needed
- Package a distributable build*
- Add Twinmotion compatibility
- Understand Datasmith modes in Unreal Engine (`Runtime Datasmith` vs `Twinmotion Datasmith`)

* Any packaged build you share must comply with the Epic EULA and all
  applicable third-party software licenses.

Watch the tutorial:

[![Project Mobius Build and Package Walkthrough](https://img.youtube.com/vi/q48IM4RXzTg/maxresdefault.jpg)](https://youtu.be/q48IM4RXzTg)

## Prerequisites

Common:

- Git
- Unreal Engine 5.5
- CMake 3.21 or newer
- Internet access during the first superbuild so HDF5 can fetch `zlib-1.3.1`
  for compression support
- Python if you plan to use the JSON-to-HDF5 conversion script

<details open>
<summary><strong>Windows</strong></summary>

- Windows 10 or 11
- Visual Studio 2022 with C++ tooling

</details>

<details>
<summary><strong>macOS (Apple Silicon)</strong></summary>

- macOS 11 or newer on Apple Silicon
- Full Xcode selected via `xcode-select`
- Unreal Engine 5.5 Xcode compatibility in the local engine source:
  `15.2.0` minimum, `16.9.0` maximum
- Current local setup used `Xcode 16.4`
- Ninja

macOS support exists in parts of the codebase, but Windows is still the
primary development and validation target.

</details>

## Clone the Repository

```bash
git clone https://github.com/sir306/ProjectMobius.git
cd ProjectMobius/UnrealFolder/ProjectMobius
```

No submodules are required for the standard source checkout.

## Build

The superbuild compiles the vendored HDF5 and Assimp dependencies into the
locations expected by the Unreal project. No separate HDF5 installation is
required, but the first HDF5 build downloads `zlib-1.3.1` unless that archive
is already cached or redirected locally.

<details open>
<summary><strong>Windows</strong></summary>

```bash
cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64
cmake --build _superbuild --config Release --parallel
```

Build the Unreal target after the superbuild:

1. Open `UnrealFolder/ProjectMobius/ProjectMobius.uproject` in Unreal Engine 5.5.
2. If Unreal prompts to rebuild missing modules, allow it to compile the editor target.
3. Generate Visual Studio project files and build `ProjectMobiusEditor` manually only if the automatic rebuild fails or you want IDE integration.

</details>

<details>
<summary><strong>macOS (Apple Silicon)</strong></summary>

```bash
cmake -S . -B _superbuild -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build _superbuild --parallel
```

Build the Unreal target after the superbuild:

1. Open `UnrealFolder/ProjectMobius/ProjectMobius.uproject` in Unreal Engine 5.5.
2. If Unreal prompts to rebuild missing modules, allow it to compile the editor target.
3. Generate IDE project files and build `ProjectMobiusEditor` manually only if the automatic rebuild fails or you want IDE integration.

</details>

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
  materials (`M_TMStdOpaque`, `M_StdTranslucentNEW`). These are only
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

## Package

### Example command

```bash
RunUAT.bat BuildCookRun ^
  -project="UnrealFolder/ProjectMobius/ProjectMobius.uproject" ^
  -noP4 -platform=Win64 -clientconfig=Development ^
  -cook -build -stage -pak -archive ^
  -archivedirectory=./Binaries/Release
```

### macOS packaged build configuration

Project Mobius currently packages macOS development builds with the App
Sandbox whitelist disabled. This is intentional for the current file workflow.
The packaged app opens user-selected simulation data files and writes
screenshots and related output into a `MobiusCaptures` folder beside the
selected data file. That flow is not currently implemented against the stricter
macOS sandbox access model, so the sandbox whitelist must remain disabled for
these builds.

Change these source entitlement files before packaging a macOS build:

- `UnrealFolder/ProjectMobius/Build/Mac/Resources/Sandbox.NoNet.entitlements`
- `UnrealFolder/ProjectMobius/Build/Mac/Resources/Sandbox.Server.entitlements`

In both files, keep the App Sandbox setting disabled:

```xml
<key>com.apple.security.app-sandbox</key>
<false/>
```

The network-enabled entitlement file also keeps client and server access
enabled:

```xml
<key>com.apple.security.network.client</key>
<true/>
<key>com.apple.security.network.server</key>
<true/>
```

For local macOS packaging, keep the following setting in
`UnrealFolder/ProjectMobius/Config/DefaultEngine.ini`:

```ini
[/Script/MacTargetPlatform.XcodeProjectSettings]
bMacSignToRunLocally=True
```

After packaging, verify the generated entitlements in:

`UnrealFolder/ProjectMobius/Saved/StagedBuilds/ProjectMobius (Mac).build/Mac/ProjectMobius.build/DerivedSources/Entitlements.plist`

Do not edit the generated entitlements file directly. It is recreated during
packaging. The source of truth is the entitlement templates under
`UnrealFolder/ProjectMobius/Build/Mac/Resources/`.

### macOS packaged build runtime permissions

Disabling the App Sandbox whitelist does not bypass macOS privacy prompts. If
the packaged app opens or saves data in protected folders such as `Documents`,
`Desktop`, or `Downloads`, macOS may still require the user to approve access
before Project Mobius can save screenshots beside the selected data file.

To review or change that permission on macOS:

1. Open `Apple menu > System Settings`.
2. Go to `Privacy & Security`.
3. Open `Files & Folders`.
4. Find `Project Mobius`.
5. Enable access for the folder in use, such as `Documents`, `Desktop`, or
   `Downloads`.

If `Project Mobius` is not listed yet, launch the packaged build and try
opening or saving a file in the target folder once. macOS should then prompt
for access.

Repackaged local builds may need this approval again after a rebuild, because
macOS tracks Files & Folders access against the packaged app's signing
identity.

## Related References

- [UE Automation Guide](../UnrealFolder/ProjectMobius/BuildDocs/UE-Automation-Guide.md)
- [Superbuild CTest Guide](../UnrealFolder/ProjectMobius/BuildDocs/Superbuild-CTest-Guide.md)
- [BuildDocs README](../UnrealFolder/ProjectMobius/BuildDocs/README.md)
