# ThirdParty — vendored HDF5

## Layout

- `hdf5-2.0.0/` — full upstream HDF5 2.0.0 source (CMake-only since 2.0; autotools was dropped
  upstream). Kept in-tree so the prebuilt libraries below can be rebuilt without hunting down the
  exact upstream snapshot.
- `hdf5-2.0.0/install/` — the build output the UE module actually consumes:
  - `include/` — public headers, **including the generated `H5pubconf.h`** (deliberately tracked so
    a fresh clone compiles without running CMake; it carries harmless machine stamps from whoever
    generated it last).
  - `lib/libhdf5.lib`, `lib/libhdf5_hl.lib`, `lib/zlib-static.lib` — static libs, linked by
    `UeHdf5Library.Build.cs`.
  - `bin/` — MSVC runtime redistributables (`UeHdf5Library.Build.cs` checks the dir exists; nothing
    is loaded from it under the static link).
- `UeHdf5Library.Build.cs` — external UE module: adds `install/include` to the include path and
  links the three static libs. Nothing else in the source tree is used by the UE build.

## What is intentionally NOT tracked (see repo `.gitignore`)

`install/cmake/`, `install/lib/pkgconfig/`, `install/lib/libhdf5.settings`, `hdf5-2.0.0/build*` —
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

Notes:
- `ZLIB_USE_LOCALCONTENT=OFF` makes the superbuild download the zlib TGZ at configure time
  (URLs in `config/CacheURLs.cmake`) — network access required.
- After an install, only `install/include/H5pubconf.h` may show a real tracked diff (path/date
  stamps). Revert it unless the rebuild was intentional; commit it together with the new libs when
  it was.
- Keep the record layout expectations of the importer in mind: the libs must stay ABI-compatible
  with `H5pubconf.h` as committed.
