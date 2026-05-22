/**
 * RUSC 3D regatta scene — Babylon.js live boats keyed by MAC.
 * Baseline: see backend/LIVE_VIEWER_BASELINE.md and rusc-viewer-manifest.js
 */
(function (global) {
  const SCENE_VERSION = (global.RuscViewer && global.RuscViewer.version) || '1.0.0-baseline';
  const DEG2RAD = Math.PI / 180;
  const samples = {};
  const boats = {};
  let engine;
  let scene;
  let camera;
  let camTarget;
  let origin = { lat: 41.125, lon: 16.87 };

  function normalizeOrigin(o) {
    if (!o) return { lat: 41.125, lon: 16.87 };
    return {
      lat: Number(o.lat ?? o.latitude ?? 41.125),
      lon: Number(o.lon ?? o.longitude ?? 16.87)
    };
  }

  function applySceneOrigin(next) {
    const normalized = normalizeOrigin(next);
    const changed = normalized.lat !== origin.lat || normalized.lon !== origin.lon;
    origin = normalized;
    if (changed) {
      for (const mac of Object.keys(samples)) delete samples[mac];
    }
    return origin;
  }

  let followMac = null;
  let boatModels = {};
  let lastSpeedByMac = {};
  const loadingMacs = new Set();
  let framedFleet = false;

  function assetBaseUrl() {
    return window.location.origin;
  }

  function resolveAssetRoot(relativeDir) {
    const dir = relativeDir.startsWith('/') ? relativeDir : `/${relativeDir}`;
    return `${assetBaseUrl()}${dir}`;
  }

  function latestPose(mac) {
    return interpolateSamples(samples[mac] || [], Date.now() / 1000);
  }

  function moveCamTarget(x, y, z) {
    if (!camTarget) return;
    if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) return;
    camTarget.position.x = x;
    camTarget.position.y = y;
    camTarget.position.z = z;
  }

  function focusCameraOn(mac) {
    if (!camera) return;
    const pose = latestPose(mac);
    if (!pose || !Number.isFinite(pose.x) || !Number.isFinite(pose.z)) return;
    moveCamTarget(pose.x, 1, pose.z);
    camera.beta = 1.05;
    camera.radius = 45;
  }

  function latLonToLocal(lat, lon) {
    const cosLat = Math.cos(origin.lat * DEG2RAD);
    const x = (lon - origin.lon) * 111320 * cosLat;
    const z = -(lat - origin.lat) * 110540;
    return { x, z };
  }

  function hexToColor3(hex) {
    const h = String(hex || '#2563eb').replace('#', '');
    const r = parseInt(h.substring(0, 2), 16) / 255;
    const g = parseInt(h.substring(2, 4), 16) / 255;
    const b = parseInt(h.substring(4, 6), 16) / 255;
    return new BABYLON.Color3(r, g, b);
  }

  function ensureSamples(mac) {
    if (!samples[mac]) samples[mac] = [];
    return samples[mac];
  }

  function pushSample(mac, state) {
    const lat = state.lat;
    const lon = state.lon;
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) return;
    const buf = ensureSamples(mac);
    buf.push({
      t: Date.now() / 1000,
      lat,
      lon,
      y: state.alt_m || 0,
      heading: state.heading_deg ?? state.course ?? 0,
      roll: state.roll_deg ?? state.roll ?? 0,
      pitch: state.pitch_deg ?? state.pitch ?? 0
    });
    if (buf.length > 8) buf.shift();
  }

  function sampleToPose(s) {
    const local = latLonToLocal(s.lat, s.lon);
    return {
      x: local.x,
      y: s.y,
      z: local.z,
      heading: s.heading,
      roll: s.roll,
      pitch: s.pitch
    };
  }

  function interpolateSamples(buf, now) {
    if (!buf.length) return null;
    if (buf.length === 1) return sampleToPose(buf[0]);
    const t = now;
    let i = buf.length - 1;
    while (i > 0 && buf[i].t > t) i--;
    const a = buf[Math.max(0, i - 1)];
    const b = buf[Math.min(buf.length - 1, i)];
    if (b.t === a.t) return sampleToPose(b);
    const u = Math.max(0, Math.min(1, (t - a.t) / (b.t - a.t)));
    return sampleToPose({
      lat: a.lat + (b.lat - a.lat) * u,
      lon: a.lon + (b.lon - a.lon) * u,
      y: a.y + (b.y - a.y) * u,
      heading: a.heading + (b.heading - a.heading) * u,
      roll: a.roll + (b.roll - a.roll) * u,
      pitch: a.pitch + (b.pitch - a.pitch) * u
    });
  }

  function createProceduralBoat(mac, profile) {
    const root = new BABYLON.TransformNode(`boat_${mac}`, scene);
    const color = hexToColor3(profile.color);
    const scale = profile.scale || 1;

    const hull = BABYLON.MeshBuilder.CreateBox(`hull_${mac}`, { width: 2 * scale, height: 0.5 * scale, depth: 5 * scale }, scene);
    hull.parent = root;
    hull.position.y = 0.5 * scale;
    hull.isPickable = false;
    const hullMat = new BABYLON.StandardMaterial(`hullMat_${mac}`, scene);
    hullMat.diffuseColor = color;
    hull.material = hullMat;

    const mast = BABYLON.MeshBuilder.CreateCylinder(`mast_${mac}`, { height: 4 * scale, diameter: 0.12 * scale }, scene);
    mast.parent = root;
    mast.position.y = 2.5 * scale;
    const mastMat = new BABYLON.StandardMaterial(`mastMat_${mac}`, scene);
    mastMat.diffuseColor = new BABYLON.Color3(0.9, 0.9, 0.85);
    mast.material = mastMat;

    const hullType = RuscWakes.resolveHullType(profile, boatModels);
    const wakes = RuscWakes.createWakes(scene, mac, root, hullType);
    const label = createNameLabel(mac, profile.displayName || mac, root);
    boats[mac] = { root, hull, profile, loadedModel: 'procedural', hullType, wakes, label };
    return boats[mac];
  }

  function createNameLabel(mac, text, root) {
    const plane = BABYLON.MeshBuilder.CreatePlane(`label_${mac}`, { width: 5, height: 1.2 }, scene);
    plane.parent = root;
    plane.position.y = 5.5;
    plane.billboardMode = BABYLON.Mesh.BILLBOARDMODE_ALL;
    const dt = new BABYLON.DynamicTexture(`labelTex_${mac}`, { width: 512, height: 128 }, scene, false);
    const mat = new BABYLON.StandardMaterial(`labelMat_${mac}`, scene);
    mat.diffuseTexture = dt;
    mat.emissiveTexture = dt;
    mat.disableLighting = true;
    mat.backFaceCulling = false;
    plane.material = mat;
    updateNameLabelTexture(dt, text);
    return { plane, dt };
  }

  function updateNameLabelTexture(dt, text) {
    const ctx = dt.getContext();
    ctx.clearRect(0, 0, 512, 128);
    ctx.fillStyle = 'rgba(15, 23, 42, 0.75)';
    ctx.fillRect(0, 0, 512, 128);
    ctx.fillStyle = '#ffffff';
    ctx.font = 'bold 36px Segoe UI, Arial, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    const name = String(text || 'Boat').slice(0, 24);
    ctx.fillText(name, 256, 64);
    dt.update();
  }

  function centerMeshesOnRoot(boatRoot, meshes) {
    const owned = meshes.filter((m) => m && m !== boatRoot);
    if (!owned.length) return;
    let minX = Infinity; let minY = Infinity; let minZ = Infinity;
    let maxX = -Infinity; let maxY = -Infinity; let maxZ = -Infinity;
    for (const mesh of owned) {
      const bi = mesh.getBoundingInfo();
      if (!bi) continue;
      const bmin = bi.boundingBox.minimum;
      const bmax = bi.boundingBox.maximum;
      minX = Math.min(minX, bmin.x); minY = Math.min(minY, bmin.y); minZ = Math.min(minZ, bmin.z);
      maxX = Math.max(maxX, bmax.x); maxY = Math.max(maxY, bmax.y); maxZ = Math.max(maxZ, bmax.z);
    }
    if (!Number.isFinite(minX)) return;
    const cx = (minX + maxX) * 0.5;
    const cy = (minY + maxY) * 0.5;
    const cz = (minZ + maxZ) * 0.5;
    const center = new BABYLON.Vector3(cx, cy, cz);
    for (const mesh of owned) {
      mesh.position.subtractInPlace(center);
    }
    const len = Math.max(maxX - minX, maxY - minY, maxZ - minZ);
    if (len > 80) boatRoot.scaling.scaleInPlace(40 / len);
    else if (len < 2) boatRoot.scaling.scaleInPlace(4 / Math.max(len, 0.5));
  }

  function parseAssetPath(assetPath) {
    const slash = assetPath.lastIndexOf('/');
    if (slash < 0) return { root: '', file: assetPath };
    return { root: assetPath.slice(0, slash + 1), file: assetPath.slice(slash + 1) };
  }

  async function tryLoadGlb(mac, profile) {
    const model = boatModels[profile.modelId];
    const asset = model && (model.glb || model.gltf);
    if (!model || model.procedural || !asset) return null;
    const { root: relDir, file } = parseAssetPath(asset);
    const loadRoot = resolveAssetRoot(relDir);
    try {
      const result = await Promise.race([
        BABYLON.SceneLoader.ImportMeshAsync('', loadRoot, file, scene),
        new Promise((_, reject) => setTimeout(() => reject(new Error('load timeout')), 12000))
      ]);
      const boatRoot = new BABYLON.TransformNode(`boat_${mac}`, scene);
      const tint = hexToColor3(profile.color);
      const toParent = result.meshes.filter((m) => m && !m.name.startsWith('__root__'));
      if (!toParent.length) return null;
      toParent.forEach((m) => {
        m.setParent(boatRoot);
        if (m.material && m.material.diffuseColor) {
          try {
            m.material = m.material.clone(`mat_${mac}_${m.name}`);
            m.material.diffuseColor = tint;
          } catch (_) { /* keep original material */ }
        }
      });
      boatRoot.computeWorldMatrix(true);
      centerMeshesOnRoot(boatRoot, toParent);
      const s = (model.scale || 1) * (profile.scale || 1);
      boatRoot.scaling.scaleInPlace(s);
      const hullType = RuscWakes.resolveHullType(profile, boatModels);
      const wakes = RuscWakes.createWakes(scene, mac, boatRoot, hullType);
      const label = createNameLabel(mac, profile.displayName || mac, boatRoot);
      boats[mac] = { root: boatRoot, profile, loadedModel: profile.modelId, hullType, wakes, label };
      return boats[mac];
    } catch (e) {
      console.warn('3D model load failed, using procedural:', profile.modelId, e.message || e);
      return null;
    }
  }

  async function ensureBoat(mac, profile) {
    const p = profile || { modelId: '470', color: '#2563eb', scale: 1 };
    const nextHull = RuscWakes.resolveHullType(p, boatModels);
    const catalog = boatModels[p.modelId];
    const wantAsset = Boolean(catalog && !catalog.procedural && (catalog.glb || catalog.gltf));

    if (boats[mac] && boats[mac].profile.modelId === p.modelId && boats[mac].hullType === nextHull) {
      boats[mac].profile = p;
      if (boats[mac].label) updateNameLabelTexture(boats[mac].label.dt, p.displayName || mac);
      return boats[mac];
    }

    if (loadingMacs.has(mac)) return boats[mac];
    loadingMacs.add(mac);
    try {
      if (boats[mac]?.wakes) RuscWakes.disposeWakes(boats[mac].wakes);
      if (boats[mac]?.root) boats[mac].root.dispose();
      delete boats[mac];

      let boat = null;
      if (wantAsset) boat = await tryLoadGlb(mac, p);
      if (!boat) boat = createProceduralBoat(mac, p);

      applyPose(mac, latestPose(mac));
      return boat;
    } finally {
      loadingMacs.delete(mac);
    }
  }

  function applyPose(mac, pose) {
    const boat = boats[mac];
    if (!boat || !pose) return;
    boat.root.position.x = pose.x;
    boat.root.position.y = pose.y;
    boat.root.position.z = pose.z;
    const yaw = (pose.heading || 0) * DEG2RAD;
    boat.root.rotationQuaternion = BABYLON.Quaternion.RotationYawPitchRoll(yaw, (pose.pitch || 0) * DEG2RAD, (pose.roll || 0) * DEG2RAD);
    RuscWakes.updateWakes(boat.wakes, boat.root, lastSpeedByMac[mac] || 0);
  }

  function ingestState(mac, data) {
    lastSpeedByMac[mac] = data.speed_knots ?? data.speedKnots ?? lastSpeedByMac[mac] ?? 0;
    pushSample(mac, {
      lat: data.lat ?? data.position?.latitude,
      lon: data.lon ?? data.position?.longitude,
      alt_m: data.altitude_m ?? data.alt_m ?? 0,
      heading_deg: data.heading_deg ?? data.course,
      roll: data.roll,
      pitch: data.pitch
    });
    const profile = {
      displayName: data.display_name,
      modelId: data.modelId || '470',
      color: data.color || '#2563eb',
      scale: data.scale || 1,
      boatType: data.boatType,
      hullType: data.hullType
    };
    if (!boats[mac] && !loadingMacs.has(mac)) {
      ensureBoat(mac, profile).catch((e) => console.error('ensureBoat failed', mac, e));
    } else if (
      !loadingMacs.has(mac) && (
      boats[mac].profile.modelId !== profile.modelId ||
      boats[mac].hullType !== RuscWakes.resolveHullType(profile, boatModels))
    ) {
      ensureBoat(mac, profile).catch((e) => console.error('ensureBoat failed', mac, e));
    } else if (boats[mac]) {
      boats[mac].profile = profile;
      if (boats[mac].label) updateNameLabelTexture(boats[mac].label.dt, profile.displayName || mac);
    }
  }

  function renderLoop() {
    const now = Date.now() / 1000;
    for (const mac of Object.keys(samples)) {
      const pose = interpolateSamples(samples[mac], now);
      applyPose(mac, pose);
      if (followMac === mac && pose) {
        moveCamTarget(pose.x, 1, pose.z);
      }
    }
    scene.render();
  }

  function buildEnvironment() {
    const hemi = new BABYLON.HemisphericLight('hemi', new BABYLON.Vector3(0, 1, 0), scene);
    hemi.intensity = 1.15;
    hemi.groundColor = new BABYLON.Color3(0.02, 0.2, 0.45);
    hemi.diffuse = new BABYLON.Color3(0.85, 0.9, 1);

    const sun = new BABYLON.DirectionalLight('sun', new BABYLON.Vector3(-0.4, -1, -0.25), scene);
    sun.intensity = 0.55;

    // Sky = scene.clearColor only (no skybox — infiniteDistance skybox caused solid blue flash)

    const water = BABYLON.MeshBuilder.CreateGround('water', { width: 3000, height: 3000, subdivisions: 2 }, scene);
    const waterMat = new BABYLON.StandardMaterial('waterMat', scene);
    waterMat.disableLighting = true;
    waterMat.emissiveColor = new BABYLON.Color3(0.02, 0.3, 0.55);
    waterMat.backFaceCulling = false;
    water.material = waterMat;
    water.isPickable = false;

    const grid = BABYLON.MeshBuilder.CreateGround('grid', { width: 3000, height: 3000, subdivisions: 40 }, scene);
    const gridMat = new BABYLON.StandardMaterial('gridMat', scene);
    gridMat.wireframe = true;
    gridMat.emissiveColor = new BABYLON.Color3(0.35, 0.75, 0.95);
    gridMat.alpha = 0.45;
    gridMat.disableLighting = true;
    grid.material = gridMat;
    grid.position.y = 0.15;
    grid.isPickable = false;
  }

  function resetCameraView() {
    if (!camera) return;
    moveCamTarget(0, 0, 0);
    camera.alpha = -Math.PI / 4;
    camera.beta = 1.05;
    camera.radius = 55;
  }

  function frameFleet() {
    if (!camera || followMac) return;
    const macs = Object.keys(samples).filter((m) => !m.startsWith('__'));
    if (!macs.length) return;
    let cx = 0; let cz = 0; let n = 0;
    let maxR = 20;
    for (const mac of macs) {
      const pose = latestPose(mac);
      if (!pose || !Number.isFinite(pose.x)) continue;
      cx += pose.x;
      cz += pose.z;
      n++;
      maxR = Math.max(maxR, Math.hypot(pose.x, pose.z));
    }
    if (!n) return;
    moveCamTarget(cx / n, 0, cz / n);
    camera.beta = 1.05;
    camera.radius = Math.max(40, Math.min(120, maxR * 2.5));
  }

  async function init(canvasId) {
    const canvas = document.getElementById(canvasId);
    if (!canvas) throw new Error('Canvas #renderCanvas not found');

    engine = new BABYLON.Engine(canvas, true, {
      preserveDrawingBuffer: true,
      stencil: true,
      adaptToDeviceRatio: true
    });
    scene = new BABYLON.Scene(engine);
    scene.clearColor = new BABYLON.Color4(0.45, 0.72, 0.95, 1);

    try {
      const cfgRes = await fetch(`${RuscDeviceUI.getApiBase()}/live/config`);
      if (cfgRes.ok) {
        const cfg = await cfgRes.json();
        applySceneOrigin(cfg.origin);
        boatModels = cfg.boatModels || {};
      }
    } catch (e) {
      console.warn('[RUSC 3D] live/config fetch failed', e);
    }

    camTarget = new BABYLON.TransformNode('camTarget', scene);
    camTarget.position.set(0, 0, 0);

    camera = new BABYLON.ArcRotateCamera(
      'cam',
      -Math.PI / 4,
      1.05,
      55,
      camTarget.position,
      scene
    );
    scene.activeCamera = camera;
    camera.setTarget(camTarget);
    camera.attachControl(canvas, true);
    camera.lowerRadiusLimit = 8;
    camera.upperRadiusLimit = 500;
    camera.lowerBetaLimit = 0.35;
    camera.upperBetaLimit = 1.35;
    camera.minZ = 0.5;
    camera.maxZ = 25000;
    camera.wheelPrecision = 30;

    buildEnvironment();
    resetCameraView();

    engine.runRenderLoop(renderLoop);
    const doResize = () => engine.resize();
    doResize();
    window.addEventListener('resize', doResize);
    if (typeof ResizeObserver !== 'undefined') {
      new ResizeObserver(doResize).observe(canvas);
    }

    console.info(
      '[RUSC 3D] scene ready —',
      scene.meshes.length, 'meshes,',
      engine.getRenderWidth() + '×' + engine.getRenderHeight() + ',',
      'origin', origin.lat.toFixed(5) + '°N', origin.lon.toFixed(5) + '°E',
      '(' + SCENE_VERSION + ')'
    );

    connectWebSocket();
    pollPositions();
    setInterval(pollPositions, 1000);
  }

  function connectWebSocket() {
    try {
      const ws = new WebSocket(RuscDeviceUI.getWsUrl());
      ws.onmessage = (ev) => {
        const msg = JSON.parse(ev.data);
        if (msg.type === 'boat_update' && msg.device_id) {
          ingestState(msg.device_id, msg);
          if (typeof global.onLiveBoatUpdate === 'function') global.onLiveBoatUpdate(msg);
        }
      };
    } catch (e) {
      console.warn('WebSocket unavailable', e);
    }
  }

  async function pollPositions() {
    const res = await fetch(`${RuscDeviceUI.getApiBase()}/positions`);
    if (!res.ok) {
      console.warn('positions API failed', res.status, RuscDeviceUI.getApiBase());
      return;
    }
    const positions = await res.json();
    for (const [mac, data] of Object.entries(positions)) {
      ingestState(mac, data);
    }
    if (!framedFleet && Object.keys(positions).length > 0) {
      framedFleet = true;
      setTimeout(frameFleet, 500);
    }
    if (typeof global.onLivePositions === 'function') global.onLivePositions(positions);
  }

  function followBoat(mac) {
    followMac = mac || null;
    if (!followMac) {
      resetCameraView();
      return;
    }
    focusCameraOn(followMac);
  }

  function stopFollow() {
    followMac = null;
    resetCameraView();
  }

  function onProfileSaved(mac, profile) {
    ensureBoat(mac, profile);
  }

  global.RuscScene = {
    version: SCENE_VERSION,
    init,
    followBoat,
    stopFollow,
    getOrigin: () => ({ ...origin }),
    onProfileSaved,
    ingestState,
    ensureBoat
  };
})(window);
