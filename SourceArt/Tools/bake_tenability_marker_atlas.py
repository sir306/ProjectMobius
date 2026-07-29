#!/usr/bin/env python3
"""Bake the tenability fail marker SVGs into the runtime texture atlas.

The four SVGs in ``SourceArt/TenabilityFailMarkers/`` are the authoring form of the
in-world markers drawn where a B-RISK agent's egress tenability fails. This script is
the only thing that turns them into the runtime texture, so the texture stays genuinely
derived from the vector source: edit an SVG, re-run this, re-import. Never hand-edit the
baked texture.

Output
------
A ``size x size`` RGBA PNG, four square slots in a 2x2 grid, **row-major from the
top-left**::

    +-----------+-----------+
    | 0 Thermal | 1 Gas     |
    +-----------+-----------+
    | 2 Vis.    | 3 Unknown |
    +-----------+-----------+

So for slot ``s`` the UV offset is ``U = (s % 2) * 0.5``, ``V = (s // 2) * 0.5``, with
UE's V running downwards from the top of the texture, matching this layout directly.

The atlas carries **coverage only** -- there is no colour and no filled plate in it. The
strokes are the mask. Every channel is written with the same coverage value
(``R = G = B = A = coverage``), which is white premultiplied against transparent black.
That makes the file correct whether the material samples alpha or a colour channel, and
lets it import as grayscale or as an alpha mask without a second bake. Colour is a
material decision, driven from the failure criterion.

Import settings the bake cannot carry (set these on the UE texture):
  * **Mipmaps ON.** These are world-projected markers that shrink with distance; an
    unmipped mask shimmers as agents move.
  * A mask compression setting (grayscale/alpha), sRGB off -- this is coverage data,
    not colour.

Rendering
---------
No SVG rasteriser is required or used. Paths are parsed, flattened to polylines and
stroked here: segment quads plus round or mitered joins and round or butt caps, filled
opaque into a supersampled single-channel buffer and box-reduced to the slot size. Box
reduction of a binary supersample *is* the coverage integral, so overlapping strokes
union rather than compounding, and there are no seams where a join meets its segments.

Verification
------------
``--check-keepout`` enforces the art README's motif keepout: all motif geometry, stroke
width and round caps included, must stay inside the warning triangle inset 10 units
perpendicular to each edge (apex ``128,51``, base ``y 212``, half-width
``(y - 51) * 0.5758``). Motif crossing a triangle edge is the single biggest cause of an
unreadable marker at small sizes, and it is cheaper to catch here than in the editor.

``--preview`` writes a human-checkable proof sheet: every icon flattened over a light and
a dark ground, then at 64 / 40 / 28 px. 28 px is where a weight-7 stroke goes sub-pixel;
that column is the evidence for clamping ``UAgentEgressTenabilityWidget::MinimumScale``
rather than thickening the art.

Usage
-----
    python bake_tenability_marker_atlas.py --out atlas.png
    python bake_tenability_marker_atlas.py --out atlas.png --preview proof.png --check-keepout
"""

from __future__ import annotations

import argparse
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover - dependency is documented in the plan
    sys.exit("Pillow is required: python -m pip install Pillow")


SVG_NS = "{http://www.w3.org/2000/svg}"

# Slot order is the contract shared with the material's UV offset maths and with the
# table in SourceArt/TenabilityFailMarkers/README.md. Row-major from the top-left.
SLOTS = [
    ("ThermalFailMarker.svg", "0 Thermal (ThermalFED)"),
    ("GasFailMarker.svg", "1 Gas (ToxicFED)"),
    ("VisibilityFailMarker.svg", "2 Visibility (Visibility)"),
    ("UnknownFailMarker.svg", "3 Unknown (diagnostic)"),
]

# The warning triangle frame, shared by all four icons. Paths whose geometry matches it
# are the frame rather than motif, so the keepout check excludes them -- the frame is
# what defines the keepout, and it necessarily sits on the boundary.
FRAME_D_PREFIX = "M128 31"

# Keepout wedge: the triangle inset 10 units perpendicular to each edge.
KEEPOUT_APEX = (128.0, 51.0)
KEEPOUT_BASE_Y = 212.0
KEEPOUT_HALF_WIDTH_PER_Y = 0.5758

DEFAULT_MITER_LIMIT = 4.0

# Flattening target, in device pixels of the supersampled buffer. Curves are subdivided
# until each segment is shorter than this, so at the default supersample the error is far
# below one output pixel.
FLATTEN_TOLERANCE_PX = 0.25
MAX_FLATTEN_SEGMENTS = 512


# ---------------------------------------------------------------------------
# SVG path parsing
# ---------------------------------------------------------------------------

_NUMBER_RE = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")
_TOKEN_RE = re.compile(r"([MmZzLlHhVvCcSsQqTtAa])|([-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?)")


def tokenize_path(d):
    """Split path data into a flat list of command letters and floats."""
    tokens = []
    for match in _TOKEN_RE.finditer(d):
        if match.group(1) is not None:
            tokens.append(match.group(1))
        else:
            tokens.append(float(match.group(2)))
    return tokens


class PathParseError(ValueError):
    pass


def parse_path(d):
    """Flatten SVG path data into subpaths of on-curve points.

    Returns a list of ``(points, closed)`` where ``points`` is a list of ``(x, y)`` in
    user units. Curves are emitted as polylines; the caller supplies the flatten scale
    via :func:`set_flatten_scale` so subdivision is chosen in device pixels.
    """
    tokens = tokenize_path(d)
    i = 0
    subpaths = []
    points = []
    closed = False
    current = (0.0, 0.0)
    start = (0.0, 0.0)
    # Reflection state for the S/T shorthands.
    last_cubic_ctrl = None
    last_quad_ctrl = None
    command = None

    def flush(is_closed):
        nonlocal points
        if len(points) >= 2:
            subpaths.append((points, is_closed))
        points = []

    def take(n):
        nonlocal i
        if i + n > len(tokens):
            raise PathParseError("path data ended mid-command near token %d" % i)
        vals = tokens[i:i + n]
        for v in vals:
            if not isinstance(v, float):
                raise PathParseError("expected number, got command %r" % v)
        i += n
        return vals

    while i < len(tokens):
        token = tokens[i]
        if isinstance(token, str):
            command = token
            i += 1
            if command in "Zz":
                if points:
                    if _dist(points[-1], start) > 1e-9:
                        points.append(start)
                    flush(True)
                current = start
                last_cubic_ctrl = last_quad_ctrl = None
                continue
        elif command is None:
            raise PathParseError("path data starts with a number")
        elif command in "Mm":
            # Implicit repeats of moveto are linetos, per the SVG grammar.
            command = "L" if command == "M" else "l"

        rel = command.islower()
        cmd = command.upper()

        if cmd == "M":
            x, y = take(2)
            if rel:
                x, y = current[0] + x, current[1] + y
            flush(False)
            current = start = (x, y)
            points = [current]
            last_cubic_ctrl = last_quad_ctrl = None

        elif cmd == "L":
            x, y = take(2)
            if rel:
                x, y = current[0] + x, current[1] + y
            current = (x, y)
            points.append(current)
            last_cubic_ctrl = last_quad_ctrl = None

        elif cmd == "H":
            (x,) = take(1)
            if rel:
                x = current[0] + x
            current = (x, current[1])
            points.append(current)
            last_cubic_ctrl = last_quad_ctrl = None

        elif cmd == "V":
            (y,) = take(1)
            if rel:
                y = current[1] + y
            current = (current[0], y)
            points.append(current)
            last_cubic_ctrl = last_quad_ctrl = None

        elif cmd in ("C", "S"):
            if cmd == "C":
                x1, y1, x2, y2, x, y = take(6)
                if rel:
                    x1, y1 = current[0] + x1, current[1] + y1
                    x2, y2 = current[0] + x2, current[1] + y2
                    x, y = current[0] + x, current[1] + y
            else:
                x2, y2, x, y = take(4)
                if rel:
                    x2, y2 = current[0] + x2, current[1] + y2
                    x, y = current[0] + x, current[1] + y
                if last_cubic_ctrl is None:
                    x1, y1 = current
                else:
                    x1 = 2 * current[0] - last_cubic_ctrl[0]
                    y1 = 2 * current[1] - last_cubic_ctrl[1]
            points.extend(flatten_cubic(current, (x1, y1), (x2, y2), (x, y))[1:])
            last_cubic_ctrl = (x2, y2)
            last_quad_ctrl = None
            current = (x, y)

        elif cmd in ("Q", "T"):
            if cmd == "Q":
                x1, y1, x, y = take(4)
                if rel:
                    x1, y1 = current[0] + x1, current[1] + y1
                    x, y = current[0] + x, current[1] + y
            else:
                x, y = take(2)
                if rel:
                    x, y = current[0] + x, current[1] + y
                if last_quad_ctrl is None:
                    x1, y1 = current
                else:
                    x1 = 2 * current[0] - last_quad_ctrl[0]
                    y1 = 2 * current[1] - last_quad_ctrl[1]
            # Promote to a cubic so there is one flattener to trust.
            c1 = (current[0] + 2.0 / 3.0 * (x1 - current[0]), current[1] + 2.0 / 3.0 * (y1 - current[1]))
            c2 = (x + 2.0 / 3.0 * (x1 - x), y + 2.0 / 3.0 * (y1 - y))
            points.extend(flatten_cubic(current, c1, c2, (x, y))[1:])
            last_quad_ctrl = (x1, y1)
            last_cubic_ctrl = None
            current = (x, y)

        elif cmd == "A":
            rx, ry, rot, large_arc, sweep, x, y = take(7)
            if rel:
                x, y = current[0] + x, current[1] + y
            points.extend(flatten_arc(current, rx, ry, rot, large_arc != 0, sweep != 0, (x, y))[1:])
            last_cubic_ctrl = last_quad_ctrl = None
            current = (x, y)

        else:
            raise PathParseError("unsupported path command %r" % command)

    flush(closed)
    return subpaths


# Flatten subdivision is chosen against the device scale so the tolerance means the same
# thing regardless of supersample factor. Module state rather than a parameter threaded
# through every parse call.
_FLATTEN_SCALE = 1.0


def set_flatten_scale(scale):
    global _FLATTEN_SCALE
    _FLATTEN_SCALE = max(scale, 1e-6)


def _segment_count(approx_length_user_units):
    device_length = approx_length_user_units * _FLATTEN_SCALE
    n = int(math.ceil(device_length / FLATTEN_TOLERANCE_PX))
    return max(2, min(n, MAX_FLATTEN_SEGMENTS))


def flatten_cubic(p0, p1, p2, p3):
    # The control polygon bounds the curve length, so it is a safe subdivision estimate.
    polygon_length = _dist(p0, p1) + _dist(p1, p2) + _dist(p2, p3)
    n = _segment_count(polygon_length)
    out = []
    for step in range(n + 1):
        t = step / n
        mt = 1.0 - t
        a = mt * mt * mt
        b = 3.0 * mt * mt * t
        c = 3.0 * mt * t * t
        e = t * t * t
        out.append((
            a * p0[0] + b * p1[0] + c * p2[0] + e * p3[0],
            a * p0[1] + b * p1[1] + c * p2[1] + e * p3[1],
        ))
    return out


def flatten_arc(p0, rx, ry, rot_deg, large_arc, sweep, p1):
    """Endpoint-parameterised SVG arc to a polyline (SVG 1.1 appendix F.6.5)."""
    x1, y1 = p0
    x2, y2 = p1
    if rx == 0.0 or ry == 0.0 or (abs(x1 - x2) < 1e-12 and abs(y1 - y2) < 1e-12):
        return [p0, p1]

    rx, ry = abs(rx), abs(ry)
    phi = math.radians(rot_deg)
    cos_phi, sin_phi = math.cos(phi), math.sin(phi)

    dx2, dy2 = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    x1p = cos_phi * dx2 + sin_phi * dy2
    y1p = -sin_phi * dx2 + cos_phi * dy2

    # Scale the radii up if they are too small to span the endpoints.
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1.0:
        scale = math.sqrt(lam)
        rx *= scale
        ry *= scale

    rx2, ry2 = rx * rx, ry * ry
    numerator = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p
    denominator = rx2 * y1p * y1p + ry2 * x1p * x1p
    coefficient = math.sqrt(max(0.0, numerator / denominator)) if denominator > 0 else 0.0
    if large_arc == sweep:
        coefficient = -coefficient
    cxp = coefficient * (rx * y1p / ry)
    cyp = coefficient * (-ry * x1p / rx)

    cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) / 2.0
    cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) / 2.0

    def angle(ux, uy, vx, vy):
        dot = ux * vx + uy * vy
        det = ux * vy - uy * vx
        return math.atan2(det, dot)

    ux, uy = (x1p - cxp) / rx, (y1p - cyp) / ry
    vx, vy = (-x1p - cxp) / rx, (-y1p - cyp) / ry
    theta1 = angle(1.0, 0.0, ux, uy)
    dtheta = angle(ux, uy, vx, vy)
    if not sweep and dtheta > 0:
        dtheta -= 2.0 * math.pi
    elif sweep and dtheta < 0:
        dtheta += 2.0 * math.pi

    n = _segment_count(abs(dtheta) * max(rx, ry))
    out = []
    for step in range(n + 1):
        theta = theta1 + dtheta * (step / n)
        ex, ey = rx * math.cos(theta), ry * math.sin(theta)
        out.append((
            cx + cos_phi * ex - sin_phi * ey,
            cy + sin_phi * ex + cos_phi * ey,
        ))
    return out


# ---------------------------------------------------------------------------
# Stroking
# ---------------------------------------------------------------------------

def _dist(a, b):
    return math.hypot(b[0] - a[0], b[1] - a[1])


def _dedupe(points):
    out = [points[0]]
    for p in points[1:]:
        if _dist(out[-1], p) > 1e-9:
            out.append(p)
    return out


def stroke_subpath(points, closed, width, linecap, linejoin, miter_limit=DEFAULT_MITER_LIMIT):
    """Convert one polyline into filled shapes approximating its stroke.

    Returns ``(polygons, discs)``. Every shape is opaque; because they are unioned into a
    binary buffer, overlap between a segment quad and its join wedge is harmless and no
    seam can appear between them.
    """
    points = _dedupe(points)
    half = width / 2.0
    polygons = []
    discs = []

    if len(points) < 2:
        # A degenerate subpath still paints a round cap's dot, which is how a zero-length
        # round-capped path is defined; anything else paints nothing.
        if points and linecap == "round":
            discs.append((points[0], half))
        return polygons, discs

    if closed and _dist(points[0], points[-1]) <= 1e-9:
        points = points[:-1]
        if len(points) < 2:
            if linecap == "round":
                discs.append((points[0], half))
            return polygons, discs

    count = len(points)
    segment_count = count if closed else count - 1

    directions = []
    for index in range(segment_count):
        a = points[index]
        b = points[(index + 1) % count]
        length = _dist(a, b)
        directions.append(((b[0] - a[0]) / length, (b[1] - a[1]) / length))

    # Segment bodies.
    for index in range(segment_count):
        a = points[index]
        b = points[(index + 1) % count]
        dx, dy = directions[index]
        nx, ny = -dy, dx
        polygons.append([
            (a[0] + nx * half, a[1] + ny * half),
            (b[0] + nx * half, b[1] + ny * half),
            (b[0] - nx * half, b[1] - ny * half),
            (a[0] - nx * half, a[1] - ny * half),
        ])

    # Joins.
    join_indices = range(count) if closed else range(1, count - 1)
    for index in join_indices:
        vertex = points[index]
        incoming = directions[(index - 1) % segment_count]
        outgoing = directions[index % segment_count]
        if linejoin == "round":
            discs.append((vertex, half))
            continue
        wedge = _join_wedge(vertex, incoming, outgoing, half, linejoin, miter_limit)
        if wedge:
            polygons.append(wedge)

    # Caps.
    if not closed:
        if linecap == "round":
            discs.append((points[0], half))
            discs.append((points[-1], half))
        elif linecap == "square":
            polygons.append(_square_cap(points[0], directions[0], half, extend_backwards=True))
            polygons.append(_square_cap(points[-1], directions[-1], half, extend_backwards=False))
        # "butt" adds nothing, which is the point of it.

    return polygons, discs


def _join_wedge(vertex, incoming, outgoing, half, linejoin, miter_limit):
    cross = incoming[0] * outgoing[1] - incoming[1] * outgoing[0]
    dot = incoming[0] * outgoing[0] + incoming[1] * outgoing[1]
    if abs(cross) < 1e-12 and dot > 0:
        return None  # Collinear: the two segment quads already meet flush.

    # Outer side of the turn: the side where the offset edges diverge and leave a gap.
    side = -1.0 if cross > 0 else 1.0
    n0 = (-incoming[1] * side, incoming[0] * side)
    n1 = (-outgoing[1] * side, outgoing[0] * side)
    a = (vertex[0] + n0[0] * half, vertex[1] + n0[1] * half)
    b = (vertex[0] + n1[0] * half, vertex[1] + n1[1] * half)

    if linejoin == "miter":
        normal_dot = n0[0] * n1[0] + n0[1] * n1[1]
        denominator = 1.0 + normal_dot
        if denominator > 1e-9:
            mx = (n0[0] + n1[0]) / denominator
            my = (n0[1] + n1[1]) / denominator
            if math.hypot(mx, my) <= miter_limit:
                tip = (vertex[0] + mx * half, vertex[1] + my * half)
                return [vertex, a, tip, b]
        # Over the miter limit (or a near-180-degree reversal): SVG falls back to bevel.

    return [vertex, a, b]


def _square_cap(point, direction, half, extend_backwards):
    dx, dy = direction
    if extend_backwards:
        dx, dy = -dx, -dy
    nx, ny = -dy, dx
    tip = (point[0] + dx * half, point[1] + dy * half)
    return [
        (point[0] + nx * half, point[1] + ny * half),
        (tip[0] + nx * half, tip[1] + ny * half),
        (tip[0] - nx * half, tip[1] - ny * half),
        (point[0] - nx * half, point[1] - ny * half),
    ]


# ---------------------------------------------------------------------------
# SVG document walking
# ---------------------------------------------------------------------------

INHERITED = ("stroke", "stroke-width", "stroke-linecap", "stroke-linejoin", "stroke-miterlimit", "fill")


def _local(tag):
    return tag[len(SVG_NS):] if tag.startswith(SVG_NS) else tag


def _style(element, inherited):
    style = dict(inherited)
    for key in INHERITED:
        value = element.get(key)
        if value is not None:
            style[key] = value
    # A style="" attribute would override presentation attributes; support the subset
    # that matters so a hand-edit in an SVG editor cannot silently drop a stroke.
    inline = element.get("style")
    if inline:
        for declaration in inline.split(";"):
            if ":" in declaration:
                key, value = declaration.split(":", 1)
                key = key.strip()
                if key in INHERITED:
                    style[key] = value.strip()
    return style


def _paints(style, key):
    value = style.get(key, "none" if key == "fill" else "none")
    return value not in ("none", "transparent", "")


def parse_svg(svg_path):
    """Parse an SVG, translating XML errors into something actionable.

    SVG is XML, so a comment may not contain a double hyphen. That is easy to write by
    accident in a prose comment and the raw parser error ("invalid token") does not say
    so, which has already cost one debugging round here.
    """
    try:
        return ET.parse(svg_path).getroot()
    except ET.ParseError as error:
        hint = ""
        try:
            with open(svg_path, "r", encoding="utf-8") as handle:
                for number, line in enumerate(handle, start=1):
                    stripped = line.strip()
                    if "--" in stripped.replace("<!--", "").replace("-->", ""):
                        hint = ("\n  line %d contains '--' inside a comment, which XML forbids: %s"
                                % (number, stripped))
                        break
        except OSError:
            pass
        raise ValueError("%s is not well-formed XML: %s%s" % (svg_path, error, hint)) from error


def _viewbox(root):
    view_box = root.get("viewBox")
    if not view_box:
        raise ValueError("SVG has no viewBox; the bake needs one to map user units to pixels")
    parts = [float(v) for v in _NUMBER_RE.findall(view_box)]
    if len(parts) != 4:
        raise ValueError("malformed viewBox %r" % view_box)
    return parts


class Shape:
    """One drawable produced by the SVG walk, already in user units."""

    __slots__ = ("polygons", "discs", "is_frame")

    def __init__(self, polygons, discs, is_frame):
        self.polygons = polygons
        self.discs = discs
        self.is_frame = is_frame


def collect_shapes(svg_path):
    """Walk an SVG and return (shapes, viewbox). Strokes become filled outlines here."""
    root = parse_svg(svg_path)
    view_box = _viewbox(root)
    shapes = []

    def walk(element, inherited):
        style = _style(element, inherited)
        tag = _local(element.tag)

        if tag == "path":
            d = element.get("d", "").strip()
            if d:
                is_frame = d.replace("\n", " ").split(",")[0].strip().startswith(FRAME_D_PREFIX)
                subpaths = parse_path(d)
                shapes.append(_shape_from_subpaths(subpaths, style, is_frame))
        elif tag in ("line", "polyline", "polygon"):
            subpaths = _subpaths_from_primitive(tag, element)
            if subpaths:
                shapes.append(_shape_from_subpaths(subpaths, style, False))
        elif tag == "rect":
            subpaths = _subpaths_from_rect(element)
            if subpaths:
                shapes.append(_shape_from_subpaths(subpaths, style, False))
        elif tag in ("circle", "ellipse"):
            shapes.append(_shape_from_ellipse(tag, element, style))

        for child in element:
            walk(child, style)

    walk(root, {})
    return [shape for shape in shapes if shape is not None], view_box


def _shape_from_subpaths(subpaths, style, is_frame):
    polygons = []
    discs = []
    if _paints(style, "fill"):
        for points, _closed in subpaths:
            if len(points) >= 3:
                polygons.append(list(points))
    if _paints(style, "stroke"):
        width = float(style.get("stroke-width", 1.0))
        linecap = style.get("stroke-linecap", "butt")
        linejoin = style.get("stroke-linejoin", "miter")
        miter_limit = float(style.get("stroke-miterlimit", DEFAULT_MITER_LIMIT))
        for points, closed in subpaths:
            stroke_polygons, stroke_discs = stroke_subpath(points, closed, width, linecap, linejoin, miter_limit)
            polygons.extend(stroke_polygons)
            discs.extend(stroke_discs)
    if not polygons and not discs:
        return None
    return Shape(polygons, discs, is_frame)


def _subpaths_from_primitive(tag, element):
    if tag == "line":
        a = (float(element.get("x1", 0)), float(element.get("y1", 0)))
        b = (float(element.get("x2", 0)), float(element.get("y2", 0)))
        return [([a, b], False)]
    raw = [float(v) for v in _NUMBER_RE.findall(element.get("points", ""))]
    points = list(zip(raw[0::2], raw[1::2]))
    if len(points) < 2:
        return []
    return [(points, tag == "polygon")]


def _subpaths_from_rect(element):
    x = float(element.get("x", 0))
    y = float(element.get("y", 0))
    w = float(element.get("width", 0))
    h = float(element.get("height", 0))
    if w <= 0 or h <= 0:
        return []
    # Rounded rects are not used by this art set; a corner radius would need arc corners.
    if element.get("rx") or element.get("ry"):
        raise ValueError("rounded <rect> is not supported by this baker")
    return [([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], True)]


def _shape_from_ellipse(tag, element, style):
    cx = float(element.get("cx", 0))
    cy = float(element.get("cy", 0))
    if tag == "circle":
        rx = ry = float(element.get("r", 0))
    else:
        rx = float(element.get("rx", 0))
        ry = float(element.get("ry", 0))
    if rx <= 0 or ry <= 0:
        return None

    outline = []
    steps = _segment_count(2.0 * math.pi * max(rx, ry))
    for step in range(steps):
        theta = 2.0 * math.pi * step / steps
        outline.append((cx + rx * math.cos(theta), cy + ry * math.sin(theta)))

    polygons = []
    discs = []
    if _paints(style, "fill"):
        polygons.append(outline)
    if _paints(style, "stroke"):
        width = float(style.get("stroke-width", 1.0))
        stroke_polygons, stroke_discs = stroke_subpath(
            outline, True, width,
            style.get("stroke-linecap", "butt"),
            style.get("stroke-linejoin", "miter"),
            float(style.get("stroke-miterlimit", DEFAULT_MITER_LIMIT)),
        )
        polygons.extend(stroke_polygons)
        discs.extend(stroke_discs)
    if not polygons and not discs:
        return None
    return Shape(polygons, discs, False)


# ---------------------------------------------------------------------------
# Rasterising
# ---------------------------------------------------------------------------

def rasterise(shapes, view_box, slot_px, supersample, include_frame=True, include_motif=True):
    """Render shapes to an 'L' coverage image of ``slot_px`` square."""
    min_x, min_y, view_w, view_h = view_box
    if view_w <= 0 or view_h <= 0:
        raise ValueError("viewBox has non-positive extent")
    device = slot_px * supersample
    # Uniform scale, matching the SVGs' square viewBox and square slots. A non-square
    # viewBox would need preserveAspectRatio handling, which this art set does not use.
    scale = device / max(view_w, view_h)

    def to_device(point):
        return ((point[0] - min_x) * scale, (point[1] - min_y) * scale)

    image = Image.new("L", (device, device), 0)
    draw = ImageDraw.Draw(image)
    for shape in shapes:
        if shape.is_frame and not include_frame:
            continue
        if not shape.is_frame and not include_motif:
            continue
        for polygon in shape.polygons:
            draw.polygon([to_device(p) for p in polygon], fill=255)
        for centre, radius in shape.discs:
            cx, cy = to_device(centre)
            r = radius * scale
            draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)

    # Exact box average of the binary supersample: this is the coverage integral.
    return image.reduce(supersample) if supersample > 1 else image


def coverage_to_rgba(coverage):
    """White premultiplied against transparent black, so R == G == B == A == coverage."""
    return Image.merge("RGBA", (coverage, coverage, coverage, coverage))


# ---------------------------------------------------------------------------
# Keepout verification
# ---------------------------------------------------------------------------

def keepout_polygon():
    half_width = (KEEPOUT_BASE_Y - KEEPOUT_APEX[1]) * KEEPOUT_HALF_WIDTH_PER_Y
    return [
        KEEPOUT_APEX,
        (KEEPOUT_APEX[0] + half_width, KEEPOUT_BASE_Y),
        (KEEPOUT_APEX[0] - half_width, KEEPOUT_BASE_Y),
    ]


def check_keepout(shapes, view_box, slot_px, supersample, threshold=8):
    """Report motif coverage that escapes the inset triangle wedge.

    Only motif is tested; the warning-triangle frame defines the keepout and necessarily
    sits on its boundary. ``threshold`` is the coverage value below which a pixel counts
    as an antialiasing tail rather than real ink.
    """
    motif = rasterise(shapes, view_box, slot_px, supersample, include_frame=False, include_motif=True)

    min_x, min_y, view_w, view_h = view_box
    scale = slot_px / max(view_w, view_h)
    wedge = Image.new("L", (slot_px, slot_px), 0)
    ImageDraw.Draw(wedge).polygon(
        [((p[0] - min_x) * scale, (p[1] - min_y) * scale) for p in keepout_polygon()],
        fill=255,
    )

    motif_pixels = motif.load()
    wedge_pixels = wedge.load()
    offenders = 0
    worst = 0
    bbox = None
    for y in range(slot_px):
        for x in range(slot_px):
            value = motif_pixels[x, y]
            if value > threshold and wedge_pixels[x, y] == 0:
                offenders += 1
                worst = max(worst, value)
                if bbox is None:
                    bbox = [x, y, x, y]
                else:
                    bbox[0] = min(bbox[0], x)
                    bbox[1] = min(bbox[1], y)
                    bbox[2] = max(bbox[2], x)
                    bbox[3] = max(bbox[3], y)
    return offenders, worst, bbox


# ---------------------------------------------------------------------------
# Proof sheet
# ---------------------------------------------------------------------------

PREVIEW_SIZES = (64, 40, 28)
PREVIEW_LIGHT = (232, 232, 232)
PREVIEW_DARK = (28, 28, 30)
PREVIEW_INK_ON_LIGHT = (40, 40, 44)
PREVIEW_INK_ON_DARK = (240, 240, 244)


def _flatten(coverage, ink, ground, size=None):
    if size is not None and size != coverage.size[0]:
        coverage = coverage.resize((size, size), Image.Resampling.LANCZOS)
    tile = Image.new("RGB", coverage.size, ground)
    tile.paste(Image.new("RGB", coverage.size, ink), (0, 0), coverage)
    return tile


def build_preview(entries, slot_px):
    """Proof sheet: each icon over a light and a dark ground, then at shrinking sizes."""
    from PIL import ImageFont

    try:
        font = ImageFont.load_default()
    except Exception:  # pragma: no cover - default font is always present in practice
        font = None

    pad = 12
    label_h = 16
    row_h = slot_px + label_h + pad
    columns = [slot_px, slot_px] + list(PREVIEW_SIZES)
    width = pad + sum(c + pad for c in columns)
    height = pad + len(entries) * row_h + label_h
    sheet = Image.new("RGB", (width, height), (250, 250, 250))
    draw = ImageDraw.Draw(sheet)

    headings = ["light ground", "dark ground"] + ["%d px" % s for s in PREVIEW_SIZES]
    x = pad
    for heading, column_width in zip(headings, columns):
        draw.text((x, 2), heading, fill=(90, 90, 90), font=font)
        x += column_width + pad

    for row, (label, coverage) in enumerate(entries):
        y = pad + label_h + row * row_h
        x = pad
        tiles = [
            _flatten(coverage, PREVIEW_INK_ON_LIGHT, PREVIEW_LIGHT),
            _flatten(coverage, PREVIEW_INK_ON_DARK, PREVIEW_DARK),
        ]
        tiles += [_flatten(coverage, PREVIEW_INK_ON_LIGHT, PREVIEW_LIGHT, size=s) for s in PREVIEW_SIZES]
        for tile, column_width in zip(tiles, columns):
            sheet.paste(tile, (x + (column_width - tile.size[0]) // 2, y))
            x += column_width + pad
        draw.text((pad, y + slot_px + 2), label, fill=(50, 50, 50), font=font)

    return sheet


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    default_art = os.path.normpath(os.path.join(here, "..", "TenabilityFailMarkers"))

    parser = argparse.ArgumentParser(
        description="Bake the tenability fail marker SVGs into the runtime texture atlas.",
    )
    parser.add_argument("--out", required=True, help="output atlas PNG path")
    parser.add_argument("--art-dir", default=default_art, help="folder holding the marker SVGs")
    parser.add_argument("--size", type=int, default=256, help="atlas edge in pixels (default 256, 2x2 slots)")
    parser.add_argument("--ss", type=int, default=16, help="supersample factor per axis (default 16)")
    parser.add_argument("--preview", help="also write a human-checkable proof sheet PNG here")
    parser.add_argument("--check-keepout", action="store_true", help="verify motif stays inside the inset triangle")
    args = parser.parse_args(argv)

    if args.size % 2 != 0:
        parser.error("--size must be even so the 2x2 grid splits cleanly")
    if args.ss < 1:
        parser.error("--ss must be at least 1")

    slot_px = args.size // 2
    atlas = Image.new("RGBA", (args.size, args.size), (0, 0, 0, 0))
    preview_entries = []
    keepout_failures = 0

    for index, (filename, label) in enumerate(SLOTS):
        svg_path = os.path.join(args.art_dir, filename)
        if not os.path.isfile(svg_path):
            sys.exit("missing source art: %s" % svg_path)

        # Flatten in device pixels of the supersampled buffer, so tolerance is meaningful.
        view_box = _viewbox(parse_svg(svg_path))
        set_flatten_scale((slot_px * args.ss) / max(view_box[2], view_box[3]))

        shapes, view_box = collect_shapes(svg_path)
        coverage = rasterise(shapes, view_box, slot_px, args.ss)

        column, row = index % 2, index // 2
        atlas.paste(coverage_to_rgba(coverage), (column * slot_px, row * slot_px))
        preview_entries.append((label, coverage))

        frame_count = sum(1 for s in shapes if s.is_frame)
        print("slot %d  %-34s %s  (%d shapes, %d frame)" % (
            index, label, filename, len(shapes), frame_count))

        if args.check_keepout:
            offenders, worst, bbox = check_keepout(shapes, view_box, slot_px, args.ss)
            if offenders:
                keepout_failures += 1
                print("        KEEPOUT VIOLATION: %d px outside the inset triangle "
                      "(max coverage %d, bbox %s)" % (offenders, worst, bbox))
            else:
                print("        keepout OK: motif entirely inside the inset triangle")

    _ensure_parent(args.out)
    atlas.save(args.out, "PNG", optimize=True)
    print("\nwrote atlas %dx%d (4 slots of %d px, supersample %dx): %s"
          % (args.size, args.size, slot_px, args.ss, args.out))
    print("import with mipmaps ON, sRGB off, a mask/grayscale compression setting; "
          "R == G == B == A == coverage")

    if args.preview:
        _ensure_parent(args.preview)
        build_preview(preview_entries, slot_px).save(args.preview, "PNG", optimize=True)
        print("wrote proof sheet: %s" % args.preview)

    if keepout_failures:
        print("\n%d icon(s) violate the motif keepout -- see "
              "SourceArt/TenabilityFailMarkers/README.md" % keepout_failures, file=sys.stderr)
        return 1
    return 0


def _ensure_parent(path):
    parent = os.path.dirname(os.path.abspath(path))
    if parent and not os.path.isdir(parent):
        os.makedirs(parent, exist_ok=True)


if __name__ == "__main__":
    sys.exit(main())
