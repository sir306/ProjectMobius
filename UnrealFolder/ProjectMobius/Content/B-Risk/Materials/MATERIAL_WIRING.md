# M_CustomHeterogeneousVolume wiring spec

UE 5.5 Volume-domain heterogeneous volume material driven by B-RISK two-zone data.

## Runtime data flow

```
B-RISK SMV -> *_zone.csv
  HGT_1   layer interface height, metres from room floor
  ULOD_1  upper-layer optical density, 1/m, base-10
  LLOD_1  lower-layer optical density, 1/m, base-10
  ULT_1   upper-layer temperature, C
  LLT_1   lower-layer temperature, C

UBRiskDataSubsystem::UpdateSmokeAtTime
  samples those series and builds FBRiskSmokeVisualState

FBRiskSmokeVisualState
  UpperExtinctionPerCm = max(ULOD_1, 0) * ln(10) / 100
  LowerExtinctionPerCm = max(LLOD_1, 0) * ln(10) / 100
  LayerHeightWorldCm   = (room origin Z + HGT_1) * room scale

ABRiskSmokeVisualizer::SetRoomSmokeState
  sets per-room Niagara user parameters and fallback material parameters
```

`k upper (1/m)` and `k lower (1/m)` in B-RISK `_results.xlsx` are already Napierian extinction coefficients. If XLSX import is added later, use `k / 100` directly instead of `OD * ln(10) / 100`.

## Expected material parameters

The material should use scalar parameters, not a global Material Parameter Collection:

| Parameter | Unit | Meaning |
|---|---:|---|
| `UpperExtinctionPerCm` | 1/cm | Extinction coefficient for the upper smoke layer. |
| `LowerExtinctionPerCm` | 1/cm | Extinction coefficient for the lower layer. |
| `LayerHeightWorldCm` | cm | World-space Z of the layer interface. |
| `LayerSoftnessCm` | cm | Half-width of the smooth transition band. |

Legacy/debug parameters are also still set by C++:
l
| Parameter | Unit | Meaning |
|---|---:|---|
| `UpperOpticalDensity` | 1/m | Raw `ULOD_1`, base-10 optical density. |
| `LowerOpticalDensity` | 1/m | Raw `LLOD_1`, base-10 optical density. |
| `RoomSmoke` | 0..1 | Layer height normalized by room height. |
| `SmokeDensity` | 0..1 | Non-physical activation/preview density. |
| `SmokeHeat` | 0..1 | Non-physical heat tint scalar. |

## Extinction branch

For the Volume-domain main material node, the Extinction pin should receive:

```
heightMask = smoothstep(
    LayerHeightWorldCm - LayerSoftnessCm,
    LayerHeightWorldCm + LayerSoftnessCm,
    WorldPosition.Z)

Extinction = lerp(
    LowerExtinctionPerCm,
    UpperExtinctionPerCm,
    heightMask)
```

This keeps the B-RISK two-zone model simple:

- Below the interface, use lower-layer extinction.
- Above the interface, use upper-layer extinction.
- Around the interface, blend smoothly to avoid voxel stair-stepping.

No Niagara sim-grid calculation is required for these values. B-RISK gives one upper and one lower value per room per time sample, so C++ can calculate the two extinction coefficients once per room update.

## Other material pins

- Albedo: artistic smoke scattering color/ratio, usually a grey constant or scalar-controlled grey.
- Emissive Color: optional heat glow, can use `SmokeHeat` or temperature later.
- Ambient Occlusion: leave unconnected unless ambient dimming is explicitly wanted.

## Current asset note

Older graph attempts may still contain `MPC_BRiskSmoke` nodes and an `UpperOpticalDensity * 0.0230259` branch. That path is single-room only because an MPC is global. Replace it with the scalar-parameter branch above so each generated smoke volume can receive its own extinction values.
