#!/usr/bin/env python3
"""
Write pedestrian trajectories in the Project Mobius HDF5 layout.

Use it three ways:

1. As a library from your own simulator or post-processing script:

       from mobius_h5_writer import write_mobius_h5, heading_and_speed_from_positions
       rot, spd = heading_and_speed_from_positions(timestep_idx, entity_id, x, y, z, 0.1)
       write_mobius_h5("run.h5", timestep_idx, entity_id, x, y, z,
                       sampling_interval_s=0.1, rotation_deg=rot, speed_mps=spd)

2. To convert a "long" CSV (one row per agent per output time) exported by any
   evacuation model, resampling it onto a uniform time grid:

       python mobius_h5_writer.py from-csv positions.csv run.h5 --dt 0.1

   Required columns: id, time, x, y. Optional: z, floor (floor needs --floor-z
   or --floor-height). Rename yours with --col-id/--col-time/--col-x/--col-y/
   --col-z/--col-floor. Heading and speed are computed from the positions.

3. To produce a small demo file that you can open in Mobius straight away:

       python mobius_h5_writer.py demo demo.h5

Layout reference: docs/PEDESTRIAN-DATA-FORMAT.md
Check any file you write with validate_mobius_h5.py before sending it on.

Requires: numpy, h5py  (pip install -r requirements.txt)

MIT License - Copyright (c) 2026 ProjectMobius contributors
"""

import argparse
import csv
import math
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence

import h5py
import numpy as np


def _fixed_strings(values: Sequence[str]) -> np.ndarray:
    """Encode strings as fixed-length, NUL-terminated byte strings.

    Mobius reads compound string members as C strings, so the field must be at
    least one byte longer than the longest value. Variable-length strings (what
    h5py writes for a plain Python str) are NOT readable inside a compound.
    """
    encoded = [str(v).encode("utf-8") for v in values]
    width = max((len(v) for v in encoded), default=0) + 1
    return np.array(encoded, dtype=f"S{width}")


def heading_and_speed_from_positions(timestep_idx, entity_id, x, y, z, sampling_interval_s: float):
    """Per-sample heading (degrees, CCW from +x) and speed (m/s) from positions.

    Uses the same rule Mobius applies when a file has no rotation/speed members:
    heading points at the agent's next position and is held while the agent stands
    still (every component of the step <= 1 mm); speed is the 3-D distance to the
    next position over the time between them; an agent's last sample repeats its
    previous values. Computing it here keeps files quick to load in Mobius
    releases from before October 2026, whose own derivation was slow for
    thousands of agents.

    Returns (rotation_deg, speed_mps), aligned with the input order.
    """
    t_idx = np.asarray(timestep_idx, dtype=np.int64)
    ids = np.asarray(entity_id, dtype=np.int64)
    pos = np.column_stack([np.asarray(v, dtype=np.float64) for v in (x, y, z)])
    rotation = np.zeros(t_idx.size)
    speed = np.zeros(t_idx.size)

    order = np.lexsort((t_idx, ids))  # group by agent, then time
    bounds = np.flatnonzero(np.diff(ids[order])) + 1
    for group in np.split(order, bounds):
        if group.size < 2:
            continue  # a single sample has no movement to measure
        step = np.diff(pos[group], axis=0)
        elapsed = np.diff(t_idx[group]) * sampling_interval_s
        if (elapsed <= 0).any():
            raise ValueError(f"entity {ids[group[0]]} has more than one sample in the same timestep")
        moving = (np.abs(step) > 0.001).any(axis=1)

        heading = np.full(group.size, np.nan)
        heading[:-1][moving] = np.degrees(np.arctan2(step[moving, 1], step[moving, 0]))
        valid = np.flatnonzero(~np.isnan(heading))
        if valid.size:
            # Hold the last heading while standing still (and on the last sample);
            # before the first move, face the way the agent will first walk.
            filled = np.maximum.accumulate(np.where(~np.isnan(heading), np.arange(group.size), -1))
            heading = np.where(filled >= 0, heading[np.maximum(filled, 0)], heading[valid[0]])
        else:
            heading[:] = 0.0

        spd = np.empty(group.size)
        spd[:-1] = np.linalg.norm(step, axis=1) / elapsed
        spd[-1] = spd[-2]
        rotation[group] = heading
        speed[group] = spd
    return rotation, speed


def write_mobius_h5(
    path,
    timestep_idx,
    entity_id,
    x,
    y,
    z,
    sampling_interval_s: float,
    rotation_deg=None,
    speed_mps=None,
    mode=None,
    entity_names: Optional[Sequence[str]] = None,
    entity_planes: Optional[Sequence[str]] = None,
    entity_max_speed=None,
    entity_map=None,
    compress: bool = True,
) -> Dict[str, int]:
    """Write one Mobius-format HDF5 file.

    All per-sample arguments are 1-D sequences of equal length, one entry per
    agent per timestep the agent is present.

    timestep_idx      int, >= 0. The sample's time is timestep_idx * sampling_interval_s.
    entity_id         int, dense 0..N-1 (remap your own IDs first).
    x, y, z           metres, right-handed, z up (z = height of the floor stood on).
    rotation_deg      optional heading in degrees, counter-clockwise from +x,
                      i.e. degrees(atan2(dy, dx)).
    speed_mps         optional speed in m/s (>= 0).
                      If your model has neither, compute both with
                      heading_and_speed_from_positions(). Leaving them out also
                      works (Mobius derives them; slowly for big runs in releases
                      from before October 2026).
    mode              optional per-sample text (e.g. "walk"); informational only.
    entity_*          optional per-entity values, indexed by entity_id.
    """
    if not sampling_interval_s > 0:
        raise ValueError("sampling_interval_s must be > 0 (seconds between samples)")

    t_idx = np.asarray(timestep_idx, dtype=np.int64)
    ids = np.asarray(entity_id, dtype=np.int64)
    pos = [np.asarray(v, dtype=np.float64) for v in (x, y, z)]
    n = t_idx.size
    if any(a.size != n for a in [ids, *pos]):
        raise ValueError("timestep_idx, entity_id, x, y and z must all have the same length")
    if n == 0:
        raise ValueError("no samples to write")
    if t_idx.min() < 0:
        raise ValueError("timestep_idx must be >= 0")
    if ids.min() < 0:
        raise ValueError("entity_id must be >= 0")
    if t_idx.max() > np.iinfo(np.int32).max or ids.max() > np.iinfo(np.int32).max:
        raise ValueError("timestep_idx and entity_id must fit in int32")
    if not all(np.isfinite(a).all() for a in pos):
        raise ValueError("positions contain NaN or Inf; write no row for absent agents instead")

    num_entities = int(ids.max()) + 1
    present = np.zeros(num_entities, dtype=bool)
    present[ids] = True
    if not present.all():
        missing = np.flatnonzero(~present)
        raise ValueError(
            f"entity_id must be dense 0..N-1; {missing.size} id(s) never appear, first: {missing[:5].tolist()}"
        )

    key = t_idx * num_entities + ids
    if np.unique(key).size != n:
        raise ValueError("an entity has more than one sample in the same timestep")

    order = np.lexsort((ids, t_idx))  # sort by timestep, then entity

    fields = [
        ("timestep_idx", "<i4"),
        ("entity_id", "<i4"),
        ("position_x", "<f4"),
        ("position_y", "<f4"),
        ("position_z", "<f4"),
    ]
    if rotation_deg is not None:
        rotation = np.asarray(rotation_deg, dtype=np.float64)
        if rotation.size != n or not np.isfinite(rotation).all():
            raise ValueError("rotation_deg must be finite and the same length as the samples")
        fields.append(("rotation", "<f4"))
    if speed_mps is not None:
        speed = np.asarray(speed_mps, dtype=np.float64)
        if speed.size != n or not np.isfinite(speed).all() or (speed < 0).any():
            raise ValueError("speed_mps must be finite, >= 0 and the same length as the samples")
        fields.append(("speed", "<f4"))
    if mode is not None:
        mode_bytes = _fixed_strings(mode)
        if mode_bytes.size != n:
            raise ValueError("mode must have the same length as the samples")
        fields.append(("mode", mode_bytes.dtype.str))

    samples = np.empty(n, dtype=np.dtype(fields))
    samples["timestep_idx"] = t_idx[order]
    samples["entity_id"] = ids[order]
    samples["position_x"] = pos[0][order]
    samples["position_y"] = pos[1][order]
    samples["position_z"] = pos[2][order]
    if rotation_deg is not None:
        samples["rotation"] = rotation[order]
    if speed_mps is not None:
        samples["speed"] = speed[order]
    if mode is not None:
        samples["mode"] = mode_bytes[order]

    num_timesteps = int(t_idx.max()) + 1
    first = np.full(num_entities, np.iinfo(np.int64).max)
    last = np.full(num_entities, -1)
    np.minimum.at(first, ids, t_idx)
    np.maximum.at(last, ids, t_idx)

    names = entity_names if entity_names is not None else [f"Agent {i}" for i in range(num_entities)]
    planes = entity_planes if entity_planes is not None else ["Floor 0"] * num_entities
    if entity_max_speed is not None:
        max_speed = np.asarray(entity_max_speed, dtype=np.float64)
    elif speed_mps is not None:
        max_speed = np.zeros(num_entities)
        np.maximum.at(max_speed, ids, speed)
    else:
        max_speed = np.zeros(num_entities)
    maps = np.asarray(entity_map if entity_map is not None else np.zeros(num_entities), dtype=np.int64)
    if len(names) != num_entities or len(planes) != num_entities or max_speed.size != num_entities or maps.size != num_entities:
        raise ValueError(f"entity_* arguments must have one entry per entity ({num_entities})")

    name_bytes = _fixed_strings(names)
    plane_bytes = _fixed_strings(planes)
    entities = np.empty(
        num_entities,
        dtype=np.dtype([
            ("id", "<i4"),
            ("name", name_bytes.dtype.str),
            ("sim_time_s", "<f4"),
            ("max_speed", "<f4"),
            ("m_plane", plane_bytes.dtype.str),
            ("map", "<i4"),
        ]),
    )
    entities["id"] = np.arange(num_entities)  # row i describes entity_id i
    entities["name"] = name_bytes
    entities["sim_time_s"] = (last - first) * sampling_interval_s
    entities["max_speed"] = max_speed
    entities["m_plane"] = plane_bytes
    entities["map"] = maps

    with h5py.File(path, "w") as hf:
        meta = hf.create_group("metadata")
        # Despite the name, sampling_rate is the INTERVAL between samples in seconds.
        meta.attrs["sampling_rate"] = np.float64(sampling_interval_s)
        # Time of the last timestep. (num_timesteps * dt can round up one step too
        # many in float32 and raise a false "data may be truncated" warning.)
        meta.attrs["duration"] = np.float64((num_timesteps - 1) * sampling_interval_s)
        meta.attrs["max_num_entities"] = np.int64(num_entities)
        meta.attrs["is_si"] = np.uint8(1)   # positions in metres
        meta.attrs["is_deg"] = np.uint8(1)  # rotation in degrees

        hf.create_dataset("entities", data=entities)

        sim = hf.create_group("simulation")
        if compress and n > 4096:
            # gzip (+ shuffle) is the only compression Mobius can read. Never use lzf/szip/blosc.
            sim.create_dataset("samples", data=samples, chunks=(4096,), compression="gzip",
                               compression_opts=4, shuffle=True)
        else:
            sim.create_dataset("samples", data=samples)
        sim.create_dataset("timesteps",
                           data=(np.arange(num_timesteps) * sampling_interval_s).astype("<f4"))
        sim.create_dataset("samples_per_timestep",
                           data=np.bincount(t_idx, minlength=num_timesteps).astype("<i4"))

    return {"entities": num_entities, "timesteps": num_timesteps, "samples": n}


# ---------------------------------------------------------------------------
# CSV conversion
# ---------------------------------------------------------------------------

def _parse_floor_z(spec: Optional[str]) -> Dict[str, float]:
    """'0=0,1=3.5,2=7' -> {'0': 0.0, '1': 3.5, '2': 7.0}"""
    mapping: Dict[str, float] = {}
    if spec:
        for item in spec.split(","):
            label, _, value = item.partition("=")
            if not _:
                raise ValueError(f"--floor-z entry '{item}' is not of the form label=metres")
            mapping[label.strip()] = float(value)
    return mapping


def convert_csv(args) -> int:
    with open(args.input, newline="", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f, delimiter=args.delimiter)
        header = reader.fieldnames or []
        for required in (args.col_id, args.col_time, args.col_x, args.col_y):
            if required not in header:
                print(f"Error: column '{required}' not found. Columns are: {header}", file=sys.stderr)
                return 1
        has_z = args.col_z in header
        has_floor = args.col_floor in header
        floor_z = _parse_floor_z(args.floor_z)
        if has_floor and not floor_z and args.floor_height is None:
            print(f"Error: the CSV has a '{args.col_floor}' column but no floor heights were given. "
                  f"Pass --floor-z (e.g. \"0=0,1=3.5\") or --floor-height, or --col-floor none "
                  f"to ignore the column.", file=sys.stderr)
            return 1

        per_agent: Dict[str, List[tuple]] = {}
        start_floor: Dict[str, tuple] = {}  # id -> (earliest time, floor label)
        for line_no, row in enumerate(reader, start=2):
            try:
                t = float(row[args.col_time])
                px = float(row[args.col_x]) * args.scale
                py = float(row[args.col_y]) * args.scale
                pz = float(row[args.col_z]) * args.scale if has_z else 0.0
            except (TypeError, ValueError):
                print(f"Error: line {line_no}: non-numeric time/x/y/z: {row}", file=sys.stderr)
                return 1
            if has_floor:
                label = row[args.col_floor].strip()
                if label in floor_z:
                    pz += floor_z[label]
                elif args.floor_height is not None:
                    try:
                        pz += float(label) * args.floor_height
                    except ValueError:
                        print(f"Error: line {line_no}: floor '{label}' is not a number, so --floor-height "
                              f"cannot place it; list it in --floor-z", file=sys.stderr)
                        return 1
                else:
                    print(f"Error: line {line_no}: floor '{label}' has no entry in --floor-z", file=sys.stderr)
                    return 1
                agent = row[args.col_id].strip()
                start_floor[agent] = min(start_floor.get(agent, (t, label)), (t, label))
            per_agent.setdefault(row[args.col_id].strip(), []).append((t, px, py, pz))

    if not per_agent:
        print("Error: CSV has no data rows", file=sys.stderr)
        return 1

    all_times = np.array(sorted({r[0] for rows in per_agent.values() for r in rows}))
    t0 = float(all_times[0])
    dt = args.dt
    if dt is None:
        steps = np.diff(all_times)
        steps = steps[steps > 1e-9]
        if steps.size == 0:
            print("Error: only one distinct time in the CSV; pass --dt", file=sys.stderr)
            return 1
        dt = float(np.median(steps))
        print(f"No --dt given; using the median output interval {dt:g} s")

    # Dense ids in a stable order: numeric ids numerically, otherwise as text.
    def sort_key(raw: str):
        try:
            return (0, float(raw), raw)
        except ValueError:
            return (1, 0.0, raw)
    original_ids = sorted(per_agent, key=sort_key)

    out_t, out_id, out_x, out_y, out_z = [], [], [], [], []
    for dense_id, raw_id in enumerate(original_ids):
        rows = np.array(sorted(per_agent[raw_id]))
        times, xs, ys, zs = rows[:, 0], rows[:, 1], rows[:, 2], rows[:, 3]
        if np.unique(times).size != times.size:
            print(f"Error: agent '{raw_id}' has two rows with the same time", file=sys.stderr)
            return 1
        k_first = math.ceil((times[0] - t0) / dt - 1e-6)
        k_last = math.floor((times[-1] - t0) / dt + 1e-6)
        if k_last < k_first:
            continue  # agent present for less than one interval
        grid = t0 + np.arange(k_first, k_last + 1) * dt
        out_t.append(np.arange(k_first, k_last + 1))
        out_id.append(np.full(grid.size, dense_id))
        if args.interp == "hold":
            # Event logs: a row marks where the agent IS from that time on, so hold it
            # there until the next row. Keeps queueing and waiting visible.
            src = np.clip(np.searchsorted(times, grid + 1e-9, side="right") - 1, 0, times.size - 1)
            out_x.append(xs[src])
            out_y.append(ys[src])
            out_z.append(zs[src])
        else:
            # Periodic snapshots: linear interpolation, exact when already on the grid.
            out_x.append(np.interp(grid, times, xs))
            out_y.append(np.interp(grid, times, ys))
            out_z.append(np.interp(grid, times, zs))

    kept = [original_ids[int(ids[0])] for ids in out_id]
    if len(kept) != len(original_ids):
        # Re-number so ids stay dense after dropping agents shorter than one interval.
        remap = {old: new for new, old in enumerate(int(ids[0]) for ids in out_id)}
        out_id = [np.full(ids.size, remap[int(ids[0])]) for ids in out_id]
        print(f"Dropped {len(original_ids) - len(kept)} agent(s) present for less than one interval")

    t_all, id_all = np.concatenate(out_t), np.concatenate(out_id)
    x_all, y_all, z_all = np.concatenate(out_x), np.concatenate(out_y), np.concatenate(out_z)
    rotation = speed = None
    if not args.positions_only:
        rotation, speed = heading_and_speed_from_positions(t_all, id_all, x_all, y_all, z_all, dt)

    stats = write_mobius_h5(
        args.output, t_all, id_all, x_all, y_all, z_all,
        sampling_interval_s=dt,
        rotation_deg=rotation,
        speed_mps=speed,
        entity_names=[f"{args.name_prefix}{raw}" for raw in kept],
        # Informational: the floor each agent starts on, shown in the agent inspector.
        entity_planes=[f"Floor {start_floor[raw][1]}" for raw in kept] if start_floor else None,
    )
    print(f"Wrote {args.output}: {stats['entities']} agents, {stats['timesteps']} timesteps "
          f"({(stats['timesteps'] - 1) * dt:g} s at {dt:g} s), {stats['samples']} samples. "
          f"Time 0 in Mobius = {t0:g} s in the CSV.")
    return 0


def write_demo(args) -> int:
    """Three people crossing a 10 m corridor, sampled every 0.1 s for 10 s."""
    dt = 0.1
    agents = [(0.0, 0.0, 1.0, 1.2), (0.0, 0.0, 2.0, 1.0), (2.0, 10.0, 3.0, -1.3)]  # start t, x0, lane y, vx
    t_idx, ids, xs, ys, rot, spd = [], [], [], [], [], []
    for step in range(101):
        t = step * dt
        for e, (t_start, x0, lane, vx) in enumerate(agents):
            x = x0 + vx * (t - t_start)
            if t < t_start or not 0.0 <= x <= 10.0:
                continue  # absent: no row
            t_idx.append(step); ids.append(e); xs.append(x); ys.append(lane)
            rot.append(math.degrees(math.atan2(0.0, vx)))  # 0 = +x, 180 = -x
            spd.append(abs(vx))
    stats = write_mobius_h5(args.output, t_idx, ids, xs, ys, np.zeros(len(xs)), dt,
                            rotation_deg=None if args.positions_only else rot,
                            speed_mps=None if args.positions_only else spd,
                            mode=None if args.positions_only else ["walk"] * len(xs))
    print(f"Wrote {args.output}: {stats}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    demo = sub.add_parser("demo", help="write a small demo file")
    demo.add_argument("output", type=Path)
    demo.add_argument("--positions-only", action="store_true",
                      help="omit rotation/speed/mode so Mobius derives them")
    demo.set_defaults(func=write_demo)

    conv = sub.add_parser("from-csv", help="convert a long-format trajectory CSV")
    conv.add_argument("input", type=Path)
    conv.add_argument("output", type=Path)
    conv.add_argument("--dt", type=float, default=None,
                      help="output sampling interval in seconds (default: median CSV interval)")
    conv.add_argument("--interp", choices=["linear", "hold"], default="linear",
                      help="'linear' for periodic snapshots (default); 'hold' for event logs that "
                           "write a row only when an agent moves, so waiting stays visible")
    conv.add_argument("--positions-only", action="store_true",
                      help="do not compute heading/speed; Mobius derives them on load "
                           "(slow for big runs in releases from before October 2026)")
    conv.add_argument("--delimiter", default=",")
    conv.add_argument("--scale", type=float, default=1.0,
                      help="multiply x/y/z by this to get metres (e.g. 0.001 for mm)")
    conv.add_argument("--floor-z", default=None,
                      help="floor label to floor height in metres, e.g. '0=0,1=3.5,2=7'")
    conv.add_argument("--floor-height", type=float, default=None,
                      help="uniform storey height in metres for numeric floor labels")
    conv.add_argument("--name-prefix", default="Agent ")
    for col, default in [("id", "id"), ("time", "time"), ("x", "x"), ("y", "y"), ("z", "z"), ("floor", "floor")]:
        conv.add_argument(f"--col-{col}", default=default, help=f"CSV column holding {col} (default '{default}')")
    conv.set_defaults(func=convert_csv)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
