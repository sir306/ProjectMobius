# Agent Sample Data Guide

## Scope

This guide documents the sample agent datasets currently shipped under the packaged Windows build tree and the HDF5 plugin test data that Project Mobius can ingest.

It covers two sample families:

- TechSchool Mobius datasets in `UnrealFolder/Build/Windows/ProjectMobius/UnitTestSampleData/TechSchoolTest/LargeTestCases/`
- Juelich trajectory HDF5 datasets in `UnrealFolder/Build/Windows/ProjectMobius/Plugins/Hdf5DataPlugin/Hdf5TestData/JuelichTestCases/trajectories_hdf5/`

The goal is to make it clear what each file contains, what format it uses, and what Project Mobius does with it at load time.

## TechSchool Sample Set

### Covered files

| File | Format | Entities | Timesteps | Samples | Duration (s) | Sampling Rate (s) | Size | Notes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `tech_school_classrooms_young_elderly_1000.json` | Mobius JSON | 1000 | 2925 | 1238681 | 292.4 | 0.1 | 132.1 MB | Mixed child and elderly population, maps `0` and `1` |
| `TechnicalSchool_1000.json` | Mobius JSON | 1000 | 2753 | 1093731 | 275.2 | 0.1 | 116.6 MB | Adult male and adult female population, maps `0` and `1` |
| `TechnicalSchool5000DefaultExits.json` | Mobius JSON | 5000 | 600 | 2884848 | 60.0 | 0.1 | 310.1 MB | High-volume scale test, maps `0` and `2` |
| `TechnicalSchool5000DefaultExits.h5` | Mobius HDF5 | 5000 | 600 | 2884848 | 60.0 | 0.1 | 54.0 MB | HDF5 equivalent of the 5000-agent JSON case, about `5.74x` smaller on disk |

### What is in these files

All three TechSchool JSON files share the same top-level layout:

- `entities`
- `metadata`
- `simulation`

The inspected metadata payloads include:

- `distance_maps_used`
- `duration`
- `isDeg`
- `isSI`
- `max_num_entities`
- `model_GUID`
- `sampling_rate`
- `timestamp_exits_links`
- `timestamp_geometry`
- `timestamp_people`
- `used_planes`

Observed entity records include:

- `id`
- `name`
- `simTimeS`
- `max_speed`
- `m_plane`
- `map`

Observed timestep and sample records include:

- `time`
- `samples`
- `entity`
- `mode`
- `position`
- `rotation`
- `speed`

### Population and layout observations

- `tech_school_classrooms_young_elderly_1000.json` contains names such as `Child[...]` and `Elderly[...]`.
- `TechnicalSchool_1000.json` contains names such as `Adult Male[...]` and `Adult Female[...]`.
- `TechnicalSchool5000DefaultExits.json` contains a mixed larger population including child, adult female, and adult male records.
- All inspected TechSchool files expose a broad `used_planes` list, but the actual entity `m_plane` values present in the data were `F#1`, `F#2`, and `F#4`.
- The observed sample `mode` values in these TechSchool files were `HF#4`.

### Mobius HDF5 structure

`TechnicalSchool5000DefaultExits.h5` uses the internal Mobius HDF5 layout consumed directly by the HDF5 plugin:

| HDF5 path | Type | Contents |
| --- | --- | --- |
| `/metadata` | Group with attributes | `duration`, `sampling_rate`, `max_num_entities`, `is_si`, `is_deg` |
| `/metadata/used_planes` | Dataset | Plane names used by the simulation |
| `/entities` | Compound dataset | `id`, `name`, `sim_time_s`, `max_speed`, `m_plane`, `map` |
| `/simulation/timesteps` | Dataset | Timestep times |
| `/simulation/samples_per_timestep` | Dataset | Number of samples stored for each timestep |
| `/simulation/samples` | Compound dataset | `timestep_idx`, `entity_id`, `position_x`, `position_y`, `position_z`, `rotation`, `speed`, `mode` |

For the inspected 5000-agent pair:

- `TechnicalSchool5000DefaultExits.json` and `TechnicalSchool5000DefaultExits.h5` preserve the same entity count, timestep count, sample count, duration, and sampling rate.
- The HDF5 version is a storage-format optimization, not a reduced-fidelity dataset.

### When to use the TechSchool files

- Use the JSON files when validating the original Mobius JSON importer or when checking schema compatibility with older workflows.
- Use `TechnicalSchool5000DefaultExits.json` when you want a heavier JSON stress case for load and parsing behavior.
- Use `TechnicalSchool5000DefaultExits.h5` when you want the same large scenario in the native HDF5 format expected by the HDF5 plugin.

## Juelich Trajectory HDF5 Sample Set

### What makes these different

The Juelich files are not stored in the native Mobius simulation layout. Project Mobius identifies them as Juelich-format HDF5 because they contain a root-level `/trajectory` dataset instead of `/metadata` plus `/simulation/samples`.

The inspected files expose:

- Root attributes such as `archive_doi`, `experiment_title`, `file_version`, `metadata`, `run_name`, `source`, `start_date`, `start_time`, and `wkt_geometry`
- A `/trajectory` compound dataset with the fields `id`, `frame`, `x`, `y`, and `z`

In this sample subset, the `/trajectory` records do not carry embedded rotation or speed fields.

### Filename convention

The filenames carry useful scenario metadata. For example:

`010_c_12_h0.h5`

Breaks down as:

- `010`: run identifier or sequence number
- `c` or `q`: priming family code found in metadata
- `12`, `23`, `34`, `45`, `56`: corridor width codes, corresponding to `1.2`, `2.3`, `3.4`, `4.5`, and `5.6`
- `h0`, `h+`, `h-`: motivation code values present in the metadata

For the inspected files:

- `c` mapped to `crowding`
- `q` mapped to `queuing`

### Covered files

| File | Participants | Corridor Width | Motivation Code | Priming | Trajectory Rows | Size |
| --- | ---: | ---: | --- | --- | ---: | ---: |
| `010_c_12_h0.h5` | 11 | 1.2 | `h0` | `crowding` | 1639 | 82200 B |
| `020_c_12_h+.h5` | 11 | 1.2 | `h+` | `crowding` | 1333 | 69960 B |
| `030_c_56_h0.h5` | 75 | 5.6 | `h0` | `crowding` | 61871 | 2491480 B |
| `040_c_56_h-.h5` | 75 | 5.6 | `h-` | `crowding` | 63110 | 2541040 B |
| `050_c_45_h0.h5` | 42 | 4.5 | `h0` | `crowding` | 21279 | 867800 B |
| `060_c_45_h-.h5` | 42 | 4.5 | `h-` | `crowding` | 22283 | 907960 B |
| `070_c_23_h0.h5` | 20 | 2.3 | `h0` | `crowding` | 4611 | 201080 B |
| `080_c_23_h-.h5` | 20 | 2.3 | `h-` | `crowding` | 5430 | 233840 B |
| `090_c_12_h0.h5` | 24 | 1.2 | `h0` | `crowding` | 7548 | 318560 B |
| `100_c_12_h-.h5` | 24 | 1.2 | `h-` | `crowding` | 7886 | 332080 B |
| `110_c_12_h0.h5` | 63 | 1.2 | `h0` | `crowding` | 36845 | 1490440 B |
| `120_c_12_h-.h5` | 63 | 1.2 | `h-` | `crowding` | 36709 | 1485000 B |
| `150_q_56_h0.h5` | 57 | 5.6 | `h0` | `queuing` | 44066 | 1779280 B |
| `160_q_56_h-.h5` | 57 | 5.6 | `h-` | `queuing` | 40601 | 1640680 B |
| `170_q_12_h0.h5` | 25 | 1.2 | `h0` | `queuing` | 8182 | 343920 B |
| `180_q_12_h-.h5` | 25 | 1.2 | `h-` | `queuing` | 8634 | 362000 B |
| `190_q_34_h0.h5` | 22 | 3.4 | `h0` | `queuing` | 6131 | 261880 B |
| `200_q_34_h-.h5` | 22 | 3.4 | `h-` | `queuing` | 7194 | 304400 B |

### How Project Mobius loads Juelich files

Project Mobius does not use the Juelich files directly as-is. The load path is:

1. Detect Juelich format by the presence of `/trajectory`
2. Read root metadata and trajectory rows
3. Convert the rows into the internal Mobius-style entity and sample arrays
4. Calculate any missing movement-derived fields needed downstream

Important conversion details:

- Unique Juelich participant IDs are remapped to sequential entity indices because downstream systems expect dense `0..N-1` entity IDs.
- The original participant ID is preserved in the generated entity name as `Entity_<originalId>`.
- `SamplingRate` is derived from `fps`. If the file does not provide an `fps` attribute, the loader defaults to `25.0 Hz`, which becomes a `0.04 s` sampling interval.
- If all `z` values are effectively constant, Project Mobius subtracts that constant Z offset so agents load onto the floor plane rather than at head height.
- When Juelich trajectory data lacks `rotation` and `speed`, Project Mobius flags them as missing during import and derives them later from movement between samples.

### Practical implications

- Juelich files are useful for validating external trajectory import and format-conversion behavior.
- They are not one-to-one schema matches with Mobius JSON or Mobius HDF5 files.
- Because the imported samples are trajectory-first, missing orientation and speed are reconstructed from motion after load.

## Choosing the Right Dataset

| Use case | Best sample family |
| --- | --- |
| Validate native Mobius JSON parsing | TechSchool JSON |
| Compare JSON versus HDF5 representations of the same large scenario | `TechnicalSchool5000DefaultExits.json` and `TechnicalSchool5000DefaultExits.h5` |
| Stress-test larger agent counts with the native HDF5 loader | `TechnicalSchool5000DefaultExits.h5` |
| Validate external HDF5 ingestion and conversion to Mobius format | Juelich trajectory HDF5 files |
| Exercise runtime derivation of rotation and speed from positions | Juelich trajectory HDF5 files |

## Implementation References

The relevant loader code paths are:

- `Plugins/Hdf5DataPlugin/Source/Hdf5DataPlugin/Public/Hdf5SimulationReader.h`
- `Plugins/Hdf5DataPlugin/Source/Hdf5DataPlugin/Private/Hdf5SimulationReader.cpp`
- `Source/ProjectMobius/Private/MassAI/SubSystems/AgentDataSubsystem.cpp`

These files contain the format detection logic, the Juelich-to-Mobius conversion path, and the fallback rotation and speed calculation used when the source data does not provide those fields.
