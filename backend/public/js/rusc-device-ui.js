/**
 * Shared device drawer UI — association + live telemetry (2D tracker + 3D viewer).
 */
(function (global) {
  const deviceEdits = {};
  let boatModels = {};
  let boatTypes = {};

  function getApiBase() {
    const host = window.location.hostname;
    const protocol = window.location.protocol;
    const port = window.location.port;
    if (protocol === 'https:') {
      return port ? `https://${host}:${port}/api` : `https://${host}/api`;
    }
    if (port && port !== '80') {
      return `http://${host}:${port}/api`;
    }
    return `http://${host}:3000/api`;
  }

  function getWsUrl() {
    const host = window.location.hostname;
    const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const port = window.location.protocol === 'https:' ? '' : ':3002';
    return `${protocol}://${host}${port}`;
  }

  function escapeHtml(value) {
    return String(value ?? '').replace(/[&<>"']/g, (ch) => ({
      '&': '&amp;',
      '<': '&lt;',
      '>': '&gt;',
      '"': '&quot;',
      "'": '&#39;'
    }[ch]));
  }

  function calibrationStateText(state) {
    switch (state) {
      case 0: return 'Idle';
      case 1: return 'Gyro calibration running';
      case 2: return 'Gyro calibration complete';
      case 3: return 'Boat level set';
      case 255: return 'Calibration error';
      default: return `Unknown (${state})`;
    }
  }

  function signalMarkup(strength) {
    let bars = 1;
    let color = '#f44336';
    if (strength >= -50) { bars = 5; color = '#4CAF50'; }
    else if (strength >= -60) { bars = 4; color = '#8BC34A'; }
    else if (strength >= -70) { bars = 3; color = '#FFC107'; }
    else if (strength >= -80) { bars = 2; color = '#FF9800'; }
    return `<span class="info-value" style="color:${color}">${'▂'.repeat(bars)}${'▂'.repeat(5 - bars)} ${strength} dBm</span>`;
  }

  async function loadCatalog() {
    const base = getApiBase();
    const [modelsRes, typesRes] = await Promise.all([
      fetch(`${base}/boat-models`),
      fetch(`${base}/boat-types`)
    ]);
    boatModels = modelsRes.ok ? await modelsRes.json() : {};
    boatTypes = typesRes.ok ? await typesRes.json() : {};
  }

  function hasActiveDeviceEdit() {
    const el = document.activeElement;
    if (el && (el.classList.contains('device-name-input') ||
        el.classList.contains('device-config-input'))) {
      return true;
    }
    return Object.keys(deviceEdits).length > 0;
  }

  function trackEdit(deviceId, field, value) {
    if (!deviceEdits[deviceId]) deviceEdits[deviceId] = {};
    deviceEdits[deviceId][field] = value;
  }

  function getEdit(deviceId, field, fallback) {
    return deviceEdits[deviceId]?.[field] ?? fallback;
  }

  function clearEdits(deviceId) {
    delete deviceEdits[deviceId];
  }

  function modelOptionsHtml(selectedId) {
    return Object.entries(boatModels).map(([id, m]) =>
      `<option value="${escapeHtml(id)}"${id === selectedId ? ' selected' : ''}>${escapeHtml(m.label || id)}</option>`
    ).join('');
  }

  function typeOptionsHtml(selectedType) {
    return Object.entries(boatTypes).map(([id, t]) =>
      `<option value="${escapeHtml(id)}"${id === selectedType ? ' selected' : ''}>${escapeHtml(t.label || id)}</option>`
    ).join('');
  }

  function colorSwatchesHtml(deviceId, selectedColor) {
    const presets = ['#2563eb', '#dc2626', '#16a34a', '#ca8a04', '#7c3aed', '#0d9488', '#ea580c', '#1e293b'];
    return presets.map((c) =>
      `<button type="button" class="color-swatch${c === selectedColor ? ' selected' : ''}" ` +
      `style="background:${c}" data-device="${escapeHtml(deviceId)}" data-color="${c}" ` +
      `onclick="RuscDeviceUI.pickColor('${deviceId}', '${c}')" aria-label="Color ${c}"></button>`
    ).join('');
  }

  function pickColor(deviceId, color) {
    trackEdit(deviceId, 'color', color);
    const input = document.getElementById(`deviceColor-${deviceId}`);
    if (input) input.value = color;
    document.querySelectorAll(`.color-swatch[data-device="${deviceId}"]`).forEach((el) => {
      el.classList.toggle('selected', el.dataset.color === color);
    });
  }

  async function applyBoatTypePreset(deviceId) {
    const sel = document.getElementById(`deviceBoatType-${deviceId}`);
    if (!sel) return;
    const preset = boatTypes[sel.value];
    if (!preset) return;
    trackEdit(deviceId, 'boatType', sel.value);
    if (preset.modelId) {
      trackEdit(deviceId, 'modelId', preset.modelId);
      const modelSel = document.getElementById(`deviceModel-${deviceId}`);
      if (modelSel) modelSel.value = preset.modelId;
    }
    if (preset.color) {
      pickColor(deviceId, preset.color);
    }
  }

  async function saveAssociation(deviceId) {
    const name = document.getElementById(`deviceName-${deviceId}`)?.value?.trim();
    const boatType = document.getElementById(`deviceBoatType-${deviceId}`)?.value;
    const modelId = document.getElementById(`deviceModel-${deviceId}`)?.value;
    const color = getEdit(deviceId, 'color', document.getElementById(`deviceColor-${deviceId}`)?.value);

    if (!name) {
      alert('Please enter a boat name.');
      return;
    }

    const body = {
      displayName: name,
      boatType: boatType || 'default',
      modelId: modelId || '470',
      color: color || '#2563eb'
    };

    const res = await fetch(`${getApiBase()}/boats/${encodeURIComponent(deviceId)}`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    const result = await res.json();
    if (!res.ok) {
      alert(result.error || 'Failed to save boat association');
      return;
    }
    clearEdits(deviceId);
    if (global.RuscScene && typeof global.RuscScene.onProfileSaved === 'function') {
      global.RuscScene.onProfileSaved(deviceId, result.profile);
    }
    if (typeof global.onBoatAssociationSaved === 'function') {
      global.onBoatAssociationSaved(deviceId, result.profile);
    }
    return result;
  }

  async function saveDeviceName(deviceId) {
    return saveAssociation(deviceId);
  }

  async function sendCalibration(deviceId, command) {
    const duration = command === 'calibrate_gyro' ? 8000 : 1000;
    const statusDiv = document.getElementById(`calibrationStatus-${deviceId}`);
    if (statusDiv) statusDiv.innerHTML = `Command pending: ${command}`;

    const res = await fetch(`${getApiBase()}/devices/${encodeURIComponent(deviceId)}/calibration`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ command, duration_ms: duration })
    });
    const result = await res.json();
    if (!res.ok) {
      alert(result.error || 'Calibration failed');
      return;
    }
    if (statusDiv) statusDiv.innerHTML = `Command ${result.command_id} sent: ${command}`;
  }

  function renderDeviceCard(deviceId, data, options = {}) {
    const showCalibration = options.showCalibration !== false;
    const displayName = getEdit(deviceId, 'displayName', data.display_name || deviceId);
    const boatType = getEdit(deviceId, 'boatType', data.boatType || 'default');
    const modelId = getEdit(deviceId, 'modelId', data.modelId || '470');
    const color = getEdit(deviceId, 'color', data.color || '#2563eb');
    const configured = Boolean(data.configured);
    const newBadge = configured ? '' : '<span class="device-badge-new">NEW</span>';

    let homeHtml = '';
    if (data.home_point) {
      homeHtml = `
        <div class="info-row"><span class="info-label">Home:</span>
          <span class="info-value">${data.home_point.latitude.toFixed(4)}° ${data.home_point.longitude.toFixed(4)}°</span></div>
        <div class="info-row"><span class="info-label">Distance:</span>
          <span class="info-value">${(data.current_distance || 0).toFixed(3)} km</span></div>
        ${options.showResetHome ? `<button onclick="resetHomePoint('${deviceId}')" style="width:100%;margin-top:8px;background:#f44336;padding:6px;">Reset Home</button>` : ''}`;
    }

    const altRow = data.altitude_m !== undefined
      ? `<div class="info-row"><span class="info-label">Alt:</span><span class="info-value">${Number(data.altitude_m).toFixed(2)} m</span></div>`
      : '';

    return `
      <div class="device-item" data-mac="${escapeHtml(deviceId)}" style="border-left-color:${escapeHtml(color)}">
        <div class="device-id">${escapeHtml(displayName)}${newBadge}</div>
        <div class="device-identity">MAC: ${escapeHtml(data.mac || deviceId)}</div>
        <div class="device-config-row">
          <label for="deviceName-${deviceId}">Boat name</label>
          <input class="device-name-input device-config-input" id="deviceName-${deviceId}" value="${escapeHtml(displayName)}"
            placeholder="Boat or athlete name" oninput="RuscDeviceUI.trackEdit('${deviceId}','displayName',this.value)">
        </div>
        <div class="device-config-row">
          <label for="deviceBoatType-${deviceId}">Boat type</label>
          <select class="device-config-input" id="deviceBoatType-${deviceId}" onchange="RuscDeviceUI.applyBoatTypePreset('${deviceId}')">
            ${typeOptionsHtml(boatType)}
          </select>
        </div>
        <div class="device-config-row">
          <label for="deviceModel-${deviceId}">Shape (3D model)</label>
          <select class="device-config-input" id="deviceModel-${deviceId}" onchange="RuscDeviceUI.trackEdit('${deviceId}','modelId',this.value)">
            ${modelOptionsHtml(modelId)}
          </select>
        </div>
        <div class="device-config-row">
          <label>Hull color</label>
          <div class="color-swatches">${colorSwatchesHtml(deviceId, color)}</div>
          <input class="device-config-input" type="text" id="deviceColor-${deviceId}" value="${escapeHtml(color)}"
            oninput="RuscDeviceUI.pickColor('${deviceId}', this.value)">
        </div>
        <button class="btn-save-association" onclick="RuscDeviceUI.saveAssociation('${deviceId}')">Save association</button>
        <div class="device-status"><span class="status-badge status-active"></span>Live telemetry</div>
        ${homeHtml}
        <div class="info-row"><span class="info-label">Lat:</span><span class="info-value">${data.position.latitude.toFixed(6)}</span></div>
        <div class="info-row"><span class="info-label">Lon:</span><span class="info-value">${data.position.longitude.toFixed(6)}</span></div>
        ${altRow}
        <div class="info-row"><span class="info-label">Speed:</span><span class="info-value">${data.speed_knots.toFixed(2)} kt</span></div>
        <div class="info-row"><span class="info-label">Course:</span><span class="info-value">${data.course.toFixed(1)}°</span></div>
        <div class="info-row"><span class="info-label">Roll:</span><span class="info-value">${(data.roll || 0).toFixed(1)}°</span></div>
        <div class="info-row"><span class="info-label">Pitch:</span><span class="info-value">${(data.pitch || 0).toFixed(1)}°</span></div>
        <div class="info-row"><span class="info-label">Yaw rate:</span><span class="info-value">${(data.yaw_rate || 0).toFixed(1)}°/s</span></div>
        <div class="info-row"><span class="info-label">Signal:</span>${signalMarkup(data.signal_strength || -90)}</div>
        <div class="info-row"><span class="info-label">Battery:</span><span class="info-value">${data.battery !== undefined ? data.battery + '%' : '-'}</span></div>
        <div class="info-row"><span class="info-label">HaLow:</span><span class="info-value">0x${(data.halow_status || 0).toString(16).padStart(2, '0')}</span></div>
        ${showCalibration ? `
        <div class="calibration-status" id="calibrationStatus-${deviceId}">
          <div class="info-row"><span class="info-label">IMU:</span><span class="info-value">${calibrationStateText(data.calibration_state || 0)}</span></div>
          <div class="info-row"><span class="info-label">Progress:</span><span class="info-value">${data.calibration_progress || 0}%</span></div>
        </div>
        <div class="controls vertical">
          <button onclick="RuscDeviceUI.sendCalibration('${deviceId}','calibrate_gyro')">Calibrate Gyro</button>
          <button onclick="RuscDeviceUI.sendCalibration('${deviceId}','set_level')">Set Boat Level</button>
        </div>` : ''}
        ${options.followButton ? `<button type="button" class="btn-follow-3d" style="width:100%;margin-top:8px;" data-mac="${escapeHtml(deviceId)}">Follow in 3D</button>` : ''}
      </div>`;
  }

  function updateDevicesPanel(positions, containerId, options = {}) {
    const el = document.getElementById(containerId);
    if (!el) return;
    if (hasActiveDeviceEdit()) return;

    let html = '';
    for (const [deviceId, data] of Object.entries(positions)) {
      html += renderDeviceCard(deviceId, data, options);
    }
    el.innerHTML = html || '<div class="error">No communicating devices right now.</div>';
    el.querySelectorAll('.btn-follow-3d').forEach((btn) => {
      btn.addEventListener('click', (ev) => {
        ev.preventDefault();
        const mac = btn.getAttribute('data-mac');
        if (mac && global.RuscScene) global.RuscScene.followBoat(mac);
      });
    });
  }

  global.RuscDeviceUI = {
    getApiBase,
    getWsUrl,
    loadCatalog,
    hasActiveDeviceEdit,
    trackEdit,
    pickColor,
    applyBoatTypePreset,
    saveAssociation,
    saveDeviceName,
    sendCalibration,
    renderDeviceCard,
    updateDevicesPanel,
    escapeHtml
  };
})(window);
