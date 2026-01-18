# ImPlot Overlay Integration

This document tracks the in-engine ImPlot overlay used for evacuation stats visualization.

## Third-Party Sources

The ImGui and ImPlot sources are vendored into the MobiusWidgets module so they only compile where used.

Paths:
- `Source/MobiusWidgets/ThirdParty/ImGui`
- `Source/MobiusWidgets/ThirdParty/ImPlot`

## Overlay Entry Points

- `UImPlotVisualizationSubsystem` manages overlay visibility and plot data.
- `SImPlotOverlay` renders ImPlot output as a Slate overlay.

## Data Flow

`UFloorStatsWidget` pushes the chart title, axis, data points, and live samples into the overlay subsystem.
