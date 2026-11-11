"""Phone BLE sets the floor. Buzz only while a proxy can see the ScanWatch."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Optional

WATCH_GONE_S = 8.0
PHONE_GONE_S = 8.0
MARIOPHONE_NAME = "mariophone"
MARIOPHONE_SERVICE = "b10b10b1-0b10-b10b-10b1-0b10b10b10b1"


@dataclass(frozen=True)
class VisibleBuzz:
    floor: int


def advertisement_kind(
    name: Optional[str],
    address: Optional[str],
    service_uuids: Optional[Iterable[str]] = None,
    *,
    phone_mac: Optional[str] = None,
    watch_mac: Optional[str] = None,
    phone_name: str = MARIOPHONE_NAME,
) -> Optional[str]:
    addr = (address or "").lower()
    label = (name or "").lower()
    services = [str(uuid).lower() for uuid in (service_uuids or ())]
    if phone_mac and addr == phone_mac.lower():
        return "phone"
    if MARIOPHONE_SERVICE in services or phone_name.lower() in label:
        return "phone"
    if watch_mac and addr == watch_mac.lower():
        return "watch"
    if "scanwatch" in label:
        return "watch"
    return None


class PresenceGate:
    """Floor from MarioPhone sightings. Buzz when the ScanWatch is visible."""

    def __init__(self, hold_s: float = WATCH_GONE_S) -> None:
        self.hold_s = hold_s
        self.floor: Optional[int] = None
        self.phone_visible = False
        self.watch_visible = False
        self.phone_last_seen = 0.0
        self.watch_last_seen = 0.0
        self.fired_this_watch = False

    def expire(self, now: float) -> None:
        if self.phone_visible and now - self.phone_last_seen > PHONE_GONE_S:
            self.phone_visible = False
        if self.watch_visible and now - self.watch_last_seen > self.hold_s:
            self.watch_visible = False
            self.fired_this_watch = False

    def record_floor(self, floor: int, now: float) -> None:
        self.expire(now)
        self.floor = floor
        self.phone_visible = True
        self.phone_last_seen = now

    def note_watch(self, now: float) -> None:
        self.expire(now)
        self.watch_visible = True
        self.watch_last_seen = now

    def consider_buzz(self, now: float) -> Optional[VisibleBuzz]:
        self.expire(now)
        if self.floor is None or not self.watch_visible or self.fired_this_watch:
            return None
        self.fired_this_watch = True
        return VisibleBuzz(self.floor)
