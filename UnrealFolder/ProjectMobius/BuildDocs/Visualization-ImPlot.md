# ImPlot Overlay Integration

This document tracks the in-engine ImPlot overlay used for side-by-side comparison with the Qt stats app.

## Third-Party Sources

The ImGui and ImPlot sources are vendored into the Visualization module so they only compile where used.

Paths:
- `Source/Visualization/ThirdParty/ImGui`
- `Source/Visualization/ThirdParty/ImPlot`

## Overlay Entry Points

- `UImPlotVisualizationSubsystem` manages overlay visibility and plot data.
- `SImPlotOverlay` renders ImPlot output as a Slate overlay.

## Data Flow

`UFloorStatsWidget` mirrors the Qt data and pushes the same title, axis, data points, and live samples into the overlay subsystem.
