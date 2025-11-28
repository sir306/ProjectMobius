# Superbuild CTest Guide (Dev Blog Edition)

## Why We Did This
We wanted a quick, repeatable sanity check after the superbuild finishes: make sure the staged Qt tools exist and can launch headless, and actually run Assimp’s unit suite. Results needed to be readable for both engineers (XML) and non-devs (plain text).

## What’s Included
- Superbuild knobs: `SUPERBUILD_ENABLE_TESTS` (generate) and `SUPERBUILD_RUN_TESTS` (run), both default `ON`. `SUPERBUILD_CTEST_CONFIG` controls the multi-config flavor (default `Release`).
- Qt app selftests: both `OpenFileTCP` and `PlotUE_Data` accept `--selftest` to start a `QCoreApplication` and exit 0 (no GUI/plugins).
- Assimp unit run: builds the `unit` target, runs it, emits gtest XML plus a text summary.

## How It Runs
- CTest target: `superbuild_tests` depends on staging (`assimp_stage`, Qt stages). On build, it runs `ctest` (with `-C ${SUPERBUILD_CTEST_CONFIG}` when needed) and logs to `_superbuild/Testing/superbuild-ctest.log`.
- Qt checks:
  - Verify staged artifacts in `Tools/bin/<platform>/...`.
  - Launch each tool with `--selftest` from the staged location.
- Assimp checks:
  - `ASSIMP_BUILD_TESTS=ON` when tests run.
  - Build and execute the `unit` target.
  - Outputs: `_superbuild/Testing/assimp-unit.xml` (dev/CI) and `_superbuild/Testing/assimp-unit-summary.txt` (plain counts + failed test names).

## Commands (copy/paste)
**Windows (PowerShell)**
```powershell
cmake -S . -B _superbuild -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.9.2/msvc2022_64"
cmake --build _superbuild --config Release --parallel
# Toggles:
# -DSUPERBUILD_ENABLE_TESTS=OFF   # don’t generate tests
# -DSUPERBUILD_RUN_TESTS=OFF      # generate but don’t run
# -DSUPERBUILD_CTEST_CONFIG=Debug
```

**macOS/Linux (Zsh/Ninja)**
```sh
cmake -S . -B _superbuild -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.9.3/macos"
cmake --build _superbuild --parallel
```

## What to Read After a Run
- `_superbuild/Testing/superbuild-ctest.log` — staging + Qt selftests.
- `_superbuild/Testing/assimp-unit.xml` — gtest XML (dev/CI).
- `_superbuild/Testing/assimp-unit-summary.txt` — human-friendly totals and any failed test names.

## Notes & Rationale
- Existence checks use small CMake scripts for clearer “Found/Missing” messages.
- Dropped fragile `--help` runs; `--selftest` is stable headless.
- MSVC/multi-config is covered via `SUPERBUILD_CTEST_CONFIG` and `ctest -C`.
- If you disable tests, Assimp builds without its test suite.

## Source Touchpoints
- `CMakeLists.txt` (options, ctest wiring, Assimp run, summary script)
- `Tools/QT_Apps/OpenFileTCP/main.cpp` (`--selftest`)
- `Tools/QT_Apps/PlotUE_Data/main.cpp` (`--selftest`)
- `Commands for Cmake.txt` (quick-start commands)
