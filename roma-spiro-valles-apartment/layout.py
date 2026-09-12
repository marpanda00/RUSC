"""Plan layout for Roma, via Spiro Valles apartment, traced from the cadastral plan.

Origin: outer south-west corner of the large terrace (bottom-left of the drawing).
+X: right on the drawing. +Y: up on the drawing (toward the stairs).
North on the cadastral sheet points down the page, so geographic north is -Y.

Scale used to trace the scan: 0.03 m per pixel, origin pixel (88, 748)
in the cropped plan image. Ceiling height is 2.80 m as labelled on the sheet.
"""

from __future__ import annotations

# Tracing calibration (plan_crop.png pixels -> meters)
PX_ORIGIN = (88.0, 748.0)
M_PER_PX = 0.03
CEILING = 2.80
SLAB = 0.25
EXT = 0.30  # exterior wall
INT = 0.15  # interior wall
PARAPET_T = 0.12
PARAPET_H = 1.10
DOOR_H = 2.10
DOOR_W = 0.80
PF_H = 2.20  # porta-finestra
SILL = 0.90
WIN_H = 1.40

# Room inner rectangles: (x0, y0, x1, y1) in metres
ROOMS = {
    "Terrazzo sud": (0.30, 0.30, 10.05, 2.55),
    "Vano 5": (0.30, 3.00, 4.65, 6.30),
    "Bagno ovest": (0.30, 6.60, 3.45, 8.10),
    "Disimpegno sud": (3.60, 3.00, 4.65, 8.40),
    "Vano 1": (0.30, 8.55, 3.45, 14.10),
    "Terrazzo nord-ovest": (0.30, 14.40, 3.60, 14.95),
    "Vano 2": (3.90, 9.55, 6.45, 14.10),
    "Bagno centrale": (6.70, 9.55, 9.15, 13.85),
    "Disimpegno": (3.60, 8.40, 10.20, 9.50),
    "Passaggio terrazzo nord": (9.35, 8.55, 10.20, 14.10),
    "Terrazzo nord": (6.70, 14.40, 10.20, 14.95),
    "Vano 3 soggiorno": (10.50, 8.70, 14.40, 13.15),
    "Vano 4 loggia": (10.50, 7.20, 14.40, 8.40),
    "Scala": (10.50, 13.30, 14.40, 17.55),
    "Interno / cortile": (12.80, 0.30, 16.50, 7.20),
}

# Elevator / shaft inner box
SHAFT = (12.55, 11.40, 13.95, 12.85)

# Axis-aligned walls as plan rectangles (xmin, ymin, xmax, ymax).
# Thickness is already baked in. z is handled in the 3D builder.
# kind: "ext" | "int" | "parapet"
WALLS = [
    # --- large south terrace parapets ---
    ("parapet", 0.00, 0.00, 10.35, 0.30),          # south
    ("parapet", 0.00, 0.00, 0.30, 2.70),           # west
    ("parapet", 10.05, 0.00, 10.35, 2.70),         # east
    # north parapet of terrace = south exterior wall of apartment (with doors)
    ("ext", 0.00, 2.70, 10.35, 3.00),
    # --- west facade ---
    ("ext", 0.00, 2.70, 0.30, 15.20),
    # --- north terrace parapets (NW) ---
    ("parapet", 0.00, 14.95, 3.90, 15.20),
    ("parapet", 0.00, 14.10, 0.30, 15.20),
    ("parapet", 3.60, 14.95, 3.90, 15.20),
    ("ext", 0.00, 14.10, 3.90, 14.40),             # room 1 north (porta-finestra)
    # --- north terrace parapets (center) ---
    ("parapet", 6.55, 14.95, 10.35, 15.20),
    ("parapet", 6.55, 14.10, 6.85, 15.20),
    ("parapet", 10.05, 14.10, 10.35, 15.20),
    ("ext", 6.55, 14.10, 10.35, 14.40),            # bath/passage north
    # room 2 north (exterior, no terrace)
    ("ext", 3.75, 14.10, 6.70, 14.40),
    # --- east facade of living / stairs (does not continue into the courtyard) ---
    ("ext", 14.40, 8.40, 14.70, 17.70),
    # --- north facade of stairwell ---
    ("ext", 10.20, 17.55, 14.70, 17.85),
    # --- west of stairwell / living ---
    ("ext", 10.20, 8.40, 10.50, 17.85),
    # south of living (double doors) / north of vano 4
    ("ext", 10.20, 8.40, 14.70, 8.70),
    # courtyard spine wall running south from the entrance (boundary of INTERNO)
    ("ext", 12.45, 0.00, 12.75, 8.40),
    # courtyard south closure
    ("parapet", 12.45, 0.00, 12.75, 0.30),
    # small east return at the south end of the spine
    ("parapet", 12.45, 0.00, 13.10, 0.30),
    # --- interior: room 5 east / disimpegno ---
    ("int", 4.65, 3.00, 4.80, 8.40),
    # room 5 north / bagno+disimpegno
    ("int", 0.30, 6.30, 4.80, 6.45),
    # bagno ovest north / vano 1
    ("int", 0.30, 8.10, 3.60, 8.25),
    # bagno ovest east
    ("int", 3.45, 6.45, 3.60, 8.25),
    # vano 1 east / vano 2 + vestibule
    ("int", 3.45, 8.25, 3.60, 14.10),
    # vano 2 west (north of vestibule)
    ("int", 3.75, 9.50, 3.90, 14.10),
    # vano 2 south
    ("int", 3.60, 9.50, 6.55, 9.65),
    # vano 2 east / bagno centrale
    ("int", 6.45, 9.50, 6.60, 14.10),
    # bagno centrale north (against the north terrace)
    ("int", 6.60, 13.85, 9.30, 14.00),
    # bagno centrale east / passaggio
    ("int", 9.15, 8.40, 9.30, 14.10),
    # bagno centrale south / disimpegno
    ("int", 6.55, 9.50, 9.35, 9.65),
    # living west already listed as ext at x=10.20-10.50
    # shaft walls
    ("int", 12.55, 11.40, 13.95, 11.55),
    ("int", 12.55, 12.70, 13.95, 12.85),
    ("int", 12.55, 11.40, 12.70, 12.85),
    ("int", 13.80, 11.40, 13.95, 12.85),
    # stair / living divider (open toward living, low wall / stringer)
    ("int", 10.50, 13.15, 14.40, 13.30),
]

# Openings: (wall-ish region x0,y0,x1,y1, z0, z1, kind)
# kind: door | pf (porta-finestra) | window | opening
OPENINGS = [
    # room 5 -> south terrace (porta-finestra)
    ("pf", 1.35, 2.70, 2.70, 3.00, 0.00, PF_H),
    # disimpegno sud -> south terrace
    ("door", 3.70, 2.70, 4.50, 3.00, 0.00, DOOR_H),
    # room 5 -> disimpegno (north-east of room 5)
    ("door", 3.70, 6.30, 4.50, 6.45, 0.00, DOOR_H),
    # bagno ovest -> disimpegno
    ("door", 3.45, 6.80, 3.60, 7.60, 0.00, DOOR_H),
    # vano 1 -> vestibule / disimpegno
    ("door", 3.45, 10.15, 3.60, 10.95, 0.00, DOOR_H),
    # vano 1 -> terrazzo nord-ovest
    ("pf", 0.90, 14.10, 2.50, 14.40, 0.00, PF_H),
    # vano 2 -> disimpegno
    ("door", 4.85, 9.50, 5.65, 9.65, 0.00, DOOR_H),
    # vano 2 north window
    ("window", 4.50, 14.10, 5.70, 14.40, SILL, SILL + WIN_H),
    # bagno centrale -> disimpegno
    ("door", 7.40, 9.50, 8.20, 9.65, 0.00, DOOR_H),
    # bagno centrale window onto terrazzo nord
    ("window", 7.40, 14.10, 8.50, 14.40, SILL, SILL + WIN_H),
    # passaggio -> terrazzo nord
    ("door", 9.40, 14.10, 10.20, 14.40, 0.00, DOOR_H),
    # passaggio -> vano 3
    ("door", 10.20, 10.40, 10.50, 11.20, 0.00, DOOR_H),
    # vano 3 double doors -> vano 4 / cortile
    ("pf", 11.50, 8.40, 13.20, 8.70, 0.00, PF_H),
    # stair open to living (wide opening in the divider)
    ("opening", 11.10, 13.15, 13.80, 13.30, 0.00, CEILING),
]

# Door leaves to draw in 3D: (x, y, z, width, height, thickness, rot_z_deg)
# rot 0 = leaf extends +X from pivot. Pivot is the hinge.
DOORS_3D = [
    # room 5 PF onto terrace, two leaves opening south
    {"name": "PF vano 5 L", "x": 1.35, "y": 2.72, "w": 0.67, "h": PF_H, "rot": -70},
    {"name": "PF vano 5 R", "x": 2.70, "y": 2.72, "w": 0.67, "h": PF_H, "rot": 250},
    {"name": "Porta terrazzo sud", "x": 3.70, "y": 2.72, "w": 0.80, "h": DOOR_H, "rot": -75},
    {"name": "Porta vano 5", "x": 4.50, "y": 6.32, "w": 0.80, "h": DOOR_H, "rot": 200},
    {"name": "Porta bagno ovest", "x": 3.58, "y": 6.80, "w": 0.80, "h": DOOR_H, "rot": 160},
    {"name": "Porta vano 1", "x": 3.58, "y": 10.15, "w": 0.80, "h": DOOR_H, "rot": 150},
    {"name": "PF vano 1", "x": 0.90, "y": 14.38, "w": 1.60, "h": PF_H, "rot": 70},
    {"name": "Porta vano 2", "x": 4.85, "y": 9.52, "w": 0.80, "h": DOOR_H, "rot": 70},
    {"name": "Porta bagno centrale", "x": 7.40, "y": 9.52, "w": 0.80, "h": DOOR_H, "rot": 75},
    {"name": "Porta terrazzo nord", "x": 9.40, "y": 14.38, "w": 0.80, "h": DOOR_H, "rot": 80},
    {"name": "Porta soggiorno", "x": 10.22, "y": 10.40, "w": 0.80, "h": DOOR_H, "rot": -20},
    {"name": "Portone L", "x": 11.50, "y": 8.42, "w": 0.85, "h": PF_H, "rot": -80},
    {"name": "Portone R", "x": 13.20, "y": 8.42, "w": 0.85, "h": PF_H, "rot": 260},
]

# Labels placed at room centres, z slightly above floor
LABELS = [
    ("Vano 1", 1.85, 11.30),
    ("Vano 2", 5.15, 11.80),
    ("Vano 3", 12.40, 10.70),
    ("Vano 4", 12.40, 7.85),
    ("Vano 5", 2.40, 4.65),
    ("Bagno", 1.85, 7.35),
    ("Bagno", 7.90, 11.05),
    ("Disimpegno", 6.50, 8.90),
    ("Terrazzo", 5.10, 1.40),
    ("Terrazzo", 1.90, 14.65),
    ("Terrazzo", 8.40, 14.65),
    ("Scala", 12.40, 15.40),
    ("Ascensore", 13.25, 12.10),
    ("Interno", 15.40, 3.60),
]


def px_to_m(px: float, py: float) -> tuple[float, float]:
    ox, oy = PX_ORIGIN
    return (px - ox) * M_PER_PX, (oy - py) * M_PER_PX


def m_to_px(x: float, y: float) -> tuple[float, float]:
    ox, oy = PX_ORIGIN
    return ox + x / M_PER_PX, oy - y / M_PER_PX
