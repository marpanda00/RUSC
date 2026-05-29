# RUSC live 3D viewer — baseline v1.0.0

Frozen reference build for **`http://localhost:3000/viewer/`** (Babylon.js + backend JSON/WebSocket).

## What works in this baseline

| Feature | Status |
|---------|--------|
| Sea + grid + sky (`clearColor`, no skybox flash) | OK |
| OpenStreetMap static ground patch (`osmGround` in live-config) | OK |
| Animated procedural water (`water` in live-config) | OK |
| GPS → local ENU from `live-config.json` origin | OK (`latitude`/`longitude` normalized) |
| Procedural boats + optional GLB/GLTF (`470`, AC75 teams) | OK |
| Wake strips (monohull / multihull) | OK |
| Device drawer (☰), association, calibration | OK |
| **Follow in 3D** (stable `camTarget`, no blue screen) | OK |
| **Reset 3D view** (stops follow, frames origin) | OK |
| `npm run simulate-gps` test loop | OK |

## Quick start

```powershell
cd backend
npm install
npm start
```

Second terminal:

```powershell
cd backend
npm run simulate-gps
```

Browser: **http://localhost:3000/viewer/** — hard refresh once after updates (`Ctrl+Shift+R`).

Verify files:

```powershell
cd backend
npm run verify-viewer
```

## Configuration

| File | Purpose |
|------|---------|
| `live-config.json` | `origin`, `osmGround`, `water`, `coastalBackdrop`, `boatFloatLiftM`, `seaExtentM` |
| `boat-models.json` | Model paths, hull type, scale |
| `boat-types.json` | Presets for device association |
| `boat-profiles.json` | Per-MAC saved names/colors/models |

Simulator and 3D scene both read `live-config.json`. Lat/lon in the ☰ menu should match origin ± ~50 m when using `simulate-gps`.

Optional 470 mesh:

```powershell
npm run sync-live-models
```

## Key source files (do not confuse with Virtual Eye)

| Path | Role |
|------|------|
| `public/viewer/index.html` | Live 3D page |
| `public/js/rusc-scene.js` | Babylon scene, boats, camera |
| `public/js/rusc-osm-ground.js` | OSM tile stitch + ground mesh |
| `public/js/rusc-water.js` | Procedural wave water shader |
| `public/js/rusc-device-ui.js` | Drawer UI |
| `public/js/rusc-wakes.js` | Wake meshes |
| `public/js/rusc-viewer-manifest.js` | Version + cache-bust query strings |
| `scripts/simulate-gps.js` | TCP :3001 test feed |
| `server.js` | HTTP :3000, WS :3002, `/api/live/config`, `/api/map/tiles/...` |

Recorded AC replay is **`/regatta-viewer/`** (different protocol, port 8080 binary WS).

## Console sanity check

After load you should see:

```text
[RUSC 3D] scene ready — 2 meshes, … origin 41.28228°N 13.21224°E (baseline 1.0.0-baseline)
[RUSC 3D] OSM ground: N tiles, z=…, 1000×1000 m, …×… px
```

`2 meshes` at first paint = water + grid; OSM ground adds a third mesh when tiles load.

## Cutting the next baseline

1. Bump `RuscViewer.version` in `public/js/rusc-viewer-manifest.js`.
2. Bump affected `RuscViewer.assets.*` numbers in the same file.
3. Update this doc and `VIEWERS.md`.
4. Run `npm run verify-viewer`.
5. Git tag, e.g. `live-viewer-1.0.0-baseline` (when you commit).
