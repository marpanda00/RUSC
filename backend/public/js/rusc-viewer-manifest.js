/**
 * Single source of truth for live 3D viewer asset versions (cache bust).
 * Bump `version` when cutting a new baseline; bump individual `assets.*` when that file changes.
 */
(function (global) {
  global.RuscViewer = {
    version: '1.0.0-baseline',
    assets: {
      wakes: 9,
      deviceUi: 8,
      osmGround: 2,
      water: 6,
      scene: 28
    }
  };
})(window);
