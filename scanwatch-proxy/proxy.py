#!/usr/bin/env python3
"""House BLE proxy: vibrate the ScanWatch once per MarioPhone floor when the phone is out of reach."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import time
from dataclasses import asdict, dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Lock
from typing import Optional
from urllib.parse import parse_qs, urlparse

from policy import MissedAlert, PhoneSighting, missed_alert, parse_floor
from wpp import (
    CMD_ASSOCIATION_KEYS_SET,
    CMD_ERROR,
    CMD_PROBE,
    CMD_PROBE_CHALLENGE,
    CMD_SETUP_OK,
    TYPE_CMDERROR,
    TYPE_PROBE_CHALLENGE,
    TYPE_PROBE_REPLY,
    FrameAssembler,
    WppError,
    association_keys_frame,
    challenge_reply_frame,
    parse_cmderror,
    parse_probe_challenge,
    parse_probe_reply_mac,
    probe_frame,
    rejects_probe,
    setup_ok_frame,
    vibrator_frame,
)

WITHINGS_SERVICE_MARKER = "5749-5448"
WITHINGS_CHARACTERISTIC_MARKER = "494e4753"
LEGACY_CHARACTERISTIC_MARKER = "5749-5448-005d"
PROTOCOL_HANDLE = "4"
DEFAULT_PORT = 8787
DEFAULT_COOLDOWN_S = 90.0
LINK_TIMEOUT_S = 20.0


@dataclass
class ProxyState:
    floor: Optional[int] = None
    watch_connected: bool = False
    last_phone_at: Optional[float] = None
    last_alert: Optional[dict] = None
    last_error: Optional[str] = None
    vibrating: bool = False


class WatchSession:
    """WPP session over a GATT write/notify characteristic."""

    def __init__(self, writer, reader_queue: asyncio.Queue, secret: str, account_id: int):
        self._write = writer
        self._incoming = reader_queue
        self._secret = secret
        self._account_id = account_id

    async def _send(self, frame) -> None:
        await self._write(frame.to_bytes())

    async def _recv(self, timeout: float = LINK_TIMEOUT_S):
        return await asyncio.wait_for(self._incoming.get(), timeout=timeout)

    async def authenticate(self) -> str:
        await self._send(probe_frame())
        while True:
            frame = await self._recv()
            if rejects_probe(frame):
                raise WppError("watch refused probe; it is associated with another key")
            if frame.opcode == CMD_ERROR:
                obj = frame.first(TYPE_CMDERROR)
                if obj is not None:
                    cmd, err = parse_cmderror(obj.data)
                    if cmd == CMD_ASSOCIATION_KEYS_SET:
                        raise WppError(f"association keys rejected ({err})")
                continue
            if frame.opcode == CMD_PROBE_CHALLENGE:
                obj = frame.first(TYPE_PROBE_CHALLENGE)
                if obj is None:
                    continue
                mac, challenge = parse_probe_challenge(obj.data)
                await self._send(challenge_reply_frame(mac, challenge, self._secret))
                ack = await self._recv()
                if rejects_probe(ack):
                    raise WppError("watch rejected the association secret")
                return mac
            if frame.opcode == CMD_PROBE:
                obj = frame.first(TYPE_PROBE_REPLY)
                if obj is None:
                    continue
                mac = parse_probe_reply_mac(obj.data)
                await self._send(association_keys_frame(self._account_id, self._secret))
                keys = await self._recv()
                if keys.opcode == CMD_ERROR:
                    obj = keys.first(TYPE_CMDERROR)
                    if obj is not None:
                        _cmd, err = parse_cmderror(obj.data)
                        if err != -3:
                            raise WppError(f"free watch rejected association ({err})")
                await self._send(setup_ok_frame())
                try:
                    await self._recv(timeout=3.0)
                except asyncio.TimeoutError:
                    pass
                return mac

    async def vibrate(self, loops: int) -> None:
        await self._send(vibrator_frame(loops))


def find_protocol_characteristic(services) -> object:
    for service in services:
        if WITHINGS_SERVICE_MARKER not in str(service.uuid).lower():
            continue
        for char in service.characteristics:
            ident = str(char.uuid).lower()
            if (
                WITHINGS_CHARACTERISTIC_MARKER in ident or LEGACY_CHARACTERISTIC_MARKER in ident
            ) and (len(ident) > 7 and ident[7] == PROTOCOL_HANDLE):
                return char
            if "notify" in getattr(char, "properties", ()) and "write" in getattr(char, "properties", ()):
                return char
    raise WppError("no Withings WPP characteristic found")


class ScanwatchProxy:
    def __init__(
        self,
        mac: Optional[str],
        secret: str,
        account_id: int,
        cooldown_s: float,
        ble_connect=None,
    ):
        self.mac = mac
        self.secret = secret
        self.account_id = account_id
        self.cooldown_s = cooldown_s
        self._ble_connect = ble_connect
        self._lock = Lock()
        self.state = ProxyState()
        self._last_sent_at = 0.0

    def snapshot(self) -> dict:
        with self._lock:
            data = asdict(self.state)
        data["watch_mac"] = self.mac
        data["cooldown_s"] = self.cooldown_s
        return data

    def note_phone(self, floor: Optional[int], watch_connected: bool) -> Optional[MissedAlert]:
        with self._lock:
            was_connected = self.state.watch_connected
            previous_floor = self.state.floor
            first_report = self.state.last_phone_at is None
            if floor is not None:
                self.state.floor = floor
            self.state.watch_connected = watch_connected
            self.state.last_phone_at = time.time()
            sighting = PhoneSighting(self.state.floor, watch_connected)
        if watch_connected:
            return None
        alert = missed_alert(sighting)
        if alert is None:
            return None
        lost_watch = was_connected and not watch_connected
        floor_changed = floor is not None and floor != previous_floor
        if lost_watch or first_report or floor_changed:
            return alert
        return None

    def force_missed(self, floor: Optional[int] = None) -> MissedAlert:
        with self._lock:
            if floor is not None:
                self.state.floor = floor
            sighting = PhoneSighting(self.state.floor, watch_connected=False)
        alert = missed_alert(sighting)
        if alert is None:
            raise WppError("need a floor (1, 2 or 3) before vibrating")
        return alert

    async def deliver(self, alert: MissedAlert) -> dict:
        now = time.time()
        with self._lock:
            if self.state.vibrating:
                raise WppError("a vibration is already in flight")
            if now - self._last_sent_at < self.cooldown_s:
                wait = int(self.cooldown_s - (now - self._last_sent_at))
                raise WppError(f"cooldown: wait {wait}s before another alert")
            self.state.vibrating = True
            self.state.last_error = None
        try:
            if self._ble_connect is None:
                result = {
                    "delivered": False,
                    "dry_run": True,
                    "message": alert.message,
                    "vibrations": alert.vibrations,
                    "frame_hex": vibrator_frame(alert.vibrations).to_bytes().hex(),
                }
            else:
                await self._ble_connect(self, alert)
                result = {
                    "delivered": True,
                    "dry_run": False,
                    "message": alert.message,
                    "vibrations": alert.vibrations,
                }
            with self._lock:
                self._last_sent_at = time.time()
                self.state.last_alert = {
                    **result,
                    "floor": alert.floor,
                    "at": self._last_sent_at,
                }
            return result
        except Exception as exc:
            with self._lock:
                self.state.last_error = str(exc)
            raise
        finally:
            with self._lock:
                self.state.vibrating = False


async def connect_and_vibrate(proxy: ScanwatchProxy, alert: MissedAlert) -> None:
    from bleak import BleakClient, BleakScanner

    device = None
    if proxy.mac:
        device = await BleakScanner.find_device_by_address(proxy.mac, timeout=8.0)
    if device is None:
        for candidate in await BleakScanner.discover(timeout=8.0):
            name = (candidate.name or "").lower()
            if "scanwatch" in name:
                device = candidate
                break
    if device is None:
        raise WppError("ScanWatch not advertising; phone may still own the link")

    incoming: asyncio.Queue = asyncio.Queue()
    assembler = FrameAssembler()

    def on_notify(_handle, data: bytearray) -> None:
        for frame in assembler.feed(bytes(data)):
            incoming.put_nowait(frame)

    async with BleakClient(device, timeout=LINK_TIMEOUT_S) as client:
        char = find_protocol_characteristic(client.services)
        await client.start_notify(char, on_notify)

        async def writer(payload: bytes) -> None:
            mtu = getattr(client, "mtu_size", 23) or 23
            chunk = max(mtu - 3, 1)
            for offset in range(0, len(payload), chunk):
                await client.write_gatt_char(char, payload[offset : offset + chunk], response=True)

        session = WatchSession(writer, incoming, proxy.secret, proxy.account_id)
        await session.authenticate()
        await session.vibrate(alert.vibrations)
        await asyncio.sleep(alert.vibrations * 0.7)


INDEX_PAGE = """<!doctype html>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Missed MarioPhone</title>
<style>
 body { font-family: sans-serif; max-width: 28rem; margin: 1.5rem auto; padding: 0 1rem; }
 button, label { display: block; width: 100%; margin: .4rem 0; padding: .8rem; font-size: 1rem; }
 pre { background: #111; color: #eee; padding: .8rem; overflow: auto; }
</style>
<h1>Missed MarioPhone</h1>
<p>When the watch can be reached and the phone cannot, the proxy vibrates once per floor.</p>
<button data-floor="1">First floor — 1 vibration</button>
<button data-floor="2">Second floor — 2 vibrations</button>
<button data-floor="3">Third floor — 3 vibrations</button>
<label><input type="checkbox" id="connected"> Watch still connected to the phone</label>
<button id="missed">Phone is out of reach — buzz the watch</button>
<pre id="out">loading…</pre>
<script>
async function refresh() {
  const r = await fetch('/status');
  out.textContent = JSON.stringify(await r.json(), null, 2);
}
async function send(path, body) {
  const r = await fetch(path, {method:'POST', headers:{'content-type':'application/json'}, body: JSON.stringify(body)});
  out.textContent = JSON.stringify(await r.json(), null, 2);
}
document.querySelectorAll('[data-floor]').forEach(btn => btn.onclick = () => send('/phone', {
  floor: Number(btn.dataset.floor), watch_connected: connected.checked
}));
missed.onclick = () => send('/missed', {watch_connected: false});
refresh();
setInterval(refresh, 4000);
</script>
"""


def json_body(handler: BaseHTTPRequestHandler) -> dict:
    length = int(handler.headers.get("Content-Length") or 0)
    if length == 0:
        return {}
    raw = handler.rfile.read(length)
    if not raw:
        return {}
    try:
        data = json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise WppError(f"invalid JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise WppError("JSON body must be an object")
    return data


def truthy(value) -> bool:
    if isinstance(value, bool):
        return value
    if value is None:
        return False
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def make_handler(proxy: ScanwatchProxy, loop: asyncio.AbstractEventLoop):
    class Handler(BaseHTTPRequestHandler):
        def _write(self, status: int, payload, content_type: str = "application/json") -> None:
            body = payload if isinstance(payload, bytes) else json.dumps(payload).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, fmt: str, *args) -> None:
            print(f"http {self.address_string()} {fmt % args}")

        def do_GET(self) -> None:
            path = urlparse(self.path).path
            if path in ("/", "/index.html"):
                self._write(200, INDEX_PAGE.encode("utf-8"), "text/html; charset=utf-8")
                return
            if path == "/status":
                self._write(200, proxy.snapshot())
                return
            self._write(404, {"error": "not found"})

        def do_POST(self) -> None:
            parsed = urlparse(self.path)
            path = parsed.path
            query = {key: values[-1] for key, values in parse_qs(parsed.query).items()}
            try:
                body = json_body(self)
            except WppError as exc:
                self._write(400, {"error": str(exc)})
                return
            merged = {**query, **body}
            try:
                if path == "/phone":
                    floor = parse_floor(merged["floor"]) if "floor" in merged else None
                    watch_connected = truthy(merged.get("watch_connected", merged.get("in_range")))
                    alert = proxy.note_phone(floor, watch_connected)
                    delivered = None
                    if alert is not None:
                        delivered = asyncio.run_coroutine_threadsafe(
                            proxy.deliver(alert), loop
                        ).result()
                    self._write(200, {"ok": True, "state": proxy.snapshot(), "alert": delivered})
                    return
                if path == "/missed":
                    floor = parse_floor(merged["floor"]) if "floor" in merged else None
                    alert = proxy.force_missed(floor)
                    delivered = asyncio.run_coroutine_threadsafe(proxy.deliver(alert), loop).result()
                    self._write(200, {"ok": True, "alert": delivered, "state": proxy.snapshot()})
                    return
                self._write(404, {"error": "not found"})
            except (WppError, ValueError) as exc:
                self._write(400, {"error": str(exc), "state": proxy.snapshot()})
            except Exception as exc:
                self._write(500, {"error": str(exc), "state": proxy.snapshot()})

    return Handler


def env_secret() -> str:
    secret = os.environ.get("SCANWATCH_SECRET", "")
    if len(secret) == 32:
        return secret
    return "CHANGE_ME_TO_32_CHAR_ASCII_SECRET"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=int(os.environ.get("SCANWATCH_PROXY_PORT", DEFAULT_PORT)))
    parser.add_argument("--mac", default=os.environ.get("SCANWATCH_MAC") or None)
    parser.add_argument("--secret", default=env_secret())
    parser.add_argument("--account-id", type=int, default=int(os.environ.get("SCANWATCH_ACCOUNT_ID", "1")))
    parser.add_argument("--cooldown", type=float, default=float(os.environ.get("SCANWATCH_COOLDOWN", DEFAULT_COOLDOWN_S)))
    parser.add_argument("--dry-run", action="store_true", help="HTTP only; print WPP frames, do not open BLE")
    parser.add_argument("--demo", type=int, choices=(1, 2, 3), help="print the vibrator frame for a floor and exit")
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    if args.demo is not None:
        frame = vibrator_frame(args.demo)
        print(missed_alert(PhoneSighting(args.demo, False)).message)
        print(frame.to_bytes().hex())
        return 0

    ble_connect = None if args.dry_run else connect_and_vibrate
    proxy = ScanwatchProxy(args.mac, args.secret, args.account_id, args.cooldown, ble_connect)
    loop = asyncio.new_event_loop()

    def run_loop() -> None:
        asyncio.set_event_loop(loop)
        loop.run_forever()

    import threading

    threading.Thread(target=run_loop, daemon=True).start()
    server = ThreadingHTTPServer((args.host, args.port), make_handler(proxy, loop))
    mode = "dry-run" if args.dry_run else "BLE"
    print(f"Missed MarioPhone proxy on http://{args.host}:{args.port} ({mode})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("stopping")
    finally:
        server.server_close()
        loop.call_soon_threadsafe(loop.stop)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
