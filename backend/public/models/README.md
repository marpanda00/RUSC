# 3D boat models (live viewer)

Register models in `backend/boat-models.json`. Each entry can use:

- `"glb": "/models/foo.glb"` — single-file mesh
- `"gltf": "/models/Team/foo.gltf"` — needs `.bin` and textures in the **same folder**
- `"procedural": true` — built-in box + mast (no file)

Set **`hullType`**: `"monohull"` or `"multihull"` (wake strips).

## Copy assets from regatta-viewer

```powershell
cd backend
npm run sync-live-models
```

This copies `470.glb`, AC75 team folders, and related files from `public/regatta-viewer/models` (or `Sailinig_3D_Viewer/viewer/models`) into this directory.

If the script warns about missing `.bin` / `.png` files, run:

```powershell
npm run download-regatta-assets
npm run sync-regatta-viewer
npm run sync-live-models
```

## License

AC / Virtual Eye models are **proprietary** (personal dev use only). See `Sailinig_3D_Viewer/README.md`.
