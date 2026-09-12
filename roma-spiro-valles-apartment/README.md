# Roma, via Spiro Valles — SketchUp model

3D model of the cadastral apartment plan (Catasto Edilizio Urbano, Roma, via Spiro Valles, piano interno **H 2.80 m**, scala 1:100).

**Files to download:**

- [`Roma_Via_Spiro_Valles.zip`](Roma_Via_Spiro_Valles.zip) — easiest from GitHub (unzip, then open the `.skp`)
- [`Roma_Via_Spiro_Valles.skp`](Roma_Via_Spiro_Valles.skp) — SketchUp 2017 file for SketchUp Web Free

Direct links (use **Save link as…** if the browser tries to preview the file):

- Zip: https://github.com/marpanda00/RUSC/raw/cursor/roma-apartment-sketchup-66e2/roma-spiro-valles-apartment/Roma_Via_Spiro_Valles.zip
- SKP: https://github.com/marpanda00/RUSC/raw/cursor/roma-apartment-sketchup-66e2/roma-spiro-valles-apartment/Roma_Via_Spiro_Valles.skp

SketchUp Web Free only imports `.skp` (not STL/DAE/OBJ).

## How to import (SketchUp Web Free)

1. Unzip `Roma_Via_Spiro_Valles.zip` if you downloaded the zip.
2. Open [app.sketchup.com](https://app.sketchup.com) and sign in with a free Trimble account.
3. On the home screen choose **Open** → **Computer** (or drag the `.skp` onto the page).
4. Select `Roma_Via_Spiro_Valles.skp`.

If the model looks huge or tiny, set units to metres: **Window / Model info → Units → Meters** (or the Web equivalent under the model info panel). Internal coordinates are real metres stored in SketchUp’s native inches, so **2.80 m walls should read as 2.80 m**.

## What is in the model

| Group | Contents |
|---|---|
| 01 Murature | Exterior (~30 cm) and interior (~15 cm) walls, with door and window openings |
| 02 Parapetti terrazzi | Terrace railings, 1.10 m |
| 03 Pavimenti | Room floors, colour-coded |
| 04 Porte | Door leaves, slightly open as on the cadastral drawing |
| 05 Vetri terrazzi | Glass in porta-finestre and windows |
| 06 Scala | U-stair to the upper landing |
| 07 Vano ascensore | Lift / technical shaft |
| 08 Soffitti | Ceilings (layer **hidden** by default) |
| 09 Solaio | Floor slab |
| 10 Planimetria di riferimento | Scanned cadastral sheet under the model (layer **hidden** by default — turn it on to compare) |
| 11 Orientamento Nord | North arrow. On the sheet, north points **down the page**, so in the model geographic north is **−Y** |

Room numbers follow the cadastral vani (1–5), plus the two bathrooms, disimpegno, terraces, stair, and the *interno* courtyard.

Turn layers on/off from the **Tags / Layers** panel. Ceilings start hidden so you can look into the rooms from above.

## Accuracy

The model is traced from the scan of the planimetria, not from a measured survey. Wall positions match the drawing topology (rooms, doors, terraces, stair, shaft). Expect roughly **±10–20 cm** versus the paper original. Use it as a working SketchUp massing you can refine, not as a legal cadastral document.

## Regenerating the file

```bash
pip install openskp pillow opencv-python-headless numpy
python3 build_skp.py
python3 render_preview.py
```

`layout.py` holds the metre coordinates. `assets/planimetria.png` is the source sheet.
