# Project Mobius Docs

This folder keeps the longer documentation that was removed from the root
README and is also structured so it can be reused for a GitHub wiki later.

Packaged builds mirror a selected subset of these pages into UnrealFolder/ProjectMobius/BuildDocs/Docs/ via the superbuild so the Unreal staging path stays stable without introducing a second authored docs tree.

## Core Pages

- [Build and Run](BUILD-AND-RUN.md)
- [Launch with Preloaded Files](BUILD-AND-RUN.md#launch-with-preloaded-files)
- [Controls](CONTROLS.md)
- [Architecture](ARCHITECTURE.md)
- [Data Pipeline](DATA-PIPELINE.md)
- [Agent Sample Data](AGENT-SAMPLE-DATA.md)

## Exporting Pedestrian Data From Other Models

- [Pedestrian Data Format](PEDESTRIAN-DATA-FORMAT.md): field-by-field definition of the files Mobius loads
- [HDF5 Export Guide](HDF5-EXPORT-GUIDE.md): write those files from Python or with the bundled HDF5 C library
- [Exporting from buildingEXODUS](EXODUS-EXPORT.md)

## Research

- [Research and Publications](RESEARCH.md)

## Generated Reports

- [Activity Report](ACTIVITY.md)
- [Tech Debt Report](TECH-DEBT.md)

## Unreal-Specific References

- [BuildDocs folder](../UnrealFolder/ProjectMobius/BuildDocs/)
- [UE Automation Guide](../UnrealFolder/ProjectMobius/BuildDocs/UE-Automation-Guide.md)
- [ImPlot Integration Notes](../UnrealFolder/ProjectMobius/BuildDocs/Visualization-ImPlot.md)
- [Project Key Bindings Reference](../UnrealFolder/ProjectMobius/BuildDocs/Project%20Mobius%20Key%20Bindings.md)
