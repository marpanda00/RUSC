#!/usr/bin/env python3
"""Top-view and isometric renderings of the modified apartment."""

from __future__ import annotations

from pathlib import Path

import cv2
import numpy as np

from layout import CEILING, OPENINGS, PARAPET_H, ROOMS, SHAFT, WALLS, wall_solids

HERE = Path(__file__).resolve().parent
OUT = HERE / "assets"

ROOM_COLORS = {
    "Terrazzo sud": (82, 118, 186),
    "Terrazzo nord-ovest": (82, 118, 186),
    "Terrazzo nord": (82, 118, 186),
    "Camera 1": (118, 164, 198),
    "Camera 5": (122, 170, 204),
    "Sala angolo cottura": (96, 168, 220),
    "Camera sud": (110, 140, 196),
    "Camera nord": (100, 132, 188),
    "Camera nord (lato bagno)": (100, 132, 188),
    "Bagno ovest": (222, 218, 214),
    "Bagno centrale": (222, 218, 214),
    "Corridoio ovest": (178, 190, 196),
    "Disimpegno centrale": (178, 190, 196),
    "Disimpegno A": (168, 184, 200),
    "Disimpegno B": (158, 176, 196),
    "Scala": (158, 164, 168),
    "Interno / cortile": (150, 188, 176),
}

SHORT = {
    "Terrazzo nord-ovest": "Terrazzo NO",
    "Sala angolo cottura": "Sala cottura",
    "Camera nord (lato bagno)": "",
    "Disimpegno centrale": "Disimpegno",
    "Corridoio ovest": "Corr.",
    "Interno / cortile": "Interno",
    "Bagno centrale": "Bagno",
    "Bagno ovest": "Bagno",
    "Disimpegno A": "Dis. A",
    "Disimpegno B": "Dis. B",
}


def top_view(scale=42) -> np.ndarray:
    xs, ys = [], []
    for _, (x0, y0, x1, y1) in ROOMS.items():
        xs += [x0, x1]
        ys += [y0, y1]
    pad = 1.4
    x0, x1 = min(xs) - pad, max(xs) + pad
    y0, y1 = min(ys) - pad, max(ys) + pad
    w, h = int((x1 - x0) * scale) + 8, int((y1 - y0) * scale) + 90
    img = np.full((h, w, 3), 245, np.uint8)

    def pt(x, y):
        return int((x - x0) * scale), int((y1 - y) * scale) + 56

    for name, rec in ROOMS.items():
        a, b = pt(rec[0], rec[3]), pt(rec[2], rec[1])
        cv2.rectangle(img, a, b, ROOM_COLORS.get(name, (200, 200, 200)), -1)
        cv2.rectangle(img, a, b, (120, 120, 120), 1)
        label = SHORT.get(name, name)
        if not label:
            continue
        cx, cy = pt((rec[0] + rec[2]) / 2, (rec[1] + rec[3]) / 2)
        cv2.putText(img, label, (cx - 4 * len(label), cy), cv2.FONT_HERSHEY_SIMPLEX, 0.36, (40, 40, 40), 1, cv2.LINE_AA)

    for kind, a0, b0, a1, b1 in WALLS:
        p0, p1 = pt(a0, b1), pt(a1, b0)
        if kind == "new":
            col = (40, 40, 200)
        elif kind == "parapet":
            col = (140, 140, 160)
        else:
            col = (70, 70, 70)
        cv2.rectangle(img, p0, p1, col, -1)

    for kind, a0, b0, a1, b1, *_ in OPENINGS:
        p0, p1 = pt(a0, b1), pt(a1, b0)
        col = (80, 200, 255) if kind == "door" else ((60, 200, 90) if kind == "window" else (40, 180, 255) if kind == "pf" else (220, 220, 220))
        cv2.rectangle(img, p0, p1, col, -1)

    sx0, sy0, sx1, sy1 = SHAFT
    cv2.rectangle(img, pt(sx0, sy1), pt(sx1, sy0), (60, 60, 60), 2)
    cv2.line(img, pt(sx0, sy0), pt(sx1, sy1), (60, 60, 60), 1)
    cv2.line(img, pt(sx0, sy1), pt(sx1, sy0), (60, 60, 60), 1)

    ax, ay = pt(16.2, 1.4)
    cv2.circle(img, (ax, ay), int(0.55 * scale), (130, 70, 40), 2)
    cv2.arrowedLine(img, (ax, ay), (ax, ay + int(0.9 * scale)), (130, 70, 40), 2, tipLength=0.25)
    cv2.putText(img, "N", (ax - 8, ay + int(1.25 * scale)), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (130, 70, 40), 2)

    cv2.putText(img, "Via Spiro Valles  —  progetto modificato  —  H 2.80 m",
                (16, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.52, (30, 30, 30), 1, cv2.LINE_AA)
    cv2.putText(img, "Rosso = nuove murature    Ciano = porte    Verde = finestre",
                (16, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.42, (80, 80, 80), 1, cv2.LINE_AA)
    return img


def isometric(scale=34, size=(1600, 1200), title="Progetto modificato — vista 3D") -> np.ndarray:
    w, h = size
    img = np.full((h, w, 3), 248, np.uint8)

    def raw(x, y, z):
        px = (x - y) * scale * 0.92
        py = -((x + y) * 0.5 * scale * 0.55 + z * scale)
        return px, py

    pts = []
    for name, (x0, y0, x1, y1) in ROOMS.items():
        if name.startswith("Interno"):
            continue
        for x, y, z in ((x0, y0, 0), (x1, y0, 0), (x1, y1, CEILING), (x0, y1, CEILING)):
            pts.append(raw(x, y, z))
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    ox = w / 2 - (min(xs) + max(xs)) / 2
    oy = h / 2 - (min(ys) + max(ys)) / 2 + 50

    def proj(x, y, z):
        px, py = raw(x, y, z)
        return int(ox + px), int(oy + py)

    def quad(corners, color, outline=(50, 50, 50)):
        arr = np.array(corners, np.int32)
        cv2.fillConvexPoly(img, arr, color)
        cv2.polylines(img, [arr], True, outline, 1, cv2.LINE_AA)

    for name, (x0, y0, x1, y1) in ROOMS.items():
        if name.startswith("Interno"):
            continue
        c = ROOM_COLORS.get(name, (200, 200, 200))
        quad([proj(x0, y0, 0), proj(x1, y0, 0), proj(x1, y1, 0), proj(x0, y1, 0)], c)

    # walls with door/window holes
    for kind, x0, y0, x1, y1 in WALLS:
        z1 = PARAPET_H if kind == "parapet" else CEILING
        if kind == "new":
            col = (96, 110, 196)
        elif kind == "parapet":
            col = (200, 196, 188)
        elif kind == "ext":
            col = (210, 200, 185)
        else:
            col = (228, 222, 210)
        for sx0, sy0, sx1, sy1, sz0, sz1 in wall_solids(x0, y0, x1, y1, 0.0, z1, OPENINGS):
            quad([proj(sx0, sy0, sz1), proj(sx1, sy0, sz1), proj(sx1, sy1, sz1), proj(sx0, sy1, sz1)],
                 tuple(int(v * 0.92) for v in col))
            quad([proj(sx1, sy0, sz0), proj(sx1, sy1, sz0), proj(sx1, sy1, sz1), proj(sx1, sy0, sz1)],
                 tuple(int(v * 0.78) for v in col))
            quad([proj(sx0, sy0, sz0), proj(sx1, sy0, sz0), proj(sx1, sy0, sz1), proj(sx0, sy0, sz1)],
                 tuple(int(v * 0.70) for v in col))

    # glass in window / porta-finestra openings
    for kind, x0, y0, x1, y1, z0, z1 in OPENINGS:
        if kind not in ("pf", "window"):
            continue
        gcol = (210, 200, 140)
        dx, dy = x1 - x0, y1 - y0
        t = 0.03
        if dx >= dy:
            ym = (y0 + y1) / 2
            quad([proj(x0, ym - t, z0), proj(x1, ym - t, z0), proj(x1, ym - t, z1), proj(x0, ym - t, z1)], gcol, (160, 140, 80))
        else:
            xm = (x0 + x1) / 2
            quad([proj(xm + t, y0, z0), proj(xm + t, y1, z0), proj(xm + t, y1, z1), proj(xm + t, y0, z1)], gcol, (160, 140, 80))

    cv2.putText(img, title, (30, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.68, (30, 30, 30), 2, cv2.LINE_AA)
    cv2.putText(img, "Muri rossi = nuove tramezze    aperture = porte e finestre",
                (30, 70), cv2.FONT_HERSHEY_SIMPLEX, 0.48, (80, 80, 80), 1, cv2.LINE_AA)
    return img


def main():
    OUT.mkdir(exist_ok=True)
    top = top_view()
    iso = isometric()
    cv2.imwrite(str(OUT / "preview_plan.png"), top)
    cv2.imwrite(str(OUT / "preview_iso.png"), iso)
    cv2.imwrite(str(OUT / "rendering_plan.png"), top)
    cv2.imwrite(str(OUT / "rendering_3d.png"), iso)
    print("wrote", OUT / "preview_plan.png", top.shape)
    print("wrote", OUT / "preview_iso.png", iso.shape)


if __name__ == "__main__":
    main()
