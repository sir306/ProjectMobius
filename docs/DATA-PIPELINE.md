# Data Pipeline

Project Mobius supports loading both trajectory data and environment geometry.
This page focuses on the trajectory formats and the JSON-to-HDF5 conversion
workflow.

## Trajectory Inputs

- `.json` simulation files loaded by `UAgentDataSubsystem`
- `.h5` simulation files loaded through `FHdf5SimulationReader`
- HDF5 support includes both Mobius-format and Juelich-format datasets

## Geometry Inputs

The project also supports loading geometry and environment data from common
formats such as:

- `fbx`
- `obj`
- `udatasmith`
- `ifc`
- `wkt`

## JSON to HDF5 Conversion

The bundled converter script lives at:

`UnrealFolder/ProjectMobius/Plugins/Hdf5DataPlugin/Scripts/json_to_hdf5_converter.py`

Example usage:

```bash
cd UnrealFolder/ProjectMobius/Plugins/Hdf5DataPlugin/Scripts
python json_to_hdf5_converter.py path/to/input.json path/to/output.h5
```

If you omit the output path, the script writes a `.h5` file next to the input
JSON using the same base name.

### Python requirements

- `numpy`
- `h5py`

## Why Convert to HDF5?

- HDF5 avoids the cost of parsing a large text document during load
- It uses much less memory during import for larger datasets
- It matches the project's dedicated HDF5 reader path directly

For one 5,000-agent sample converted with the bundled script, the HDF5 version
reduced load time from `21.29 s` to `3.18 s` and memory use from `317,555 KB`
to `55,283 KB`.

<p align="center">
  <img src="images/SpedUpJsonHDF5Load5000Agents.gif" alt="Project Mobius loading a 5,000-agent sample converted from JSON to HDF5" width="900">
</p>

| Format | Load time | Memory use |
|--------|-----------|------------|
| JSON | `21.29 s` | `317,555 KB` |
| HDF5 | `3.18 s` | `55,283 KB` |

That benchmark works out to:

- `6.69x` faster loading
- `262,272 KB` less memory used
- About `82.6%` lower memory consumption

## Related References

- [Build and Run](BUILD-AND-RUN.md)
- [Architecture](ARCHITECTURE.md)
- [UE Automation Guide](../UnrealFolder/ProjectMobius/BuildDocs/UE-Automation-Guide.md)
