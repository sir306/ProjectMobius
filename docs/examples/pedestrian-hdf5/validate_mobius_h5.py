#!/usr/bin/env python3
"""
Check that an HDF5 file will load correctly as Project Mobius pedestrian data.

    python validate_mobius_h5.py run.h5 [more.h5 ...]

Exit code 0 when every file passes (warnings allowed), 1 when any file has an
error. The checks mirror what the Mobius importer actually does
(Plugins/MobiusDataImporter/.../Hdf5SimulationReader.cpp and
Source/ProjectMobius/.../AgentDataSubsystem.cpp), including the things it does
silently: it decodes most fields by POSITION with fixed widths, so a float64
position or a variable-length string does not fail to load, it loads garbage.

Layout reference: docs/PEDESTRIAN-DATA-FORMAT.md

Requires: numpy, h5py  (pip install -r requirements.txt)

MIT License - Copyright (c) 2026 ProjectMobius contributors
"""

import math
import sys
from pathlib import Path

import h5py
import numpy as np

# (position, name, kind) - kind is "i4", "f4" or "str". Order is the contract.
ENTITY_MEMBERS = [
    (0, "id", "i4"),
    (1, "name", "str"),
    (2, "sim_time_s", "f4"),
    (3, "max_speed", "f4"),
    (4, "m_plane", "str"),
    (5, "map", "i4"),
]
SAMPLE_MEMBERS = [
    (0, "timestep_idx", "i4"),
    (1, "entity_id", "i4"),
    (2, "position_x", "f4"),
    (3, "position_y", "f4"),
    (4, "position_z", "f4"),
]
OPTIONAL_SAMPLE_MEMBERS = {"rotation": "f4", "speed": "f4", "mode": "str"}
# HDF5 filter ids the bundled library can decode: 1 deflate (gzip), 2 shuffle, 3 fletcher32,
# 5 n-bit, 6 scale-offset. Not 4 (szip, built without it) and no plugin filters (lzf, blosc, ...).
READABLE_FILTERS = {1, 2, 3, 5, 6}


class Report:
    def __init__(self, path):
        self.path = path
        self.errors = []
        self.warnings = []
        self.info = []

    def error(self, msg):
        self.errors.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)

    def note(self, msg):
        self.info.append(msg)

    def print(self):
        status = "FAIL" if self.errors else "PASS"
        print(f"\n[{status}] {self.path}")
        for msg in self.errors:
            print(f"  ERROR   {msg}")
        for msg in self.warnings:
            print(f"  WARNING {msg}")
        for msg in self.info:
            print(f"  info    {msg}")


def describe_member(member_type):
    """Return 'i4', 'f4', 'str', 'vlen-str' or a description of anything else."""
    cls = member_type.get_class()
    size = member_type.get_size()
    if cls == h5py.h5t.STRING:
        return "vlen-str" if member_type.is_variable_str() else "str"
    order = "LE" if member_type.get_order() == h5py.h5t.ORDER_LE else "BE"
    if cls == h5py.h5t.INTEGER:
        signed = member_type.get_sign() == h5py.h5t.SGN_2
        if size == 4 and signed and order == "LE":
            return "i4"
        return f"{'' if signed else 'unsigned '}{8 * size}-bit {order} integer"
    if cls == h5py.h5t.FLOAT:
        if size == 4 and order == "LE":
            return "f4"
        return f"{8 * size}-bit {order} float"
    return f"HDF5 class {cls} ({size} bytes)"


KIND_TEXT = {"i4": "32-bit little-endian signed int", "f4": "32-bit little-endian float",
             "str": "fixed-length string"}


def check_compound(rep, dset, label, required, optional=None):
    """Check member order/types. Returns {name: index} of the members found."""
    ftype = dset.id.get_type()
    if ftype.get_class() != h5py.h5t.COMPOUND:
        rep.error(f"{label} must be a compound (table) dataset, found {dset.dtype}")
        return {}
    nmembers = ftype.get_nmembers()
    names = [ftype.get_member_name(i).decode() for i in range(nmembers)]
    found = {n: i for i, n in enumerate(names)}

    for pos, name, kind in required:
        if pos >= nmembers:
            rep.error(f"{label}: needs at least {len(required)} members, has {nmembers} ({names})")
            return found
        actual = describe_member(ftype.get_member_type(pos))
        if names[pos] != name:
            rep.warn(f"{label}: member {pos} is named '{names[pos]}', expected '{name}' "
                     f"(Mobius reads this member by POSITION, so it will be used as {name})")
        if actual != kind:
            if actual == "vlen-str":
                rep.error(f"{label}.{names[pos]} is a variable-length string; Mobius needs a fixed-length "
                          f"string here (in h5py use numpy dtype 'S<n>', not str)")
            else:
                rep.error(f"{label}.{names[pos]} (member {pos}) is {actual}; Mobius reads it as "
                          f"{KIND_TEXT[kind]} WITHOUT conversion, so the values would be garbage")

    for name, kind in (optional or {}).items():
        if name in found:
            actual = describe_member(ftype.get_member_type(found[name]))
            if actual != kind:
                what = "a variable-length string" if actual == "vlen-str" else actual
                rep.error(f"{label}.{name} is {what}; Mobius reads it as {KIND_TEXT[kind]} without conversion")
    return found


def check_strings(rep, dset, label, member):
    """Fixed strings must leave room for a NUL: Mobius reads them as C strings."""
    if dset.shape[0] == 0 or member not in (dset.dtype.names or ()):
        return
    size = dset.dtype[member].itemsize
    values = dset[member]
    lengths = np.char.str_len(values) if values.size else np.array([0])
    if lengths.max() >= size:
        rep.error(f"{label}.{member}: some values fill all {size} bytes with no terminating NUL; "
                  f"make the field at least one byte longer than the longest value")


def check_heading(rep, t_idx, e_id, xyz, rotation_deg):
    """Compare stored headings with the direction each agent actually moves next."""
    order = np.lexsort((t_idx, e_id))
    t, e = t_idx[order], e_id[order]
    dx = np.diff(xyz[0][order])
    dy = np.diff(xyz[1][order])
    step = (np.diff(e) == 0) & (np.diff(t) == 1) & (np.hypot(dx, dy) > 0.05)
    if step.sum() < 10:
        return
    travel = np.degrees(np.arctan2(dy[step], dx[step]))
    diff = (rotation_deg[order][:-1][step] - travel + 180.0) % 360.0 - 180.0
    agree = np.mean(np.abs(diff) < 30.0)
    if agree < 0.5:
        rep.warn(f"rotation matches the direction of travel for only {100 * agree:.0f}% of moving samples. "
                 f"It must be degrees counter-clockwise from +x, i.e. degrees(atan2(dy, dx)), not a compass "
                 f"bearing. If your model has no heading, compute it from positions or leave it out")
    else:
        rep.note(f"rotation matches the direction of travel for {100 * agree:.0f}% of moving samples")


def read_attr(rep, group, name, default):
    if name not in group.attrs:
        rep.warn(f"/metadata has no '{name}' attribute; Mobius will use {default}")
        return None
    value = group.attrs[name]
    arr = np.asarray(value)
    if arr.shape != ():
        rep.error(f"/metadata '{name}' must be a scalar, found shape {arr.shape}")
        return None
    if arr.dtype.kind not in "biuf":
        rep.error(f"/metadata '{name}' must be numeric, found {arr.dtype}")
        return None
    return arr.item()


def validate(path):
    rep = Report(path)
    p = Path(path)
    if p.suffix.lower() != ".h5":
        rep.error(f"extension is '{p.suffix}'; Mobius only opens '.h5' files as pedestrian data")
    if not h5py.is_hdf5(path):
        rep.error("not an HDF5 file")
        return rep

    with h5py.File(path, "r") as f:
        if "trajectory" in f:
            rep.error("file has a root /trajectory dataset, so Mobius reads it as a Juelich trajectory "
                      "file and ignores /metadata, /entities and /simulation. Remove /trajectory.")
            return rep
        if "metadata" not in f or not isinstance(f["metadata"], h5py.Group):
            rep.error("missing /metadata group")
        if "simulation/samples" not in f:
            rep.error("missing /simulation/samples dataset")
        if "entities" not in f:
            rep.error("missing /entities dataset (the import fails without it)")
        if rep.errors:
            return rep

        # ---------------- metadata ----------------
        meta = f["metadata"]
        duration = read_attr(rep, meta, "duration", "0 (playback length from the samples)")
        dt = read_attr(rep, meta, "sampling_rate", "0.1 s")
        max_entities = read_attr(rep, meta, "max_num_entities", "the /entities row count")
        is_si = read_attr(rep, meta, "is_si", "true (metres)")
        is_deg = read_attr(rep, meta, "is_deg", "true (degrees)")
        dt = 0.1 if dt is None else float(dt)
        if dt <= 0:
            rep.error(f"sampling_rate is {dt}; it must be the seconds between samples (> 0)")
            dt = 0.1
        elif dt > 5:
            rep.warn(f"sampling_rate is {dt}. It is the INTERVAL in seconds, not a frequency: "
                     f"10 Hz output is 0.1")
        if is_si is not None and not bool(is_si):
            rep.warn("is_si is false: Mobius then multiplies positions by 10 instead of 100. "
                     "Write positions in metres and set is_si = 1")

        # ---------------- entities ----------------
        ents = f["entities"]
        check_compound(rep, ents, "/entities", ENTITY_MEMBERS)
        n_rows = ents.shape[0]
        if not rep.errors:
            check_strings(rep, ents, "/entities", "name")
            check_strings(rep, ents, "/entities", "m_plane")
            ids = ents["id"]
            if n_rows and not np.array_equal(ids, np.arange(n_rows)):
                rep.warn("/entities.id is not 0,1,2,... in row order. Mobius shows row i as the "
                         "details of entity_id i, whatever its id field says")

        # ---------------- samples ----------------
        samples = f["simulation/samples"]
        found = check_compound(rep, samples, "/simulation/samples", SAMPLE_MEMBERS, OPTIONAL_SAMPLE_MEMBERS)
        for name in ("rotation", "speed"):
            if name not in found:
                rep.note(f"no '{name}' member: Mobius derives {name} from the movement on load (slow for "
                         f"thousands of agents in releases from before October 2026; "
                         f"mobius_h5_writer.heading_and_speed_from_positions computes it up front)")
        filters = samples.id.get_create_plist()
        for i in range(filters.get_nfilters()):
            code = filters.get_filter(i)[0]
            if code not in READABLE_FILTERS:
                rep.error(f"/simulation/samples uses HDF5 filter id {code}; the bundled HDF5 can only "
                          f"decode gzip/shuffle. Rewrite without lzf/szip/blosc/etc.")
        if rep.errors:
            return rep

        data = samples[...]
        n = data.shape[0]
        if n == 0:
            rep.error("/simulation/samples is empty")
            return rep
        t_idx = data[data.dtype.names[0]].astype(np.int64)
        e_id = data[data.dtype.names[1]].astype(np.int64)
        xyz = [data[data.dtype.names[k]].astype(np.float64) for k in (2, 3, 4)]

        declared = int(max_entities) if max_entities else n_rows
        if t_idx.min() < 0:
            rep.error(f"{int((t_idx < 0).sum())} samples have a negative timestep_idx")
        if e_id.min() < 0:
            rep.error(f"{int((e_id < 0).sum())} samples have a negative entity_id (they are skipped)")
        over = e_id >= declared
        if over.any():
            rep.error(f"{int(over.sum())} samples have entity_id >= {declared} (max_num_entities or the "
                      f"entity count). IDs must be dense 0..N-1; these samples are dropped")
        seen = np.unique(e_id[(e_id >= 0) & ~over])
        if seen.size < declared:
            rep.warn(f"only {seen.size} of {declared} declared entities appear in the samples "
                     f"(Mobius reports 'Incomplete entity data')")
        if max_entities and n_rows and int(max_entities) != n_rows:
            rep.warn(f"max_num_entities = {int(max_entities)} but /entities has {n_rows} rows. It must be "
                     f"the TOTAL number of agents in the run, not the peak at one time")
        if n_rows < declared:
            rep.warn(f"/entities has {n_rows} rows for {declared} entities; agents beyond row "
                     f"{n_rows - 1} have no details in the inspector")

        valid = (t_idx >= 0) & (e_id >= 0)
        key = t_idx[valid] * (int(e_id.max()) + 1) + e_id[valid]
        dupes = key.size - np.unique(key).size
        if dupes:
            rep.error(f"{dupes} samples repeat an (entity_id, timestep_idx) pair; write at most one "
                      f"row per agent per timestep")

        bad_pos = ~(np.isfinite(xyz[0]) & np.isfinite(xyz[1]) & np.isfinite(xyz[2]))
        if bad_pos.any():
            rep.error(f"{int(bad_pos.sum())} samples have NaN/Inf positions (each raises an error dialog). "
                      f"Write no row for an absent agent instead")
        if "rotation" in found:
            rot = data["rotation"].astype(np.float64)
            if not np.isfinite(rot).all():
                rep.error(f"{int((~np.isfinite(rot)).sum())} samples have NaN/Inf rotation")
            else:
                check_heading(rep, t_idx, e_id, xyz, rot if is_deg is None or bool(is_deg) else np.degrees(rot))
        if "speed" in found:
            spd = data["speed"].astype(np.float64)
            bad = ~np.isfinite(spd) | (spd < 0)
            if bad.any():
                rep.error(f"{int(bad.sum())} samples have a negative or NaN speed (each raises an error "
                          f"dialog and is replaced by 0)")
            elif np.all(spd == 0) and n > 1:
                rep.warn("every speed is 0. If your model has no speed, leave the speed member out so "
                         "Mobius derives it from movement")
        if "mode" in found:
            check_strings(rep, samples, "/simulation/samples", "mode")

        n_steps = int(t_idx.max()) + 1
        if duration:
            expected = math.ceil(np.float32(duration) / np.float32(dt))
            if n_steps < expected:
                rep.warn(f"duration {duration} s / sampling_rate {dt} s implies {expected} timesteps but "
                         f"the samples only reach {n_steps} (Mobius warns 'Data may be truncated'). "
                         f"Set duration = (number of timesteps - 1) * sampling_rate = {(n_steps - 1) * dt:g}")
        empty_steps = n_steps - np.unique(t_idx).size
        if empty_steps:
            rep.note(f"{empty_steps} timesteps have no agents at all (allowed)")

        if "timesteps" in f["simulation"]:
            stored = f["simulation/timesteps"][...]
            if stored.size and not np.allclose(stored, np.arange(stored.size) * dt, atol=max(1e-3, dt * 1e-3)):
                rep.warn("/simulation/timesteps is not 0, dt, 2dt, ... Mobius ignores it and places "
                         "sample k at k * sampling_rate, so resample non-uniform output first")

        span = (n_steps - 1) * dt
        rep.note(f"{declared} entities, {n_steps} timesteps ({span:g} s at {dt:g} s), {n} samples")
        rep.note("x [{:.2f}, {:.2f}] m, y [{:.2f}, {:.2f}] m, z [{:.2f}, {:.2f}] m".format(
            np.nanmin(xyz[0]), np.nanmax(xyz[0]), np.nanmin(xyz[1]), np.nanmax(xyz[1]),
            np.nanmin(xyz[2]), np.nanmax(xyz[2])))
        if is_deg is not None and not bool(is_deg):
            rep.note("is_deg is false: rotation is read as radians")
        if "trajectory" not in f and "wkt_geometry" in f.attrs:
            rep.warn("root wkt_geometry attribute found: Mobius will offer to load it as geometry, but it "
                     "loads WKT without the y mirror it applies to these agents, so the outline shows flipped "
                     "across the x axis. Negate every y value in the WKT to line it up")
    return rep


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    failed = False
    for path in argv[1:]:
        rep = validate(path)
        rep.print()
        failed |= bool(rep.errors)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
