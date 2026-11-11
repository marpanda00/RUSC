import { missedMessage, parseFloor } from "./floors.js";
import { sql } from "./db.js";

function rowToState(row) {
  return {
    floor: row?.floor ?? null,
    watch_connected: Boolean(row?.watch_connected),
    pending_vibrations: row?.pending_vibrations ?? null,
    last_message: row?.last_message ?? null,
    last_error: row?.last_error ?? null,
    updated_at: row?.updated_at ?? null,
  };
}

export async function getState() {
  const rows = await sql(
    "select floor, watch_connected, pending_vibrations, last_message, last_error, updated_at from household_state where id = 1",
  );
  return rowToState(rows[0]);
}

export async function setPhone(floor, watchConnected) {
  const parsed = floor == null ? null : parseFloor(floor);
  await sql(
    `update household_state
        set floor = coalesce($1, floor),
            watch_connected = $2,
            updated_at = now()
      where id = 1`,
    [parsed, watchConnected],
  );
  if (!watchConnected && parsed != null) {
    return queueMissed(parsed);
  }
  return getState();
}

export async function queueMissed(floor) {
  const parsed = parseFloor(floor);
  const message = missedMessage(parsed);
  await sql(
    `update household_state
        set floor = $1,
            watch_connected = false,
            pending_vibrations = $1,
            last_message = $2,
            last_error = null,
            updated_at = now()
      where id = 1`,
    [parsed, message],
  );
  const state = await getState();
  return {
    ...state,
    alert: {
      message,
      vibrations: parsed,
      floor: parsed,
    },
  };
}
