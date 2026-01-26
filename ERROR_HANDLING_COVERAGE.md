# Error Handling and Logging Coverage Plan for Mobius Modules

## Quick Summary

**Coverage Status:**
- **Total files analyzed:** 127 (63 MobiusCore + 64 ProjectMobius)
- **Files requiring implementation:** 113
- **Current coverage:** ~10 files (8%)
- **Breakdown:**
  - **Part 1 - Critical Error Handling:** 27 files (Priority: highest user impact)
  - **Part 2 - Important Error Handling:** 35 files (Priority: runtime operations)
  - **Part 3 - Diagnostic Logging:** 51 files (Priority: debugging/monitoring)

**Target Systems:**
- `UMobiusUserFeedbackSubsystem`: Error reporting with UI popups, throttling, severity levels
- `UMobiusCustomLoggerSubsystem`: Thread-safe file logging for diagnostics

---

# PART 1: CRITICAL ERROR HANDLING (27 Files)

Files with operations that can fail catastrophically and require immediate user notification via popup.

## 1.1 FILE I/O & DATA LOADING (11 Files)

| File | Priority | Current Status | Key Operations | Required Error Popups |
|------|----------|-----------------|-----------------|----------------------|
| **MobiusCore/Private/AsyncAssimpMeshLoader.cpp** | CRITICAL | Partial | File validation, WKT/FBX/OBJ parsing, triangulation | ✓ "Mesh file not found", "Triangulation failed", "WKT parse failed", + "Unsupported file format", "File corrupted" |
| **ProjectMobius/Private/MassAI/SubSystems/AgentDataSubsystem.cpp** | CRITICAL | Partial | JSON loading/parsing, data validation, background threads | ✓ "File not found", + "JSON parsing failed", "Validation failed", "Memory exceeded" |
| **MobiusCore/Private/Subsystems/NativeFileDialogSubsystem.cpp** | CRITICAL | Partial (Mac only) | File dialogs, permission checks | ✓ "Dialog creation failed" (Mac), + Windows/Linux coverage needed |
| **MobiusCore/Private/BuildingGenerator/RuntimeMeshBuilder.cpp** | HIGH | Minimal | Procedural mesh, Datasmith, materials, collisions | "Mesh generation failed", "Material not found", "Collision creation failed" |
| **ProjectMobius/Private/RuntimeMeshGeneration/RuntimeHeatmapBuilder.cpp** | HIGH | Minimal | Heatmap mesh, texture coords, vertex colors | "Heatmap generation failed", "Texture coord error" |
| **MobiusCore/Private/Materials/MaterialCache.cpp** | MEDIUM | Minimal | Material loading, instance creation | "Material not found", "Instance creation failed" |
| **MobiusCore/Private/Util/FrameGrabberHelper.cpp** | MEDIUM | ✓ EXCELLENT | Screenshot capture, file I/O | ✓ Complete - Reference implementation for other files |
| **MobiusWidgets/Private/UI/LoadSave/LoadAgentDataWidget.cpp** | HIGH | Unknown | File browser, validation, preview loading | "Failed to open dialog", "Invalid agent data file" |
| **MobiusWidgets/Private/UI/LoadSave/LoadMeshWidget.cpp** | HIGH | Unknown | File browser, mesh validation, preview | "Failed to open dialog", "Invalid mesh file" |
| **MobiusWidgets/Private/UI/LoadSave/LoadDataParentWidget.cpp** | MEDIUM | Unknown | File validation, progress tracking | "Load cancelled", "Validation failed" |
| **HeatmapVisualization/Private/HeatmapGenerator/HeatmapTextureGenerator.cpp** | MEDIUM | Unknown | Texture generation, pixel buffers, export | "Texture generation failed", "Export failed" |

**Logging Needs (All):**
- Operation start/completion timestamps
- Performance metrics (duration in ms)
- File/resource metadata (counts, sizes)
- Validation statistics

---

## 1.2 IPC & NETWORK OPERATIONS (4 Files)

| File | Priority | Current Status | Key Operations | Required Error Popups |
|------|----------|-----------------|-----------------|----------------------|
| **MobiusCore/Private/Components/MobiusIpcClient.cpp** | CRITICAL | Partial | Socket/pipe connection, message framing, thread management | ✓ "Socket creation failed", "Connect failed", + "Connection lost", "Invalid message format", "Thread failed" |
| **MobiusCore/Private/Subsystems/IpcSubsystem.cpp** | CRITICAL | Partial | Thread startup, config loading, Qt app launch | ✓ "Thread failed", + "Config missing", "Qt launch failed", "Disconnected" |
| **MobiusCore/Private/Subsystems/WebSocketSubsystem.cpp** | HIGH | Unknown (Deprecated UE 5.5) | Connection, SSL/TLS, message parsing | "Connection failed", "Timeout", "SSL validation failed" |
| **MobiusCore/Private/Interfaces/ProjectMobiusInterface.cpp** | MEDIUM | Unknown | External process launching, communication | "Process launch failed", "Process crashed", "Timeout" |

**Logging Needs (All):**
- Connection attempts (success/failure)
- Message throughput statistics
- Reconnection events
- Thread lifecycle events

---

## 1.3 SUBSYSTEM INITIALIZATION (7 Files)

| File | Priority | Current Status | Key Operations | Required Error Popups |
|------|----------|-----------------|-----------------|----------------------|
| **MobiusCore/Private/GameInstances/ProjectMobiusGameInstance.cpp** | CRITICAL | Good logging only | Module init, subsystem registration, config | "Subsystem init failed", "Config corrupted", "Dependency unavailable" |
| **MobiusCore/Private/Subsystems/HeatmapSubsystem.cpp** | HIGH | Unknown | Heatmap structures, QuadTree, memory allocation | "Init failed - insufficient memory", "QuadTree creation failed" |
| **MobiusCore/Private/Subsystems/LoadingSubsystem.cpp** | HIGH | Unknown | Loading screens, asset streaming, progress | "Asset load failed", "Timeout exceeded" |
| **ProjectMobius/Private/MassAI/SubSystems/MassEntitySpawnSubsystem.cpp** | CRITICAL | Partial | Entity spawning, memory allocation, fragments | "Entity limit reached", "Insufficient memory", "Invalid spawn data" |
| **ProjectMobius/Private/MassAI/SubSystems/MassRepresentation/NiagaraActorRepSubsystem.cpp** | HIGH | Unknown | Niagara systems, actor pools, data binding | "Niagara creation failed", "Actor pool exhausted", "Binding failed" |
| **ProjectMobius/Private/MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.cpp** | HIGH | Unknown | Representation switching, LOD, culling | "Switch failed", "LOD init failed", "Actor creation failed" |
| **MobiusWidgets/Private/Core/MobiusWidgetSubsystem.cpp** | HIGH | Unknown | Widget creation, UI init, event binding | "UI init failed", "Widget creation error", "Subsystem unavailable" |

**Logging Needs (All):**
- Initialization sequence steps
- Performance timing for init phases
- Subsystem dependencies
- Configuration values

---

## 1.4 RESOURCE LOADING & ASSET MANAGEMENT (5 Files)

| File | Priority | Key Operations | Required Error Popups |
|------|----------|-----------------|----------------------|
| **MobiusCore/Private/Actors/HeatmapPixelTextureVisualizer.cpp** | MEDIUM | Texture creation, pixel buffers, materials | "Texture creation failed", "Material creation failed" |
| **Visualization/Private/DynamicPixelRenderingTexture.cpp** | MEDIUM | GPU updates, pixel streaming, format conversion | "GPU allocation failed", "Streaming error", "Format unsupported" |
| **Visualization/Private/HeatmapVisualizer.cpp** | MEDIUM | Heatmap rendering, gradients, mesh updates | "Render failed", "Gradient calculation error" |
| **ProjectMobius/Private/Tools/AnimationScriptingTool.cpp** | LOW | Animation loading, scripting, blending | "Asset not found", "Script execution failed", "Blending error" |
| **MobiusCore/Private/Interfaces/AssimpInterface.cpp** | MEDIUM | Assimp init, format validation, scene processing | "Assimp init failed", "Unsupported format", "Scene error" |

**Logging Needs (All):**
- Resource allocation sizes
- Performance metrics
- Format/configuration details

---

# PART 2: IMPORTANT ERROR HANDLING (35 Files)

Runtime operations that should report errors but may use warnings or log-only approach (no popups per-frame).

## 2.1 MASS AI PROCESSORS (8 Files)

| File | Priority | Key Operations | Logging Focus |
|------|----------|-----------------|----------------|
| PedestrianMovementProcessor.cpp | HIGH | Movement validation, path following, velocity calc | Entity counts, validation failures (throttled) |
| PedestrianCollisionProcessor.cpp | HIGH | Collision detection, spatial queries, response | Collision counts, query performance |
| NiagaraAgentRepProcessor.cpp | HIGH | Data interface updates, particle validation | Particle counts, buffer usage, update perf |
| AgentHeatmapProcessor.cpp | MEDIUM | Heatmap accumulation, grid validation | Data density, accumulation performance |
| DisplayAgentUIStatsProcessor.cpp | LOW | UI data extraction, statistics calc | UI update events, selected agent changes |
| FlowCounterProcessor.cpp | MEDIUM | Flow counting, boundary detection, overflow | Flow counts, overflow events |
| TimeDilationProcessor.cpp | MEDIUM | Time step validation, simulation time | Time scale changes, simulation progression |
| PedestrianInitializeMOP.cpp | HIGH | Entity init, fragment assignment, validation | Init counts, fragment assignments, performance |

**Pattern:** Use throttled warnings (not every frame), periodic performance logs

---

## 2.2 SUBSYSTEMS & CONTROLLERS (10 Files)

| File | Priority | Key Operations | Logging Focus |
|------|----------|-----------------|----------------|
| MobiusControllerSubsystem.cpp | MEDIUM | State management, input validation, commands | Command execution, state changes |
| StatisticSubsystem.cpp | MEDIUM | Statistics calc, data aggregation, export | Collection events, export operations |
| StatisticActorManagementSubsystem.cpp | MEDIUM | Actor spawn/despawn, pool management | Actor lifecycle, pool stats |
| PerformanceUtilSubsystem.cpp | LOW | Metric collection, FPS tracking, profiling | Performance metrics (existing: enhance) |
| TimeDilationSubSystem.cpp | MEDIUM | Time scale validation, pause/resume, sync | Time scale changes, control events |
| PedestrianSignalSubsystem.cpp | MEDIUM | Signal processing, event dispatch, queues | Signal events, queue sizes |
| QuadTreeSubsystem.cpp | MEDIUM | Construction, spatial queries, rebalancing | Tree depth, query performance |
| QuadTreeDataMap.cpp | LOW | Data mapping, subdivision, validation | Subdivision events, data density |
| SplineGraphLocationBucket.cpp | LOW | Spatial bucketing, location queries | Bucket stats, query counts |
| ImPlotDataSubsystem.cpp | MEDIUM | Plot data collection, buffer management | Series updates, buffer usage |

---

## 2.3 UI WIDGETS & COMPONENTS (17 Files)

All UI files benefit from logging interactions and settings persistence error handling.

**High Priority:**
- LoadAgentDataWidget.cpp, LoadMeshWidget.cpp (file operations)
- MaterialPicker.cpp, BaseChangePedestrianMaterial.cpp (material operations)
- ResolutionScalabilityWidget.cpp, ScalabilitySettingWidget.cpp (settings)
- MobiusSettingPanel.cpp (settings persistence)

**Medium Priority:** All Scalability components

**Low Priority:** Display/info widgets, MoveableWidget, LoadingNotifyWidget, etc.

---

# PART 3: DIAGNOSTIC LOGGING ONLY (51 Files)

These files benefit from structured logging for diagnostics, performance, and debugging. No error popups needed.

## 3.1 PERFORMANCE MONITORING & ANALYTICS (12 Files)

- FlowCounter.cpp (has extensive logging - consider enhancement)
- FlowCounterSpawnerComponent.cpp
- HeatmapGenerator.cpp, QuadTree.cpp
- Signal processors (2 files)
- ImPlotVisualizationSubsystem.cpp, LogWindow.cpp
- GraphVisualization.cpp, VoronoiGenerator.cpp
- ExperimentScorer.cpp, VRScreenCaptureComponent.cpp

**Logging Metrics:**
- Operation counts
- Performance timings
- Resource usage
- Progress tracking

---

## 3.2 MASS AI ENTITY MANAGEMENT (15 Files)

Actors, fragments, tags, and observer processors related to agent spawning, representation, and lifecycle.

**Key Entities:**
- AgentRepresentationActorISM.cpp
- NiagaraAgentRepActor.cpp
- PedestrianCollisionHolder.cpp
- SimulationTimeStepFragment.cpp
- AgentNiagaraDataFrag.cpp
- DestroyEntities_MOP.cpp
- AgentRepresentation_MOP.cpp
- MassAITags.cpp
- Controller, widgets, UI components

**Logging Focus:**
- Lifecycle events (create/destroy)
- State changes
- Configuration parameters
- Data updates

---

## 3.3 UTILITY & HELPER CLASSES (24 Files)

- TextHelperInterface.cpp, UnitConversionAndScaling.cpp
- TextOperationHelpers.cpp, WidgetUtilHelpers.cpp
- All Slate widget components (SSimulationPanel, SAgentFollowIndicator, SFloorPlanVisualizer, etc.)
- Dynamic texture test actors
- Remaining widget implementations

**Logging Focus:**
- Operation execution
- Performance metrics
- Lifecycle events
- Data transformations

---

# IMPLEMENTATION PATTERNS

## Pattern 1: File I/O Error Handling

```cpp
if (!FPaths::FileExists(Path))
{
    if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
    {
        Feedback->ReportError(
            FText::FromString("File Error"),
            FText::FromString("File Not Found"),
            FText::FromString(FString::Printf(TEXT("The file '%s' does not exist."), *Path)),
            FText::FromString("ClassName::MethodName"),
            EMobiusErrorSeverity::Error,
            true  // Show popup
        );
    }
    return false;
}
```

## Pattern 2: Performance Logging

```cpp
const double StartTime = FPlatformTime::Seconds();
// ... operation ...
const double DurationMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
if (UMobiusCustomLoggerSubsystem* Logger = UMobiusCustomLoggerSubsystem::Get(this))
{
    Logger->EnqueueTimedMessage(TEXT("OperationName"), DurationMs);
}
```

## Pattern 3: Thread-Safe Error Reporting (Reference: FrameGrabberHelper.cpp)

```cpp
namespace
{
    void MobiusReportError(const FText& Title, const FText& Message,
                          const FText& Location, EMobiusErrorSeverity Severity = EMobiusErrorSeverity::Error,
                          bool bShowPrompt = true)
    {
        if (IsInGameThread())
        {
            if (UMobiusUserFeedbackSubsystem* Feedback = GEngine ?
                UMobiusUserFeedbackSubsystem::Get(GEngine->GetWorld()) : nullptr)
            {
                Feedback->ReportError(FText::FromString(TEXT("Module Error")),
                    Title, Message, Location, Severity, bShowPrompt);
            }
        }
        else
        {
            AsyncTask(ENamedThreads::GameThread, [Title, Message, Location, Severity, bShowPrompt]()
            {
                if (UMobiusUserFeedbackSubsystem* Feedback = GEngine ?
                    UMobiusUserFeedbackSubsystem::Get(GEngine->GetWorld()) : nullptr)
                {
                    Feedback->ReportError(FText::FromString(TEXT("Module Error")),
                        Title, Message, Location, Severity, bShowPrompt);
                }
            });
        }
    }
}
```

## Pattern 4: Subsystem Initialization with Logging

```cpp
void UMySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const double StartTime = FPlatformTime::Seconds();

    if (UMobiusCustomLoggerSubsystem* Logger = UMobiusCustomLoggerSubsystem::Get(this))
    {
        Logger->EnqueueLogMessage(TEXT("MySubsystem::Initialize begin"));
    }

    // Initialization code...

    if (bInitializationFailed)
    {
        if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
        {
            Feedback->ReportError(FText::FromString("Init Error"),
                FText::FromString("Subsystem Failed"),
                FText::FromString("Critical initialization failure."),
                FText::FromString("MySubsystem::Initialize"),
                EMobiusErrorSeverity::Error, true);
        }
    }

    if (UMobiusCustomLoggerSubsystem* Logger = UMobiusCustomLoggerSubsystem::Get(this))
    {
        Logger->EnqueueTimedMessage(TEXT("MySubsystem::Initialize"),
            (FPlatformTime::Seconds() - StartTime) * 1000.0);
    }
}
```

---

# REFERENCE IMPLEMENTATION

**FrameGrabberHelper.cpp** is the gold standard for error handling and logging:
- Lines 22-78: Thread-safe helper functions `MobiusLog()` and `MobiusReportError()`
- Lines 148-156: Error reporting with context
- Comprehensive performance logging with timing

**Copy this pattern to other modules for consistency.**

---

# SEVERITY LEVELS

| Level | Usage | bShowPrompt |
|-------|-------|------------|
| **Info** | Successful operations, state changes | false |
| **Warning** | Non-critical errors, recoverable failures, validation warnings | false (usually) |
| **Error** | Operation failures, I/O errors, resource failures, validation errors | true (usually) |
| **Fatal** | Critical system failures, initialization failures, stability risk | true (always) |

---

# COVERAGE SUMMARY TABLE

| Category | Files | Current | Target | Gap |
|----------|-------|---------|--------|-----|
| Critical Error Handling | 27 | 6 (22%) | 27 (100%) | 21 files |
| Important Error Handling | 35 | 4 (11%) | 35 (100%) | 31 files |
| Diagnostic Logging | 51 | 5 (10%) | 51 (100%) | 46 files |
| **TOTAL** | **113** | **15 (13%)** | **113 (100%)** | **98 files** |

---

# RECOMMENDED IMPLEMENTATION PHASES

**Phase 1 (Critical File I/O):** 15 files - 2-3 weeks
- AsyncAssimpMeshLoader, AgentDataSubsystem, NativeFileDialogSubsystem
- MobiusIpcClient, IpcSubsystem, RuntimeMeshBuilder, MaterialCache
- All LoadSave widgets, FrameGrabberHelper (enhance), etc.

**Phase 2 (Subsystem Init):** 12 files - 2 weeks
- GameInstance, HeatmapSubsystem, LoadingSubsystem, MassEntitySpawnSubsystem
- NiagaraActorRepSubsystem, MRS_RepresentationSubsystem, MobiusWidgetSubsystem, etc.

**Phase 3 (Mass Processors):** 8 files - 1-2 weeks
- Movement, Collision, Representation, Heatmap, Flow, Time dilation processors, etc.

**Phase 4 (UI Widgets):** 35 files - 2-3 weeks
- All LoadSave, Scalability, Material, and Settings widgets

**Phase 5 (Diagnostics):** 51 files - 3-4 weeks
- Analytics, performance monitoring, entity management, utilities

---

# QUICK START CHECKLIST

- [ ] Review this document
- [ ] Start with Phase 1 (file I/O - highest user impact)
- [ ] Copy FrameGrabberHelper patterns to other files
- [ ] Test error handling as you implement
- [ ] Monitor `MobiusCustomLog.txt` for logging verification
- [ ] Gather user feedback on error messages
- [ ] Document any additional patterns discovered

---

**Document Version:** 1.0
**Generated:** 2026-01-26
**Total Files Analyzed:** 127 (MobiusCore: 63, ProjectMobius: 64)
**Estimated Implementation Time:** 10-14 weeks (all phases)
**Quick Reference:** Start with Part 1 for highest ROI
