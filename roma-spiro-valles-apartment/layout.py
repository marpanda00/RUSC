"""Modified apartment layout (red markup on the cadastral plan).

Origin: outer south-west corner of the large terrace.
+X: right on the drawing. +Y: up on the drawing (toward the stairs).
Ceiling height 2.80 m. New partitions are 15 cm, tagged kind "new".

Changes vs the original planimetria
-----------------------------------
- Old soggiorno split: south half = Camera sud; north half = Camera nord
  (with the old passaggio), ensuite to the central bathroom.
- Two new disimpegno at the entrance, with doors that separate
  the west side of the house from the east/camera side.
- Old vano 2 = Sala angolo cottura, linked by the central corridor.
- West wing (camera 1, bagno ovest, camera 5, terraces) unchanged.
- Old living south wall / loggia opened into Camera sud, then closed
  on the courtyard with a new door.
"""

from __future__ import annotations

PX_ORIGIN = (88.0, 748.0)
M_PER_PX = 0.03
CEILING = 2.80
SLAB = 0.25
EXT = 0.30
INT = 0.15
PARAPET_T = 0.12
PARAPET_H = 1.10
DOOR_H = 2.10
DOOR_W = 0.80
PF_H = 2.20
SILL = 0.90
WIN_H = 1.40

# Inner rectangles (x0, y0, x1, y1) in metres
ROOMS = {
    "Terrazzo sud": (0.30, 0.30, 10.05, 2.55),
    "Camera 5": (0.30, 3.00, 4.65, 6.30),
    "Bagno ovest": (0.30, 6.60, 3.45, 8.10),
    "Corridoio ovest": (3.60, 3.00, 4.65, 8.40),
    "Camera 1": (0.30, 8.55, 3.45, 14.10),
    "Terrazzo nord-ovest": (0.30, 14.40, 3.60, 14.95),
    "Sala angolo cottura": (3.90, 9.55, 6.45, 14.10),
    "Bagno centrale": (6.70, 9.55, 9.15, 13.85),
    "Disimpegno centrale": (3.60, 8.40, 10.50, 9.50),
    "Terrazzo nord": (6.70, 14.40, 10.20, 14.95),
    "Camera sud": (10.50, 7.20, 14.40, 9.45),
    "Disimpegno A": (10.50, 9.45, 14.40, 10.45),
    "Disimpegno B": (10.50, 10.45, 14.40, 11.40),
    "Camera nord": (10.50, 11.40, 14.40, 13.15),
    "Camera nord (lato bagno)": (9.35, 9.55, 10.20, 14.10),
    "Scala": (10.50, 13.30, 14.40, 17.55),
    "Interno / cortile": (12.80, 0.30, 16.50, 7.00),
}

SHAFT = (12.55, 11.40, 13.95, 12.85)

# kind: ext | int | new | parapet
WALLS = [
    # --- south terrace parapets ---
    ("parapet", 0.00, 0.00, 10.35, 0.30),
    ("parapet", 0.00, 0.00, 0.30, 2.70),
    ("parapet", 10.05, 0.00, 10.35, 2.70),
    ("ext", 0.00, 2.70, 10.35, 3.00),
    # west facade
    ("ext", 0.00, 2.70, 0.30, 15.20),
    # NW terrace
    ("parapet", 0.00, 14.95, 3.90, 15.20),
    ("parapet", 0.00, 14.10, 0.30, 15.20),
    ("parapet", 3.60, 14.95, 3.90, 15.20),
    ("ext", 0.00, 14.10, 3.90, 14.40),
    # north terrace
    ("parapet", 6.55, 14.95, 10.35, 15.20),
    ("parapet", 6.55, 14.10, 6.85, 15.20),
    ("parapet", 10.05, 14.10, 10.35, 15.20),
    ("ext", 6.55, 14.10, 10.35, 14.40),
    ("ext", 3.75, 14.10, 6.70, 14.40),
    # east facade living / stairs
    ("ext", 14.40, 7.20, 14.70, 17.70),
    ("ext", 10.20, 17.55, 14.70, 17.85),
    ("ext", 10.20, 8.40, 10.50, 17.85),
    # courtyard spine stops at the new camera sud
    ("ext", 12.45, 0.00, 12.75, 7.20),
    ("parapet", 12.45, 0.00, 13.10, 0.30),
    # west wing interiors (unchanged)
    ("int", 4.65, 3.00, 4.80, 8.40),
    ("int", 0.30, 6.30, 4.80, 6.45),
    ("int", 0.30, 8.10, 3.60, 8.25),
    ("int", 3.45, 6.45, 3.60, 8.25),
    ("int", 3.45, 8.25, 3.60, 14.10),
    ("int", 3.75, 9.50, 3.90, 14.10),
    ("int", 3.60, 9.50, 6.55, 9.65),
    ("int", 6.45, 9.50, 6.60, 14.10),
    ("int", 6.60, 13.85, 9.30, 14.00),
    ("int", 9.15, 8.40, 9.30, 14.10),
    ("int", 6.55, 9.50, 9.35, 9.65),
    # shaft
    ("int", 12.55, 11.40, 13.95, 11.55),
    ("int", 12.55, 12.70, 13.95, 12.85),
    ("int", 12.55, 11.40, 12.70, 12.85),
    ("int", 13.80, 11.40, 13.95, 12.85),
    ("int", 10.50, 13.15, 14.40, 13.30),
    # --- NEW partitions (red on the marked plan) ---
    ("new", 10.20, 7.20, 10.50, 8.45),          # close west of old loggia
    ("new", 10.50, 7.20, 14.40, 7.35),          # camera sud onto cortile
    ("new", 10.50, 9.45, 14.40, 9.60),          # camera sud / disimpegno A
    ("new", 10.50, 10.45, 14.40, 10.60),        # disimpegno A / B
    ("new", 10.50, 11.40, 14.40, 11.55),        # disimpegno B / camera nord
]

OPENINGS = [
    # west wing (unchanged)
    ("pf", 1.35, 2.70, 2.70, 3.00, 0.00, PF_H),
    ("door", 3.70, 2.70, 4.50, 3.00, 0.00, DOOR_H),
    ("door", 3.70, 6.30, 4.50, 6.45, 0.00, DOOR_H),
    ("door", 3.45, 6.80, 3.60, 7.60, 0.00, DOOR_H),
    ("door", 3.45, 10.15, 3.60, 10.95, 0.00, DOOR_H),
    ("pf", 0.90, 14.10, 2.50, 14.40, 0.00, PF_H),
    ("door", 4.85, 9.50, 5.65, 9.65, 0.00, DOOR_H),
    ("window", 4.50, 14.10, 5.70, 14.40, SILL, SILL + WIN_H),
    ("door", 7.40, 9.50, 8.20, 9.65, 0.00, DOOR_H),
    ("window", 7.40, 14.10, 8.50, 14.40, SILL, SILL + WIN_H),
    ("door", 9.40, 14.10, 10.20, 14.40, 0.00, DOOR_H),
    ("opening", 11.10, 13.15, 13.80, 13.30, 0.00, CEILING),
    # camera nord: wide opening through old living west wall so the L-shape connects
    ("opening", 10.20, 11.60, 10.50, 13.10, 0.00, CEILING),
    # ensuite: bagno centrale -> camera nord (lato bagno)
    ("door", 9.15, 11.80, 9.30, 12.60, 0.00, DOOR_H),
    # disimpegno A -> west house (disimpegno centrale)
    ("door", 10.20, 9.55, 10.50, 10.35, 0.00, DOOR_H),
    # camera sud -> disimpegno A
    ("door", 12.00, 9.45, 12.80, 9.60, 0.00, DOOR_H),
    # disimpegno A -> disimpegno B
    ("door", 12.00, 10.45, 12.80, 10.60, 0.00, DOOR_H),
    # disimpegno B -> camera nord
    ("door", 12.00, 11.40, 12.80, 11.55, 0.00, DOOR_H),
    # camera sud -> cortile
    ("door", 12.00, 7.20, 12.80, 7.35, 0.00, DOOR_H),
    # camera sud window onto cortile (east)
    ("window", 14.40, 8.00, 14.70, 9.20, SILL, SILL + WIN_H),
    # camera nord window onto cortile / east facade
    ("window", 14.40, 11.80, 14.70, 12.90, SILL, SILL + WIN_H),
]

DOORS_3D = [
    {"name": "PF camera 5 L", "x": 1.35, "y": 2.72, "w": 0.67, "h": PF_H, "rot": -70},
    {"name": "PF camera 5 R", "x": 2.70, "y": 2.72, "w": 0.67, "h": PF_H, "rot": 250},
    {"name": "Porta terrazzo sud", "x": 3.70, "y": 2.72, "w": 0.80, "h": DOOR_H, "rot": -75},
    {"name": "Porta camera 5", "x": 4.50, "y": 6.32, "w": 0.80, "h": DOOR_H, "rot": 200},
    {"name": "Porta bagno ovest", "x": 3.58, "y": 6.80, "w": 0.80, "h": DOOR_H, "rot": 160},
    {"name": "Porta camera 1", "x": 3.58, "y": 10.15, "w": 0.80, "h": DOOR_H, "rot": 150},
    {"name": "PF camera 1", "x": 0.90, "y": 14.38, "w": 1.60, "h": PF_H, "rot": 70},
    {"name": "Porta sala cottura", "x": 4.85, "y": 9.52, "w": 0.80, "h": DOOR_H, "rot": 70},
    {"name": "Porta bagno centrale S", "x": 7.40, "y": 9.52, "w": 0.80, "h": DOOR_H, "rot": 75},
    {"name": "Porta terrazzo nord", "x": 9.40, "y": 14.38, "w": 0.80, "h": DOOR_H, "rot": 80},
    {"name": "Porta bagno-camera nord", "x": 9.28, "y": 11.80, "w": 0.80, "h": DOOR_H, "rot": -15},
    {"name": "Porta dis A ovest", "x": 10.22, "y": 9.55, "w": 0.80, "h": DOOR_H, "rot": 160},
    {"name": "Porta camera sud", "x": 12.00, "y": 9.47, "w": 0.80, "h": DOOR_H, "rot": 75},
    {"name": "Porta dis A-B", "x": 12.00, "y": 10.47, "w": 0.80, "h": DOOR_H, "rot": 75},
    {"name": "Porta dis B-camera", "x": 12.00, "y": 11.42, "w": 0.80, "h": DOOR_H, "rot": 75},
    {"name": "Porta camera sud cortile", "x": 12.00, "y": 7.22, "w": 0.80, "h": DOOR_H, "rot": -75},
]

LABELS = [
    ("Camera 1", 1.85, 11.30),
    ("Sala angolo cottura", 5.15, 11.80),
    ("Camera nord", 12.40, 12.20),
    ("Disimpegno B", 12.40, 10.90),
    ("Disimpegno A", 12.40, 9.95),
    ("Camera sud", 12.40, 8.30),
    ("Camera 5", 2.40, 4.65),
    ("Bagno", 1.85, 7.35),
    ("Bagno", 7.90, 11.05),
    ("Disimpegno", 6.50, 8.90),
    ("Terrazzo", 5.10, 1.40),
    ("Scala", 12.40, 15.40),
    ("Ascensore", 13.25, 12.10),
]


def px_to_m(px: float, py: float) -> tuple[float, float]:
    ox, oy = PX_ORIGIN
    return (px - ox) * M_PER_PX, (oy - py) * M_PER_PX


def m_to_px(x: float, y: float) -> tuple[float, float]:
    ox, oy = PX_ORIGIN
    return ox + x / M_PER_PX, oy - y / M_PER_PX


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
