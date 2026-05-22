# 3D Live Viewer

Scripts and styles live in the parent folder (`public/js`, `public/css`), not inside `viewer/`.

## Run (recommended)

From **`backend/public`** (not `viewer/`):

```powershell
cd C:\Code\RUSC\backend\public
npx serve .
```

Open: **http://localhost:3000/viewer/**

The backend API must be running separately (`cd backend` → `npm start`) on port **3000** for REST/WS — if `serve` also uses 3000, use another port:

```powershell
npx serve . -l 8080
```

Then open http://localhost:8080/viewer/ and set API host via the same machine (device UI uses `window.location.hostname` and port 3000 for HTTP API).

## Wrong: `npx serve .` inside `viewer/`

That serves only this folder, so `/js/rusc-wakes.js` is missing and you see **"rusc-wakes.js failed to load"**.

## From `viewer/` folder only

Serve the parent `public` directory:

```powershell
cd C:\Code\RUSC\backend\public\viewer
npx serve .. -l 8080
```

Open http://localhost:8080/viewer/
