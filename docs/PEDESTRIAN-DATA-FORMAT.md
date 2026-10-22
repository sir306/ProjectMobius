# Pedestrian Data Format

This page defines the pedestrian (agent trajectory) data Project Mobius loads, field
by field, so that any evacuation or crowd model can export results Mobius will
replay correctly.

- **Writing a file from your own simulator?** Read this page for *what* to write, then
  [HDF5 Export Guide](HDF5-EXPORT-GUIDE.md) for *how* (Python or the HDF5 C library
  bundled in this repository).
- **Using buildingEXODUS?** See [Exporting from buildingEXODUS](EXODUS-EXPORT.md).
- **Checking a file?** Run
  [`validate_mobius_h5.py`](examples/pedestrian-hdf5/validate_mobius_h5.py) on it. It
  applies every rule on this page.

> **Why this page is strict.** The Mobius HDF5 reader decodes most fields by
> **position and fixed width**, without converting types. A position written as
> 64-bit float, or a name written as a variable-length string, does not produce an
> error. It loads garbage. Follow the types in the tables below exactly.

## Contents

- [Which format to use](#which-format-to-use)
- [The data model in one minute](#the-data-model-in-one-minute)
- [Coordinates, units and time](#coordinates-units-and-time)
- [Mobius HDF5 layout](#mobius-hdf5-layout)
- [Rules the importer relies on](#rules-the-importer-relies-on)
- [Heading (rotation) convention](#heading-rotation-convention)
- [Floors, stairs and geometry alignment](#floors-stairs-and-geometry-alignment)
- [What Mobius does on load](#what-mobius-does-on-load)
- [Mobius JSON layout](#mobius-json-layout)
- [Checklist](#checklist)
- [Source references](#source-references)

## Which format to use

| Format | Use it when | Notes |
| --- | --- | --- |
| **Mobius HDF5** (`.h5`) | **Always, for new exports.** | Fast to load and small. Heading and speed are optional (Mobius can derive them). |
| Mobius JSON (`.json`) | Small hand-made test files. | Human-readable, but slow and memory-heavy for big runs. Heading and speed **must** be supplied. Convert to HDF5 with [`json_to_hdf5_converter.py`](../UnrealFolder/ProjectMobius/Plugins/MobiusDataImporter/Scripts/json_to_hdf5_converter.py). |
| Jülich trajectory HDF5 (`.h5` with `/trajectory`) | Only for datasets from the Jülich pedestrian-experiment archive. | Read-only compatibility path, described in [Agent Sample Data](AGENT-SAMPLE-DATA.md). **Do not use it for new exports**: it handles axes differently and ignores any heading or speed you add. |

The file extension must be **`.h5`** (any case). Mobius does not open `.hdf5` or `.hdf`
as pedestrian data.

## The data model in one minute

- An **entity** is one person (agent) in the run. Entities are numbered
  **0, 1, 2, … N-1** with no gaps.
- A **timestep** is one output instant. Timesteps are numbered 0, 1, 2, … and are
  **evenly spaced**: timestep *k* happens at *k* × `sampling_rate` seconds.
- A **sample** is one entity at one timestep: where it is, which way it faces, how fast it
  moves.
- An entity that is not in the building at a timestep (not yet arrived, already
  evacuated) simply has **no sample** at that timestep. Never write a placeholder row.

## Coordinates, units and time

| Quantity | Unit | Convention |
| --- | --- | --- |
| Position `x`, `y` | metres | Plan view, **right-handed**: +x to the right (east), +y up the page (north). The same frame as a CAD floor plan. |
| Position `z` | metres | Up. Height of the floor the person is standing on (0 on the ground floor), **not** head height. |
| Heading `rotation` | degrees | Direction of travel, counter-clockwise from +x. See [Heading convention](#heading-rotation-convention). |
| Speed `speed` | m/s | Magnitude, never negative. |
| Time | seconds | `time = timestep_idx × sampling_rate`. The run starts at time 0. |

Mobius converts metres to Unreal centimetres and mirrors the y axis into Unreal's
left-handed frame itself. Do **not** pre-convert either. Seen from above in Mobius, agents
keep the layout of your plan; they are not mirrored. This was checked in the app on
2026-10-22.

## Mobius HDF5 layout

```text
run.h5
├── /metadata                       group, 5 scalar attributes
├── /entities                       table, one row per entity
└── /simulation
    ├── samples                     table, one row per entity per timestep present
    ├── timesteps                   float32[num_timesteps]   (recommended)
    └── samples_per_timestep        int32[num_timesteps]     (recommended)
```

A file is recognised as Mobius format when it has a `/metadata` group **and** a
`/simulation/samples` dataset, and **no** root `/trajectory` dataset (a `/trajectory`
makes Mobius treat the whole file as Jülich format).

### `/metadata` attributes

Scalar attributes on the `/metadata` group. Numeric attributes are converted on read, so
any integer or float type works.

| Attribute | Type | Meaning | If missing |
| --- | --- | --- | --- |
| `sampling_rate` | float | **Seconds between timesteps**, e.g. `0.1` for 10 Hz output. Despite the name it is an interval, not a frequency. | 0.1 |
| `duration` | float | Time of the **last** timestep in seconds: `(num_timesteps − 1) × sampling_rate`. Used as the playback length. | taken from the samples |
| `max_num_entities` | integer | **Total** number of entities in the run (N), which is also the row count of `/entities`. Not the peak number present at one time. `0` means "use the `/entities` row count". | `/entities` row count |
| `is_si` | integer 0/1 | `1` = positions in metres. Always write `1`. | 1 |
| `is_deg` | integer 0/1 | `1` = `rotation` in degrees, `0` = radians. | 1 |

Write `is_si` and `is_deg` as 8-bit unsigned integers (`uint8`). Python `bool` also
works with h5py, but plain integers are unambiguous for every writer.

### `/entities` table

A 1-D compound dataset, **row *i* describes entity *i***. Mobius reads the six members
**by position with these exact types**: keep the order and the widths.

| # | Member | HDF5 type | Meaning |
| --- | --- | --- | --- |
| 0 | `id` | 32-bit little-endian signed int (`H5T_STD_I32LE`, numpy `<i4`) | Equal to the row index (0, 1, 2, …). |
| 1 | `name` | **fixed-length** string, NUL-terminated or NUL-padded | Label shown in the agent inspector, e.g. `"Adult Male 17"`. |
| 2 | `sim_time_s` | 32-bit float (`H5T_IEEE_F32LE`, `<f4`) | Seconds the entity spent in the run (last seen − first seen). |
| 3 | `max_speed` | 32-bit float | Unimpeded walking speed in m/s, informational. |
| 4 | `m_plane` | **fixed-length** string | Free text, conventionally the floor the entity starts on (`"Floor 0"`). |
| 5 | `map` | 32-bit signed int | Free integer for your own use (e.g. a group or profile id). `0` if unused. |

All six are informational: they appear in the agent inspector (`Ctrl` + hover) and do
not change playback. They still must be present with these types.

### `/simulation/samples` table

A 1-D compound dataset with one row per entity per timestep the entity is present.

| # | Member | HDF5 type | Required | Meaning |
| --- | --- | --- | --- | --- |
| 0 | `timestep_idx` | 32-bit signed int | yes | Timestep number *k* ≥ 0. |
| 1 | `entity_id` | 32-bit signed int | yes | Entity number 0 … N−1. |
| 2 | `position_x` | 32-bit float | yes | metres |
| 3 | `position_y` | 32-bit float | yes | metres |
| 4 | `position_z` | 32-bit float | yes | metres (floor height) |
| – | `rotation` | 32-bit float | optional | Heading, degrees CCW from +x. |
| – | `speed` | 32-bit float | optional | m/s, ≥ 0. |
| – | `mode` | fixed-length string | optional | Free text such as `"walk"`. Stored, currently not displayed. |

- Members 0–4 are read **by position**: they must come first, in this order, with these
  exact widths.
- `rotation`, `speed` and `mode` are found **by name**, may appear in any order after
  member 4, and may each be left out.
- **If your model has no heading or speed, compute them from the positions** (heading
  towards the next position, speed = distance to it over the time between): the Python
  writer's `heading_and_speed_from_positions()` and the CSV converter do this for you.
  Leaving both members out also works: Mobius then derives them on load with the same
  rule. Builds from before October 2026 derive them much more slowly (about 12 s for
  2,000 agents × 1,500 timesteps), so compute them up front if your file may be opened
  in an older release. **Never write zeros**: every agent would face the same way and
  report a speed of 0.
- Sort rows by `timestep_idx`, then `entity_id`. Mobius does not require it, but it keeps
  the file readable and compresses better.

### `/simulation/timesteps` and `/simulation/samples_per_timestep`

`timesteps` holds `k × sampling_rate` for every timestep and `samples_per_timestep`
holds the number of samples at each timestep. The current importer does not need them,
but it logs their sizes, the bundled converter writes them, and they let other tools
read the file by timestep. Write both.

### Strings

Every string member inside `/entities` and `/simulation/samples` must be a
**fixed-length byte string** (`H5T_C_S1` with a set size, numpy `S<n>`), at least one
byte longer than the longest value so a terminating NUL always fits. Use ASCII or UTF-8.
A variable-length string (what h5py writes for a Python `str`) is decoded as pointer
bytes and shows garbage or crashes.

### Compression

Optional. Mobius can read **gzip (deflate)**, **shuffle** and **fletcher32** filters on
chunked datasets. It cannot read `lzf`, `szip`, Blosc or any other plugin filter: those
files fail to load.

### Optional root attribute: `wkt_geometry`

A string attribute on the root group holding a Well-Known-Text `POLYGON` or
`GEOMETRYCOLLECTION` of the floor outline in the same metres. When present, Mobius
offers to load it as geometry.

> **WKT geometry is mirrored against Mobius-format agents.** Mobius mirrors the agents'
> y axis into Unreal's frame (see [Coordinates](#coordinates-units-and-time)) but loads
> WKT, both this attribute and a separate `.wkt` file, **without** that mirror. A WKT
> outline therefore appears flipped across the x axis relative to the agents. This was
> verified in the app on 2026-10-22. Until it is fixed, **negate every y value** in WKT
> you pair with Mobius-format or JSON data (`POLYGON((0 0, 10 0, 10 4, …))` becomes
> `POLYGON((0 0, 10 0, 10 -4, …))`); that was verified to line the two up. Jülich-format
> files are unaffected, because Mobius mirrors neither their agents nor their WKT.

## Rules the importer relies on

Breaking these does not always stop the load, which is why they are listed here.

1. **Dense entity numbers.** `entity_id` runs 0 … N−1 with no gaps, N =
   `max_num_entities` = rows in `/entities`. Remap your simulator's own IDs (which may
   start at 1 or have gaps) and keep the original in `name`. Samples with an id ≥ N are
   dropped with an *"Unexpected entity IDs"* warning; ids that never appear raise
   *"Incomplete entity data"*.
2. **Even time spacing.** Sample *k* is placed at *k* × `sampling_rate`; the values in
   `/simulation/timesteps` are not used for placement. If your model writes output at
   irregular times (event-driven models often do), resample to a fixed interval first.
   [`mobius_h5_writer.py from-csv`](HDF5-EXPORT-GUIDE.md#convert-a-csv-export) does
   this.
3. **One sample per entity per timestep.**
4. **No NaN or infinite values, no negative speeds.** Each bad value raises its own error
   dialog and is replaced by zero. To show that an agent is absent, write no row.
5. **`duration` = time of the last timestep** = `(num_timesteps − 1) × sampling_rate`.
   A larger value triggers a false *"Data may be truncated"* warning.
6. **Little-endian 32-bit numbers in the tables.** Every x86-64 and ARM (Apple Silicon)
   machine writes little-endian by default; just do not request 64-bit or big-endian
   types for table members.
7. **Whole file in memory.** Mobius reads the entire samples table at load and keeps the
   processed trajectories in RAM, so memory grows with agents × timesteps. 0.1 s output
   is typical; for very long or very large runs a coarser `sampling_rate` reduces memory
   in proportion.

## Heading (rotation) convention

`rotation` is the direction the person faces, measured **counter-clockwise from +x in
your plan frame**, in degrees (or radians with `is_deg = 0`). For a person walking with
plan velocity (dx, dy):

```text
rotation = degrees(atan2(dy, dx))
```

| Walking towards | rotation |
| --- | --- |
| +x (east, right on the plan) | 0 |
| +y (north, up the plan) | 90 |
| −x (west) | 180 (or −180) |
| −y (south) | 270 (or −90) |

Any range works (0–360 or −180–180). Two checks confirm this convention:

- **Bundled Simulex sample.** In `iso-test-1.h5` the stored rotation matches
  `atan2(dy, dx)` of the movement for 98.6 % of moving samples.
- **In the app, 2026-10-22.** A demo file written with this rule renders the same as the
  same file with `rotation` left out for Mobius to derive: agent silhouettes overlap at
  IoU ≥ 0.98, with no measurable offset. Agents walking towards the camera show their
  faces; agents walking away show their backs.

## Floors, stairs and geometry alignment

- `position_z` is the height of the floor surface in metres. On stairs, use the height of
  the stair surface under the person (interpolating between landings is fine).
- Multi-storey output from models that report a floor number instead of a height: convert
  floor number → floor height before writing (`--floor-z` in the CSV converter).
- Export the building geometry from the **same CAD model, origin and units** as the
  simulation, so (0, 0, 0) is the same point in both. Mobius does not offset or rescale
  trajectories to fit the geometry.
- WKT floor outlines need their y values negated to line up with Mobius-format agents
  (see [`wkt_geometry`](#optional-root-attribute-wkt_geometry)).

## What Mobius does on load

1. Detects the format (`/trajectory` → Jülich; `/metadata` + `/simulation/samples` →
   Mobius).
2. Reads metadata, the entity table and every sample.
3. Converts positions to centimetres (`× 100`) and mirrors y into Unreal's frame.
4. Converts `rotation` to an Unreal yaw.
5. If the samples table has **no** `rotation` member, derives heading from the direction
   to the agent's next position (holding the last heading while stationary). If it has no
   `speed` member, derives speed from the distance to the next position divided by
   `sampling_rate`. This takes one pass over the samples (well under a second for
   millions of samples); builds from before October 2026 used a much slower loop.
6. Checks entity coverage and timestep count, and reports problems in an error dialog.

## Mobius JSON layout

Accepted for small files. The same rules apply (dense ids, even spacing, metres,
heading convention), with these differences:

- `rotation` and `speed` are **required** in every sample. JSON cannot ask Mobius to
  derive them; missing values load as 0.
- Timesteps are numbered by their **position** in the `simulation` array. The `time`
  value is not used, so the array must be evenly spaced and contain an entry (possibly
  with an empty `samples` array) for every timestep.
- Metadata accepts `isSI`/`is_si` and `isDeg`/`is_deg`.

```json
{
  "metadata": {
    "duration": 0.1,
    "sampling_rate": 0.1,
    "max_num_entities": 2,
    "isSI": true,
    "isDeg": true
  },
  "entities": [
    { "id": 0, "name": "Agent 0", "simTimeS": "0.1", "max_speed": 1.2, "m_plane": "Floor 0", "map": 0 },
    { "id": 1, "name": "Agent 1", "simTimeS": "0.0", "max_speed": 1.0, "m_plane": "Floor 0", "map": 0 }
  ],
  "simulation": [
    { "time": 0.0, "samples": [
      { "entity": 0, "position": { "x": 0.00, "y": 1.0, "z": 0.0 }, "rotation": 0.0, "speed": 1.2, "mode": "walk" },
      { "entity": 1, "position": { "x": 5.00, "y": 2.0, "z": 0.0 }, "rotation": 180.0, "speed": 1.0, "mode": "walk" }
    ]},
    { "time": 0.1, "samples": [
      { "entity": 0, "position": { "x": 0.12, "y": 1.0, "z": 0.0 }, "rotation": 0.0, "speed": 1.2, "mode": "walk" }
    ]}
  ]
}
```

Convert a JSON file to HDF5 with
[`json_to_hdf5_converter.py`](../UnrealFolder/ProjectMobius/Plugins/MobiusDataImporter/Scripts/json_to_hdf5_converter.py):

```bash
python json_to_hdf5_converter.py run.json run.h5
```

## Checklist

- [ ] File ends in `.h5`, has `/metadata`, `/entities`, `/simulation/samples`, and no `/trajectory`.
- [ ] `sampling_rate` is the interval in seconds (0.1 for 10 Hz), and output is evenly spaced.
- [ ] `duration` = `(num_timesteps − 1) × sampling_rate`.
- [ ] `max_num_entities` = total agents = rows in `/entities`; `entity_id` runs 0 … N−1.
- [ ] `/entities` members in order `id, name, sim_time_s, max_speed, m_plane, map` as `int32, string, float32, float32, string, int32`.
- [ ] `/simulation/samples` starts `timestep_idx, entity_id, position_x, position_y, position_z` as `int32, int32, float32 ×3`.
- [ ] Every string member is fixed-length with room for a NUL.
- [ ] Positions in metres, right-handed plan frame, z = floor height.
- [ ] `rotation` / `speed` are real values (from the model or computed from positions), never zeros. Leaving them out is fine for current Mobius builds.
- [ ] No NaN, no negative speed, no placeholder rows for absent agents.
- [ ] Compression, if any, is gzip.
- [ ] [`validate_mobius_h5.py`](examples/pedestrian-hdf5/validate_mobius_h5.py) reports PASS.

## Source references

The authority for every rule above is the importer code:

- `UnrealFolder/ProjectMobius/Plugins/MobiusDataImporter/Source/MobiusDataImporter/Private/Hdf5SimulationReader.cpp`: format detection and field decoding
- `UnrealFolder/ProjectMobius/Plugins/MobiusDataImporter/Source/MobiusDataImporter/Private/MobiusAgentDataImporter.cpp`: file-type dispatch
- `UnrealFolder/ProjectMobius/Source/ProjectMobius/Private/MassAI/SubSystems/AgentDataSubsystem.cpp`: units, axes, heading, derivation and the load-time checks
- `UnrealFolder/ProjectMobius/Source/ProjectMobiusTests/Private/Tests/Hdf5ImportMatrixTest.cpp`: automated import test that writes Mobius and Jülich files with the HDF5 C API
