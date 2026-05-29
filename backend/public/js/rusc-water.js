/**
 * Virtual Eye–style animated water (normal-map shader) for RUSC live viewer.
 */
(function (global) {
  const SHADER_BASE = '/viewer/shaders/';
  const SHADER_VERSION = '5';
  let readyPromise = null;

  function smoothstep(edge0, edge1, x) {
    if (edge0 === edge1) return x >= edge1 ? 1 : 0;
    const t = Math.max(0, Math.min(1, (x - edge0) / (edge1 - edge0)));
    return t * t * (3 - 2 * t);
  }

  /** +Z = south, −Z = north. Return 0..1 sea visibility at scene X/Z. */
  function seaMaskAt(x, z, coast) {
    if (!coast || coast.enabled === false) return 1;
    const fade = Math.max(20, Number(coast.fadeM) || 200);
    const shore = Number(coast.shorelineM) || 0;
    const dir = String(coast.landDirection || 'north').toLowerCase();
    if (dir === 'north') return smoothstep(shore, shore + fade, z);
    if (dir === 'south') return 1 - smoothstep(shore - fade, shore, z);
    if (dir === 'west') return smoothstep(shore, shore + fade, x);
    if (dir === 'east') return 1 - smoothstep(shore - fade, shore, x);
    return 1;
  }

  /** Map landDirection → shader coastDir code (0=north 1=south 2=west 3=east). */
  function coastDirCode(coast) {
    const dir = String((coast && coast.landDirection) || 'north').toLowerCase();
    if (dir === 'south') return 1;
    if (dir === 'west') return 2;
    if (dir === 'east') return 3;
    return 0;
  }

  function resolveSizeM(cfg) {
    const osm = cfg.osmGround;
    if (cfg.matchOsmSize !== false && osm && osm.enabled !== false) {
      const w = Number(osm.widthM) || 1000;
      const h = Number(osm.heightM) || w;
      return { widthM: w, heightM: h };
    }
    const size = Number(cfg.sizeM) || 6000;
    return { widthM: size, heightM: size };
  }

  function texUrl(cfg, name) {
    const base = String(cfg.textureBaseUrl || '/viewer/textures').replace(/\/$/, '');
    return `${base}/${name}`;
  }

  async function loadText(url) {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`shader load failed: ${url}`);
    return res.text();
  }

  async function ensureReady() {
    if (readyPromise) return readyPromise;
    readyPromise = (async () => {
      if (typeof BABYLON === 'undefined') throw new Error('Babylon not loaded');
      const vert = await loadText(`${SHADER_BASE}waterShader.vert.glsl?v=${SHADER_VERSION}`);
      const frag = await loadText(`${SHADER_BASE}waterShader.frag.glsl?v=${SHADER_VERSION}`);
      BABYLON.Effect.ShadersStore.waterShaderVertexShader = vert;
      BABYLON.Effect.ShadersStore.waterShaderFragmentShader = frag;
    })();
    return readyPromise;
  }

  /**
   * @param {BABYLON.Scene} babylonScene
   * @param {object} cfg
   * @returns {{ mesh: BABYLON.Mesh, update: function(number): void } | null}
   */
  function buildWater(babylonScene, cfg) {
    if (typeof BABYLON === 'undefined') return null;
    if (cfg && cfg.enabled === false) return null;
    if (!BABYLON.Effect.ShadersStore.waterShaderVertexShader) {
      console.warn('[RUSC water] shaders not loaded — call RuscWater.ensureReady() first');
      return null;
    }

    const { widthM, heightM } = resolveSizeM(cfg || {});
    const opacity = Number.isFinite(Number(cfg.opacity)) ? Number(cfg.opacity) : 0.88;
    const coast = cfg.coastBorder || null;
    const useCoast = coast && coast.enabled !== false;
    const yOff = Number.isFinite(Number(cfg.yOffsetM)) ? Number(cfg.yOffsetM) : 0.08;
    const windOffset = new BABYLON.Vector2(0, 0);

    const mesh = BABYLON.MeshBuilder.CreateGround(
      'water',
      { width: widthM, height: heightM, subdivisions: 1 },
      babylonScene
    );
    mesh.position.y = yOff;
    mesh.isPickable = false;
    // Layer order: OSM ground (0) < water (1) < boats (2). Keeps boats on top of water.
    mesh.renderingGroupId = 1;

    const bumpTex = new BABYLON.Texture(texUrl(cfg, 'RippledWater_Greyscale_NRM_F_NRM.png'), babylonScene);
    const heightTex = new BABYLON.Texture(texUrl(cfg, 'RippledWater_Greyscale_NRM_F_DISP.png'), babylonScene);
    const foamTex = new BABYLON.Texture(texUrl(cfg, 'communityWaterFoam.png'), babylonScene);
    const cubeTex = new BABYLON.CubeTexture(texUrl(cfg, 'ClearBlue_SunLess/ClearBlue'), babylonScene);
    const arrowTex = new BABYLON.Texture(texUrl(cfg, 'windarrow-02.png'), babylonScene);
    const floorTex = new BABYLON.Texture(texUrl(cfg, 'WaterBaseLayer.png'), babylonScene);
    const fresnelTex = new BABYLON.Texture(texUrl(cfg, 'waterFresnel.png'), babylonScene);
    fresnelTex.wrapU = BABYLON.Texture.CLAMP_ADDRESSMODE;
    fresnelTex.wrapV = BABYLON.Texture.CLAMP_ADDRESSMODE;

    const mat = new BABYLON.ShaderMaterial(
      'waterMat',
      babylonScene,
      { vertex: 'waterShader', fragment: 'waterShader' },
      {
        attributes: ['position', 'normal', 'uv'],
        uniforms: [
          'world',
          'worldView',
          'worldViewProjection',
          'view',
          'projection',
          'cameraPosition',
          'screensize',
          'time',
          'windAngle',
          'windSpeed',
          'windAlpha',
          'windOffset',
          'useCoastMask',
          'waterOpacity',
          'coastDir',
          'shorelineM',
          'fadeM'
        ],
        samplers: [
          'bumpMap',
          'rippleBump',
          'heightMap',
          'waterFoam',
          'cubeMap',
          'arrowTexture',
          'waterFresnel',
          'underwaterTexture'
        ],
        needAlphaBlending: true
      }
    );
    mat.setFloat('time', 0);
    mat.setTexture('bumpMap', bumpTex);
    mat.setTexture('rippleBump', bumpTex);
    mat.setTexture('heightMap', heightTex);
    mat.setTexture('waterFoam', foamTex);
    mat.setTexture('cubeMap', cubeTex);
    mat.setTexture('arrowTexture', arrowTex);
    mat.setTexture('underwaterTexture', floorTex);
    mat.setTexture('waterFresnel', fresnelTex);
    mat.setFloat('windAngle', Math.PI);
    mat.setFloat('windSpeed', 0);
    mat.setFloat('windAlpha', 0);
    mat.setVector2('windOffset', windOffset);
    mat.setFloat('waterOpacity', opacity);
    mat.setFloat('useCoastMask', useCoast ? 1 : 0);
    mat.setFloat('coastDir', useCoast ? coastDirCode(coast) : 0);
    mat.setFloat('shorelineM', useCoast ? Number(coast.shorelineM) || 0 : 0);
    mat.setFloat('fadeM', useCoast ? Math.max(20, Number(coast.fadeM) || 200) : 200);
    mat.backFaceCulling = false;
    mat.disableDepthWrite = true;
    mat.transparencyMode = BABYLON.Material.MATERIAL_ALPHABLEND;
    mesh.material = mat;

    const engine = babylonScene.getEngine();
    mat.setVector2('screensize', new BABYLON.Vector2(engine.getRenderWidth(), engine.getRenderHeight()));

    console.info(
      `[RUSC 3D] water (shader): ${widthM}×${heightM} m, ` +
        `coast ${useCoast ? coast.landDirection + ' @ ' + coast.shorelineM + 'm' : 'off'}`
    );

    return {
      mesh,
      update(nowSec) {
        mat.setFloat('time', nowSec);
        const engine = babylonScene.getEngine();
        mat.setVector2('screensize', new BABYLON.Vector2(engine.getRenderWidth(), engine.getRenderHeight()));
      }
    };
  }

  global.RuscWater = { ensureReady, buildWater, seaMaskAt };
})(window);
