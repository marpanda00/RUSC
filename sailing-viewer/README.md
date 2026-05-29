# RUSC Sailing Viewer (source)

Production 3D viewer is served from `backend/public/viewer/` (static HTML + Babylon CDN).

This folder is reserved for a future Vite/TypeScript build. To develop locally:

```bash
cd backend
npm install
npm start
```

Open:

- http://localhost:3000/index.html — 2D tracker + device association menu
- http://localhost:3000/viewer/ — 3D regatta viewer
- ws://localhost:3002 — live boat updates

Add `.glb` models under `backend/public/models/` per `boat-models.json`.
