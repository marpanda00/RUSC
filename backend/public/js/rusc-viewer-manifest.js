/**
 * Single source of truth for live 3D viewer asset versions (cache bust).
 * Bump `version` when cutting a new baseline; bump individual `assets.*` when that file changes.
 */
(function (global) {
  global.RuscViewer = {
    version: '1.0.0-baseline',
    assets: {
      wakes: 5,
      deviceUi: 7,
      scene: 8
    }
  };
})(window);
