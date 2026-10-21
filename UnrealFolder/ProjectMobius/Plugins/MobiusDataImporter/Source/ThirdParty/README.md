# ThirdParty — vendored libraries

## simdjson (JSON import fast path, perf task A7)

- `simdjson/` — the upstream amalgamated release of [simdjson](https://github.com/simdjson/simdjson)
  **v4.6.4** (`simdjson.h` + `simdjson.cpp` + `LICENSE`, Apache-2.0), byte-identical to the release
  assets — never edit these files; to upgrade, replace both with a newer amalgamation and re-run the
  `ProjectMobius.SimData.JsonParserParity` automation test.
- Compiled inside the MobiusDataImporter module via `Private/MobiusSimdJsonAmalgamation.cpp`
  (wrapped in `THIRD_PARTY_INCLUDES_START/END`); consumed only through
  `Private/MobiusSimdJson.h`. `SIMDJSON_EXCEPTIONS=0` is forced in `MobiusDataImporter.Build.cs`
  because UE compiles without exception support — only simdjson's error-code API may be used.
- Runtime CPU dispatch is automatic (SSE4.2/AVX2 on x64, NEON on arm64), so the same source works
  for the future macOS target.

## HDF5

> **Writing pedestrian data from another simulator?** You can build this HDF5 source on its own,
> without Unreal or the superbuild: see `docs/HDF5-EXPORT-GUIDE.md` at the repository root.

## Layout

- `hdf5-2.0.0/` — full upstream HDF5 2.0.0 source (CMake-only since 2.0; autotools was dropped
  upstream). This source is what git tracks; the libraries below are built from it.
- `hdf5-2.0.0/install/` — the build output the UE module actually consumes. **None of it is in
  git** (see `.gitignore`): a fresh clone gets it by running the superbuild
  (`python superbuild.py` from `UnrealFolder/ProjectMobius`).
  - `include/` — public headers, including the generated `H5pubconf.h`.
  - `lib/libhdf5.lib`, `lib/libhdf5_hl.lib`, `lib/zlib-static.lib` — static libs, linked by
    `UeHdf5Library.Build.cs`.
  - `bin/` — MSVC runtime redistributables (`UeHdf5Library.Build.cs` checks the dir exists; nothing
    is loaded from it under the static link).
- `UeHdf5Library.Build.cs` — external UE module: adds `install/include` to the include path and
  links the three static libs. Nothing else in the source tree is used by the UE build.

## What is intentionally NOT tracked (see repo `.gitignore`)

The whole `install/` tree and `hdf5-2.0.0/build*` —
machine-generated at configure/install time and re-stamped with absolute local paths on every
`cmake --install`. If they show up as modified/untracked after a rebuild, that is expected.

## Rebuilding the libs (only needed when bumping HDF5 or changing build flags)

Configuration used for the current libs (from `libhdf5.settings`): Release, static-only C + HL
libraries, zlib compression via the bundled TGZ superbuild, tests/tools/threadsafety/parallel OFF,
built with MSVC 14.38 (the UE 5.5 toolchain).

```bat
cd hdf5-2.0.0
cmake -B build -S . ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON ^
  -DHDF5_BUILD_HL_LIB=ON -DHDF5_BUILD_TOOLS=OFF -DBUILD_TESTING=OFF ^
  -DHDF5_ENABLE_ZLIB_SUPPORT=ON -DZLIB_USE_LOCALCONTENT=OFF ^
  -DCMAKE_INSTALL_PREFIX=%CD%\install
cmake --build build --config Release
cmake --install build --config Release
```

## macOS status

`UeHdf5Library.Build.cs` has a Mac branch expecting `install/lib/libhdf5.a`, `libhdf5_hl.a` and
`libzlib-static.a` — **none are vendored yet**, so MobiusDataImporter does not link on Mac until an
hdf5 build is run on a Mac (same CMake recipe as below, static libs) and its libs land in
`install/lib`. `H5pubconf.h` is platform-generated too — a Mac build produces its own; do not reuse
the Windows one for the Mac libs.

Notes:
- `ZLIB_USE_LOCALCONTENT=OFF` makes the superbuild download the zlib TGZ at configure time
  (URLs in `config/CacheURLs.cmake`) — network access required.
- `install/` is regenerated as a whole (headers, including the platform-generated `H5pubconf.h`,
  and libs together), so nothing under it needs reverting or committing after a rebuild.
- Keep the record layout expectations of the importer in mind: the libs must stay ABI-compatible
  with the `H5pubconf.h` installed beside them.
