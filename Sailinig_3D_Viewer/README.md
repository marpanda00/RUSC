# Sailboat 3D Regatta Visualizer — Knowledge Transfer Package

## What This Is

This package contains everything needed to run and extend the **Virtual Eye** AC36 America's Cup
3D sailing race visualizer **fully offline**, plus all the knowledge to build your own regatta
visualizer on top of it.

The original viewer is at `https://dx6j99ytnx80e.cloudfront.net/` — a proprietary system by
**Animation Research Limited (ARL)**, Dunedin, New Zealand. This package was assembled by
reverse-engineering the public-facing assets for **educational/personal use only**.

---

## Package Contents

```
sailboat_3d_package/
├── README.md                    ← this file (start here)
├── download-assets.ps1          ← PowerShell script to download all viewer assets
├── viewer/                      ← complete offline viewer (serve with npx serve .)
│   ├── index.html
│   ├── appconfig.json           ← EDIT THIS to customize teams/boats/flags
│   ├── bundle.js                ← main app
│   ├── vendors~main.bundle.js   ← React + utilities
│   ├── 0.bundle.js              ← Babylon.js core (~1.6MB)
│   ├── 3.bundle.js              ← RaceScene + all custom shaders
│   ├── 4.bundle.js              ← Babylon.js extended
│   ├── 5.bundle.js              ← Babylon.js entry shim
│   ├── models/                  ← AC75 boat GLTFs + terrain + buoys
│   ├── textures/                ← water, foam, skybox, wake textures
│   ├── racedata/                ← race telemetry .bin files
│   │   ├── acws2020/
│   │   ├── prada2021/
│   │   └── ac2021/
│   └── [hashed].svg/png/otf     ← UI icons and fonts
├── shaders/                     ← extracted GLSL source (for your own viewer)
│   ├── waterShader.vert.glsl    ← animated ocean surface vertex shader
│   ├── waterShader.frag.glsl    ← full PBR water fragment shader
│   ├── wakeShader.vert.glsl     ← boat wake vertex shader
│   ├── wakeShader.frag.glsl     ← boat wake fragment shader
│   ├── trailShader.vert.glsl    ← boat trail line vertex shader
│   └── trailShader.frag.glsl    ← boat trail line fragment shader
└── ws-relay/                    ← Node.js WebSocket relay for LIVE data
    ├── package.json
    ├── server.js                ← relay server (devices → viewer)
    └── packet-encoder.js        ← binary packet encoder
```

---

## How to Run the Viewer (Offline)

### Prerequisites
- Node.js (any version with `npx`)

### Steps
```powershell
cd viewer
npx serve .
# Open http://localhost:3000
```

**Important:** run `serve` from the `viewer/` folder, not the package root. Serving the root only shows a folder listing (or a redirect to `viewer/`). The app needs `appconfig.json`, `bundle.js`, and `localconfig.json` in the URL root.

The viewer loads the race menu automatically. Click any race to watch the 3D replay.

If you only see the Virtual Eye splash and it never advances: `viewer/localconfig.json` must list offline events (e.g. `"eventIds": ["acws2020"]`) for recorded replay, **or** run the live WebSocket relay (`ws-relay/`) when `eventIds` is empty.

### If assets are missing
Run the downloader to re-fetch everything from CloudFront:
```powershell
.\download-assets.ps1
```
It skips files already present, so it is safe to re-run.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                    BROWSER (index.html)                  │
│                                                          │
│  ┌─────────────┐    ┌──────────────────────────────┐    │
│  │  React UI   │    │    Babylon.js 3D Scene        │    │
│  │  (overlay)  │    │                              │    │
│  │  - Menu     │    │  - ArcRotateCamera            │    │
│  │  - Controls │    │  - HemisphericLight           │    │
│  │  - Leaderbd │    │  - Water plane (ShaderMat)    │    │
│  │  - Timeline │    │  - Skybox (CubeTexture)       │    │
│  └─────────────┘    │  - Boat meshes (.gltf)        │    │
│                     │    ├ morph targets (sails)    │    │
│                     │    ├ foil bones               │    │
│                     │    └ wake mesh (ShaderMat)    │    │
│                     │  - Buoy models (.gltf)        │    │
│                     │  - Terrain (.glb)             │    │
│                     │  - Boat trails (ShaderMat)    │    │
│                     │  - Wind arrows (sprites)      │    │
│                     │  - Start/finish line          │    │
│                     └──────────────────────────────┘    │
│                                    ▲                     │
│                         Catmull-Rom spline interpolation │
│                         (smooths 1-10Hz → 60fps)        │
└──────────────────────────┬──────────────────────────────┘
                           │
              ┌────────────┴────────────┐
              │                         │
        PLAYBACK mode              LIVE mode
        fetch(*.bin)               WebSocket
        → ArrayBuffer              binaryType="arraybuffer"
        → parse packets            → parse packets
```

---

## Binary Packet Protocol

Race data is transmitted as a **DLE-framed binary stream** (both in `.bin` files and WebSocket).

### Framing
| Bytes | Meaning |
|-------|---------|
| `0x10` | Start of packet (DLE) |
| `0x10 0x03` | End of packet (DLE ETX) |
| `0x10 0x10` | Escaped literal `0x10` inside payload |

### Packet Types
| ID (hex) | ID (dec) | Name |
|----------|----------|------|
| `0xB3` | 179 | Boat position |
| `0xB2` | 178 | Wind |
| `0xB1` | 177 | Course info / race status |
| `0xB9` | 185 | Course boundary polygon |
| `0xB6` | 182 | Penalty |
| `0xBE` | 190 | Wind point (local) |

### Boat Packet Layout (version 8)
```
[0x10]           frame start
[0xB3]           type = boat
[0x08]           version = 8
[raceId   : u16] race identifier
[boatId   : u16] encoded: bit15=trailColor, bits6-14=teamId, bits0-5=yachtId
[time     : u32] seconds × 100
[lat      : i32] WGS84 latitude  × 1e7
[lon      : i32] WGS84 longitude × 1e7
[elevation: u16] (metres × 1000) + 32768
[heading  : u16] degrees × 100
[heel     : u16] (degrees + 180) × 100
[pitch    : u16] (degrees + 180) × 100
[sails    : u8 ] sail state bitmask
[status   : u8 ] race status
[speed    : u16] knots × 100
[dtl      : u24] distance-to-leader, metres × 1000
[flyTime  : u16] (seconds + 100) × 100
[skip     : 3  ] reserved
[rank     : u8 ]
[currentLeg:u8 ]
[foilState: u8 ] 0=down, 1=foiling
[rudderAngle:u8] degrees + 90
[0x10][0x03]     frame end
```

### boatId Encoding
```javascript
const boatId = (trailColor << 15) | (teamId << 6) | yachtId;
// teamId must match a team_id in appconfig.json
// yachtId = 0-63 (boat number within team)
// trailColor = 0 or 1 (trail line color variant)
```

---

## Customization Guide

### 1. Edit Teams, Names, Flags — `viewer/appconfig.json`

```json
{
  "teams": [
    {
      "team_id": 1,
      "name": "My Sailing Club",
      "abbr": "MSC",
      "flag_id": "",          // "" = show color dot; "nz"/"it"/"uk"/"usa" = flag image
      "color": "#FF6600",     // hull tint color + UI color
      "boatmodel": {
        "name": "myboat/sailboat.gltf",
        "hullType": "monohull",   // "monohull" | "multihull" (see wake table below)
        "topMastOffset": {"x": 0, "y": 20, "z": 2},
        "defaultbowoffset": 8.0,
        "jibtarget": "",          // morph target name in GLTF, or "" to skip
        "mainsailtarget": "",
        "leftfoil": "",           // foil bone name, or "" if no foils
        "rightfoil": ""
      }
    }
  ]
}
```

### 2. Monohull vs multihull wake foam (`hullType`)

Wake foam is **procedural** (not part of the GLTF). Per team, set `boatmodel.hullType`:

| `hullType` | `leftfoil` / `rightfoil` | Wake strips |
|------------|--------------------------|-------------|
| `"monohull"` | empty | 1 stern wake |
| `"multihull"` | both set (AC75) | stern + port foil + starboard foil + bow |
| `"multihull"` | both empty | stern + port hull + starboard hull (no foil telemetry) |

If `hullType` is omitted: both foil names set → multihull foiling; both empty → monohull.

`leftfoil` / `rightfoil` only drive **foil bone animation** on the model; they do not disable wakes by themselves.

Fleet replays still show wakes for **every boat** in the `.bin` — only teams you configure as monohull get a single stern strip.

### 3. Swap 3D Boat Model
- Export any sailboat as `.gltf` or `.glb` (Blender, Sketchfab, etc.)
- Put it under `viewer/models/myteam/myboat.gltf`
- Update `boatmodel.name` in `appconfig.json`
- Free AC yacht models: search Sketchfab for "sailing yacht" (CC licensed)

### 4. Add a Race Listing
Create `viewer/racedata/myevent/RacesList.dat`:
```
DataFile	= MyRace_1.bin
RaceNameA	= Race 1
RaceInfoA	= My Venue
RaceInfoB	= Fleet Race
Date		= Jun 01
DayOfWeek	= Saturday
Time		= 14:00
Y1		= 0
```
Also create `viewer/racedata/myevent/MatchInfoTitles.txt` (can be empty).
Drop your `.bin` file in the same folder.

The viewer fetches race listings from the URLs in `raceconfig.json` (fetched from
`https://ac36.americascup.com/en/feed/raceconfig.json` by default). To go fully offline, replace
`appconfig.json`'s `raceConfigUrl` with a local JSON file:
```json
{ "raceConfigUrl": "localconfig.json" }
```

---

## Live Data: WebSocket Relay Server

See `ws-relay/server.js` for a complete Node.js server that:
1. Accepts device JSON telemetry (HTTP POST or UDP)
2. Encodes it into the binary packet format
3. Broadcasts to all connected viewer WebSocket clients

### Quick start
```bash
cd ws-relay
npm install
node server.js
# optional demo boats:
node server.js --demo
# or from package root:
node scripts/simulate-live-telemetry.js
```

Use **only** the binary relay/simulator on port 8080. The RUSC backend live WebSocket (port 3002, JSON) is a different protocol and will cause `failed to parse packet` errors if the viewer is pointed at it.

Set the viewer's WebSocket URL by modifying `appconfig.json`:
```json
{ "websocketUrl": "ws://localhost:8080" }
```
*(Note: WEBSOCKET_URL is an env var baked into the bundle — for local testing you can intercept
the WebSocket connection by patching the JS, or rebuild with env vars set.)*

---

## Device Telemetry → Viewer Mapping

| IoT Sensor | Binary Packet Field | Notes |
|---|---|---|
| GPS latitude | `lat` | WGS84, multiply by 1e7 |
| GPS longitude | `lon` | WGS84, multiply by 1e7 |
| GPS speed | `speed` | Convert m/s → knots (÷ 0.5144) |
| Compass / GPS heading | `heading` | 0-360° |
| IMU roll | `heel` | -180 to +180° |
| IMU pitch | `pitch` | -180 to +180° |
| Barometer (optional) | `elevation` | metres above sea level |
| Timestamp | `time` | Unix seconds × 100 |

**Update rate:** 1 Hz is sufficient. The viewer's Catmull-Rom spline interpolator renders
smooth 60fps motion between sparse position updates automatically.

---

## Water Shader Breakdown (`shaders/waterShader.frag.glsl`)

The ocean surface uses 8 texture units (WebGL maximum):

| Uniform | Texture | Purpose |
|---|---|---|
| `bumpMap` | `RippledWater_Greyscale_NRM_F_NRM.png` | Primary wave normals |
| `rippleBump` | same | Detail ripple normals |
| `heightMap` | `RippledWater_Greyscale_NRM_F_DISP.png` | Parallax height |
| `waterFoam` | `communityWaterFoam.png` | Whitecap foam |
| `cubeMap` | `ClearBlue_SunLess/ClearBlue_*.jpg` | Skybox reflection (6 faces) |
| `arrowTexture` | `windarrow-02.png` | Wind direction arrows |
| `waterFresnel` | `waterFresnel.png` | 1D Fresnel LUT |
| `underwaterTexture` | `WaterBaseLayer.png` | Seafloor / underwater base |

Animated by uniforms updated every frame:
- `time` — drives UV scrolling speed
- `windAngle` — rotates arrow sprites
- `windSpeed` — controls foam/wave intensity
- `windAlpha` — fades arrows in/out

---

## Technology Stack

| Component | Technology | License |
|---|---|---|
| 3D Engine | Babylon.js | Apache 2.0 ✅ |
| UI Framework | React | MIT ✅ |
| Coordinate math | utm-converter (npm) | MIT ✅ |
| Interpolation | Custom Catmull-Rom | Part of viewer |
| Water shader | Custom GLSL | Part of viewer |
| Viewer app | Proprietary (ARL/Virtual Eye) | ⚠️ Personal use only |
| Race data | Proprietary (AC Event Authority) | ⚠️ Personal use only |
| 3D models | Proprietary (ARL) | ⚠️ Personal use only |

**For a commercial product:** use Babylon.js + your own models + your own shaders (the GLSL
techniques — parallax mapping, Fresnel, cubemap reflections — are standard industry methods,
not proprietary).

---

## Recommended Next Steps

1. **Run the viewer** — `cd viewer && npx serve .` — watch a race to understand the UX
2. **Try live mode** — run `ws-relay/server.js`, feed it GPS data, see boats move in real-time
3. **Swap boat model** — download a free sailboat .glb from Sketchfab, drop it in `models/`
4. **Build your own viewer** — use Babylon.js from scratch, copy the shader patterns, add your
   device data pipeline

---

## Source / Credits

- Original viewer: Animation Research Limited (ARL) / Virtual Eye — https://www.virtualeye.com
- AC36 data reference: https://github.com/dorox/ac36
- Babylon.js: https://www.babylonjs.com
- GLSL parallax technique: https://learnopengl.com/Advanced-Lighting/Parallax-Mapping
