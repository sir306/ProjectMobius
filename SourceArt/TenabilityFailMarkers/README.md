# Tenability fail marker source art

Editable vector source for the in-world markers drawn where an agent's egress tenability fails
during B-RISK playback. These SVGs are the *authoring* form; the runtime consumes a baked texture
atlas under `UnrealFolder/ProjectMobius/Content/`. Edit the SVGs here, re-bake, never hand-edit the
texture.

## Provenance

These icons were created in-house for Project Mobius from geometric primitives only — straight
lines, circular arcs, cubic Béziers and circles. Coordinates were computed against the warning
triangle's own inset geometry.

No third-party icon pack, stock image, marketplace asset, traced reference, standards-body signage
file (ISO, OSHA or otherwise) or paid artwork was used. Each file contains path data and comments
and nothing else: no embedded raster image, no external reference, no editor metadata. Open any of
them in a text editor to confirm.

They are covered by the repository's own licence.

## Files

| File | Criterion | Atlas slot |
| --- | --- | --- |
| `ThermalFailMarker.svg` | `ETenabilityCriterion::ThermalFED` | 0 |
| `GasFailMarker.svg` | `ETenabilityCriterion::ToxicFED` | 1 |
| `VisibilityFailMarker.svg` | `ETenabilityCriterion::Visibility` | 2 |
| `UnknownFailMarker.svg` | *diagnostic — see below* | 3 |

`UnknownFailMarker` is not a hazard type. It is drawn when an agent has `bTenabilityFailed` set but
`FirstFailureCriterion` is not one of the three criteria above — that is, `Temperature` or
`LayerHeight` (both default off in `FTenabilityAnalysisSettings`, though the tenability header
advises enabling `Temperature` for scenarios where `FEDRadSum` saturates), or `None`, which
alongside a failure flag indicates a bug.

It exists so that case is visible instead of silently drawing nothing. It stands in for a log line:
the only places the condition can be detected are `SAgentEgressTenability::OnPaint` and the MASS
processors, all per-frame hot paths where this project does not log.

## The mask contract

The runtime atlas stores **coverage only** — one greyscale channel, no colour and no filled plate.
The material supplies the colour per criterion, reusing the palette `M_AgentEgressTenability`
already decodes for the tenability bar so markers and bars agree.

The `#333333` in each file is therefore an authoring colour that makes the file legible in a
browser. It is not the in-world colour, and changing it changes nothing at runtime.

One consequence worth knowing before retinting: no single fixed colour works on every background.
A light marker reads over dark B-RISK smoke and washes out on a pale floor; a dark one does the
reverse. That is why colour is driven from the criterion in the material rather than baked here.

## House rules for edits

These proportions were established deliberately. Drifting from them makes the set look amateurish,
so they are recorded rather than left to taste:

| Property | Value | Why |
| --- | --- | --- |
| Triangle | `M128 31 L238 222 L18 222 Z`, mitered, no fill | Sharp apex is the shape language |
| Line weight | 7 (≈3% of triangle width) | Heavier reads as cartoonish; much lighter disappears |
| Heavier elements | 8 — hot-surface bar, haze bars only | One weight step, no more |
| Wave / wisp swing | ±4 | Wider swings thicken into blobs when downscaled |
| Eye corners | Mitered points | Rounding them softens the whole icon |
| Gas cloud | Nine small circular arcs, open at the base | A few broad curves read as a speech bubble |
| Haze bars | Three, uneven lengths, top bar left of centre | Even bars look mechanical and merge when small |

**Motif keepout.** All motif geometry — stroke width and round caps included — must stay inside the
triangle inset 10 units perpendicular to each edge: apex `128,51`, base `y 212`, half-width
`(y − 51) × 0.5758`. Motif geometry crossing a triangle edge is the single biggest cause of an
unreadable marker at small sizes. Check any edit against that wedge.

**Legibility floor.** At weight 7 on a 128 px atlas slot the stroke is about 3.5 px, so a marker
below roughly 28 px on screen goes sub-pixel. Do not thicken the art to compensate — clamp
`UAgentEgressTenabilityWidget::MinimumScale` so markers never render that small. The floor belongs
in a tunable property, not in the drawing.

## Baking

The atlas is 256 × 256: four 128 px slots in a 2 × 2 grid, indexed as in the table above, with
mipmaps enabled. Mipmaps matter here — these are world-projected markers that shrink with distance,
and an unmipped mask shimmers as agents move. Source stays at a 256 viewBox and supersamples down,
so the atlas can be re-baked at 512 later without redrawing anything.
