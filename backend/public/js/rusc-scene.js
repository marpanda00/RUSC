/**
 * RUSC 3D regatta scene — Babylon.js live boats keyed by MAC.
 * Baseline: see backend/LIVE_VIEWER_BASELINE.md and rusc-viewer-manifest.js
 */
(function (global) {
  const SCENE_VERSION = (global.RuscViewer && global.RuscViewer.version) || '1.0.0-baseline';
  const DEG2RAD = Math.PI / 180;
  const samples = {};
  const displayPose = {};
  const playheads = {};
  const boats = {};
  const MAX_SAMPLES = 24;
  const EXTRAPOLATE_MAX_SEC = 0.4;
  const MIN_MOVE_FOR_YAW_M = 0.06;
  const POS_SMOOTH_RATE = 10;
  const ROT_SMOOTH_RATE = 14;
  /** Boat model bow points along −Z, so rotate 180° to face the direction of travel. */
  const BOAT_YAW_OFFSET_RAD = Math.PI;
  /**
   * Auto-align at model load: target Y for the hull waterline (not the keel tip).
   * Takes effect when boats are created/reloaded (change model or hard-refresh).
   */
  const WATERLINE_CLEARANCE_M = 0.2;
  /** Fraction of hull height above lowest vertex (keel/foils) for the waterline. */
  const WATERLINE_HULL_FRAC = 0.18;
  /** Extra meters above the water surface — tune via live-config.json `boatFloatLiftM` (no code edit). */
  let boatFloatLiftM = 0;
  /** World Y of the water surface; boats are placed relative to this so lift is intuitive. */
  let waterSurfaceY = 0;
  /** Water + grid ground size (meters); centered on origin. 20000 = ±10 km from Terracina origin. */
  let seaExtentM = 20000;
  let engine;
  let scene;
  let camera;
  let camTarget;
  const DEFAULT_ORIGIN = { lat: 41.282284, lon: 13.212244 };
  let origin = { ...DEFAULT_ORIGIN };

  function normalizeOrigin(o) {
    if (!o) return { ...DEFAULT_ORIGIN };
    return {
      lat: Number(o.lat ?? o.latitude ?? DEFAULT_ORIGIN.lat),
      lon: Number(o.lon ?? o.longitude ?? DEFAULT_ORIGIN.lon)
    };
  }

  function applySceneOrigin(next) {
    const normalized = normalizeOrigin(next);
    const changed = normalized.lat !== origin.lat || normalized.lon !== origin.lon;
    origin = normalized;
    if (changed) {
      for (const mac of Object.keys(samples)) delete samples[mac];
      for (const mac of Object.keys(displayPose)) delete displayPose[mac];
      for (const mac of Object.keys(playheads)) delete playheads[mac];
    }
    return origin;
  }

  let followMac = null;
  let boatModels = {};
  let lastSpeedByMac = {};
  const loadingMacs = new Set();
  let framedFleet = false;
  let waterController = null;

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

  function lerp(a, b, t) {
    return a + (b - a) * t;
  }

  function lerpAngleRad(a, b, t) {
    let d = b - a;
    while (d > Math.PI) d -= 2 * Math.PI;
    while (d < -Math.PI) d += 2 * Math.PI;
    return a + d * t;
  }

  /** Yaw (rad) so hull faces direction of travel in scene X/Z (+Z = south, −Z = north). */
  function yawFromDelta(dx, dz) {
    if (Math.hypot(dx, dz) < MIN_MOVE_FOR_YAW_M) return null;
    return Math.atan2(dx, dz);
  }

  function yawFromSamplePair(a, b) {
    const pa = latLonToLocal(a.lat, a.lon);
    const pb = latLonToLocal(b.lat, b.lon);
    return yawFromDelta(pb.x - pa.x, pb.z - pa.z);
  }

  /** Navigation course (0°=north, 90°=east) → Babylon yaw on X/Z plane. */
  function courseDegToYawRad(deg) {
    if (!Number.isFinite(deg)) return 0;
    return Math.PI - (deg % 360) * DEG2RAD;
  }

  function lerpAngleDeg(a, b, t) {
    let d = ((b - a + 540) % 360) - 180;
    return a + d * t;
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
    const heading = state.heading_deg ?? state.course ?? 0;
    // The viewer ingests from BOTH the WebSocket push and the 1 Hz REST poll, so the
    // same position arrives twice. Appending identical points makes the boat sit still
    // then jump (stop-go), so skip samples that carry no new motion.
    const last = buf[buf.length - 1];
    if (
      last &&
      Math.abs(last.lat - lat) < 1e-8 &&
      Math.abs(last.lon - lon) < 1e-8 &&
      Math.abs((last.heading || 0) - heading) < 0.05
    ) {
      return;
    }
    buf.push({
      t: Date.now() / 1000,
      lat,
      lon,
      y: state.alt_m || 0,
      heading,
      roll: state.roll_deg ?? state.roll ?? 0,
      pitch: state.pitch_deg ?? state.pitch ?? 0
    });
    if (buf.length > MAX_SAMPLES) {
      buf.shift();
      // Sample indices shifted down by one; keep the playback head aligned.
      if (playheads[mac] != null) playheads[mac] = Math.max(0, playheads[mac] - 1);
    }
  }

  function sampleToPose(s, yawRad) {
    const local = latLonToLocal(s.lat, s.lon);
    const yaw = yawRad != null ? yawRad : courseDegToYawRad(s.heading);
    return {
      x: local.x,
      y: s.y,
      z: local.z,
      yaw,
      roll: (s.roll || 0) * DEG2RAD,
      pitch: (s.pitch || 0) * DEG2RAD
    };
  }

  function resolveYaw(a, b, u) {
    const motionYaw = yawFromSamplePair(a, b);
    if (motionYaw != null) return motionYaw;
    return courseDegToYawRad(lerpAngleDeg(a.heading, b.heading, u));
  }

  /**
   * Average sample period (seconds) over the buffer span. Uses total span ÷ count
   * rather than per-gap medians so it is immune to bursty/clustered WS delivery
   * (where several samples arrive together, then a gap) — which otherwise makes the
   * playhead race ahead and stall (stop-go).
   */
  function recentInterval(buf) {
    const n = buf.length;
    if (n < 2) return 1.0;
    const span = buf[n - 1].t - buf[0].t;
    if (!(span > 0)) return 1.0;
    return span / (n - 1);
  }

  /**
   * Sample render time → pose, interpolating between the two bracketing samples.
   * `now` is expected to be a delayed render clock (see renderLoop) so that a
   * future sample almost always exists, giving continuous motion between samples.
   */
  function interpolateSamples(buf, now) {
    if (!buf.length) return null;
    if (buf.length === 1) return sampleToPose(buf[0], null);

    const t = now;
    const first = buf[0];
    const last = buf[buf.length - 1];

    // Render time precedes the buffer (startup): hold the oldest sample.
    if (t <= first.t) return sampleToPose(first, yawFromSamplePair(first, buf[1]));

    // Past the latest sample (data gap): short extrapolation, then freeze.
    if (t >= last.t) {
      const a = buf[buf.length - 2];
      const b = last;
      const dt = b.t - a.t;
      if (dt > 0.001 && t - b.t <= EXTRAPOLATE_MAX_SEC) {
        const ahead = Math.min(EXTRAPOLATE_MAX_SEC, t - b.t);
        const lat = b.lat + ((b.lat - a.lat) / dt) * ahead;
        const lon = b.lon + ((b.lon - a.lon) / dt) * ahead;
        return sampleToPose({
          lat, lon, y: b.y, heading: b.heading, roll: b.roll, pitch: b.pitch
        }, yawFromSamplePair(a, b));
      }
      return sampleToPose(b, yawFromSamplePair(a, b));
    }

    // Find the bracket [j, j+1] with buf[j].t <= t < buf[j+1].t and interpolate.
    let j = buf.length - 2;
    while (j > 0 && buf[j].t > t) j--;
    const a = buf[j];
    const b = buf[j + 1];
    if (b.t === a.t) return sampleToPose(b, yawFromSamplePair(a, b));
    const u = Math.max(0, Math.min(1, (t - a.t) / (b.t - a.t)));
    const yaw = resolveYaw(a, b, u);
    return sampleToPose({
      lat: lerp(a.lat, b.lat, u),
      lon: lerp(a.lon, b.lon, u),
      y: lerp(a.y, b.y, u),
      heading: lerp(a.heading, b.heading, u),
      roll: lerp(a.roll, b.roll, u),
      pitch: lerp(a.pitch, b.pitch, u)
    }, yaw);
  }

  /**
   * Steady playback head: advances at real-time rate through samples in arrival
   * order (immune to bursty/jittery arrival timestamps), interpolating between
   * adjacent samples. Adaptive speed keeps a small buffer cushion so motion never
   * stalls between updates — the core fix for the advance/stop/advance stutter.
   */
  function playbackPose(mac, dt) {
    const buf = samples[mac];
    if (!buf || !buf.length) return null;
    if (buf.length === 1) {
      playheads[mac] = 0;
      return sampleToPose(buf[0], null);
    }

    const lastIdx = buf.length - 1;
    const TARGET_LAG = 2.0; // samples to stay behind the newest (cushion vs. burst gaps)
    let ph = playheads[mac];
    if (ph == null || !Number.isFinite(ph)) ph = Math.max(0, lastIdx - TARGET_LAG);

    const period = Math.max(0.05, recentInterval(buf)); // seconds per sample
    const available = lastIdx - ph; // samples queued ahead of the head
    // Speed up when we have spare buffer, ease down hard when running low so brief
    // gaps stretch smoothly instead of snapping to a stop.
    let speed = 1 + (available - TARGET_LAG) * 0.4;
    speed = Math.max(0.2, Math.min(2.5, speed));

    ph += (dt / period) * speed;
    if (ph > lastIdx) ph = lastIdx;
    if (ph < 0) ph = 0;
    playheads[mac] = ph;

    const i = Math.floor(ph);
    if (i >= lastIdx) {
      const a = buf[lastIdx - 1];
      const b = buf[lastIdx];
      return sampleToPose(b, yawFromSamplePair(a, b));
    }
    const frac = ph - i;
    const a = buf[i];
    const b = buf[i + 1];
    const yaw = resolveYaw(a, b, frac);
    return sampleToPose({
      lat: lerp(a.lat, b.lat, frac),
      lon: lerp(a.lon, b.lon, frac),
      y: lerp(a.y, b.y, frac),
      heading: lerp(a.heading, b.heading, frac),
      roll: lerp(a.roll, b.roll, frac),
      pitch: lerp(a.pitch, b.pitch, frac)
    }, yaw);
  }

  function smoothDisplayPose(mac, target, dt) {
    if (!target) return null;
    const posAlpha = 1 - Math.exp(-POS_SMOOTH_RATE * dt);
    const rotAlpha = 1 - Math.exp(-ROT_SMOOTH_RATE * dt);
    let d = displayPose[mac];
    if (!d) {
      displayPose[mac] = { ...target };
      return displayPose[mac];
    }
    d.x = lerp(d.x, target.x, posAlpha);
    d.y = lerp(d.y, target.y, posAlpha);
    d.z = lerp(d.z, target.z, posAlpha);
    d.yaw = lerpAngleRad(d.yaw, target.yaw, rotAlpha);
    d.roll = lerpAngleRad(d.roll, target.roll, rotAlpha);
    d.pitch = lerpAngleRad(d.pitch, target.pitch, rotAlpha);
    return d;
  }

  /**
   * Boats share the water's render group (1) so depth testing makes the water surface
   * cut across the hull — boatFloatLiftM then visibly sets how deep the boat sits.
   * Labels stay in group 2 so name/speed are always readable on top.
   */
  function setBoatRenderGroup(boat) {
    const root = boat && boat.root ? boat.root : boat;
    if (!root || !root.getChildMeshes) return;
    root.getChildMeshes(false).forEach((m) => {
      m.renderingGroupId = 1;
    });
    if (boat && boat.label && boat.label.plane) {
      boat.label.plane.renderingGroupId = 2;
    }
    // Wake foam must render above the water surface so it stays visible.
    if (boat && boat.wakes && boat.wakes.meshes) {
      boat.wakes.meshes.forEach(({ mesh }) => {
        mesh.renderingGroupId = 2;
      });
    }
  }

  function createProceduralBoat(mac, profile) {
    const root = new BABYLON.TransformNode(`boat_${mac}`, scene);
    const color = hexToColor3(profile.color);
    const scale = profile.scale || 1;

    const hullH = 0.5 * scale;
    const hull = BABYLON.MeshBuilder.CreateBox(`hull_${mac}`, { width: 2 * scale, height: hullH, depth: 5 * scale }, scene);
    hull.parent = root;
    hull.position.y = hullH * 0.5;
    hull.isPickable = false;
    const hullMat = new BABYLON.StandardMaterial(`hullMat_${mac}`, scene);
    hullMat.diffuseColor = color;
    hull.material = hullMat;

    const mastH = 4 * scale;
    const mast = BABYLON.MeshBuilder.CreateCylinder(`mast_${mac}`, { height: mastH, diameter: 0.12 * scale }, scene);
    mast.parent = root;
    mast.position.y = hullH + mastH * 0.5;
    const mastMat = new BABYLON.StandardMaterial(`mastMat_${mac}`, scene);
    mastMat.diffuseColor = new BABYLON.Color3(0.9, 0.9, 0.85);
    mast.material = mastMat;

    const hullType = RuscWakes.resolveHullType(profile, boatModels);
    const wakes = RuscWakes.createWakes(scene, mac, root, hullType);
    const labelY = hullH + mastH + 4;
    const label = createNameLabel(mac, profile.displayName || mac, root, labelY);
    alignWaterlineOnMeshes([hull, mast]);
    boats[mac] = { root, hull, profile, loadedModel: 'procedural', hullType, wakes, label };
    setBoatRenderGroup(boats[mac]);
    return boats[mac];
  }

  function createNameLabel(mac, text, root, yOffset = 12) {
    const plane = BABYLON.MeshBuilder.CreatePlane(`label_${mac}`, { width: 18, height: 4.5 }, scene);
    plane.parent = root;
    plane.position.y = yOffset;
    plane.billboardMode = BABYLON.Mesh.BILLBOARDMODE_ALL;
    const dt = new BABYLON.DynamicTexture(`labelTex_${mac}`, { width: 1024, height: 256 }, scene, false);
    const mat = new BABYLON.StandardMaterial(`labelMat_${mac}`, scene);
    mat.diffuseTexture = dt;
    mat.emissiveTexture = dt;
    mat.disableLighting = true;
    mat.backFaceCulling = false;
    plane.material = mat;
    const label = { plane, dt, name: text, speedKnots: 0 };
    updateNameLabelTexture(label);
    return label;
  }

  /** Redraw the billboard label: boat name on top, speed in knots below. */
  function updateNameLabelTexture(label, nameOverride, speedKnots) {
    const dt = label && label.dt ? label.dt : label; // tolerate (dt) legacy callers
    if (!dt || typeof dt.getContext !== 'function') return;
    if (label && label.dt) {
      if (nameOverride != null) label.name = nameOverride;
      if (Number.isFinite(speedKnots)) label.speedKnots = speedKnots;
    }
    const name = label && label.name != null ? label.name : nameOverride;
    const speed = label && Number.isFinite(label.speedKnots) ? label.speedKnots : 0;

    const size = typeof dt.getSize === 'function' ? dt.getSize() : { width: 1024, height: 256 };
    const w = size.width;
    const h = size.height;
    const ctx = dt.getContext();
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = 'rgba(15, 23, 42, 0.82)';
    ctx.fillRect(0, 0, w, h);
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = '#ffffff';
    ctx.font = 'bold 64px Segoe UI, Arial, sans-serif';
    ctx.fillText(String(name || 'Boat').slice(0, 24), w * 0.5, h * 0.34);
    ctx.fillStyle = '#7dd3fc';
    ctx.font = 'bold 58px Segoe UI, Arial, sans-serif';
    ctx.fillText(`${speed.toFixed(1)} kn`, w * 0.5, h * 0.74);
    dt.update();
  }

  /** Axis-aligned bounds of all meshes in their parent (boat root) space. */
  function meshBoundsInParent(meshes) {
    let minX = Infinity; let minY = Infinity; let minZ = Infinity;
    let maxX = -Infinity; let maxY = -Infinity; let maxZ = -Infinity;
    for (const mesh of meshes) {
      if (!mesh) continue;
      mesh.refreshBoundingInfo(true);
      const bi = mesh.getBoundingInfo();
      if (!bi) continue;
      const p = mesh.position;
      const bmin = bi.boundingBox.minimum;
      const bmax = bi.boundingBox.maximum;
      minX = Math.min(minX, p.x + bmin.x); minY = Math.min(minY, p.y + bmin.y); minZ = Math.min(minZ, p.z + bmin.z);
      maxX = Math.max(maxX, p.x + bmax.x); maxY = Math.max(maxY, p.y + bmax.y); maxZ = Math.max(maxZ, p.z + bmax.z);
    }
    return { minX, minY, minZ, maxX, maxY, maxZ };
  }

  function meshBoundsY(meshes) {
    const b = meshBoundsInParent(meshes);
    return { minY: b.minY, maxY: b.maxY };
  }

  /** Shift mesh children so the hull waterline (not deepest keel vertex) is at WATERLINE_CLEARANCE_M. */
  function alignWaterlineOnMeshes(meshes) {
    const { minY, maxY } = meshBoundsY(meshes);
    if (!Number.isFinite(minY)) return 0;
    const span = Math.max(maxY - minY, 0.05);
    const waterlineY = minY + span * WATERLINE_HULL_FRAC;
    const lift = WATERLINE_CLEARANCE_M - waterlineY;
    for (const mesh of meshes) {
      if (mesh) mesh.position.y += lift;
    }
    return lift;
  }

  function centerMeshesOnRoot(boatRoot, meshes) {
    const owned = meshes.filter((m) => m && m !== boatRoot);
    if (!owned.length) return;
    const b = meshBoundsInParent(owned);
    const { minX, minY, minZ, maxX, maxY, maxZ } = b;
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
      boatRoot.computeWorldMatrix(true);
      alignWaterlineOnMeshes(toParent);
      const { maxY } = meshBoundsY(toParent);
      const labelY = (Number.isFinite(maxY) ? maxY : 6) + 5;
      const hullType = RuscWakes.resolveHullType(profile, boatModels);
      const wakes = RuscWakes.createWakes(scene, mac, boatRoot, hullType);
      const label = createNameLabel(mac, profile.displayName || mac, boatRoot, labelY);
      boats[mac] = { root: boatRoot, profile, loadedModel: profile.modelId, hullType, wakes, label };
      setBoatRenderGroup(boats[mac]);
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
      if (boats[mac].label) updateNameLabelTexture(boats[mac].label, p.displayName || mac, lastSpeedByMac[mac]);
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
    // Mesh waterline sits at WATERLINE_CLEARANCE_M in root space; offset the root so that
    // waterline lands on the water surface, then boatFloatLiftM raises/lowers from there.
    boat.root.position.y = pose.y + waterSurfaceY - WATERLINE_CLEARANCE_M + boatFloatLiftM;
    boat.root.position.z = pose.z;
    const yaw = (pose.yaw != null ? pose.yaw : courseDegToYawRad(pose.heading || 0)) + BOAT_YAW_OFFSET_RAD;
    boat.root.rotationQuaternion = BABYLON.Quaternion.RotationYawPitchRoll(
      yaw,
      pose.pitch || 0,
      pose.roll || 0
    );
    RuscWakes.updateWakes(boat.wakes, boat.root, lastSpeedByMac[mac] || 0, waterSurfaceY, Date.now() / 1000);

    const spd = lastSpeedByMac[mac] || 0;
    if (boat.label && Math.abs((boat.label.speedKnots || 0) - spd) >= 0.1) {
      updateNameLabelTexture(boat.label, null, spd);
    }
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
      if (boats[mac].label) updateNameLabelTexture(boats[mac].label, profile.displayName || mac, lastSpeedByMac[mac]);
    }
  }

  function renderLoop() {
    const now = Date.now() / 1000;
    const dt = Math.min(0.1, engine.getDeltaTime() / 1000 || 1 / 60);
    for (const mac of Object.keys(samples)) {
      const target = playbackPose(mac, dt);
      const pose = smoothDisplayPose(mac, target, dt);
      applyPose(mac, pose);
      if (followMac === mac && pose) {
        moveCamTarget(pose.x, 1, pose.z);
      }
    }
    if (waterController) waterController.update(now);
    scene.render();
  }

  /** Coastal photo draped flat on the sea floor (XZ plane); origin = sailing center. */
  function buildCoastalBackdrop(cfg) {
    const b = cfg && cfg.coastalBackdrop;
    if (!b || b.enabled === false) return;

    const rel = b.url || '/viewer/textures/Terracina_coastal.png';
    const url = rel.startsWith('http') ? rel : `${assetBaseUrl()}${rel.startsWith('/') ? rel : `/${rel}`}`;
    const w = Number(b.widthM) || 700;
    const depth = Number(b.heightM) || 700;
    const dist = Number(b.distanceM) || 0;
    const facing = (b.facing || 'south').toLowerCase();
    const invertY = b.invertY !== false;

    const ground = BABYLON.MeshBuilder.CreateGround(
      'coastalGround',
      { width: w, height: depth, subdivisions: 1 },
      scene
    );
    ground.position = new BABYLON.Vector3(0, 0.28, -dist);
    if (facing === 'north') ground.rotation.y = Math.PI;
    else if (facing === 'east') ground.rotation.y = -Math.PI / 2;
    else if (facing === 'west') ground.rotation.y = Math.PI / 2;
    if (Number.isFinite(Number(b.rotationDeg))) {
      ground.rotation.y += Number(b.rotationDeg) * DEG2RAD;
    }

    const tex = new BABYLON.Texture(
      url,
      scene,
      true,
      invertY,
      BABYLON.Texture.TRILINEAR_SAMPLINGMODE,
      function () {
        console.info('[RUSC 3D] coastal ground loaded:', url);
      },
      function (message) {
        console.error('[RUSC 3D] coastal ground failed to load:', url, message || '');
      }
    );
    tex.hasAlpha = false;

    const mat = new BABYLON.StandardMaterial('coastalGroundMat', scene);
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
  }

  function showGroundWarning(text) {
    const id = 'rusc-ground-warning';
    let el = document.getElementById(id);
    if (!el) {
      el = document.createElement('div');
      el.id = id;
      el.style.cssText =
        'position:fixed;top:12px;left:50%;transform:translateX(-50%);z-index:2000;' +
        'max-width:80%;padding:10px 14px;background:rgba(180,60,30,0.92);color:#fff;' +
        'font:13px/1.4 system-ui,sans-serif;border-radius:6px;box-shadow:0 2px 8px rgba(0,0,0,0.3);';
      document.body.appendChild(el);
    }
    el.textContent = text;
    el.style.display = 'block';
  }

  /** OSM mosaic when enabled, else coastal photo, else none. */
  async function applyGroundBackdrop(liveCfg) {
    const warn = document.getElementById('rusc-ground-warning');
    if (warn) warn.style.display = 'none';
    const osm = liveCfg && liveCfg.osmGround;
    if (osm && osm.enabled !== false && global.RuscOsmGround) {
      if (global.RuscOsmGround.hideAttribution) global.RuscOsmGround.hideAttribution();
      try {
        await global.RuscOsmGround.buildOsmGround(scene, origin, osm, assetBaseUrl());
        if (global.RuscOsmGround.showAttribution) {
          global.RuscOsmGround.showAttribution(osm.attribution || '© OpenStreetMap contributors');
        }
        return;
      } catch (err) {
        console.warn('[RUSC 3D] OSM ground failed, falling back:', err.message || err);
        showGroundWarning(
          'Map tiles failed to load (OSM may be rate-limiting). Land not shown — ' +
            'check the server console; cached tiles will reappear once available.'
        );
      }
    }
    if (global.RuscOsmGround && global.RuscOsmGround.hideAttribution) {
      global.RuscOsmGround.hideAttribution();
    }
    buildCoastalBackdrop(liveCfg);
  }

  function buildEnvironment(extentM, waterCfg) {
    const sea = Number(extentM) || seaExtentM || 20000;
    const hemi = new BABYLON.HemisphericLight('hemi', new BABYLON.Vector3(0, 1, 0), scene);
    hemi.intensity = 1.15;
    hemi.groundColor = new BABYLON.Color3(0.02, 0.2, 0.45);
    hemi.diffuse = new BABYLON.Color3(0.85, 0.9, 1);

    const sun = new BABYLON.DirectionalLight('sun', new BABYLON.Vector3(-0.4, -1, -0.25), scene);
    sun.intensity = 0.55;

    // Sky = scene.clearColor only (no skybox — infiniteDistance skybox caused solid blue flash)

    waterController = null;
    const waterConfig = Object.assign({ extentM: sea }, waterCfg || {});
    if (global.RuscWater && waterConfig.enabled !== false) {
      waterController = global.RuscWater.buildWater(scene, waterConfig);
    }
    waterSurfaceY = waterController && waterController.mesh ? waterController.mesh.position.y : 0;
    if (!waterController) {
      const water = BABYLON.MeshBuilder.CreateGround('water', { width: sea, height: sea, subdivisions: 2 }, scene);
      const waterMat = new BABYLON.StandardMaterial('waterMat', scene);
      waterMat.disableLighting = true;
      waterMat.emissiveColor = new BABYLON.Color3(0.02, 0.3, 0.55);
      waterMat.backFaceCulling = false;
      water.material = waterMat;
      water.isPickable = false;
    }

    const showGrid = waterCfg && waterCfg.showGrid === false ? false : !waterController;
    if (showGrid) {
      const subdiv = Math.min(80, Math.max(40, Math.round(sea / 250)));
      const grid = BABYLON.MeshBuilder.CreateGround('grid', { width: sea, height: sea, subdivisions: subdiv }, scene);
      const gridMat = new BABYLON.StandardMaterial('gridMat', scene);
      gridMat.wireframe = true;
      gridMat.emissiveColor = new BABYLON.Color3(0.35, 0.75, 0.95);
      gridMat.alpha = 0.45;
      gridMat.disableLighting = true;
      grid.material = gridMat;
      grid.position.y = 0.15;
      grid.isPickable = false;
    }
  }

  /** Place static course marks (buoys) from liveCfg.buoys. Procedural inflatable cones. */
  function buildBuoys(liveCfg) {
    const list = liveCfg && Array.isArray(liveCfg.buoys) ? liveCfg.buoys : [];
    if (!list.length) return;
    let placed = 0;
    list.forEach((b, idx) => {
      const lat = Number(b.latitude ?? b.lat);
      const lon = Number(b.longitude ?? b.lon);
      if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
        console.warn('[RUSC 3D] buoy skipped (invalid coordinates):', b);
        return;
      }
      const local = latLonToLocal(lat, lon);
      const name = b.name || b.label || `Mark ${idx + 1}`;
      const color = b.color || '#ff7a00';
      const scale = Number(b.scale) > 0 ? Number(b.scale) : 1;

      const root = new BABYLON.TransformNode(`buoy_${idx}`, scene);
      root.position.set(local.x, waterSurfaceY, local.z);
      addProceduralBuoy(root, idx, name, color, scale);
      addBuoyLabel(root, idx, name, scale);
      placed++;
    });
    if (placed) console.info(`[RUSC 3D] ${placed} course mark(s) placed`);
  }

  function addProceduralBuoy(root, idx, name, color, scale) {
    const col = hexToColor3(color);
    const mat = new BABYLON.StandardMaterial(`buoyMat_${idx}`, scene);
    mat.diffuseColor = col;
    mat.emissiveColor = col.scale(0.3);
    mat.specularColor = new BABYLON.Color3(0.15, 0.15, 0.15);

    // Tall inflatable cone — the body of the mark.
    const cone = BABYLON.MeshBuilder.CreateCylinder(
      `buoyCone_${idx}`,
      { diameterTop: 0.25 * scale, diameterBottom: 2.6 * scale, height: 4.2 * scale, tessellation: 20 },
      scene
    );
    cone.parent = root;
    cone.position.y = 2.1 * scale;
    cone.material = mat;
    cone.isPickable = false;
    cone.renderingGroupId = 1;

    // White flotation collar at the waterline.
    const base = BABYLON.MeshBuilder.CreateCylinder(
      `buoyBase_${idx}`,
      { diameter: 2.8 * scale, height: 0.6 * scale, tessellation: 20 },
      scene
    );
    base.parent = root;
    base.position.y = 0.3 * scale;
    const baseMat = new BABYLON.StandardMaterial(`buoyBaseMat_${idx}`, scene);
    baseMat.diffuseColor = new BABYLON.Color3(0.96, 0.96, 0.96);
    baseMat.emissiveColor = new BABYLON.Color3(0.25, 0.25, 0.25);
    base.material = baseMat;
    base.isPickable = false;
    base.renderingGroupId = 1;
  }

  function addBuoyLabel(root, idx, name, scale) {
    const plane = BABYLON.MeshBuilder.CreatePlane(`buoyLabel_${idx}`, { width: 16, height: 4 }, scene);
    plane.parent = root;
    plane.position.y = 4.4 * scale + 1.2;
    plane.billboardMode = BABYLON.Mesh.BILLBOARDMODE_ALL;
    plane.isPickable = false;
    plane.renderingGroupId = 2;
    const dt = new BABYLON.DynamicTexture(`buoyLabelTex_${idx}`, { width: 768, height: 192 }, scene, false);
    const ctx = dt.getContext();
    ctx.clearRect(0, 0, 768, 192);
    ctx.fillStyle = 'rgba(15, 23, 42, 0.78)';
    ctx.fillRect(0, 0, 768, 192);
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = '#ffd166';
    ctx.font = 'bold 84px Segoe UI, Arial, sans-serif';
    ctx.fillText(String(name).slice(0, 18), 384, 96);
    dt.update();
    const mat = new BABYLON.StandardMaterial(`buoyLabelMat_${idx}`, scene);
    mat.diffuseTexture = dt;
    mat.emissiveTexture = dt;
    mat.diffuseTexture.hasAlpha = false;
    mat.disableLighting = true;
    mat.backFaceCulling = false;
    plane.material = mat;
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

    let liveCfg = null;
    try {
      const cfgRes = await fetch(`${RuscDeviceUI.getApiBase()}/live/config?t=${Date.now()}`, { cache: 'no-store' });
      if (cfgRes.ok) {
        liveCfg = await cfgRes.json();
        console.info('[RUSC 3D] live/config loaded — water:', JSON.stringify(liveCfg.water), 'osmGround:', JSON.stringify(liveCfg.osmGround));
        applySceneOrigin(liveCfg.origin);
        boatModels = liveCfg.boatModels || {};
        boatFloatLiftM = Number(liveCfg.boatFloatLiftM) || 0;
        if (Number.isFinite(Number(liveCfg.seaExtentM)) && Number(liveCfg.seaExtentM) > 0) {
          seaExtentM = Number(liveCfg.seaExtentM);
        }
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

    const waterCfg = liveCfg
      ? Object.assign({}, liveCfg.water || {}, { osmGround: liveCfg.osmGround })
      : null;
    if (global.RuscWater && global.RuscWater.ensureReady) {
      await global.RuscWater.ensureReady();
    }
    if (global.RuscWakes && global.RuscWakes.ensureReady) {
      try { await global.RuscWakes.ensureReady(); }
      catch (e) { console.warn('[RUSC 3D] wake shader load failed, using fallback foam:', e.message || e); }
    }
    buildEnvironment(seaExtentM, waterCfg);
    buildBuoys(liveCfg);
    applyGroundBackdrop(liveCfg).catch((err) => {
      console.warn('[RUSC 3D] ground backdrop:', err.message || err);
    });
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
      '| boatFloatLiftM=' + boatFloatLiftM + ' waterSurfaceY=' + waterSurfaceY,
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
