#!/usr/bin/env python3
"""Build a SketchUp (.skp) model of the Roma / via Spiro Valles apartment.

The output is a SketchUp 2017-format file that SketchUp Web Free can open
(File > Open, or drag onto the home screen). Coordinates are inches inside
the SKP (SketchUp native units); the model is drawn in metres and converted.
"""

from __future__ import annotations

import math
from pathlib import Path

from openskp import create

from layout import (
    CEILING,
    DOORS_3D,
    LABELS,
    OPENINGS,
    PARAPET_H,
    ROOMS,
    SHAFT,
    SLAB,
    WALLS,
)

IN = 39.37007874015748  # inches per metre
HERE = Path(__file__).resolve().parent
OUT = HERE / "Roma_Via_Spiro_Valles.skp"
PLAN_PNG = HERE / "assets" / "planimetria.png"


def m(*xyz: float) -> tuple[float, ...]:
    return tuple(v * IN for v in xyz)


def box_faces(x0, y0, z0, x1, y1, z1):
    """Six outward-wound faces of an axis-aligned box, in metres."""
    x0, x1 = (x0, x1) if x0 <= x1 else (x1, x0)
    y0, y1 = (y0, y1) if y0 <= y1 else (y1, y0)
    z0, z1 = (z0, z1) if z0 <= z1 else (z1, z0)
    if x1 - x0 < 1e-6 or y1 - y0 < 1e-6 or z1 - z0 < 1e-6:
        return []
    return [
        [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)],  # -Z
        [(x0, y0, z1), (x0, y1, z1), (x1, y1, z1), (x1, y0, z1)],  # +Z
        [(x0, y0, z0), (x0, y0, z1), (x1, y0, z1), (x1, y0, z0)],  # -Y
        [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)],  # +Y
        [(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)],  # -X
        [(x1, y0, z0), (x1, y0, z1), (x1, y1, z1), (x1, y1, z0)],  # +X
    ]


def add_box(target, x0, y0, z0, x1, y1, z1, material=None, layer=None):
    for face in box_faces(x0, y0, z0, x1, y1, z1):
        target.add_face([m(*p) for p in face], material=material, layer=layer)


def add_poly(target, points, material=None, layer=None, holes=None):
    pts = [m(*p) if len(p) == 3 else m(p[0], p[1], 0.0) for p in points]
    hole_pts = []
    if holes:
        for hole in holes:
            hole_pts.append([m(*p) if len(p) == 3 else m(p[0], p[1], 0.0) for p in hole])
    target.add_face(pts, material=material, layer=layer, holes=hole_pts)


def wall_solids(wx0, wy0, wx1, wy1, wz0, wz1, openings):
    """Split an axis-aligned wall around door/window openings."""
    hits = []
    for o in openings:
        _kind, ox0, oy0, ox1, oy1, oz0, oz1 = o
        ix0, ix1 = max(wx0, ox0), min(wx1, ox1)
        iy0, iy1 = max(wy0, oy0), min(wy1, oy1)
        iz0, iz1 = max(wz0, oz0), min(wz1, oz1)
        if ix1 - ix0 < 0.05 or iy1 - iy0 < 0.05 or iz1 - iz0 < 0.05:
            continue
        hits.append((ix0, iy0, ix1, iy1, max(wz0, oz0), min(wz1, oz1)))
    if not hits:
        return [(wx0, wy0, wx1, wy1, wz0, wz1)]

    dx, dy = wx1 - wx0, wy1 - wy0
    solids = []
    if dx >= dy:
        xs = [wx0]
        for h in hits:
            xs.extend([h[0], h[2]])
        xs.append(wx1)
        xs = sorted(set(round(v, 4) for v in xs))
        for a, b in zip(xs, xs[1:]):
            if b - a < 0.02:
                continue
            covering = [h for h in hits if h[0] <= a + 0.01 and h[2] >= b - 0.01]
            if not covering:
                solids.append((a, wy0, b, wy1, wz0, wz1))
                continue
            oz0 = min(h[4] for h in covering)
            oz1 = max(h[5] for h in covering)
            if oz0 - wz0 > 0.04:
                solids.append((a, wy0, b, wy1, wz0, oz0))
            if wz1 - oz1 > 0.04:
                solids.append((a, wy0, b, wy1, oz1, wz1))
    else:
        ys = [wy0]
        for h in hits:
            ys.extend([h[1], h[3]])
        ys.append(wy1)
        ys = sorted(set(round(v, 4) for v in ys))
        for a, b in zip(ys, ys[1:]):
            if b - a < 0.02:
                continue
            covering = [h for h in hits if h[1] <= a + 0.01 and h[3] >= b - 0.01]
            if not covering:
                solids.append((wx0, a, wx1, b, wz0, wz1))
                continue
            oz0 = min(h[4] for h in covering)
            oz1 = max(h[5] for h in covering)
            if oz0 - wz0 > 0.04:
                solids.append((wx0, a, wx1, b, wz0, oz0))
            if wz1 - oz1 > 0.04:
                solids.append((wx0, a, wx1, b, oz1, wz1))
    return solids


def door_leaf_box(x, y, w, h, rot_deg, thickness=0.04):
    """Return AABB-unfriendly leaf as a list of 8 corners; we emit a thin box
    in local coords then rotate in XY around the hinge (x, y)."""
    rad = math.radians(rot_deg)
    c, s = math.cos(rad), math.sin(rad)

    def xf(lx, ly, z):
        return (x + lx * c - ly * s, y + lx * s + ly * c, z)

    # leaf in local: x=0..w, y=0..thickness, z=0..h
    corners = [
        xf(0, 0, 0), xf(w, 0, 0), xf(w, thickness, 0), xf(0, thickness, 0),
        xf(0, 0, h), xf(w, 0, h), xf(w, thickness, h), xf(0, thickness, h),
    ]
    b0, b1, b2, b3, t0, t1, t2, t3 = corners
    return [
        [b0, b1, b2, b3],
        [t0, t3, t2, t1],
        [b0, t0, t1, b1],
        [b1, t1, t2, b2],
        [b2, t2, t3, b3],
        [b3, t3, t0, b0],
    ]


def add_stairs(target, mat_tread, mat_riser, layer=None):
    x0, y0, x1, y1 = 10.50, 13.30, 14.40, 17.55
    flight_w = 1.15
    n = 8
    riser = CEILING / (2 * n)  # 0.175
    going = 0.28
    # Flight 1: west, south -> north, z 0 -> 1.40
    for i in range(n):
        yy0 = y0 + i * going
        yy1 = yy0 + going
        z0 = i * riser
        z1 = (i + 1) * riser
        add_box(target, x0, yy0, z0, x0 + flight_w, yy0 + 0.04, z1, mat_riser, layer)
        add_box(target, x0, yy0, z1, x0 + flight_w, yy1, z1 + 0.04, mat_tread, layer)
    landing_y0 = y0 + n * going
    landing_z = n * riser
    add_box(target, x0, landing_y0, landing_z, x1, y1, landing_z + 0.04, mat_tread, layer)
    # Flight 2: east, north -> south, z 1.40 -> 2.80
    for i in range(n):
        yy1 = landing_y0 - i * going
        yy0 = yy1 - going
        z0 = landing_z + i * riser
        z1 = z0 + riser
        add_box(target, x1 - flight_w, yy1 - 0.04, z0, x1, yy1, z1, mat_riser, layer)
        add_box(target, x1 - flight_w, yy0, z1, x1, yy1, z1 + 0.04, mat_tread, layer)
    # well railing (simple)
    well_x0, well_x1 = x0 + flight_w + 0.05, x1 - flight_w - 0.05
    well_y0, well_y1 = y0 + 0.10, landing_y0 - 0.05
    rh = 0.95
    t = 0.04
    add_box(target, well_x0, well_y0, landing_z, well_x1, well_y0 + t, landing_z + rh, mat_riser, layer)
    add_box(target, well_x0, well_y1 - t, landing_z, well_x1, well_y1, landing_z + rh, mat_riser, layer)
    add_box(target, well_x0, well_y0, landing_z, well_x0 + t, well_y1, landing_z + rh, mat_riser, layer)
    add_box(target, well_x1 - t, well_y0, landing_z, well_x1, well_y1, landing_z + rh, mat_riser, layer)


def add_north_arrow(target, mat, layer=None):
    """Compass at the south-east of the sheet, pointing -Y (as on the cadastral plan)."""
    cx, cy, z = 16.2, 1.4, 0.02
    r = 0.55
    # disc
    target.add_circle(m(cx, cy, z), normal=(0, 0, 1), radius=r * IN, num_segments=24)
    # pointer toward -Y (north on the drawing)
    tip = (cx, cy - 0.95, z)
    left = (cx - 0.22, cy - 0.15, z)
    right = (cx + 0.22, cy - 0.15, z)
    add_poly(target, [tip, right, left], material=mat, layer=layer)
    # letter N as two chevrons / simple boxes
    add_box(target, cx - 0.18, cy + 0.05, z, cx - 0.10, cy + 0.42, z + 0.04, mat, layer)
    add_box(target, cx + 0.10, cy + 0.05, z, cx + 0.18, cy + 0.42, z + 0.04, mat, layer)
    add_box(target, cx - 0.18, cy + 0.20, z, cx + 0.18, cy + 0.28, z + 0.04, mat, layer)


def add_shaft_x(target, mat, layer=None):
    x0, y0, x1, y1 = SHAFT
    z0, z1 = 0.0, CEILING
    add_box(target, x0, y0, z0, x1, y1, z0 + 0.06, mat, layer)
    add_box(target, x0, y0, z1 - 0.06, x1, y1, z1, mat, layer)
    # X mark as two thin beams
    t = 0.06
    # diagonal approximated with stepped boxes is ugly; use two crossing walls
    add_box(target, x0 + 0.08, y0 + 0.08, 0.2, x0 + 0.08 + t, y1 - 0.08, z1 - 0.2, mat, layer)
    add_box(target, x1 - 0.08 - t, y0 + 0.08, 0.2, x1 - 0.08, y1 - 0.08, z1 - 0.2, mat, layer)
    add_box(target, x0 + 0.08, y0 + 0.08, 0.2, x1 - 0.08, y0 + 0.08 + t, z1 - 0.2, mat, layer)
    add_box(target, x0 + 0.08, y1 - 0.08 - t, 0.2, x1 - 0.08, y1 - 0.08, z1 - 0.2, mat, layer)


def build():
    builder = create()

    # --- materials (must come first) ---
    wall_int = builder.add_material("Intonaco", (245, 239, 228))
    wall_ext = builder.add_material("Muratura esterna", (228, 218, 200))
    parapet = builder.add_material("Parapetto", (236, 232, 224))
    wood = builder.add_material("Legno porte", (128, 90, 58))
    glass = builder.add_material("Vetro", (170, 205, 220), opacity=0.35)
    stair = builder.add_material("Pietra scala", (168, 164, 158))
    stair_r = builder.add_material("Alzata scala", (150, 146, 140))
    shaft_m = builder.add_material("Vano tecnico", (110, 110, 110))
    slab_m = builder.add_material("Solaio", (190, 186, 178))
    ceiling_m = builder.add_material("Soffitto", (248, 246, 240))
    north_m = builder.add_material("Nord", (40, 70, 130))
    grass = builder.add_material("Cortile", (176, 188, 150))
    floor_mats = {
        "Terrazzo sud": builder.add_material("Pav. terrazzo sud", (186, 118, 82)),
        "Terrazzo nord-ovest": builder.add_material("Pav. terrazzo NO", (186, 118, 82)),
        "Terrazzo nord": builder.add_material("Pav. terrazzo N", (186, 118, 82)),
        "Vano 1": builder.add_material("Pav. vano 1", (198, 164, 118)),
        "Vano 2": builder.add_material("Pav. vano 2", (186, 154, 112)),
        "Vano 3 soggiorno": builder.add_material("Pav. soggiorno", (176, 172, 164)),
        "Vano 4 loggia": builder.add_material("Pav. loggia", (168, 160, 148)),
        "Vano 5": builder.add_material("Pav. vano 5", (204, 170, 122)),
        "Bagno ovest": builder.add_material("Pav. bagno O", (214, 218, 222)),
        "Bagno centrale": builder.add_material("Pav. bagno C", (214, 218, 222)),
        "Disimpegno sud": builder.add_material("Pav. disimpegno S", (196, 190, 178)),
        "Disimpegno": builder.add_material("Pav. disimpegno", (196, 190, 178)),
        "Passaggio terrazzo nord": builder.add_material("Pav. passaggio", (196, 190, 178)),
        "Scala": builder.add_material("Pav. pianerottolo", (168, 164, 158)),
        "Interno / cortile": grass,
    }

    plan_mat = None
    if PLAN_PNG.exists():
        # Size of one texture tile = the cropped-plan extents in inches so a
        # default mapping already roughly fits; faces also pin UVs.
        plan_mat = builder.add_texture_material(
            "Planimetria catastale",
            str(PLAN_PNG),
            applied_width=20.0 * IN,
            applied_height=28.0 * IN,
        )

    # --- layers ---
    lyr_walls = builder.add_layer("01_Murature", color=(180, 170, 150))
    lyr_para = builder.add_layer("02_Parapetti", color=(200, 190, 170))
    lyr_floors = builder.add_layer("03_Pavimenti", color=(160, 130, 90))
    lyr_doors = builder.add_layer("04_Porte", color=(120, 80, 50))
    lyr_glass = builder.add_layer("05_Vetri", color=(140, 190, 210))
    lyr_stairs = builder.add_layer("06_Scala", color=(140, 140, 140))
    lyr_ceil = builder.add_layer("07_Soffitti", color=(230, 230, 230), hidden=True)
    lyr_slab = builder.add_layer("08_Solaio", color=(150, 150, 150))
    lyr_plan = builder.add_layer("09_Planimetria", color=(80, 80, 80), hidden=True)
    lyr_labels = builder.add_layer("10_Etichette", color=(40, 70, 130))
    lyr_site = builder.add_layer("11_Cortile", color=(140, 170, 120))

    # --- groups (all definitions before root geometry) ---
    with builder.add_group(name="01 Murature") as g:
        for kind, x0, y0, x1, y1 in WALLS:
            if kind == "parapet":
                continue
            z1 = CEILING
            mat = wall_ext if kind == "ext" else wall_int
            for solid in wall_solids(x0, y0, x1, y1, 0.0, z1, OPENINGS):
                add_box(g, *solid, material=mat, layer=lyr_walls)

    with builder.add_group(name="02 Parapetti terrazzi") as g:
        for kind, x0, y0, x1, y1 in WALLS:
            if kind != "parapet":
                continue
            for solid in wall_solids(x0, y0, x1, y1, 0.0, PARAPET_H, OPENINGS):
                add_box(g, *solid, material=parapet, layer=lyr_para)

    with builder.add_group(name="03 Pavimenti") as g:
        for name, (x0, y0, x1, y1) in ROOMS.items():
            mat = floor_mats.get(name, slab_m)
            lyr = lyr_site if name.startswith("Interno") else lyr_floors
            z = 0.01
            holes = []
            if name == "Vano 3 soggiorno":
                sx0, sy0, sx1, sy1 = SHAFT
                holes.append([(sx0, sy0, z), (sx1, sy0, z), (sx1, sy1, z), (sx0, sy1, z)])
            add_poly(
                g,
                [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)],
                material=mat,
                layer=lyr,
                holes=holes or None,
            )

    with builder.add_group(name="04 Porte") as g:
        for d in DOORS_3D:
            for face in door_leaf_box(d["x"], d["y"], d["w"], d["h"], d["rot"]):
                g.add_face([m(*p) for p in face], material=wood, layer=lyr_doors)

    with builder.add_group(name="05 Vetri terrazzi") as g:
        # Glass infill in porta-finestra openings (thin, centered in the wall)
        for kind, x0, y0, x1, y1, z0, z1 in OPENINGS:
            if kind not in ("pf", "window"):
                continue
            dx, dy = x1 - x0, y1 - y0
            t = 0.02
            if dx >= dy:
                ym = (y0 + y1) / 2
                add_box(g, x0, ym - t, z0 + 0.04, x1, ym + t, z1 - 0.04, glass, lyr_glass)
            else:
                xm = (x0 + x1) / 2
                add_box(g, xm - t, y0, z0 + 0.04, xm + t, y1, z1 - 0.04, glass, lyr_glass)

    with builder.add_group(name="06 Scala") as g:
        add_stairs(g, stair, stair_r, lyr_stairs)

    with builder.add_group(name="07 Vano ascensore") as g:
        add_shaft_x(g, shaft_m, lyr_stairs)

    with builder.add_group(name="08 Soffitti") as g:
        for name, (x0, y0, x1, y1) in ROOMS.items():
            if name.startswith("Terrazzo") or name.startswith("Interno"):
                continue
            z = CEILING
            holes = []
            if name == "Scala":
                continue  # stairwell open above
            if name == "Vano 3 soggiorno":
                sx0, sy0, sx1, sy1 = SHAFT
                holes.append([(sx0, sy0, z), (sx1, sy0, z), (sx1, sy1, z), (sx0, sy1, z)])
            add_poly(
                g,
                [(x0, y0, z), (x0, y1, z), (x1, y1, z), (x1, y0, z)],
                material=ceiling_m,
                layer=lyr_ceil,
                holes=holes or None,
            )

    with builder.add_group(name="09 Solaio") as g:
        # structural slab under the heated envelope
        add_box(g, -0.10, 2.60, -SLAB, 14.80, 17.95, 0.0, slab_m, lyr_slab)
        add_box(g, -0.10, -0.10, -SLAB, 10.45, 2.70, 0.0, slab_m, lyr_slab)

    if plan_mat is not None:
        with builder.add_group(name="10 Planimetria di riferimento") as g:
            # Original scan is 741 x 1050 px. Place it so the traced origin
            # (cropped plan pixel 88, 748 which sits at crop offset) lines up.
            # Using the full sheet: keep extra header/footer around the model.
            # Crop used for tracing started at (0.04w, 0.13h) of the full image.
            full_w, full_h = 741.0, 1050.0
            crop_x0, crop_y0 = 0.04 * full_w, 0.13 * full_h
            # origin pixel in full image:
            ox = crop_x0 + 88.0
            oy = crop_y0 + 748.0
            scale = 0.03
            x_min = -ox * scale
            y_max = oy * scale
            x_max = (full_w - ox) * scale
            y_min = -(full_h - oy) * scale
            z = -0.04
            g.add_face(
                [m(x_min, y_min, z), m(x_max, y_min, z), m(x_max, y_max, z), m(x_min, y_max, z)],
                material=plan_mat,
                layer=lyr_plan,
                front_uv=[
                    (m(x_min, y_min, z), (0.0, 0.0)),
                    (m(x_max, y_min, z), (1.0, 0.0)),
                    (m(x_min, y_max, z), (0.0, 1.0)),
                ],
            )

    with builder.add_group(name="11 Orientamento Nord") as g:
        add_north_arrow(g, north_m, lyr_labels)

    # Root-level leader texts (after all groups)
    for text, x, y in LABELS:
        builder.add_text(text, m(x, y, 1.15), leader=m(0.15, 0.15, 0.55))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    builder.save(str(OUT))
    print(f"Wrote {OUT} ({OUT.stat().st_size / 1024:.1f} KB)")
    return OUT


if __name__ == "__main__":
    build()
