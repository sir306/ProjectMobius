# HDF5 Export Guide

How to write your simulation's pedestrian output as a Project Mobius `.h5` file, using
either Python or the HDF5 C library that ships with this repository.

What the file must contain is defined in [Pedestrian Data Format](PEDESTRIAN-DATA-FORMAT.md).
This page is the practical side: getting the code, building the library, writing the
file, checking it, and opening it in Mobius.

## Contents

- [Choose a route](#choose-a-route)
- [Get the files from the repository](#get-the-files-from-the-repository)
- [Route A: Python](#route-a-python)
- [Route B: C or C++ with the bundled HDF5](#route-b-c-or-c-with-the-bundled-hdf5)
- [Validate the file](#validate-the-file)
- [Open it in Mobius](#open-it-in-mobius)
- [Troubleshooting](#troubleshooting)

## Choose a route

| Your situation | Use |
| --- | --- |
| Your model already writes a CSV/text file of positions over time | [Route A](#convert-a-csv-export), `from-csv`: no programming |
| You post-process results in Python, or can | [Route A](#write-from-your-own-python-code), `write_mobius_h5()` |
| You want your C/C++ simulator to write the file directly | [Route B](#route-b-c-or-c-with-the-bundled-hdf5), the bundled HDF5 C library |
| Your model is buildingEXODUS | [Exporting from buildingEXODUS](EXODUS-EXPORT.md) (Route A underneath) |

Both routes produce equivalent files (string field sizes differ, nothing Mobius cares
about). Everything below lives in
[`docs/examples/pedestrian-hdf5/`](examples/pedestrian-hdf5/):

| File | What it is |
| --- | --- |
| [`mobius_h5_writer.py`](examples/pedestrian-hdf5/mobius_h5_writer.py) | Python writer: importable `write_mobius_h5()` function, plus `from-csv` and `demo` commands |
| [`validate_mobius_h5.py`](examples/pedestrian-hdf5/validate_mobius_h5.py) | Checks any `.h5` against the rules the Mobius importer relies on |
| [`write_mobius_h5.c`](examples/pedestrian-hdf5/write_mobius_h5.c) | Self-contained C example using the HDF5 C API |
| [`CMakeLists.txt`](examples/pedestrian-hdf5/CMakeLists.txt) | Builds the C example against your HDF5 install |
| [`requirements.txt`](examples/pedestrian-hdf5/requirements.txt) | Python dependencies (`numpy`, `h5py`) |

## Get the files from the repository

Repository: <https://github.com/sir306/ProjectMobius>

You do **not** need Unreal Engine, Git LFS or the full repository to write data files.

**Python only (Route A):** download `mobius_h5_writer.py`, `validate_mobius_h5.py` and
`requirements.txt` from [`docs/examples/pedestrian-hdf5/`](examples/pedestrian-hdf5/)
through the GitHub web interface (open each file, then *Download raw file*). Nothing
else is needed.

**C/C++ (Route B), or to have everything locally:** fetch only the docs and the bundled
HDF5 source with a sparse clone (about 30 MB to download, 200 MB on disk, instead of the
multi-gigabyte full repository):

```bash
git clone --depth 1 --filter=blob:none --sparse https://github.com/sir306/ProjectMobius.git
cd ProjectMobius
git sparse-checkout set docs UnrealFolder/ProjectMobius/Plugins/MobiusDataImporter/Source/ThirdParty/hdf5-2.0.0
```

The bundled library is the **complete, unmodified HDF5 2.0.0 source**, at
`UnrealFolder/ProjectMobius/Plugins/MobiusDataImporter/Source/ThirdParty/hdf5-2.0.0/`.
It is the same version Mobius reads with; files written with older HDF5 releases read
fine too, so the library your tool already links is usually good enough. Its licence is in
`hdf5-2.0.0/LICENSE` and summarised in
[THIRD-PARTY-LICENSES.md](../THIRD-PARTY-LICENSES.md).

## Route A: Python

### Install

Python 3.8 or newer:

```bash
pip install -r docs/examples/pedestrian-hdf5/requirements.txt
```

`h5py` wheels contain their own copy of HDF5 (2.0.0 in current releases), so no
compiler or separate HDF5 install is needed.

### Write from your own Python code

Put `mobius_h5_writer.py` next to your script (or on `sys.path`). Collect one entry per
agent per timestep the agent is present, then call `write_mobius_h5()` once at the end:

```python
from mobius_h5_writer import write_mobius_h5, heading_and_speed_from_positions

# One entry per (agent, timestep) the agent is in the building.
timestep_idx = [0, 0, 1, 1, 2]           # time = timestep_idx * 0.1 s
entity_id    = [0, 1, 0, 1, 0]           # dense 0..N-1; remap your own IDs first
x            = [0.0, 5.0, 0.12, 4.9, 0.24]   # metres
y            = [1.0, 2.0, 1.0, 2.0, 1.0]
z            = [0.0, 0.0, 0.0, 0.0, 0.0]     # floor height, metres

# Use your model's own heading/speed if it has them; otherwise compute them:
rotation, speed = heading_and_speed_from_positions(timestep_idx, entity_id, x, y, z, 0.1)

write_mobius_h5(
    "run.h5", timestep_idx, entity_id, x, y, z,
    sampling_interval_s=0.1,
    rotation_deg=rotation,   # degrees, CCW from +x = degrees(atan2(dy, dx))
    speed_mps=speed,         # m/s
    entity_names=["Occupant 101", "Occupant 102"],   # e.g. your original IDs
)
```

The function refuses input that would load badly (sparse IDs, duplicate rows, NaN
positions, negative speeds) and handles every type detail for you: fixed-length strings,
32-bit fields, `duration`, and gzip compression for large files.

`heading_and_speed_from_positions()` applies the same rule Mobius uses when a file has
no heading or speed (face the next position, hold the heading while standing still).
Leaving `rotation_deg`/`speed_mps` out also works, but computing them here keeps large
files quick to load in Mobius releases from before October 2026, which derive them
slowly.

### Convert a CSV export

If your model can export a "long" table, **one row per agent per output time**, convert
it directly:

```text
id,time,x,y,floor
1,0.0,2.00,5.00,1
1,0.5,2.50,5.00,1
2,0.0,0.00,1.00,0
...
```

```bash
python mobius_h5_writer.py from-csv positions.csv run.h5 --dt 0.1 --floor-z "0=0,1=3.5"
```

- Required columns: `id`, `time` (seconds), `x`, `y` (metres). Optional: `z`, `floor`.
  Different column names: `--col-id OccupantNo --col-time T --col-x X ...`.
- `--dt` is the output interval. Every agent is resampled onto `0, dt, 2·dt, …`
  between its first and last appearance, so the CSV times do not need to be evenly
  spaced. Without `--dt` the median gap in the CSV is used; pass it explicitly.
- `--interp` chooses how positions between CSV rows are filled:
  - `linear` (default) for **periodic snapshots** (a row for every agent every *n*
    seconds): straight-line motion between rows.
  - `hold` for **event logs** (a row only when an agent moves): the agent stays at its
    last recorded position until the next row. Linear interpolation would turn a wait
    in a queue into a slow creep and hide the congestion.
- `--floor-z "0=0,1=3.5,2=7"` maps a floor label to a floor height in metres;
  `--floor-height 3.5` does the same for numeric floors with a uniform storey height.
  Both add to `z` if a `z` column is present too. If the CSV has a floor column, one of
  the two is required (or `--col-floor none` to ignore it), so a multi-storey run cannot
  silently collapse onto one floor.
- `--scale 0.001` converts millimetres to metres (and so on).
- IDs can be any text or numbers; they are renumbered 0 … N−1 and the original goes into
  the agent name (`Agent 17`; change the prefix with `--name-prefix`).
- The earliest time in the CSV becomes time 0 in Mobius.
- Heading and speed are computed from the resampled positions with
  `heading_and_speed_from_positions()`. `--positions-only` leaves them out for Mobius to
  derive instead (fast in current builds, slow in releases from before October 2026).

### Quick demo

```bash
python mobius_h5_writer.py demo demo.h5
```

writes three people crossing a 10 m corridor, which you can open in Mobius to confirm
the toolchain end to end.

## Route B: C or C++ with the bundled HDF5

### 1. Build the bundled HDF5

Needs CMake 3.21+ and a C compiler (Visual Studio on Windows, GCC/Clang on Linux,
Xcode Command Line Tools on macOS). From the repository root, pick an install folder
for the library (`HDF5_PREFIX` below):

**Windows (PowerShell):**

```powershell
$HDF5_SRC    = "UnrealFolder/ProjectMobius/Plugins/MobiusDataImporter/Source/ThirdParty/hdf5-2.0.0"
$HDF5_PREFIX = "C:/libs/hdf5-2.0.0"
cmake -S $HDF5_SRC -B build-hdf5 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DHDF5_BUILD_TOOLS=ON -DHDF5_BUILD_HL_LIB=OFF -DHDF5_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF "-DCMAKE_INSTALL_PREFIX=$HDF5_PREFIX"
cmake --build build-hdf5 --config Release --parallel
cmake --install build-hdf5 --config Release
```

**Linux / macOS:**

```bash
HDF5_SRC=UnrealFolder/ProjectMobius/Plugins/MobiusDataImporter/Source/ThirdParty/hdf5-2.0.0
HDF5_PREFIX=$HOME/libs/hdf5-2.0.0
cmake -S "$HDF5_SRC" -B build-hdf5 -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
  -DHDF5_BUILD_TOOLS=ON -DHDF5_BUILD_HL_LIB=OFF \
  -DHDF5_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX="$HDF5_PREFIX"
cmake --build build-hdf5 --config Release --parallel
cmake --install build-hdf5 --config Release
```

This installs the static C library, headers, a CMake package and the command-line tools
(`h5dump`, `h5ls`, …) into `HDF5_PREFIX`. It needs no network access.

Options worth knowing:

- **gzip compression** (optional, only worth it for big files): add
  `-DHDF5_ENABLE_ZLIB_SUPPORT=ON -DHDF5_ALLOW_EXTERNAL_SUPPORT=TGZ
  -DZLIB_TGZ_ORIGPATH=https://github.com/madler/zlib/releases/download/v1.3.1
  -DZLIB_TGZ_NAME=zlib-1.3.1.tar.gz -DHDF5_USE_ZLIB_STATIC=ON`, the flags the Mobius
  superbuild itself uses. This downloads zlib during configure. The example enables gzip
  automatically when the library supports it.
- **Fortran** or the **C++ wrapper API**: add `-DHDF5_BUILD_FORTRAN=ON` or
  `-DHDF5_BUILD_CPP_LIB=ON`. The C API works from C++ unchanged, so the wrapper is
  optional.

### 2. Build and run the example

```bash
cmake -S docs/examples/pedestrian-hdf5 -B build-example -DCMAKE_PREFIX_PATH="<HDF5_PREFIX>"
cmake --build build-example --config Release
```

Run it (the executable is in `build-example/Release/` on Windows, `build-example/` elsewhere):

```bash
write_mobius_h5 example.h5                    # positions, heading, speed and mode
write_mobius_h5 example.h5 --positions-only   # positions only; Mobius derives heading/speed
<HDF5_PREFIX>/bin/h5dump -H example.h5         # inspect the layout
```

If CMake cannot find HDF5, pass the folder containing `hdf5-config.cmake` instead:
`-DHDF5_DIR=<HDF5_PREFIX>/cmake` on Windows (search the prefix for that file elsewhere).

### 3. Put it in your simulator

[`write_mobius_h5.c`](examples/pedestrian-hdf5/write_mobius_h5.c) is written to be
copied. Keep these parts as they are:

- the `entity_t` and `sample_t` structs: member order and widths are the contract;
- `write_metadata`, `write_entities`, `write_samples` and `write_table`.

Replace the demo loop in `main()` with your own:

1. Number your agents 0 … N−1 as they are created (keep your own ID in `name`).
2. At each output interval `dt`, append one `sample_t` per agent currently in the
   building, with `timestep_idx` = output count so far. Skip agents that have left.
   Fill `rotation`/`speed` from your model; if it has no heading, use
   `atan2(dy, dx)` towards the agent's next position (hold the last value while it stands
   still) and distance ÷ time for speed. Positions-only output
   (`with_rotation_speed_mode = 0`) also loads; Mobius then derives heading and speed
   itself, which releases from before October 2026 do slowly for thousands of agents.
3. At the end, fill `sim_time_s` per agent and write the file:
   `duration = (num_timesteps − 1) × dt`, `max_num_entities = N`.

Linking in your own build:

- **CMake:** `find_package(HDF5 CONFIG REQUIRED)` and link the target `hdf5-static`
  (see the example's [`CMakeLists.txt`](examples/pedestrian-hdf5/CMakeLists.txt)).
- **Other build systems:** add `<HDF5_PREFIX>/include` to the include path and link
  `<HDF5_PREFIX>/lib/libhdf5.lib` plus `shlwapi.lib` on Windows, or
  `<HDF5_PREFIX>/lib/libhdf5.a` plus the system libraries it needs (typically
  `-lm -ldl -lpthread` on Linux). `<HDF5_PREFIX>/lib/libhdf5.settings` lists exactly how
  the library was built.

For very long runs you can write samples in batches instead of holding them all in
memory: create `/simulation/samples` chunked with an unlimited maximum size and grow it
with `H5Dset_extent` after each batch. The resulting file is identical to the
single-write version.

## Validate the file

Always run the validator before sending a file on or opening it in Mobius:

```bash
python docs/examples/pedestrian-hdf5/validate_mobius_h5.py run.h5
```

```text
[PASS] run.h5
  info    rotation matches the direction of travel for 100% of moving samples
  info    2 entities, 9 timesteps (4 s at 0.5 s), 16 samples
  info    x [0.00, 6.00] m, y [1.00, 5.00] m, z [0.00, 3.50] m
```

It exits with code 1 on any error, so it can gate an automated export. Check the
`info` lines too: an `x`/`y` range in thousands usually means millimetres, and a time
span far from your run length usually means `--dt` or `sampling_rate` is wrong.

## Open it in Mobius

- In the app: load the `.h5` from the pedestrian data **Browse** button.
- From the command line or another program:

  ```bash
  ProjectMobius.exe -MobiusPedestrian="C:/runs/run.h5" -MobiusGeometry="C:/runs/building.fbx"
  ```

  See [Launch with Preloaded Files](BUILD-AND-RUN.md#launch-with-preloaded-files).

`Ctrl` + hover over an agent shows its entity details (`name`, `sim_time_s`,
`max_speed`, `m_plane`, `map`), a quick way to confirm IDs mapped as intended.

## Troubleshooting

| Symptom in Mobius | Usual cause |
| --- | --- |
| *"HDF5 file has unrecognized format"* | No `/metadata` group or no `/simulation/samples` dataset. |
| Agents everywhere / at absurd coordinates / all at the origin | A table member has the wrong type (64-bit float, big-endian) or the members are in the wrong order. Run the validator. |
| Garbled agent names, or a crash on load | A variable-length string inside `/entities` or `/simulation/samples`. Use fixed-length strings. |
| *"Unexpected entity IDs"* | `entity_id` not 0 … N−1 (often 1-based), or `max_num_entities` set to the peak instead of the total. |
| *"Incomplete entity data"* | Some entity numbers never appear in the samples (gaps in your numbering). |
| *"Data may be truncated"* | `duration` larger than `(num_timesteps − 1) × sampling_rate`. |
| Playback far too fast or slow | `sampling_rate` given as a frequency (10) instead of an interval (0.1), or output not evenly spaced. |
| Agents 100× too small/large, or mirrored against the building | Positions not in metres, or geometry exported with a different origin/axes. With a WKT floor outline, mirroring is expected: negate the WKT's y values ([why](PEDESTRIAN-DATA-FORMAT.md#optional-root-attribute-wkt_geometry)). |
| All agents face one way | Zeros written in `rotation`. Leave the member out instead. |
| An error dialog per sample about NaN or speed | NaN placeholders for absent agents, or negative speeds. Write no row instead. |
| *"Unable to import Mobius HDF5 data"* with a compression error in the log | `lzf`/`szip`/Blosc compression. Use gzip or none. |
| File loads as Jülich data | It contains a root `/trajectory` dataset. Remove it. |
