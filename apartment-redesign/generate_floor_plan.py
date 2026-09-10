#!/usr/bin/env python3
"""Generate 2D floor plan images: current vs proposed 4-bedroom layout."""

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import Rectangle, FancyBboxPatch, Polygon
import matplotlib.lines as mlines
import os

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "output")

# Scale: 1 unit = 1 meter. Apartment footprint ~12.4m x 10.8m (interior).
# Coordinates: origin bottom-left of main envelope.

COLORS = {
    "bedroom": "#C8D8F0",
    "living": "#F5E6C8",
    "kitchen": "#FFE0B2",
    "bath": "#B8E0D2",
    "hall": "#E8E8E8",
    "balcony": "#D4EDDA",
    "wall": "#2C3E50",
    "door": "#8B4513",
    "window": "#87CEEB",
    "new_wall": "#E74C3C",
    "demolish": "#FF6B6B",
}

def draw_room(ax, x, y, w, h, label, color, fontsize=8, sublabel=None):
    rect = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02",
                          facecolor=color, edgecolor=COLORS["wall"], linewidth=1.5)
    ax.add_patch(rect)
    cy = y + h / 2
    ax.text(x + w/2, cy + (0.15 if sublabel else 0), label,
            ha="center", va="center", fontsize=fontsize, fontweight="bold", wrap=True)
    if sublabel:
        ax.text(x + w/2, cy - 0.25, sublabel, ha="center", va="center",
                fontsize=6, color="#555", style="italic")

def draw_balcony(ax, x, y, w, h):
    rect = Rectangle((x, y), w, h, facecolor=COLORS["balcony"],
                     edgecolor="#5a9e6f", linewidth=1, linestyle="--", alpha=0.7)
    ax.add_patch(rect)
    ax.text(x + w/2, y + h/2, "Balcone", ha="center", va="center", fontsize=5, color="#2d6a4f")

def draw_door(ax, x, y, w, h, swing="right"):
    ax.add_patch(Rectangle((x, y), w, h, facecolor="white", edgecolor=COLORS["door"], linewidth=1))

def draw_window(ax, x1, y1, x2, y2):
    ax.plot([x1, x2], [y1, y2], color=COLORS["window"], linewidth=4, solid_capstyle="round")

def draw_new_wall(ax, x1, y1, x2, y2):
    ax.plot([x1, x2], [y1, y2], color=COLORS["new_wall"], linewidth=3, linestyle="-")

def draw_demolish_wall(ax, x1, y1, x2, y2):
    ax.plot([x1, x2], [y1, y2], color=COLORS["demolish"], linewidth=2.5, linestyle="--")

def draw_outer_walls(ax, points):
    poly = Polygon(points, closed=True, fill=False, edgecolor=COLORS["wall"], linewidth=3)
    ax.add_patch(poly)

def generate_current_plan():
    fig, ax = plt.subplots(1, 1, figsize=(14, 11))
    ax.set_aspect("equal")
    ax.set_xlim(-1.5, 13.5)
    ax.set_ylim(-2.5, 12)
    ax.axis("off")
    ax.set_title("PIANO ATTUALE (3 camere + cucina separata)", fontsize=14, fontweight="bold", pad=15)

    # Outer envelope
    draw_outer_walls(ax, [(0, 0), (12.4, 0), (12.4, 10.8), (0, 10.8)])

    # Balconies
    draw_balcony(ax, -1.2, 8.5, 1.0, 2.0)   # top-left small
    draw_balcony(ax, -1.2, 3.5, 1.0, 2.5)   # mid-left
    draw_balcony(ax, 0, 10.9, 8.5, 0.9)     # top long
    draw_balcony(ax, 12.5, 6.0, 0.9, 2.5)   # right kitchen
    draw_balcony(ax, -1.2, 0.2, 1.0, 1.5)   # bottom-left WC

    # Bedroom 1 - top left
    draw_room(ax, 0, 7.8, 4.2, 3.0, "Camera 1", COLORS["bedroom"], sublabel="~12.6 m²")
    # Bedroom 2 - center top
    draw_room(ax, 4.2, 7.8, 3.8, 3.0, "Camera 2", COLORS["bedroom"], sublabel="~11.4 m²")
    # Bedroom 3 - mid left
    draw_room(ax, 0, 3.2, 3.5, 4.6, "Camera 3", COLORS["bedroom"], sublabel="~16.1 m²")

    # Bagno (fixed)
    draw_room(ax, 3.5, 4.8, 2.2, 2.8, "Bagno", COLORS["bath"], fontsize=7, sublabel="FISSO")
    # WC (fixed)
    draw_room(ax, 0, 0.8, 1.8, 2.4, "WC", COLORS["bath"], fontsize=7, sublabel="FISSO")

    # Hallway / ingresso
    draw_room(ax, 1.8, 0, 4.0, 3.2, "Ingresso\n+ Disimpegno", COLORS["hall"], fontsize=7)

    # Kitchen
    draw_room(ax, 8.8, 0, 3.6, 3.5, "Cucina", COLORS["kitchen"], sublabel="~12.6 m²")

    # Living - L shaped
    draw_room(ax, 5.8, 3.2, 6.6, 4.6, "Soggiorno\n+ Pranzo", COLORS["living"], fontsize=9, sublabel="~30 m²")
    draw_room(ax, 8.0, 7.8, 4.4, 3.0, "", COLORS["living"])

    # Internal walls (simplified)
    for seg in [(4.2, 3.2, 4.2, 10.8), (0, 7.8, 8.0, 7.8), (3.5, 3.2, 3.5, 7.6),
                (5.8, 3.2, 12.4, 3.2), (5.8, 3.2, 5.8, 7.8), (8.8, 0, 8.8, 3.5),
                (1.8, 3.2, 5.7, 3.2), (3.5, 4.8, 5.7, 4.8), (0, 3.2, 3.5, 3.2)]:
        ax.plot([seg[0], seg[2]], [seg[1], seg[3]], color=COLORS["wall"], linewidth=1.5)

    # Legend
    legend_items = [
        mpatches.Patch(color=COLORS["bedroom"], label="Camera"),
        mpatches.Patch(color=COLORS["living"], label="Soggiorno"),
        mpatches.Patch(color=COLORS["kitchen"], label="Cucina"),
        mpatches.Patch(color=COLORS["bath"], label="Bagno (fisso)"),
        mpatches.Patch(color=COLORS["balcony"], label="Balcone"),
    ]
    ax.legend(handles=legend_items, loc="lower right", fontsize=8, framealpha=0.9)

    ax.text(6.2, -1.8, "Scala di riferimento stimata · Misure indicative basate sulla planimetria catastale",
            ha="center", fontsize=8, color="#666")

    plt.tight_layout()
    path = os.path.join(OUTPUT_DIR, "01_current_floor_plan.png")
    fig.savefig(path, dpi=200, bbox_inches="tight", facecolor="white")
    plt.close()
    return path


def generate_proposed_plan():
    fig, ax = plt.subplots(1, 1, figsize=(14, 11))
    ax.set_aspect("equal")
    ax.set_xlim(-1.5, 13.5)
    ax.set_ylim(-2.5, 12)
    ax.axis("off")
    ax.set_title("PIANO PROPOSTO — 4 CAMERE DA LETTO", fontsize=14, fontweight="bold", pad=15,
                 color="#1a5276")

    draw_outer_walls(ax, [(0, 0), (12.4, 0), (12.4, 10.8), (0, 10.8)])

    # Balconies (unchanged)
    draw_balcony(ax, -1.2, 8.5, 1.0, 2.0)
    draw_balcony(ax, -1.2, 3.5, 1.0, 2.5)
    draw_balcony(ax, 0, 10.9, 8.5, 0.9)
    draw_balcony(ax, 12.5, 6.0, 0.9, 2.5)
    draw_balcony(ax, -1.2, 0.2, 1.0, 1.5)

    # 4 Bedrooms
    draw_room(ax, 0, 7.8, 4.2, 3.0, "Camera 1\n(Master)", COLORS["bedroom"], sublabel="~12.6 m²")
    draw_room(ax, 4.2, 7.8, 3.8, 3.0, "Camera 2", COLORS["bedroom"], sublabel="~11.4 m²")
    draw_room(ax, 0, 3.2, 3.5, 4.6, "Camera 3", COLORS["bedroom"], sublabel="~16.1 m²")
    draw_room(ax, 8.8, 0, 3.6, 3.5, "Camera 4\n(NUOVA)", "#A8C4E8", fontsize=8, sublabel="~12.6 m²\nex cucina")

    # Fixed bathrooms
    draw_room(ax, 3.5, 4.8, 2.2, 2.8, "Bagno", COLORS["bath"], fontsize=7, sublabel="FISSO")
    draw_room(ax, 0, 0.8, 1.8, 2.4, "WC", COLORS["bath"], fontsize=7, sublabel="FISSO")

    # Hallway
    draw_room(ax, 1.8, 0, 4.0, 3.2, "Ingresso", COLORS["hall"], fontsize=7)

    # Open plan living + kitchen
    draw_room(ax, 5.8, 3.2, 6.6, 7.6, "Soggiorno + Pranzo\n+ Cucina a vista", COLORS["living"],
              fontsize=9, sublabel="~50 m² open space")

    # Kitchen zone highlight within living
    kitchen_rect = FancyBboxPatch((5.9, 3.3), 2.8, 2.4, boxstyle="round,pad=0.02",
                                   facecolor=COLORS["kitchen"], edgecolor="#E67E22",
                                   linewidth=2, linestyle="--", alpha=0.85)
    ax.add_patch(kitchen_rect)
    ax.text(7.3, 4.5, "Cucina\nopen", ha="center", va="center", fontsize=7, fontweight="bold", color="#D35400")

    # New walls (red)
    draw_new_wall(ax, 8.8, 3.5, 8.8, 0)  # wall closing old kitchen door to hall
    # Demolished walls (dashed red)
    draw_demolish_wall(ax, 8.8, 3.5, 8.8, 0)
    draw_demolish_wall(ax, 5.8, 3.2, 8.8, 3.2)  # wall between hall and living removed

    # Remaining internal walls
    for seg in [(4.2, 3.2, 4.2, 10.8), (0, 7.8, 8.0, 7.8), (3.5, 3.2, 3.5, 7.6),
                (5.8, 3.2, 12.4, 3.2), (1.8, 3.2, 5.7, 3.2), (3.5, 4.8, 5.7, 4.8),
                (0, 3.2, 3.5, 3.2), (8.8, 0, 8.8, 3.5), (8.8, 3.5, 12.4, 3.5)]:
        ax.plot([seg[0], seg[2]], [seg[1], seg[3]], color=COLORS["wall"], linewidth=1.5)

    # Annotations for changes
    ax.annotate("", xy=(7.0, 2.8), xytext=(7.0, 1.5),
                arrowprops=dict(arrowstyle="->", color=COLORS["new_wall"], lw=2))
    ax.text(7.5, 2.0, "Cucina\nspostata", fontsize=7, color=COLORS["new_wall"], fontweight="bold")

    ax.annotate("", xy=(10.5, 1.8), xytext=(11.5, 2.8),
                arrowprops=dict(arrowstyle="->", color="#2980B9", lw=2))
    ax.text(11.0, 3.2, "Nuova\ncamera", fontsize=7, color="#2980B9", fontweight="bold")

    # Legend
    legend_items = [
        mpatches.Patch(color=COLORS["bedroom"], label="Camera"),
        mpatches.Patch(color="#A8C4E8", label="Camera 4 (nuova)"),
        mpatches.Patch(color=COLORS["living"], label="Soggiorno open"),
        mpatches.Patch(color=COLORS["kitchen"], label="Cucina a vista"),
        mpatches.Patch(color=COLORS["bath"], label="Bagno (fisso)"),
        mlines.Line2D([], [], color=COLORS["demolish"], linestyle="--", linewidth=2, label="Parete da demolire"),
        mlines.Line2D([], [], color=COLORS["new_wall"], linewidth=3, label="Nuova parete"),
    ]
    ax.legend(handles=legend_items, loc="lower right", fontsize=7, framealpha=0.9)

    ax.text(6.2, -1.8, "Proposta: cucina in open space · Ex cucina → Camera 4 con balcone · Bagni invariati",
            ha="center", fontsize=8, color="#666")

    plt.tight_layout()
    path = os.path.join(OUTPUT_DIR, "02_proposed_4bed_floor_plan.png")
    fig.savefig(path, dpi=200, bbox_inches="tight", facecolor="white")
    plt.close()
    return path


def generate_comparison():
    fig, axes = plt.subplots(1, 2, figsize=(20, 10))

    for ax, title, is_proposed in zip(axes, ["ATTUALE", "PROPOSTO 4 CAMERE"], [False, True]):
        ax.set_aspect("equal")
        ax.set_xlim(-1, 13)
        ax.set_ylim(-1, 11.5)
        ax.axis("off")
        ax.set_title(title, fontsize=12, fontweight="bold")

        draw_outer_walls(ax, [(0, 0), (12.4, 0), (12.4, 10.8), (0, 10.8)])

        # Simplified room blocks
        rooms_current = [
            (0, 7.8, 4.2, 3.0, "Cam 1", COLORS["bedroom"]),
            (4.2, 7.8, 3.8, 3.0, "Cam 2", COLORS["bedroom"]),
            (0, 3.2, 3.5, 4.6, "Cam 3", COLORS["bedroom"]),
            (3.5, 4.8, 2.2, 2.8, "Bagno", COLORS["bath"]),
            (0, 0.8, 1.8, 2.4, "WC", COLORS["bath"]),
            (1.8, 0, 4.0, 3.2, "Ingresso", COLORS["hall"]),
            (8.8, 0, 3.6, 3.5, "Cucina", COLORS["kitchen"]),
            (5.8, 3.2, 6.6, 7.6, "Soggiorno", COLORS["living"]),
        ]
        rooms_proposed = [
            (0, 7.8, 4.2, 3.0, "Cam 1", COLORS["bedroom"]),
            (4.2, 7.8, 3.8, 3.0, "Cam 2", COLORS["bedroom"]),
            (0, 3.2, 3.5, 4.6, "Cam 3", COLORS["bedroom"]),
            (8.8, 0, 3.6, 3.5, "Cam 4", "#A8C4E8"),
            (3.5, 4.8, 2.2, 2.8, "Bagno", COLORS["bath"]),
            (0, 0.8, 1.8, 2.4, "WC", COLORS["bath"]),
            (1.8, 0, 4.0, 3.2, "Ingresso", COLORS["hall"]),
            (5.8, 3.2, 6.6, 7.6, "Living+Kitchen", COLORS["living"]),
        ]
        rooms = rooms_proposed if is_proposed else rooms_current
        for x, y, w, h, label, color in rooms:
            rect = Rectangle((x, y), w, h, facecolor=color, edgecolor=COLORS["wall"], linewidth=1.2)
            ax.add_patch(rect)
            ax.text(x + w/2, y + h/2, label, ha="center", va="center", fontsize=7, fontweight="bold")

        if is_proposed:
            kr = Rectangle((5.9, 3.3), 2.8, 2.4, facecolor=COLORS["kitchen"],
                             edgecolor="#E67E22", linewidth=1.5, linestyle="--")
            ax.add_patch(kr)

    fig.suptitle("Confronto Layout — Prima e Dopo", fontsize=14, fontweight="bold", y=0.98)
    plt.tight_layout()
    path = os.path.join(OUTPUT_DIR, "03_comparison.png")
    fig.savefig(path, dpi=180, bbox_inches="tight", facecolor="white")
    plt.close()
    return path


if __name__ == "__main__":
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    p1 = generate_current_plan()
    p2 = generate_proposed_plan()
    p3 = generate_comparison()
    print(f"Generated:\n  {p1}\n  {p2}\n  {p3}")
