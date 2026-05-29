/**
 * Virtual Eye Binary Packet Encoder
 *
 * Encodes telemetry objects into the DLE-framed binary protocol used by
 * the Virtual Eye viewer for both .bin playback files and live WebSocket streams.
 *
 * Framing:
 *   0x10          = frame start (DLE)
 *   0x10 0x03     = frame end   (DLE ETX)
 *   0x10 0x10     = escaped literal 0x10 inside payload
 */

const DLE = 0x10;
const ETX = 0x03;

const PACKET_BOAT   = 0xB3;  // 179
const PACKET_WIND   = 0xB2;  // 178
const PACKET_COURSE = 0xB1;  // 177

// ── Helpers ──────────────────────────────────────────────────────────────────

/**
 * Escape DLE bytes inside payload and wrap with DLE / DLE ETX frame.
 * @param {Buffer} payload  Raw packet payload (type byte + data, no framing)
 * @returns {Buffer}
 */
function frame(payload) {
  const escaped = [];
  for (const byte of payload) {
    if (byte === DLE) escaped.push(DLE, DLE);  // escape
    else              escaped.push(byte);
  }
  return Buffer.from([DLE, ...escaped, DLE, ETX]);
}

/**
 * Encode a boatId from its components.
 * @param {number} teamId     0-511  (matches team_id in appconfig.json)
 * @param {number} yachtId    0-63   (boat number within team)
 * @param {number} trailColor 0 or 1 (trail color variant)
 */
function encodeBoatId(teamId, yachtId, trailColor = 0) {
  return ((trailColor & 1) << 15) | ((teamId & 0x1FF) << 6) | (yachtId & 0x3F);
}

// ── Boat Packet (type 0xB3, version 8) ───────────────────────────────────────

/**
 * @param {object} p
 * @param {number} p.raceId        - Race identifier (u16)
 * @param {number} p.teamId        - Team ID (matches appconfig.json team_id)
 * @param {number} p.yachtId       - Boat number within team (0-63)
 * @param {number} p.timeSeconds   - Timestamp in seconds (float OK)
 * @param {number} p.lat           - WGS84 latitude  (decimal degrees)
 * @param {number} p.lon           - WGS84 longitude (decimal degrees)
 * @param {number} p.headingDeg    - Heading 0-360°
 * @param {number} p.heelDeg       - Roll/heel -180 to +180° (IMU roll)
 * @param {number} p.pitchDeg      - Pitch -180 to +180°     (IMU pitch)
 * @param {number} p.speedKnots    - Speed through water in knots
 * @param {number} [p.elevationM]  - Elevation above sea level in metres (default 0)
 * @param {number} [p.dtlMetres]   - Distance to leader in metres (default 0)
 * @param {number} [p.rank]        - Current rank (default 0)
 * @param {number} [p.currentLeg]  - Current leg number (default 0)
 * @param {number} [p.foilState]   - 0=hull, 1=foiling (default 0)
 * @param {number} [p.rudderDeg]   - Rudder angle -90 to +90° (default 0)
 * @param {number} [p.trailColor]  - 0 or 1 (default 0)
 * @returns {Buffer} Framed binary packet ready to send
 */
function encodeBoatPacket(p) {
  const boatId    = encodeBoatId(p.teamId, p.yachtId, p.trailColor || 0);
  const elevation = Math.round(((p.elevationM || 0) * 1000) + 32768);
  const dtl       = Math.round((p.dtlMetres || 0) * 1000);

  const buf = Buffer.alloc(48);
  let i = 0;

  buf[i++] = PACKET_BOAT;
  buf[i++] = 8;                                         // version
  buf.writeUInt16BE(p.raceId & 0xFFFF, i);   i += 2;
  buf.writeUInt16BE(boatId,            i);   i += 2;
  buf.writeUInt32BE(Math.round(p.timeSeconds * 100) & 0xFFFFFFFF, i); i += 4;
  buf.writeInt32BE(Math.round(p.lat * 1e7),  i);        i += 4;
  buf.writeInt32BE(Math.round(p.lon * 1e7),  i);        i += 4;
  buf.writeUInt16BE(Math.min(65535, Math.max(0, elevation)), i); i += 2;
  buf.writeUInt16BE(Math.round(p.headingDeg * 100) & 0xFFFF, i); i += 2;
  buf.writeUInt16BE(Math.round((p.heelDeg  + 180) * 100) & 0xFFFF, i); i += 2;
  buf.writeUInt16BE(Math.round((p.pitchDeg + 180) * 100) & 0xFFFF, i); i += 2;
  buf[i++] = 0;                                         // sails (unused)
  buf[i++] = 0;                                         // status
  buf.writeUInt16BE(Math.round(p.speedKnots * 100) & 0xFFFF, i); i += 2;
  // dtl is 3 bytes (u24)
  buf[i++] = (dtl >> 16) & 0xFF;
  buf[i++] = (dtl >>  8) & 0xFF;
  buf[i++] =  dtl        & 0xFF;
  // version 8 extra fields
  const flyTime = 0;
  buf.writeUInt16BE(Math.round((flyTime + 100) * 100), i); i += 2;
  buf[i++] = 0; buf[i++] = 0; buf[i++] = 0;            // skip 3
  buf[i++] = (p.rank       || 0) & 0xFF;
  buf[i++] = (p.currentLeg || 0) & 0xFF;
  buf[i++] = (p.foilState  || 0) & 0xFF;
  buf[i++] = Math.round((p.rudderDeg || 0) + 90) & 0xFF;

  return frame(buf.slice(0, i));
}

// ── Wind Packet (type 0xB2) ───────────────────────────────────────────────────

/**
 * @param {object} w
 * @param {number} w.raceId       - Race identifier
 * @param {number} w.timeSeconds  - Timestamp
 * @param {number} w.windDeg      - True wind direction 0-360°
 * @param {number} w.windKnots    - True wind speed in knots
 * @returns {Buffer}
 */
/**
 * Wind packet layout (viewer parsePacket, type 0xB2):
 *   version 1: raceId u16, time u24, speed u16 (/1000), heading u16 (/100),
 *              upwind_layline u16 (/100), downwind_layline u16 (/100)  → 15 bytes
 *   version ≥2: time is u32 instead of u24
 */
function encodeWindPacket(w) {
  const buf = Buffer.alloc(16);
  let i = 0;
  buf[i++] = PACKET_WIND;
  buf[i++] = 1; // match ACWS .bin recordings (version 1)
  buf.writeUInt16BE(w.raceId & 0xFFFF, i);
  i += 2;
  const t = Math.round(w.timeSeconds * 100);
  buf[i++] = (t >> 16) & 0xff;
  buf[i++] = (t >> 8) & 0xff;
  buf[i++] = t & 0xff;
  const speedRaw = Math.round((w.windKnots ?? 0) * 1000);
  const headingRaw = Math.round((w.windDeg ?? 0) * 100);
  const layline = Math.round((w.laylineDeg ?? 39) * 100);
  buf.writeUInt16BE(speedRaw & 0xffff, i);
  i += 2;
  buf.writeUInt16BE(headingRaw & 0xffff, i);
  i += 2;
  buf.writeUInt16BE(layline & 0xffff, i);
  i += 2;
  buf.writeUInt16BE(layline & 0xffff, i);
  i += 2;
  return frame(buf.slice(0, i));
}

// ── .bin File Writer ──────────────────────────────────────────────────────────

/**
 * Write an array of telemetry frames to a .bin file (for offline playback).
 * @param {object[]} records  Array of {type:'boat'|'wind', ...fields}
 * @returns {Buffer}  Complete .bin file buffer
 */
function encodeBinFile(records) {
  const chunks = [];
  for (const r of records) {
    if (r.type === 'boat') chunks.push(encodeBoatPacket(r));
    if (r.type === 'wind') chunks.push(encodeWindPacket(r));
  }
  return Buffer.concat(chunks);
}

module.exports = { encodeBoatPacket, encodeWindPacket, encodeBinFile, encodeBoatId };
