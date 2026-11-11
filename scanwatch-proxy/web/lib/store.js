import { parseFloor, visibleMessage } from "./floors.js";
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

export async function recordPhoneFloor(floor) {
  const parsed = parseFloor(floor);
  const message = visibleMessage(parsed);
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
  return getState();
}

export async function setPhone(floor, watchConnected) {
  if (floor != null) {
    await recordPhoneFloor(floor);
  }
  if (watchConnected) {
    await sql(
      `update household_state
          set watch_connected = true,
              updated_at = now()
        where id = 1`,
    );
  }
  return getState();
}

export async function queueMissed(floor) {
  return recordPhoneFloor(floor);
}

export async function ackDelivery(error) {
  if (error) {
    await sql(
      `update household_state
          set last_error = $1,
              updated_at = now()
        where id = 1`,
      [String(error).slice(0, 500)],
    );
    return getState();
  }
  await sql(
    `update household_state
        set last_error = null,
            updated_at = now()
      where id = 1`,
  );
  return getState();
}
