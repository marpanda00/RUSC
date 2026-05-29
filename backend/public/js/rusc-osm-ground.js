/**
 * OpenStreetMap static ground patch for RUSC live 3D viewer.
 * Fetches tiles, stitches a canvas mosaic, applies to a Babylon ground mesh.
 */
(function (global) {
  const TILE_SIZE = 256;
  const DEG2RAD = Math.PI / 180;
  const DEFAULT_MAX_TEXTURE_PX = 2048;
  const DEFAULT_MAX_TILES = 36;
  const FETCH_CONCURRENCY = 5;

  /** Scene ENU corner offsets → lat/lon (matches rusc-scene.js latLonToLocal). */
  function patchBoundsLatLon(origin, widthM, heightM) {
    const lat = Number(origin.lat ?? origin.latitude);
    const lon = Number(origin.lon ?? origin.longitude);
    const cosLat = Math.cos(lat * DEG2RAD);
    const halfW = widthM / 2;
    const halfH = heightM / 2;
    const corners = [
      { x: -halfW, z: -halfH },
      { x: halfW, z: -halfH },
      { x: halfW, z: halfH },
      { x: -halfW, z: halfH }
    ];
    let minLat = 90;
    let maxLat = -90;
    let minLon = 180;
    let maxLon = -180;
    for (const c of corners) {
      const cLat = lat - c.z / 110540;
      const cLon = lon + c.x / (111320 * cosLat);
      minLat = Math.min(minLat, cLat);
      maxLat = Math.max(maxLat, cLat);
      minLon = Math.min(minLon, cLon);
      maxLon = Math.max(maxLon, cLon);
    }
    return { minLat, maxLat, minLon, maxLon, centerLat: lat, centerLon: lon };
  }

  function latLonToMercatorPx(lat, lon, zoom) {
    const scale = Math.pow(2, zoom) * TILE_SIZE;
    const x = ((lon + 180) / 360) * scale;
    const latRad = lat * DEG2RAD;
    const y =
      ((1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2) * scale;
    return { x, y };
  }

  function metersPerPixel(zoom, lat) {
    return (156543.03392 * Math.cos(lat * DEG2RAD)) / Math.pow(2, zoom);
  }

  function pickZoom(widthM, heightM, centerLat, cfg) {
    if (cfg.zoom !== 'auto' && cfg.zoom != null && cfg.zoom !== '') {
      const z = Math.round(Number(cfg.zoom));
      if (z >= 10 && z <= 19) return z;
    }
    const maxPx = Number(cfg.maxTexturePx) || DEFAULT_MAX_TEXTURE_PX;
    const maxTiles = Number(cfg.maxTiles) || DEFAULT_MAX_TILES;
    const patchM = Math.max(widthM, heightM);
    for (let z = 19; z >= 10; z--) {
      const mpp = metersPerPixel(z, centerLat);
      const px = patchM / mpp;
      if (px > maxPx) continue;
      const tileSpan = Math.ceil(px / TILE_SIZE);
      if (tileSpan * tileSpan > maxTiles) continue;
      return z;
    }
    return 14;
  }

  function tileUrl(template, z, x, y, baseUrl) {
    let url = String(template || '/api/map/tiles/{z}/{x}/{y}.png')
      .replace('{z}', z)
      .replace('{x}', x)
      .replace('{y}', y);
    if (url.startsWith('http')) return url;
    const base = (baseUrl || '').replace(/\/$/, '');
    return `${base}${url.startsWith('/') ? url : `/${url}`}`;
  }

  function loadImage(url) {
    return new Promise((resolve, reject) => {
      const img = new Image();
      img.crossOrigin = 'anonymous';
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error(`tile load failed: ${url}`));
      img.src = url;
    });
  }

  async function fetchTiles(tileJobs, concurrency) {
    const results = new Array(tileJobs.length);
    let idx = 0;
    async function worker() {
      while (idx < tileJobs.length) {
        const i = idx++;
        const job = tileJobs[i];
        try {
          results[i] = { ...job, img: await loadImage(job.url) };
        } catch (err) {
          try {
            results[i] = { ...job, img: await loadImage(job.url) };
          } catch (err2) {
            results[i] = { ...job, error: err2 };
          }
        }
      }
    }
    const workers = Array.from({ length: Math.min(concurrency, tileJobs.length) }, () => worker());
    await Promise.all(workers);
    return results;
  }

  /**
   * @returns {Promise<{ canvas: HTMLCanvasElement, zoom: number, tileCount: number, widthM: number, heightM: number }>}
   */
  async function stitchOsmMosaic(origin, cfg, baseUrl) {
    const widthM = Number(cfg.widthM) || 1000;
    const heightM = Number(cfg.heightM) || widthM;
    const bounds = patchBoundsLatLon(origin, widthM, heightM);
    const zoom = pickZoom(widthM, heightM, bounds.centerLat, cfg);
    const nw = latLonToMercatorPx(bounds.maxLat, bounds.minLon, zoom);
    const se = latLonToMercatorPx(bounds.minLat, bounds.maxLon, zoom);
    const x0 = Math.min(nw.x, se.x);
    const y0 = Math.min(nw.y, se.y);
    const x1 = Math.max(nw.x, se.x);
    const y1 = Math.max(nw.y, se.y);
    let canvasW = Math.ceil(x1 - x0);
    let canvasH = Math.ceil(y1 - y0);
    const maxPx = Number(cfg.maxTexturePx) || DEFAULT_MAX_TEXTURE_PX;
    if (canvasW > maxPx || canvasH > maxPx) {
      const scale = maxPx / Math.max(canvasW, canvasH);
      canvasW = Math.max(1, Math.floor(canvasW * scale));
      canvasH = Math.max(1, Math.floor(canvasH * scale));
    }
    const tileX0 = Math.floor(x0 / TILE_SIZE);
    const tileY0 = Math.floor(y0 / TILE_SIZE);
    const tileX1 = Math.floor((x1 - 1) / TILE_SIZE);
    const tileY1 = Math.floor((y1 - 1) / TILE_SIZE);
    const jobs = [];
    for (let ty = tileY0; ty <= tileY1; ty++) {
      for (let tx = tileX0; tx <= tileX1; tx++) {
        jobs.push({
          tx,
          ty,
          url: tileUrl(cfg.tileUrl, zoom, tx, ty, baseUrl)
        });
      }
    }
    const fetched = await fetchTiles(jobs, FETCH_CONCURRENCY);
    const canvas = document.createElement('canvas');
    canvas.width = canvasW;
    canvas.height = canvasH;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#a8c5e8';
    ctx.fillRect(0, 0, canvasW, canvasH);
    const scaleX = canvasW / (x1 - x0);
    const scaleY = canvasH / (y1 - y0);
    let drawn = 0;
    for (const t of fetched) {
      if (!t.img) continue;
      const px = t.tx * TILE_SIZE - x0;
      const py = t.ty * TILE_SIZE - y0;
      ctx.drawImage(
        t.img,
        px * scaleX,
        py * scaleY,
        TILE_SIZE * scaleX,
        TILE_SIZE * scaleY
      );
      drawn++;
    }
    if (drawn === 0) {
      throw new Error('no OSM tiles loaded');
    }
    return { canvas, zoom, tileCount: drawn, widthM, heightM };
  }

  /**
   * @param {BABYLON.Scene} babylonScene
   * @param {{ lat: number, lon: number }} origin
   * @param {object} cfg osmGround config block
   * @param {string} baseUrl window.location.origin or API base
   * @returns {Promise<BABYLON.Mesh|null>}
   */
  async function buildOsmGround(babylonScene, origin, cfg, baseUrl) {
    if (!cfg || cfg.enabled === false) return null;
    if (typeof BABYLON === 'undefined') {
      console.warn('[RUSC OSM] Babylon not loaded');
      return null;
    }
    const normalizedOrigin = {
      lat: Number(origin.lat ?? origin.latitude),
      lon: Number(origin.lon ?? origin.longitude)
    };
    const mosaic = await stitchOsmMosaic(normalizedOrigin, cfg, baseUrl);
    const yOff = Number(cfg.yOffsetM);
    const y = Number.isFinite(yOff) ? yOff : 0.25;
    const ground = BABYLON.MeshBuilder.CreateGround(
      'osmGround',
      { width: mosaic.widthM, height: mosaic.heightM, subdivisions: 1 },
      babylonScene
    );
    ground.position = new BABYLON.Vector3(0, y, 0);
    const tex = new BABYLON.DynamicTexture(
      'osmGroundTex',
      { width: mosaic.canvas.width, height: mosaic.canvas.height },
      babylonScene,
      false
    );
    const ctx = tex.getContext();
    ctx.drawImage(mosaic.canvas, 0, 0);
    tex.update();
    tex.hasAlpha = false;
    // Mosaic canvas has NORTH at the top, but a Babylon ground maps v=1 (canvas top
    // with invertY) to +Z (scene south). Flip V so map-north aligns to scene-north (−Z),
    // matching boat positions (latLonToLocal: north = −Z) and the water coast mask.
    const flipV = cfg.flipV === false ? false : true;
    if (flipV) {
      tex.vScale = -1;
      tex.vOffset = 1;
    }
    const mat = new BABYLON.StandardMaterial('osmGroundMat', babylonScene);
    mat.diffuseTexture = tex;
    mat.emissiveTexture = tex;
    mat.diffuseColor = new BABYLON.Color3(1, 1, 1);
    mat.emissiveColor = new BABYLON.Color3(1, 1, 1);
    mat.specularColor = new BABYLON.Color3(0, 0, 0);
    mat.disableLighting = true;
    mat.backFaceCulling = true;
    ground.material = mat;
    ground.isPickable = false;
    ground.renderingGroupId = 0;
    console.info(
      `[RUSC 3D] OSM ground: ${mosaic.tileCount} tiles, z=${mosaic.zoom}, ` +
        `${mosaic.widthM}×${mosaic.heightM} m, ${mosaic.canvas.width}×${mosaic.canvas.height} px`
    );
    return ground;
  }

  function showAttribution(text) {
    const id = 'osm-attribution';
    let el = document.getElementById(id);
    if (!el) {
      el = document.createElement('div');
      el.id = id;
      el.style.cssText =
        'position:fixed;bottom:8px;right:8px;z-index:1500;padding:4px 8px;' +
        'font-size:11px;background:rgba(255,255,255,0.85);color:#333;border-radius:4px;' +
        'pointer-events:none;box-shadow:0 1px 4px rgba(0,0,0,0.15);';
      document.body.appendChild(el);
    }
    el.textContent = text || '© OpenStreetMap contributors';
    el.style.display = 'block';
  }

  function hideAttribution() {
    const el = document.getElementById('osm-attribution');
    if (el) el.style.display = 'none';
  }

  global.RuscOsmGround = {
    stitchOsmMosaic,
    buildOsmGround,
    showAttribution,
    hideAttribution,
    patchBoundsLatLon,
    pickZoom
  };
})(window);
