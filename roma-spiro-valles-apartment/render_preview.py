#!/usr/bin/env python3
"""Top-view and isometric previews of the traced apartment (no SketchUp required)."""

from __future__ import annotations

from pathlib import Path

import cv2
import numpy as np

from layout import CEILING, OPENINGS, PARAPET_H, ROOMS, SHAFT, WALLS

HERE = Path(__file__).resolve().parent
OUT = HERE / "assets"

ROOM_COLORS = {
    "Terrazzo sud": (82, 118, 186),
    "Terrazzo nord-ovest": (82, 118, 186),
    "Terrazzo nord": (82, 118, 186),
    "Vano 1": (118, 164, 198),
    "Vano 2": (112, 154, 186),
    "Vano 3 soggiorno": (164, 172, 176),
    "Vano 4 loggia": (148, 160, 168),
    "Vano 5": (122, 170, 204),
    "Bagno ovest": (222, 218, 214),
    "Bagno centrale": (222, 218, 214),
    "Disimpegno sud": (178, 190, 196),
    "Disimpegno": (178, 190, 196),
    "Passaggio terrazzo nord": (178, 190, 196),
    "Scala": (158, 164, 168),
    "Interno / cortile": (150, 188, 176),
}


def top_view(scale=42) -> np.ndarray:
    xs, ys = [], []
    for _, (x0, y0, x1, y1) in ROOMS.items():
        xs += [x0, x1]
        ys += [y0, y1]
    pad = 1.4
    x0, x1 = min(xs) - pad, max(xs) + pad
    y0, y1 = min(ys) - pad, max(ys) + pad
    w, h = int((x1 - x0) * scale), int((y1 - y0) * scale)
    img = np.full((h, w, 3), 245, np.uint8)

    def pt(x, y):
        return int((x - x0) * scale), int((y1 - y) * scale)

    for name, rec in ROOMS.items():
        a, b = pt(rec[0], rec[3]), pt(rec[2], rec[1])
        cv2.rectangle(img, a, b, ROOM_COLORS.get(name, (200, 200, 200)), -1)
        cv2.rectangle(img, a, b, (120, 120, 120), 1)
        cx, cy = pt((rec[0] + rec[2]) / 2, (rec[1] + rec[3]) / 2)
        label = (
            name.replace(" soggiorno", "")
            .replace(" / cortile", "")
            .replace("Passaggio terrazzo nord", "Passaggio")
            .replace("Disimpegno sud", "Corr.")
            .replace("nord-ovest", "NO")
        )
        cv2.putText(img, label, (cx - 4 * len(label), cy), cv2.FONT_HERSHEY_SIMPLEX, 0.38, (40, 40, 40), 1, cv2.LINE_AA)

    for kind, a0, b0, a1, b1 in WALLS:
        p0, p1 = pt(a0, b1), pt(a1, b0)
        col = (70, 70, 70) if kind != "parapet" else (140, 140, 160)
        cv2.rectangle(img, p0, p1, col, -1)

    for kind, a0, b0, a1, b1, *_ in OPENINGS:
        p0, p1 = pt(a0, b1), pt(a1, b0)
        cv2.rectangle(img, p0, p1, (80, 200, 255) if kind != "window" else (200, 220, 80), -1)

    sx0, sy0, sx1, sy1 = SHAFT
    cv2.rectangle(img, pt(sx0, sy1), pt(sx1, sy0), (60, 60, 60), 2)
    cv2.line(img, pt(sx0, sy0), pt(sx1, sy1), (60, 60, 60), 1)
    cv2.line(img, pt(sx0, sy1), pt(sx1, sy0), (60, 60, 60), 1)

    # north arrow (drawing north = -Y)
    ax, ay = pt(16.2, 1.4)
    cv2.circle(img, (ax, ay), int(0.55 * scale), (130, 70, 40), 2)
    cv2.arrowedLine(img, (ax, ay), (ax, ay + int(0.9 * scale)), (130, 70, 40), 2, tipLength=0.25)
    cv2.putText(img, "N", (ax - 8, ay + int(1.25 * scale)), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (130, 70, 40), 2)

    cv2.putText(img, "Via Spiro Valles, Roma  —  piano interno H 2.80 m  —  scala 1:100",
                (16, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (30, 30, 30), 1, cv2.LINE_AA)
    return img


def isometric(scale=36) -> np.ndarray:
    w, h = 1600, 1200
    img = np.full((h, w, 3), 248, np.uint8)

    def raw(x, y, z):
        px = (x - y) * scale * 0.92
        py = -((x + y) * 0.5 * scale * 0.55 + z * scale)
        return px, py

    pts = []
    for _, (x0, y0, x1, y1) in ROOMS.items():
        if _.startswith("Interno"):
            continue
        for x, y, z in ((x0, y0, 0), (x1, y0, 0), (x1, y1, CEILING), (x0, y1, CEILING)):
            pts.append(raw(x, y, z))
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    ox = w / 2 - (min(xs) + max(xs)) / 2
    oy = h / 2 - (min(ys) + max(ys)) / 2 + 40

    def proj(x, y, z):
        px, py = raw(x, y, z)
        return int(ox + px), int(oy + py)

    def quad(pts, color, outline=(50, 50, 50)):
        arr = np.array(pts, np.int32)
        cv2.fillConvexPoly(img, arr, color)
        cv2.polylines(img, [arr], True, outline, 1, cv2.LINE_AA)

    # floors
    for name, (x0, y0, x1, y1) in ROOMS.items():
        if name.startswith("Interno"):
            continue
        c = ROOM_COLORS.get(name, (200, 200, 200))
        quad([proj(x0, y0, 0), proj(x1, y0, 0), proj(x1, y1, 0), proj(x0, y1, 0)], c)

    # walls as extruded quads (top + two visible sides), skip parapets first pass
    for kind, x0, y0, x1, y1 in WALLS:
        z1 = PARAPET_H if kind == "parapet" else CEILING
        col = (200, 196, 188) if kind == "parapet" else ((210, 200, 185) if kind == "ext" else (228, 222, 210))
        # top
        quad([proj(x0, y0, z1), proj(x1, y0, z1), proj(x1, y1, z1), proj(x0, y1, z1)], tuple(int(v * 0.92) for v in col))
        # +X side
        quad([proj(x1, y0, 0), proj(x1, y1, 0), proj(x1, y1, z1), proj(x1, y0, z1)], tuple(int(v * 0.78) for v in col))
        # -Y side
        quad([proj(x0, y0, 0), proj(x1, y0, 0), proj(x1, y0, z1), proj(x0, y0, z1)], tuple(int(v * 0.7) for v in col))

    cv2.putText(img, "Roma, via Spiro Valles — modello 3D (H 2.80 m)",
                (30, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (30, 30, 30), 2, cv2.LINE_AA)
    cv2.putText(img, "Importa Roma_Via_Spiro_Valles.skp in SketchUp Web Free",
                (30, 72), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (80, 80, 80), 1, cv2.LINE_AA)
    return img


def main():
    OUT.mkdir(exist_ok=True)
    top = top_view()
    iso = isometric()
    cv2.imwrite(str(OUT / "preview_plan.png"), top)
    cv2.imwrite(str(OUT / "preview_iso.png"), iso)
    print("wrote", OUT / "preview_plan.png", top.shape)
    print("wrote", OUT / "preview_iso.png", iso.shape)


if __name__ == "__main__":
    main()
