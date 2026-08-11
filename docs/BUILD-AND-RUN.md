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
- CMake 3.21 or newer (3.24+ to use `cmake --fresh`)
- Python 3.8 or newer — for `superbuild.py`, and for the JSON-to-HDF5
  conversion script
- Internet access during the first superbuild so HDF5 can fetch `zlib-1.3.1`
  for compression support

<details open>
<summary><strong>Windows</strong></summary>

- Windows 10 or 11
- Visual Studio 2022 or 2026 with the **Desktop development with C++** workload.
  The superbuild detects which one you have — nothing is hardcoded to a
  particular Visual Studio version.
- Optional but recommended: the **MSVC v143 14.38.33130** toolset (Visual Studio
  Installer → Individual components). That is the toolset Unreal Engine 5.5
  itself builds with, and the superbuild prefers it when present so the
  dependency DLLs link the same CRT as the engine. If it is missing the
  superbuild uses the newest installed toolset and says so.

</details>

<details>
<summary><strong>macOS (Apple Silicon)</strong></summary>

- macOS 11 or newer on Apple Silicon
- Full Xcode selected via `xcode-select`
- Unreal Engine 5.5 in the local engine source supports Xcode `15.2.0`
  through `16.9.0`
- These notes were last tested with `Xcode 16.4`
- Ninja is optional. With none installed CMake falls back to Unix Makefiles,
  which works; to prefer Ninja pass `--generator Ninja` to `superbuild.py` (or
  export `CMAKE_GENERATOR=Ninja` for the plain-CMake route).

macOS support exists in parts of the codebase, but Windows is still the
primary development and validation target.

Known macOS limitation: heatmap Gaussian blur is currently unavailable. The
current Visualization module relies on Epic's built-in OpenCV plugin, and this
project does not yet ship the Mac OpenCV libraries needed for that path.
Supporting Gaussian blur on macOS will require a custom plugin or an equivalent
code-side replacement.

</details>

## Clone the Repository

```bash
git clone https://github.com/sir306/ProjectMobius.git
cd ProjectMobius/UnrealFolder/ProjectMobius
```

No submodules are required for the standard source checkout.

## Build

The superbuild compiles the vendored Assimp, HDF5 and IFC++ dependencies into
the locations expected by the Unreal project:

| Dependency | Installed to |
| --- | --- |
| Assimp | `Plugins/UE4_Assimp/Source/ThirdParty/UE_AssimpLibrary/assimp/{include,lib,bin}` |
| HDF5 | `Plugins/MobiusDataImporter/Source/ThirdParty/hdf5-2.0.0/install/{include,lib}` |
| MobiusIfcBridge | `Source/ThirdParty/MobiusIfcLibrary/install/{include,lib,bin}` (Win64 only) |

None of those outputs are committed, so **a fresh checkout must run the
superbuild once before opening the `.uproject`.** What is excluded differs per
dependency, because the vendored trees differ:

- **Assimp** — the upstream source *including* `include/` is vendored and
  tracked; `lib/`, `bin/` and the CMake-generated `include/assimp/config.h` and
  `revision.h` are ignored (the last two by assimp's own nested `.gitignore`).
- **HDF5** — the upstream source is tracked; the entire `install/` tree is
  ignored. It used to be half-tracked, which meant 69 generated headers were
  committed and could drift against the libs beside them.
- **MobiusIfcBridge** — `IfcBridgeSource/` (the vendored IFC++ plus the shim's
  own `.h`/`.cpp`) is tracked; the entire `MobiusIfcLibrary/install/` tree is
  ignored.

The `_superbuild/` build tree is ignored in full. No separate HDF5 installation is
required, but the first HDF5 build downloads `zlib-1.3.1` unless that archive
is already cached or redirected locally.

Run from `UnrealFolder/ProjectMobius`. The same command works on every
platform:

```bash
python superbuild.py
```

(`python3` on macOS/Linux. Needs Python 3.8+ and CMake 3.21+.)

That is the whole thing. It detects your compiler — on Windows, your Visual
Studio and MSVC toolset — discards a `_superbuild/` tree that cannot be reused,
builds and installs every dependency, then verifies that each file
UnrealBuildTool will look for is actually on disk. The IFC++ bridge is Win64-only
today and is skipped automatically elsewhere.

You should never need to clean the build tree by hand. It is discarded and
regenerated automatically when it was created on a different machine, by a
different CMake, or for a different generator or MSVC toolset — and it says
which of those happened.

Useful switches:

| Switch | Effect |
| --- | --- |
| `--clean` | Discard the CMake build tree first |
| `--rebuild` | Also discard the installed dependency trees |
| `--skip-ifc` | Skip the IFC++ pass (by far the longest) |
| `--force-ifc` | Rebuild `MobiusIfcBridge` after editing its `.cpp`/`.h` |
| `--generator` / `--toolset` | Override the auto-detected toolchain |
| `--help` | Everything else |

### Plain CMake

Equally supported — the script is a convenience layer, not a requirement:

```bash
cmake --fresh -S . -B _superbuild -DCMAKE_BUILD_TYPE=Release
cmake --build _superbuild --config Release --parallel
```

`--fresh` (CMake 3.24+) is the "clear the old build files" half: it drops the
existing cache and reconfigures. Omit it for an ordinary incremental build.

It is **not** "rebuild everything": it removes `CMakeCache.txt` and
`CMakeFiles/`, but the ExternalProject stamps survive, so Assimp and HDF5 are
still considered built. For a genuine from-scratch build — which is what you
want when changing MSVC toolsets, since the installed `.lib`/`.dll` filenames
themselves move — use `python superbuild.py --rebuild`.

Note the absence of `-G`. CMake picks the newest installed Visual Studio itself,
and the platform default for VS 2019+ generators is already the host
architecture. Hardcoding `-G "Visual Studio 17 2022"` — as these docs used to —
fails on any machine with a different Visual Studio, with an error that never
says what *is* installed.

What plain CMake gives up: the MSVC toolset is not pinned to `14.38.33130`. To
pin it by hand, note the version alone is **not** enough — the VS 2026 generator
pairs it with its own default `v145` and rejects it. Name the platform toolset:

```bash
cmake --fresh -S . -B _superbuild -T v143,version=14.38.33130
```

If no usable Visual Studio is found, `superbuild.py` distinguishes the three
cases — none installed, one without the C++ workload, or one newer than its
detection table — lists what *is* installed, and prints the exact `--generator`
override. New Visual Studio releases need one row added to
`VS_MAJOR_TO_GENERATOR` in `superbuild.py` (and its PowerShell twin in
`cmake/Resolve-MsvcToolchain.ps1`, used only when
`Build-MobiusIfcBridge.ps1` is run by hand).

### Then build the Unreal target

1. Open `UnrealFolder/ProjectMobius/ProjectMobius.uproject` in Unreal Engine 5.5.
2. If Unreal prompts to rebuild missing modules, let it compile the editor
   target.
3. If you want IDE integration, open the `.uproject` directly in an IDE that
   supports it, or generate the Visual Studio / Xcode project files if your
   setup requires them. Build `ProjectMobiusEditor` manually only if the
   automatic rebuild fails.

## Run Tests

### Superbuild tests

These run automatically at the end of `superbuild.py`. To re-run them on
their own:

```bash
ctest --test-dir _superbuild -C Release --output-on-failure
```

Each check names exactly one file, and they are the same files UnrealBuildTool
probes. A failure here is the same failure you would otherwise hit twenty
minutes into an Unreal build.

### Troubleshooting

**`Unable to instantiate module 'MobiusIfcLibrary': ... lib dir not found ... Run the CMake install step.`**
(or the same message for `UE_AssimpLibrary` / HDF5) — the superbuild has not
been run, or its IFC pass was skipped. Run `python superbuild.py`. The final
summary lists every dependency as `[ok]` or `[MISSING]`.

**`Generator "Visual Studio 17 2022" could not find any instance of Visual Studio.`**
— you invoked CMake directly with a generator that does not match your
installed Visual Studio. Drop the `-G` (CMake picks it), or use `superbuild.py`.

**`The current CMakeCache.txt directory ... is different than the directory ... where CMakeCache.txt was created`**
— the `_superbuild/` folder came from another machine (a backup restore or a
copied tree). Re-run with `cmake --fresh`, or let `superbuild.py` detect and replace it.

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

## Launch with Preloaded Files

Project Mobius can be started with geometry, pedestrian and B-RISK files already
loaded, so another application can hand a prepared scenario straight to the
viewer. This is the supported integration surface for third-party tools.

```bash
ProjectMobius.exe -MobiusGeometry="<path>/building.fbx" -MobiusPedestrian="<path>/pedestrians.json" -MobiusBRisk="<path>/scenario.smv"
```

All three arguments are **optional and independent**. Pass one, two, or all
three; each file is loaded on its own and one failure never blocks the others.

| Argument | Accepted types |
| --- | --- |
| `-MobiusGeometry=` | `.fbx` `.obj` `.udatasmith` `.ifc` `.wkt` `.h5` |
| `-MobiusPedestrian=` | `.json` `.h5` |
| `-MobiusBRisk=` | `.smv` (the manifest only — its companion files are read from the same folder) |

Each preloaded file behaves exactly as if its **Browse** button had been used:
the file field in the UI is populated so the user can see what is loaded, and the
full import and pre-processing chain runs.

### Quoting

Wrap every path in double quotes so spaces survive, and do not leave a trailing
path separator immediately before the closing quote — on Windows a trailing
backslash escapes the quote and the argument runs into the next one.

### Driving an already-running instance

The same three loads are available as console commands, which also makes them
usable through Unreal's own `-ExecCmds` argument:

```
Mobius.Load.Geometry <path>
Mobius.Load.Pedestrian <path>
Mobius.Load.BRisk <path>
Mobius.Load.Status          # what was requested, and what happened to each file
```

```bash
ProjectMobius.exe -ExecCmds="Mobius.Load.Pedestrian <path>/pedestrians.json"
```

### Behaviour to expect

- **Startup timing.** A queued file is not loaded until the level has finished
  initialising and the loader for that file type is live. This is deliberate: the
  geometry loader in particular belongs to a level actor, so an earlier request
  would be discarded silently.
- **First-launch legal notice.** On a packaged first run the mandatory terms and
  licence notice appears before anything is loaded. Queued files are held until
  it is accepted; if it is declined they are discarded and the application exits
  without loading. Time spent reading the notice is not counted against the
  readiness timeout.
- **Do not pass `-unattended`.** It takes effect in packaged builds, not only in
  the editor, and it suppresses the notice — so acceptance can never be recorded.
  Preloading is refused in that state rather than treated as consent.
- **Invalid paths** are reported in the application's error window and named in
  the log under `LogMobiusPreload`. Any valid files supplied alongside them still
  load.
- **Once per process.** Launch arguments are consumed by the first level load.
  In the editor that means the first Play In Editor session of an editor run;
  re-trigger later sessions with the `Mobius.Load.*` commands above.

Optional: `-MobiusPreloadTimeout=<seconds>` overrides how long a queued file
waits for its loader after the notice is accepted (console variable
`Mobius.Preload.TimeoutSeconds`, default 30).

### Helper script

`Scripts/Launch-Mobius.ps1` in the source repository wraps all of the above,
validates the files before launching, and prints the exact command line it used
so it can be lifted into another application. It is a developer convenience and
is not staged into packaged builds — the arguments above are the contract:

```powershell
./Scripts/Launch-Mobius.ps1 -Geometry '<path>/building.fbx' -Pedestrian '<path>/pedestrians.json'
./Scripts/Launch-Mobius.ps1 -Pedestrian '<path>/pedestrians.json' -DryRun
./Scripts/Launch-Mobius.ps1 -Mode EditorGame -BRisk '<path>/scenario.smv' -ShowLog
```

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
Sandbox disabled. This is intentional for the current file workflow.
The packaged app opens user-selected simulation data files and writes
screenshots and related output into a `MobiusCaptures` folder beside the
selected data file. That flow is not currently implemented against the stricter
macOS sandbox access model, so the App Sandbox must remain disabled for these
builds.

Local macOS packaging uses the following entitlement templates when they are
present:

- `UnrealFolder/ProjectMobius/Build/Mac/Resources/Sandbox.NoNet.entitlements`
- `UnrealFolder/ProjectMobius/Build/Mac/Resources/Sandbox.Server.entitlements`

These are local packaging resources, not authored project source. A fresh
checkout may not contain them yet.

If these files are missing on your machine, run one local macOS packaging pass
first so Unreal creates the local Mac packaging resources. Then review the
local entitlement files before packaging again.

Because this is a public repository, keep files under
`UnrealFolder/ProjectMobius/Build/Mac/Resources/` local and do not commit them.
Do not commit generated files under `Saved/StagedBuilds/...` either.

In both files, keep the App Sandbox disabled:

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

Disabling the App Sandbox does not bypass macOS privacy prompts. If
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
