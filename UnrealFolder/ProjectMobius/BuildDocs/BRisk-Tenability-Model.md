# B-RISK Tenability Visualizer — Model & Assumptions

This document describes how ProjectMobius turns B-RISK fire-simulation output into a
per-agent **tenability** visualization. It is written for research defensibility: a
reviewer should be able to confirm exactly what is and is not claimed.

## It is a tenability visualizer, not a biological health model

The system reports **loss of safe-evacuation conditions (tenability)**, not biological
health or death. A visibility failure means evacuation is no longer effective — it does
**not** mean the agent died. User-facing wording uses "Tenability failure: <criterion>"
and "First exceeded criterion", never "Death cause".

A backwards-compatible `Health = 1 - clamp01(DisplayRisk)` value is still published for
older consumers, but it is **display-only and not analytical**.

## Consume B-RISK's calculated values; do not re-derive FED

B-RISK already computes FED-style dose curves (`FEDSum`, `FEDRadSum`) and `Visibility`
using its own validated equations at the configured monitor height / egress path. The
visualizer consumes these directly. It does **not** rebuild toxic or thermal FED from raw
sampled species (optical density, CO, CO₂, HCN, O₂, temperature) or arbitrary dose
budgets — doing so would not be defensible unless it fully reproduced the underlying
B-RISK / ISO / C-VM2 equations.

Source of the calculated values: `output1.xml` (a sibling of the `.smv`, not referenced by
the manifest). The raw `_zone.csv` series contain **no** FED or Visibility columns.

## Failure categories are tracked separately — display is MAX, never SUM

Each criterion (Visibility, Toxic FED, Thermal FED, Temperature, Layer Height) is tracked
independently. The single display bar uses:

```
DisplayRisk = max(VisibilityRisk, ToxicFEDRisk, ThermalFEDRisk, TemperatureRisk, LayerHeightRisk)
```

Unrelated categories are **never added together**. (Within the toxic FED category B-RISK
itself already sums gas species per ISO 13571 — that within-category sum is correct; the
rule forbids only cross-category addition.)

`ASET` for an agent is the earliest time any **enabled** criterion is exceeded. The first
failed criterion is locked and reported as the cause; a bitmask preserves all criteria
that failed in the same frame.

## Two tracks: per-zone cumulative sums vs per-agent accumulated dose

B-RISK `FEDSum` / `FEDRadSum` are **cumulative-per-zone** (whole-room, since t=0). The
system keeps two distinct, separately-published tracks:

- **Track A — per-zone cumulative sums.** The raw B-RISK room curves, retained and exposed
  per zone for zone-level tenability, heatmaps and modelling. Independent of any agent.
- **Track B — per-agent accumulated FED.** A moving agent accrues only the **delta while
  present** in its current zone. On entering or changing zone the baseline is set to that
  zone's *current* cumulative value; the zone's earlier exposure is never inherited. On
  re-entry (A→B→A) the baseline is reset again — no subtraction, no re-add. Leaving
  **every** modelled zone (e.g. crossing an unmodelled corridor) banks the delta accrued
  so far, so dose is retained across the gap and on re-entry. A zone exit seen while sim
  time steps **backward** (timeline scrub) is spurious and banks nothing — replay
  re-accrues the span from the re-entry baseline. The B-RISK curve is interpolated to the
  current time before differencing, so partial-interval presence is time-weighted
  correctly (B-RISK samples at 10 s or 30 s).

## Instantaneous vs accumulated criteria (cumulative-saturation caveat)

B-RISK clamps `FEDRadSum` at 1.0; in the validation data it saturates by ~150 s and stays
flat. A pure per-agent **delta** therefore reads 0 for an agent that enters an
already-saturated zone, which would wrongly look safe. Accumulated-dose criteria (toxic /
thermal FED) use Track B; the **instantaneous** criteria — Visibility, Temperature, Layer
Height — are read from Track A's current zone values at the monitor height. The
instantaneous **Temperature** criterion is the correct signal for "this zone is thermally
untenable now"; enable it for scenarios where `FEDRadSum` saturates.

## Monitor height

B-RISK calculated values correspond to the configured **monitor height / egress path**,
not each agent's mesh height. The temperature criterion selects the upper or lower layer
by comparing the monitor height to the smoke-layer interface. Per-agent height re-sampling
is intentionally **not** used to reinterpret B-RISK monitor-height FED values; if added
later it must be a separate, documented analysis mode.

## Endpoints (defaults loaded from `input1.xml`)

| Setting | Source tag | Default | Note |
|---|---|---|---|
| Monitor height | `monitor_height` | 2.0 m | |
| Visibility endpoint | `endpoint_visibility` | 10 m | failure when visibility ≤ endpoint |
| Toxic FED endpoint | `endpoint_FED` | 0.3 | conservative regulatory ASET endpoint |
| Thermal FED endpoint | `endpoint_radiation` | 1.0 (B-RISK input often 0.3) | |
| Temperature endpoint | — | 60 °C, criterion **off** by default | |

`endpoint_temp` from B-RISK (observed ≈1146) is **not** a layer temperature in Celsius and
is deliberately **not** mapped to the Celsius temperature criterion. Missing endpoints fall
back to the documented defaults and log a warning.

The toxic FED endpoint of 0.3 is the conservative **regulatory ASET** endpoint and differs
from the ~1.0 **physiological incapacitation** threshold used by agent-behaviour models
(e.g. FDS+Evac / pyFDS-Evac). This visualizer reports ASET, so 0.3 is intended; if agent
collapse behaviour is ever modelled, use the physiological threshold (≈1.0, ideally
probabilistic) instead.

## Limitations

- B-RISK provides only a **per-zone scalar visibility (m)**; there is no per-position /
  line-of-sight visibility as in FDS-based tools.
- Multi-zone agent movement (Track B re-entry) is currently covered by deterministic unit
  tests with synthetic samples; the bundled single-room `basemodel_testBox` validates the
  golden FED/Visibility curve but cannot exercise room changes. A multi-room B-RISK
  `output1.xml` is needed to validate moving-agent re-entry against real data.
- Timeline rewind restores the compatibility `Health` curve; per-category cumulative state
  is monotonic and is not decremented on scrub-back.

## Validation data (golden curve — `basemodel_testBox/output1.xml`)

- Visibility 20 m @30 s → 1.917 m @60 s.
- `FEDRadSum` reaches 1.0 by 150 s (then saturates).
- `FEDSum` crosses the 0.3 endpoint between 510 s (0.285) and 540 s (0.306).

These are asserted by `ProjectMobius.BRisk.Tenability.ParserGoldenCurve`. The model rules
(room entry baseline, delta accumulation, simultaneous failure, MAX-not-SUM) are asserted
by `ProjectMobius.BRisk.Tenability.Model`; re-entry dose retention and the exit-banking
scrub guard by `ProjectMobius.BRisk.Tenability.ReentryDoseRetention`.
