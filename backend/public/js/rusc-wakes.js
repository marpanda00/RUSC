/**
 * Procedural wake foam for live RUSC viewer (monohull / multihull).
 * Mirrors the Virtual Eye / Sailing_3D_Viewer wake: a foam-textured strip that
 * trails behind the stern, animated via a scrolling-foam ShaderMaterial.
 *
 * Wake meshes live in WORLD space (not parented to the boat) so they are
 * immune to per-model scaling and always sit flat on the water surface,
 * trailing straight behind the hull regardless of the boat's vertical offset.
 */
(function (global) {
  const SHADER_BASE = '/viewer/shaders/';
  const SHADER_VERSION = '1';
  const TEXTURE_BASE = '/viewer/textures';
  let readyPromise = null;

  function resolveHullType(profile, modelCatalog) {
    const explicit = String(profile.hullType || '').toLowerCase();
    if (explicit === 'monohull' || explicit === 'multihull') return explicit;
    const model = modelCatalog[profile.modelId] || {};
    const fromModel = String(model.hullType || '').toLowerCase();
    if (fromModel === 'monohull' || fromModel === 'multihull') return fromModel;
    if (profile.modelId === 'catamaran') return 'multihull';
    const hasFoils = Boolean(model.leftfoil) && Boolean(model.rightfoil);
    if (hasFoils) return 'multihull';
    return 'monohull';
  }

  /**
   * World-space wake strips. All values in metres.
   *  x      = lateral offset from the hull centreline (+ = starboard)
   *  behind = gap between the stern and the near edge of the strip
   *  w      = strip width
   *  len    = strip length (trail distance)
   */
  function wakeSpecs(hullType) {
    if (hullType === 'multihull') {
      return [
        { x: 0, behind: 3, w: 7, len: 26 },
        { x: -3.2, behind: 2, w: 2.6, len: 16 },
        { x: 3.2, behind: 2, w: 2.6, len: 16 }
      ];
    }
    return [{ x: 0, behind: 3, w: 4.5, len: 20 }];
  }

  async function loadText(url) {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`wake shader load failed: ${url}`);
    return res.text();
  }

  async function ensureReady() {
    if (readyPromise) return readyPromise;
    readyPromise = (async () => {
      if (typeof BABYLON === 'undefined') throw new Error('Babylon not loaded');
      const vert = await loadText(`${SHADER_BASE}wakeShader.vert.glsl?v=${SHADER_VERSION}`);
      const frag = await loadText(`${SHADER_BASE}wakeShader.frag.glsl?v=${SHADER_VERSION}`);
      BABYLON.Effect.ShadersStore.ruscWakeVertexShader = vert;
      BABYLON.Effect.ShadersStore.ruscWakeFragmentShader = frag;
    })();
    return readyPromise;
  }

  function createWakeMaterial(scene, mac, index) {
    const usingShader = Boolean(BABYLON.Effect.ShadersStore.ruscWakeVertexShader);
    if (!usingShader) {
      // Fallback: plain translucent foam if shaders failed to load.
      const mat = new BABYLON.StandardMaterial(`wakeMat_${mac}_${index}`, scene);
      mat.diffuseColor = new BABYLON.Color3(0.95, 0.98, 1);
      mat.emissiveColor = new BABYLON.Color3(0.5, 0.6, 0.65);
      mat.alpha = 0.25;
      mat.backFaceCulling = false;
      mat.disableLighting = true;
      mat.disableDepthWrite = true;
      return { mat, shader: false };
    }
    const mat = new BABYLON.ShaderMaterial(
      `wakeMat_${mac}_${index}`,
      scene,
      { vertex: 'ruscWake', fragment: 'ruscWake' },
      {
        attributes: ['position', 'uv'],
        uniforms: ['world', 'worldViewProjection', 'time', 'wakeAlpha'],
        samplers: ['wakeTexture'],
        needAlphaBlending: true
      }
    );
    const tex = new BABYLON.Texture(`${TEXTURE_BASE}/TP52WakeFoam.png`, scene);
    tex.wrapU = BABYLON.Texture.WRAP_ADDRESSMODE;
    tex.wrapV = BABYLON.Texture.WRAP_ADDRESSMODE;
    mat.setTexture('wakeTexture', tex);
    mat.setFloat('time', 0);
    mat.setFloat('wakeAlpha', 0);
    mat.backFaceCulling = false;
    mat.disableDepthWrite = true;
    mat.transparencyMode = BABYLON.Material.MATERIAL_ALPHABLEND;
    return { mat, shader: true };
  }

  function createWakes(scene, mac, _root, hullType) {
    const meshes = [];
    const specs = wakeSpecs(hullType);
    specs.forEach((spec, i) => {
      const mesh = BABYLON.MeshBuilder.CreateGround(
        `wake_${mac}_${i}`,
        { width: spec.w, height: spec.len },
        scene
      );
      const { mat, shader } = createWakeMaterial(scene, mac, i);
      mesh.material = mat;
      mesh.isPickable = false;
      mesh.renderingGroupId = 2; // above the water surface
      mesh.alwaysSelectAsActiveMesh = true;
      mesh.setEnabled(false);
      meshes.push({ mesh, mat, shader, spec });
    });
    return { hullType, meshes };
  }

  function disposeWakes(wakeState) {
    if (!wakeState || !wakeState.meshes) return;
    for (const { mesh, mat } of wakeState.meshes) {
      if (mat) {
        if (mat.getActiveTextures) {
          mat.getActiveTextures().forEach((t) => t.dispose());
        }
        mat.dispose();
      }
      mesh.dispose();
    }
  }

  /**
   * Position/animate the wake strips in world space behind the stern.
   * @param {object} wakeState
   * @param {BABYLON.TransformNode} root   top-level boat node (world transform)
   * @param {number} speedKnots
   * @param {number} surfaceY              water surface Y in world units
   * @param {number} nowSec                animation clock
   */
  function updateWakes(wakeState, root, speedKnots, surfaceY, nowSec) {
    if (!wakeState || !root) return;
    const spd = speedKnots || 0;
    const visible = spd > 0.5;
    const alpha = Math.min(0.55, Math.max(0.1, spd / 12));

    const yaw = root.rotationQuaternion
      ? root.rotationQuaternion.toEulerAngles().y
      : root.rotation.y;
    // Direction straight behind the boat and to starboard (scene: +Z south, −Z north).
    const bx = Math.sin(yaw);
    const bz = Math.cos(yaw);
    const rx = Math.cos(yaw);
    const rz = -Math.sin(yaw);

    const base = root.position; // top-level node → world position
    const y = (Number.isFinite(surfaceY) ? surfaceY : 0.08) + 0.04;
    const t = Number.isFinite(nowSec) ? nowSec : 0;

    for (const { mesh, mat, shader, spec } of wakeState.meshes) {
      mesh.setEnabled(visible);
      if (!visible) continue;
      const along = spec.behind + spec.len * 0.5; // strip centre distance behind stern
      mesh.position.set(
        base.x + bx * along + rx * spec.x,
        y,
        base.z + bz * along + rz * spec.x
      );
      mesh.rotation.set(0, yaw, 0);
      if (shader) {
        mat.setFloat('time', t);
        mat.setFloat('wakeAlpha', alpha);
      } else if (mat) {
        mat.alpha = alpha * 0.5;
      }
    }
  }

  global.RuscWakes = {
    ensureReady,
    resolveHullType,
    createWakes,
    disposeWakes,
    updateWakes
  };
})(window);
