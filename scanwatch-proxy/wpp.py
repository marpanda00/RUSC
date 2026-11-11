"""Minimal Withings WPP codec for probe auth and vibrator commands."""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from typing import Optional

PROTOCOL_VERSION = 0x01
HEADER_LEN = 5
OBJECT_HEADER_LEN = 4
OPCODE_MASK = 0x3FFF

CMD_ERROR = 256
CMD_PROBE = 257
CMD_SETUP_OK = 275
CMD_PROBE_CHALLENGE = 296
CMD_ASSOCIATION_KEYS_SET = 308
CMD_VIBRATOR = 2490

TYPE_PROBE_REPLY = 257
TYPE_CMDERROR = 272
TYPE_PROBE_CHALLENGE = 290
TYPE_PROBE_CHALLENGE_RESPONSE = 291
TYPE_APP_PROBE = 298
TYPE_ACCOUNT_KEY = 309
TYPE_ADV_KEY = 310
TYPE_APP_PROBE_OS_VERSION = 2344
TYPE_VIBRATOR_SET_SIMPLE = 2511

ERR_AUTH_ERR = -6
ERR_NOT_AUTH = -5
ERR_CMDUNKN = -3

SECRET_LEN = 32
APP_PROBE_VERSION = 8070101
APP_PROBE_OS_VERSION = 35

VIBRATE_ON_MS = 250
VIBRATE_OFF_MS = 350


class WppError(ValueError):
    pass


def _u16(value: int) -> bytes:
    return struct.pack(">H", value)


def _u32(value: int) -> bytes:
    return struct.pack(">I", value)


def encode_bytes(value: bytes) -> bytes:
    if len(value) > 255:
        raise WppError("length-prefixed field exceeds 255 bytes")
    return bytes([len(value)]) + value


def encode_string(value: str) -> bytes:
    return encode_bytes(value.encode("utf-8"))


def read_bytes(buf: bytes, offset: int) -> tuple[bytes, int]:
    if offset >= len(buf):
        raise WppError("truncated length-prefixed field")
    size = buf[offset]
    start = offset + 1
    end = start + size
    if end > len(buf):
        raise WppError("truncated length-prefixed field")
    return buf[start:end], end


def read_string(buf: bytes, offset: int) -> tuple[str, int]:
    raw, end = read_bytes(buf, offset)
    return raw.decode("utf-8"), end


@dataclass(frozen=True)
class WppObject:
    type_id: int
    data: bytes

    def to_bytes(self) -> bytes:
        return _u16(self.type_id) + _u16(len(self.data)) + self.data


@dataclass(frozen=True)
class WppFrame:
    command: int
    objects: tuple[WppObject, ...]

    @property
    def opcode(self) -> int:
        return self.command & OPCODE_MASK

    def payload(self) -> bytes:
        return b"".join(obj.to_bytes() for obj in self.objects)

    def to_bytes(self) -> bytes:
        payload = self.payload()
        return bytes([PROTOCOL_VERSION]) + _u16(self.command) + _u16(len(payload)) + payload

    def first(self, type_id: int) -> Optional[WppObject]:
        for obj in self.objects:
            if obj.type_id == type_id:
                return obj
        return None


def parse_frame(buf: bytes) -> WppFrame:
    if len(buf) < HEADER_LEN:
        raise WppError("short WPP header")
    if buf[0] != PROTOCOL_VERSION:
        raise WppError(f"unsupported WPP version 0x{buf[0]:02x}")
    command = int.from_bytes(buf[1:3], "big")
    declared = int.from_bytes(buf[3:5], "big")
    if len(buf) < HEADER_LEN + declared:
        raise WppError("incomplete WPP payload")
    if len(buf) != HEADER_LEN + declared:
        raise WppError("trailing bytes after WPP frame")
    payload = buf[HEADER_LEN:]
    objects = []
    pos = 0
    while pos + OBJECT_HEADER_LEN <= len(payload):
        type_id = int.from_bytes(payload[pos : pos + 2], "big")
        size = int.from_bytes(payload[pos + 2 : pos + 4], "big")
        start = pos + OBJECT_HEADER_LEN
        end = start + size
        if end > len(payload):
            raise WppError("truncated WPP object")
        objects.append(WppObject(type_id, payload[start:end]))
        pos = end
    if pos != declared:
        raise WppError("WPP payload length mismatch")
    return WppFrame(command, tuple(objects))


class FrameAssembler:
    """Rejoins GATT notifications until one complete WPP frame is present."""

    def __init__(self) -> None:
        self._buf = bytearray()

    def feed(self, chunk: bytes) -> list[WppFrame]:
        self._buf.extend(chunk)
        frames: list[WppFrame] = []
        while True:
            if len(self._buf) < HEADER_LEN:
                return frames
            if self._buf[0] != PROTOCOL_VERSION:
                del self._buf[0]
                continue
            declared = int.from_bytes(self._buf[3:5], "big")
            needed = HEADER_LEN + declared
            if len(self._buf) < needed:
                return frames
            raw = bytes(self._buf[:needed])
            del self._buf[:needed]
            frames.append(parse_frame(raw))


def app_probe_object() -> WppObject:
    return WppObject(
        TYPE_APP_PROBE,
        bytes([1, 1]) + _u32(APP_PROBE_VERSION),
    )


def probe_frame() -> WppFrame:
    return WppFrame(
        CMD_PROBE,
        (
            app_probe_object(),
            WppObject(TYPE_APP_PROBE_OS_VERSION, _u16(APP_PROBE_OS_VERSION)),
        ),
    )


def normalize_mac(mac: str) -> str:
    parts = mac.strip().lower().replace("-", ":").split(":")
    if len(parts) != 6 or any(len(part) != 2 or any(c not in "0123456789abcdef" for c in part) for part in parts):
        raise WppError(f"{mac!r} is not a MAC address")
    return ":".join(parts)


def challenge_answer(challenge: bytes, mac: str, secret: str) -> bytes:
    identity = normalize_mac(mac)
    if len(secret) != SECRET_LEN or not secret.isascii():
        raise WppError("association secret must be 32 ASCII characters")
    return hashlib.sha1(challenge + identity.encode("ascii") + secret.encode("ascii")).digest()


def parse_probe_challenge(data: bytes) -> tuple[str, bytes]:
    mac, offset = read_string(data, 0)
    challenge, end = read_bytes(data, offset)
    if end != len(data):
        raise WppError("trailing bytes in probe challenge")
    return mac, challenge


def parse_probe_reply_mac(data: bytes) -> str:
    offset = 4  # vid, pid
    _name, offset = read_string(data, offset)
    mac, _offset = read_string(data, offset)
    return mac


def parse_cmderror(data: bytes) -> tuple[int, int]:
    if len(data) < 6:
        raise WppError("truncated CMDERROR")
    cmd = int.from_bytes(data[:2], "big")
    err = int.from_bytes(data[2:6], "big", signed=True)
    return cmd, err


def challenge_reply_frame(mac: str, challenge: bytes, secret: str) -> WppFrame:
    identity = normalize_mac(mac)
    return WppFrame(
        CMD_PROBE_CHALLENGE,
        (
            WppObject(TYPE_PROBE_CHALLENGE_RESPONSE, encode_bytes(challenge_answer(challenge, identity, secret))),
            WppObject(TYPE_PROBE_CHALLENGE, encode_string(identity) + encode_bytes(bytes(16))),
        ),
    )


def association_keys_frame(account_id: int, secret: str) -> WppFrame:
    if account_id == 0:
        raise WppError("account id must be non-zero")
    if len(secret) != SECRET_LEN or not secret.isascii():
        raise WppError("association secret must be 32 ASCII characters")
    return WppFrame(
        CMD_ASSOCIATION_KEYS_SET,
        (
            WppObject(TYPE_ACCOUNT_KEY, _u32(account_id) + encode_string(secret)),
            WppObject(TYPE_ADV_KEY, encode_string(secret)),
        ),
    )


def setup_ok_frame() -> WppFrame:
    return WppFrame(CMD_SETUP_OK, ())


def vibrator_frame(loops: int, on_ms: int = VIBRATE_ON_MS, off_ms: int = VIBRATE_OFF_MS) -> WppFrame:
    if loops < 1 or loops > 3:
        raise WppError("vibration count must be 1, 2 or 3")
    data = _u16(on_ms) + _u16(off_ms) + bytes([loops])
    return WppFrame(CMD_VIBRATOR, (WppObject(TYPE_VIBRATOR_SET_SIMPLE, data),))


def parse_vibrator(frame: WppFrame) -> tuple[int, int, int]:
    obj = frame.first(TYPE_VIBRATOR_SET_SIMPLE)
    if frame.opcode != CMD_VIBRATOR or obj is None or len(obj.data) != 5:
        raise WppError("not a vibrator frame")
    on_ms = int.from_bytes(obj.data[0:2], "big")
    off_ms = int.from_bytes(obj.data[2:4], "big")
    return on_ms, off_ms, obj.data[4]


def rejects_probe(frame: WppFrame) -> bool:
    if frame.opcode != CMD_ERROR:
        return False
    obj = frame.first(TYPE_CMDERROR)
    if obj is None:
        return False
    cmd, err = parse_cmderror(obj.data)
    return cmd in (CMD_PROBE, CMD_PROBE_CHALLENGE) and err in (ERR_NOT_AUTH, ERR_AUTH_ERR)
