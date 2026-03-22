# Architecture

Project Mobius is split across Unreal runtime modules that handle data loading,
simulation playback, UI, analytics, and supporting infrastructure.

## Runtime Modules

| Module | Purpose |
|--------|---------|
| `MobiusLogging` | Logging and shared utility plumbing |
| `MobiusCore` | Core subsystems such as loading, heatmaps, IPC, time control, and file dialogs |
| `HeatmapVisualization` | Heatmap rendering support |
| `Visualization` | Visualization helpers and related processing support |
| `ProjectMobius` | Main playback, controller logic, MASS processors, and simulation-facing widgets |
| `MobiusWidgets` | UMG, Slate, in-world widgets, and ImPlot overlay support |
| `MobiusEditor` | Editor-only module that auto-generates Datasmith override materials on startup |
| `HIT_ThesisWork` | Thesis-era research functionality; much of its original work has since been migrated into other runtime modules |

## High-Level Flow

1. Trajectory data is loaded through `UAgentDataSubsystem` from JSON or HDF5.
2. Spawn and playback systems feed MASS-based pedestrian entities over time.
3. Representation systems update rendered pedestrians and related visuals.
4. Heatmap and statistics subsystems collect spatial and flow data.
5. UI widgets and ImPlot overlays expose playback controls, metrics, and charts.

## Key Subsystems

| Subsystem | Module | Purpose |
|-----------|--------|---------|
| `UIpcSubsystem` | `MobiusCore` | Inter-process communication |
| `UHeatmapSubsystem` | `MobiusCore` | Heatmap state, updates, and floor-related analytics |
| `UStatisticSubsystem` | `MobiusCore` | Flow and selection statistics |
| `ULoadingSubsystem` | `MobiusCore` | Loading orchestration |
| `UTimeDilationSubSystem` | `MobiusCore` | Playback speed and stepping control |
| `UNativeFileDialogSubsystem` | `MobiusCore` | Agent and mesh file dialogs |
| `UImPlotVisualizationSubsystem` | `MobiusWidgets` | Overlay visibility and rendering state |
| `UImPlotDataSubsystem` | `MobiusWidgets` | Chart data management and forwarding |

## User-Facing UI Pieces

| UI element | Role |
|------------|------|
| `USimulationPlayBar` | Play, pause, scrub, and step through imported timesteps |
| `UTimeDilationWidget` | Playback time display |
| `UFloorStatsWidget` | Floor counts and ImPlot chart generation |
| `UPedestrianDataDisplay` | Selected pedestrian metrics |
| `UAgentInfoDisplay` | In-world agent information |
| `SImPlotOverlay` | Slate-hosted plotting overlay |

## Plugin Dependencies

| Plugin | Purpose |
|--------|---------|
| `Hdf5DataPlugin` | HDF5 trajectory file reading |
| `UE4_Assimp` | Geometry and mesh import support |
| `OpenCV` | Epic's built-in Unreal engine plugin that provides the OpenCV runtime used by visualization-related code |

## Related References

- [Data Pipeline](DATA-PIPELINE.md)
- [ImPlot Integration Notes](../UnrealFolder/ProjectMobius/BuildDocs/Visualization-ImPlot.md)
- [UE Automation Guide](../UnrealFolder/ProjectMobius/BuildDocs/UE-Automation-Guide.md)
