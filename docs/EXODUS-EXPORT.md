# Exporting from buildingEXODUS

How to bring occupant movement from **buildingEXODUS** (Fire Safety Engineering Group,
University of Greenwich) into Project Mobius. This page is about the evacuation model,
not the Sandia *Exodus II* finite-element mesh format.

> **Status.** The Mobius side of this workflow (the CSV converter and the validator) is
> tested. The buildingEXODUS side is not: we have not yet had an EXODUS export to work
> from, so this page does not name EXODUS menus, file names or column layouts. **Check
> step 1 against your EXODUS version**, and if you can share a small sample export,
> please open an issue so this page can document the exact route.

## The short version

1. Get **one row per occupant per output time** out of EXODUS: occupant id, time,
   x, y and floor.
2. Convert it with the bundled script, which resamples onto an even time step, turns
   floors into heights and computes heading and speed:

   ```bash
   python mobius_h5_writer.py from-csv exodus_positions.csv run.h5 --dt 0.5 --interp linear --floor-z "0=0,1=3.5"
   ```

   Use `--interp linear` if the export is a snapshot of every occupant at a fixed
   interval, `--interp hold` if it writes a row only when an occupant moves
   ([why](#3-exodus-specific-points)).

3. Check it, then open it in Mobius:

   ```bash
   python validate_mobius_h5.py run.h5
   ```

Both scripts are in [`docs/examples/pedestrian-hdf5/`](examples/pedestrian-hdf5/);
install with `pip install -r requirements.txt`. No Unreal Engine or C compiler is needed.

## 1. What to get out of EXODUS

Mobius needs only occupant positions over time. Per occupant, per output time:

| Column | Meaning |
| --- | --- |
| `id` | Occupant identifier, any number or text, stable for the whole run |
| `time` | Simulation time in seconds |
| `x`, `y` | Plan position in metres, in the same coordinate system as the building geometry |
| `floor` | Floor/level the occupant is on (needed for multi-storey buildings) |

Occupants who have left the building should simply have no further rows. Heading and
speed are **not** needed: the converter computes both from the movement.

EXODUS can export runs to its vrEXODUS replay tool and can save scenario data to
spreadsheet-style output files. Use whichever route your licence and version provide to
get the table above, then save it as CSV. If your export is
**wide** (one column per occupant, or one block per occupant), reshape it to the long
form below first. A spreadsheet or a few lines of Python will do it.

```text
id,time,x,y,floor
1,0.0,12.25,4.75,0
1,1.0,12.75,4.75,0
1,2.0,13.25,5.25,0
2,0.0,3.75,18.25,1
2,1.0,3.75,17.75,1
...
```

## 2. Convert

```bash
python mobius_h5_writer.py from-csv exodus_positions.csv run.h5 \
  --dt 0.5 --interp linear \
  --floor-z "0=0,1=3.5,2=7" \
  --col-id id --col-time time --col-x x --col-y y --col-floor floor
```

| Option | Use |
| --- | --- |
| `--dt` | Output interval in seconds. Use your export interval, or smaller to get smoother playback from irregular output. Always pass it explicitly. |
| `--interp` | `linear` for fixed-interval snapshots, `hold` for logs that write a row only when an occupant moves. |
| `--floor-z` | Floor label → floor height in metres. Get the heights from your EXODUS geometry. |
| `--floor-height` | Alternative to `--floor-z` when every storey has the same height and floors are numbered 0, 1, 2… |
| `--col-…` | Map your column names if they differ from `id/time/x/y/floor`. |
| `--scale` | Multiply coordinates to get metres (e.g. `0.001` if exported in mm). |

The converter renumbers occupants 0 … N−1 (the EXODUS id is kept in the name, so
`Ctrl` + hover in Mobius shows `Agent 17` for occupant 17), resamples every occupant
onto the `--dt` grid between their first and last row, makes the earliest time in the
file time 0, computes heading and speed from the movement, and writes the Mobius HDF5
file. It stops with a message
instead of writing a file Mobius would misread (duplicate times for an occupant,
non-numeric values, a floor with no height).

## 3. EXODUS-specific points

- **Node-based movement.** EXODUS moves occupants across a grid of nodes, so exported
  positions usually sit on node centres and change in node-sized steps. Heading is
  computed from successive positions, so headings follow the grid directions (straight
  and diagonal moves). That is the data, not a fault. With `--interp linear`, a larger
  `--dt` averages the direction over several node steps; a smaller one keeps every move.
- **Event logs vs snapshots.** If your export records a position only when an occupant
  moves, use `--interp hold`: the occupant stays where it was last recorded until its
  next row, so time spent queueing at a door stays visible in density maps and flow
  counts. With `linear`, a 10 s wait followed by one step would turn into a slow
  10 s creep. Choose `--dt` at or below the typical time between moves. Expect brief
  speed spikes in `hold` mode, since each node step happens within one `--dt`.
- **Stairs.** If your export only labels positions with a floor, an occupant's height
  jumps from one floor to the next when the label changes on the stairs. If it gives
  stairs their own label, map that label to a sensible height (for example the landing)
  with `--floor-z`; if it gives a height along the stair, supply it as a `z` column,
  which is added to the floor height.
- **Coordinates.** Export the building geometry for Mobius from the **same CAD/DXF**
  that the EXODUS geometry was built from, with the same origin and in metres, so the
  occupants line up with walls and doors. Mobius does not shift or scale trajectories.
- **What does not transfer.** Occupant attributes (age, gender, response time, travel
  speeds) and EXODUS fire-hazard results (FED, toxicity) are not imported. Mobius takes
  fire and tenability data from B-RISK separately. To keep an attribute visible in
  Mobius, build the agent names yourself (for example `"17 Adult Male"`) and pass them
  as `entity_names` to `write_mobius_h5()` from Python.

## 4. Check and open

```bash
python validate_mobius_h5.py run.h5
ProjectMobius.exe -MobiusPedestrian="C:/runs/run.h5" -MobiusGeometry="C:/runs/building.fbx"
```

Check the validator's `info` lines against what you know of the run: occupant count,
time span (about the evacuation time), and the x/y/z extents of the building.

## See also

- [Pedestrian Data Format](PEDESTRIAN-DATA-FORMAT.md): what every field means
- [HDF5 Export Guide](HDF5-EXPORT-GUIDE.md): the converter, the Python writer and the C route in full
