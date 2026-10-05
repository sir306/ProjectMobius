#!/usr/bin/env python3
"""Merge the three AK Studio Art wheelchair OBJs into one Unreal-ready mesh.

Run from this directory:

    python merge_wheelchair.py

Produces SM_Wheelchair.obj + SM_Wheelchair.mtl: a single object with a single
material slot, positioned and oriented so that Unreal's OBJ importer -- with
default settings and no build-scale fiddling -- yields a chair that is upright,
in centimetres, facing +X, pivoted at floor-centre.

Why the coordinate handling looks convoluted
--------------------------------------------
Unreal's OBJ importer was measured against the unmodified source frame mesh
(bounds compared axis by axis) and does exactly two things:

    (x, y, z)  ->  (x, -y, z)          and no scale at all.

That is, it treats the source metres as centimetres and applies NO up-axis
conversion. Negating a single axis is a *mirror*: the imported mesh is a
left-right flipped copy of the original. On a near-symmetric wheelchair that is
invisible by eye, but it does swap the wheels, so it is cancelled here rather
than tolerated.

Source convention, established by measurement rather than assumption:

  * +Y is up          (frame Y range 0.0001 .. 1.1144, i.e. sits on the floor)
  * +Z is the BACK    (push handles, height band 0.85-1.20, mean Z +0.393;
                       footrests and castors, height band 0.00-0.20, mean Z -0.289)
  * +X is the LEFT    (the mesh named Left_Wheel is placed at X = +0.311)

Unreal wants +X forward, +Y right, +Z up. Composing that target with the
importer's mirror gives the file-space transform applied below:

    file = (-z, x, y) * 100

which has a negative determinant, so face winding is reversed here to keep the
file self-consistent. Importer mirror x file mirror = a proper rotation, so the
mesh that lands in Unreal is a faithful, non-mirrored copy.

Verified end to end: winding agrees with the authored vertex normals on
3518/3518 sampled triangles after import.
"""

import os

SCALE = 100.0  # source metres -> Unreal centimetres

# Wheel placement, in source metres. The wheels are modelled at the origin;
# their placement lived in the Godot prefab, which was not carried over. The
# 3 mm Z asymmetry between left and right is present in the source -- it is
# reproduced, not corrected, so the merged mesh matches the original.
PARTS = [
    ("SM_WhellChair.obj", (0.0, 0.0, 0.0)),
    ("SM_Left_Wheel.obj", (+0.31133676, 0.3306277, +0.22152776)),
    ("SM_Right_Wheel.obj", (-0.31134912, 0.3306277, +0.21844496)),
]

OUT_OBJ = "SM_Wheelchair.obj"
OUT_MTL = "SM_Wheelchair.mtl"
MATERIAL = "WheelchairBody"  # matches SM_WheelchairPlaceholder's slot name


def read_obj(path, offset):
    """Return (positions, uvs, normals, faces) with the offset applied."""
    verts, uvs, norms, faces = [], [], [], []
    ox, oy, oz = offset
    with open(path, "r") as handle:
        for line in handle:
            if line.startswith("v "):
                x, y, z = (float(v) for v in line.split()[1:4])
                verts.append((x + ox, y + oy, z + oz))
            elif line.startswith("vt "):
                parts = line.split()[1:3]
                uvs.append((float(parts[0]), float(parts[1])))
            elif line.startswith("vn "):
                nx, ny, nz = (float(v) for v in line.split()[1:4])
                norms.append((nx, ny, nz))
            elif line.startswith("f "):
                tri = []
                for token in line.split()[1:]:
                    bits = token.split("/")
                    tri.append((int(bits[0]), int(bits[1]), int(bits[2])))
                faces.append(tri)
    return verts, uvs, norms, faces


def main():
    here = os.path.dirname(os.path.abspath(__file__))

    all_v, all_vt, all_vn, all_f = [], [], [], []
    for name, offset in PARTS:
        v, vt, vn, f = read_obj(os.path.join(here, name), offset)
        # OBJ indices are 1-based and global, so shift each part's faces past
        # everything already emitted.
        dv, dvt, dvn = len(all_v), len(all_vt), len(all_vn)
        for tri in f:
            all_f.append([(a + dv, b + dvt, c + dvn) for (a, b, c) in tri])
        all_v.extend(v)
        all_vt.extend(vt)
        all_vn.extend(vn)
        print("  %-22s %5d verts  %5d tris" % (name, len(v), len(f)))

    # Re-pivot in source space, matching SM_WheelchairPlaceholder's convention:
    # centred horizontally, floor at zero. Doing this before the linear map
    # leaves the result centred in Unreal space too.
    xs = [p[0] for p in all_v]
    ys = [p[1] for p in all_v]
    zs = [p[2] for p in all_v]
    cx = (min(xs) + max(xs)) / 2.0
    cz = (min(zs) + max(zs)) / 2.0
    floor = min(ys)
    print("\n  source bounds  X %.4f..%.4f  Y %.4f..%.4f  Z %.4f..%.4f"
          % (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))
    print("  re-pivot: x-=%.5f  y-=%.5f  z-=%.5f" % (cx, floor, cz))

    with open(os.path.join(here, OUT_MTL), "w") as handle:
        handle.write("newmtl %s\n" % MATERIAL)
        handle.write("Kd 1.000000 1.000000 1.000000\n")

    with open(os.path.join(here, OUT_OBJ), "w") as handle:
        handle.write("# AK Studio Art wheelchair, merged for Unreal by "
                     "merge_wheelchair.py\n")
        handle.write("# Source licence: CC0 1.0 Universal. See ../LICENSE.md\n")
        handle.write("mtllib %s\n" % OUT_MTL)
        handle.write("o SM_Wheelchair\n")

        for (x, y, z) in all_v:
            x -= cx
            y -= floor
            z -= cz
            handle.write("v %.6f %.6f %.6f\n"
                         % (-z * SCALE, x * SCALE, y * SCALE))
        for (u, v) in all_vt:
            handle.write("vt %.6f %.6f\n" % (u, v))
        for (nx, ny, nz) in all_vn:
            handle.write("vn %.6f %.6f %.6f\n" % (-nz, nx, ny))
        # Declare the group and material immediately before the faces, which is
        # where the source OBJs put them. Putting them above the vertex block is
        # still legal OBJ, but the source layout is the one Unreal's importer is
        # known to accept here, so don't diverge from it for no reason.
        handle.write("g %s\n" % MATERIAL)
        handle.write("usemtl %s\n" % MATERIAL)
        # The position transform mirrors, so reverse winding to keep the file
        # self-consistent with its own normals.
        for tri in all_f:
            handle.write("f " + " ".join("%d/%d/%d" % t for t in reversed(tri))
                         + "\n")

    print("\n  wrote %s (%d verts, %d tris, 1 material slot)"
          % (OUT_OBJ, len(all_v), len(all_f)))


if __name__ == "__main__":
    main()
