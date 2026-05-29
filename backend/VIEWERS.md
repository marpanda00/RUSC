# RUSC viewers

**Live 3D baseline (v1.0.0):** see [LIVE_VIEWER_BASELINE.md](./LIVE_VIEWER_BASELINE.md) — run `npm run verify-viewer` before tagging or deploying.

## Quick start (live 3D)

```powershell
cd C:\Code\RUSC\backend
npm start
```

In another terminal:

```powershell
cd C:\Code\RUSC\backend\public
npx serve . -l 8080
```

Open **http://localhost:8080/viewer/** (not `npx serve` inside `viewer/` — that breaks `/js/rusc-wakes.js`).

### 3D boat models (GLB / GLTF)

```powershell
cd backend
npm run sync-live-models
```

Copies `470.glb` and AC75 team folders from `regatta-viewer/models` into `public/models/`. Proprietary assets — dev/personal use only.

### Simulate live data (no hardware)

**RUSC live viewer** (`/viewer/`, JSON WebSocket on port 3002):

```powershell
cd backend
npm start
# second terminal:
npm run simulate-gps
```

Default center **41.282284°N, 13.212244°E** in `live-config.json`. `simulate-gps` orbits ~40 m around that point; lat/lon in the ☰ menu should match (± orbit). Override: `node scripts/simulate-gps.js --lat 41.282284 --lon 13.212244`.

**OpenStreetMap ground (live `/viewer/`):** static tile mosaic (~1000×1000 m by default) centered on `origin`, stitched at load. Configure in `live-config.json` → `osmGround` (`enabled`, `widthM`, `heightM`, `zoom`: `"auto"` or 10–19). Tiles are fetched via **`GET /api/map/tiles/:z/:x/:y.png`** (backend proxy to OSM with cache). OSM **attribution** appears bottom-right when enabled. Takes precedence over `coastalBackdrop`; set `"coastalBackdrop.enabled": false` when using OSM.

**Coastal backdrop (fallback):** optional photo draped **flat on the ground** (700×700 m by default). Configure in `live-config.json` → `coastalBackdrop` (image under `public/viewer/textures/`). Used when `osmGround.enabled` is false or tile load fails.

**Boat height on water:** tune `boatFloatLiftM` in `live-config.json` (meters, applied live after refresh). Auto mesh align at load uses `WATERLINE_CLEARANCE_M` / `WATERLINE_HULL_FRAC` in `rusc-scene.js` (requires hard-refresh + boats recreated).

**Animated water (live `/viewer/`):** procedural waves via `live-config.json` → `water` (`waveHeightM`, `waveSpeed`, `opacity`, `sizeM`, `subdivisions`). Set `"enabled": false` for the flat blue plane. Wire grid is hidden when animated water is active.

**Virtual Eye regatta viewer** (`/regatta-viewer/`, binary WebSocket on port 8080):

```powershell
node C:\Code\RUSC\Sailinig_3D_Viewer\scripts\simulate-live-telemetry.js
# serve viewer (from backend/public or Sailinig_3D_Viewer/viewer)
```

`localconfig.json` must use `"websocketUrl": "ws://localhost:8080"` (not 3002).

---

The backend serves **two** 3D experiences:

| URL | Purpose |
|-----|---------|
| `/viewer/` | **Live** GPS boats (`rusc-scene.js` + `rusc-wakes.js`) |
| `/regatta-viewer/` | **Recorded** AC-style replay (Virtual Eye bundles, `.bin` races) |

## Hull type / wake foam

Both viewers support **monohull** vs **multihull** wake strips.

### Live viewer (`/viewer/`)

Configured in `boat-models.json` and `boat-types.json`:

```json
"470": {
  "hullType": "monohull",
  "glb": "/models/470.glb"
}
```

Device profiles inherit `hullType` from the boat type preset or model catalog (`PATCH /api/boats/:mac`).

### Recorded viewer (`/regatta-viewer/`)

Configured per team in `public/regatta-viewer/appconfig.json`:

```json
"boatmodel": {
  "name": "470.glb",
  "hullType": "monohull",
  "leftfoil": "",
  "rightfoil": ""
}
```

See `Sailinig_3D_Viewer/README.md` for the full wake table.

## `rusc-wakes.js failed to load`

You started the static server in `public/viewer/` instead of `public/`. Scripts are in `public/js/`.

Fix: `cd backend/public` then `npx serve .` and open `/viewer/`.

---

## Black screen / missing ocean?

The recorded viewer needs **textures**, **skybox**, **terrain**, and **`.bin` race files** (not in git).

```bash
npm run download-regatta-assets
```

Then hard-refresh the page (Ctrl+Shift+R). Open `/regatta-viewer/`, pick **acws2020**, then a race.

`localconfig.json` must use the AC `raceconfig.json` shape (`eventIds`, not a custom `events` array).

## Sync recorded viewer from Sailinig_3D_Viewer

After updating the upstream viewer project:

```bash
node scripts/sync-regatta-viewer.js
```

Or from a custom path:

```bash
node scripts/sync-regatta-viewer.js "C:\Code\RUSC\Sailinig_3D_Viewer\viewer"
```

This copies bundles, applies `patch-hull-wakes.js`, sets `raceConfigUrl` to `localconfig.json`, and copies `470.glb` into `public/models/`.

## Offline race list

`public/regatta-viewer/localconfig.json` points at `racedata/` under the regatta viewer. Add your own `.bin` files and `RacesList.dat` as documented in the Sailinig project.
