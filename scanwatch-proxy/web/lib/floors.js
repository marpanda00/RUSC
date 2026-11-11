export const FLOOR_NAMES = {
  1: "first floor",
  2: "second floor",
  3: "third floor",
};

export function parseFloor(value) {
  const aliases = {
    1: 1,
    2: 2,
    3: 3,
    first: 1,
    second: 2,
    third: 3,
    "first floor": 1,
    "second floor": 2,
    "third floor": 3,
  };
  const key =
    typeof value === "string" ? value.trim().toLowerCase() : Number(value);
  const floor = aliases[key];
  if (!floor) {
    throw new Error("Pick first, second or third floor");
  }
  return floor;
}

export function missedMessage(floor) {
  return `Missed MarioPhone on ${FLOOR_NAMES[parseFloor(floor)]}`;
}
