# Build and Run

This page keeps the longer setup and testing notes out of the root README.

## Prerequisites

- Windows 10 or 11 is the verified platform
- Unreal Engine 5.5
- Visual Studio 2022 with C++ tooling
- CMake 3.21 or newer
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
locations expected by the Unreal project.

```bash
cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64
cmake --build _superbuild --config Release --parallel
```

## Build the Unreal Editor Target

1. Open `UnrealFolder/ProjectMobius/ProjectMobius.uproject` in Unreal Engine 5.5.
2. Generate Visual Studio project files from the `.uproject`.
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
