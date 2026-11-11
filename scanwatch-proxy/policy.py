"""Map a missed MarioPhone floor to ScanWatch vibration count."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

FLOOR_NAMES = {
    1: "first floor",
    2: "second floor",
    3: "third floor",
}

PHONE_NAME = "MarioPhone"


class UnknownFloor(ValueError):
    pass


def parse_floor(value) -> int:
    if isinstance(value, bool) or value is None:
        raise UnknownFloor(f"unknown floor: {value!r}")
    if isinstance(value, str):
        text = value.strip().lower().replace("_", " ")
        aliases = {
            "1": 1,
            "first": 1,
            "first floor": 1,
            "1st": 1,
            "2": 2,
            "second": 2,
            "second floor": 2,
            "2nd": 2,
            "3": 3,
            "third": 3,
            "third floor": 3,
            "3rd": 3,
        }
        if text in aliases:
            return aliases[text]
        raise UnknownFloor(f"unknown floor: {value!r}")
    try:
        floor = int(value)
    except (TypeError, ValueError) as exc:
        raise UnknownFloor(f"unknown floor: {value!r}") from exc
    if floor not in FLOOR_NAMES:
        raise UnknownFloor(f"unknown floor: {value!r}")
    return floor


def vibrations_for_floor(floor: int) -> int:
    floor = parse_floor(floor)
    return floor


def missed_message(floor: int) -> str:
    return f"Missed {PHONE_NAME} on {FLOOR_NAMES[parse_floor(floor)]}"


@dataclass(frozen=True)
class PhoneSighting:
    floor: Optional[int]
    watch_connected: bool


@dataclass(frozen=True)
class MissedAlert:
    floor: int
    vibrations: int
    message: str


def missed_alert(sighting: PhoneSighting) -> Optional[MissedAlert]:
    """Alert only when the phone is out of watch range and a floor is known."""
    if sighting.watch_connected or sighting.floor is None:
        return None
    floor = parse_floor(sighting.floor)
    return MissedAlert(
        floor=floor,
        vibrations=vibrations_for_floor(floor),
        message=missed_message(floor),
    )
