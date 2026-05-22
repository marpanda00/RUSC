/**
 * Procedural wake foam for live RUSC viewer (monohull / multihull).
 * Mirrors Virtual Eye hullType rules from regatta-viewer appconfig.
 */
(function (global) {
  const DEG2RAD = Math.PI / 180;

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

  function wakeOffsets(hullType) {
    if (hullType === 'multihull') {
      return [
        { x: 0, z: -2.5, w: 5, h: 10 },
        { x: -1.8, z: -1.2, w: 2.5, h: 7 },
        { x: 1.8, z: -1.2, w: 2.5, h: 7 }
      ];
    }
    return [{ x: 0, z: -2, w: 3.5, h: 8 }];
  }

  function createWakeMaterial(scene, mac, index) {
    const mat = new BABYLON.StandardMaterial(`wakeMat_${mac}_${index}`, scene);
    mat.diffuseColor = new BABYLON.Color3(0.95, 0.98, 1);
    mat.emissiveColor = new BABYLON.Color3(0.4, 0.5, 0.55);
    mat.alpha = 0.28;
    mat.backFaceCulling = false;
    mat.disableLighting = true;
    return mat;
  }

  function createWakes(scene, mac, root, hullType) {
    const meshes = [];
    const specs = wakeOffsets(hullType);
    specs.forEach((spec, i) => {
      const mesh = BABYLON.MeshBuilder.CreateGround(
        `wake_${mac}_${i}`,
        { width: spec.w, height: spec.h },
        scene
      );
      mesh.parent = root;
      mesh.position.set(spec.x, 0.08, spec.z);
      mesh.material = createWakeMaterial(scene, mac, i);
      mesh.isPickable = false;
      mesh.renderingGroupId = 1;
      meshes.push({ mesh, spec });
    });
    return { hullType, meshes };
  }

  function disposeWakes(wakeState) {
    if (!wakeState || !wakeState.meshes) return;
    for (const { mesh } of wakeState.meshes) {
      if (mesh.material) mesh.material.dispose();
      mesh.dispose();
    }
  }

  function updateWakes(wakeState, root, speedKnots) {
    if (!wakeState || !root) return;
    const alpha = Math.min(0.45, Math.max(0.08, (speedKnots || 0) / 12));
    const yaw = root.rotationQuaternion
      ? root.rotationQuaternion.toEulerAngles().y
      : root.rotation.y;
    for (const { mesh, spec } of wakeState.meshes) {
      const lx = spec.x;
      const lz = spec.z;
      mesh.position.x = lx * Math.cos(yaw) - lz * Math.sin(yaw);
      mesh.position.z = lx * Math.sin(yaw) + lz * Math.cos(yaw);
      mesh.position.y = 0.08;
      mesh.rotation.y = yaw;
      if (mesh.material) mesh.material.alpha = alpha;
      mesh.setEnabled(speedKnots > 0.5);
    }
  }

  global.RuscWakes = {
    resolveHullType,
    createWakes,
    disposeWakes,
    updateWakes
  };
})(window);
