import asyncio
import json
import threading
import unittest
import urllib.request
from http.server import ThreadingHTTPServer

from policy import UnknownFloor, missed_alert, missed_message, parse_floor, vibrations_for_floor, PhoneSighting
from proxy import ScanwatchProxy, listen_urls, make_handler
from visibility import PresenceGate, advertisement_kind
from wpp import WppError, vibrator_frame


class FloorPolicyTests(unittest.TestCase):
    def test_one_vibration_per_floor(self):
        self.assertEqual(vibrations_for_floor(1), 1)
        self.assertEqual(vibrations_for_floor(2), 2)
        self.assertEqual(vibrations_for_floor(3), 3)
        self.assertEqual(vibrations_for_floor("second floor"), 2)

    def test_messages_match_the_requested_wording(self):
        self.assertEqual(missed_message(1), "Missed MarioPhone on first floor")
        self.assertEqual(missed_message(2), "Missed MarioPhone on second floor")
        self.assertEqual(missed_message(3), "Missed MarioPhone on third floor")

    def test_no_alert_while_the_phone_still_owns_the_watch(self):
        self.assertIsNone(missed_alert(PhoneSighting(2, watch_connected=True)))

    def test_alert_when_phone_is_out_of_reach(self):
        alert = missed_alert(PhoneSighting(3, watch_connected=False))
        self.assertEqual(alert.vibrations, 3)
        self.assertIn("third floor", alert.message)

    def test_unknown_floor_is_rejected(self):
        with self.assertRaises(UnknownFloor):
            parse_floor(4)
        with self.assertRaises(UnknownFloor):
            parse_floor("attic")


class PresenceTests(unittest.TestCase):
    def test_phone_advertisement_matches_name_or_service(self):
        self.assertEqual(
            advertisement_kind("MarioPhone", "aa:bb:cc:dd:ee:ff"),
            "phone",
        )
        self.assertEqual(
            advertisement_kind(
                None,
                "11:22:33:44:55:66",
                ["B10B10B1-0B10-B10B-10B1-0B10B10B10B1"],
            ),
            "phone",
        )

    def test_watch_advertisement_matches_name_or_mac(self):
        self.assertEqual(advertisement_kind("ScanWatch 2", "aa:aa:aa:aa:aa:aa"), "watch")
        self.assertEqual(
            advertisement_kind("other", "aa:bb:cc:dd:ee:ff", watch_mac="AA:BB:CC:DD:EE:FF"),
            "watch",
        )

    def test_recording_the_floor_does_not_buzz(self):
        gate = PresenceGate()
        gate.record_floor(2, 1.0)
        self.assertIsNone(gate.consider_buzz(1.0))

    def test_watch_becoming_visible_buzzes_recorded_floor(self):
        gate = PresenceGate()
        gate.record_floor(3, 1.0)
        gate.note_watch(2.0)
        buzz = gate.consider_buzz(2.0)
        self.assertEqual(buzz.floor, 3)
        self.assertIsNone(gate.consider_buzz(3.0))

    def test_phone_visibility_while_watch_already_visible_buzzes_once(self):
        gate = PresenceGate()
        gate.note_watch(1.0)
        self.assertIsNone(gate.consider_buzz(1.0))
        gate.record_floor(1, 2.0)
        self.assertEqual(gate.consider_buzz(2.0).floor, 1)
        gate.record_floor(1, 3.0)
        self.assertIsNone(gate.consider_buzz(3.0))

    def test_watch_must_leave_before_another_buzz(self):
        gate = PresenceGate(hold_s=8.0)
        gate.record_floor(2, 1.0)
        gate.note_watch(2.0)
        self.assertEqual(gate.consider_buzz(2.0).floor, 2)
        gate.note_watch(20.0)
        self.assertEqual(gate.consider_buzz(20.0).floor, 2)


class ProxyAlertTests(unittest.TestCase):
    def setUp(self):
        self.proxy = ScanwatchProxy(
            mac="aa:bb:cc:dd:ee:ff",
            secret="gUf8Np69A4GvJxjY1XOcIHKQm2HcPZnO",
            account_id=1,
            cooldown_s=0,
            ble_connect=None,
            proxy_floor=2,
        )

    def test_phone_report_does_not_vibrate(self):
        self.assertIsNone(self.proxy.note_phone(1, watch_connected=False))
        self.assertEqual(self.proxy.state.floor, 1)
        self.assertIsNone(self.proxy.buzz_if_watch_visible())

    def test_watch_sighting_uses_phone_floor(self):
        self.proxy.note_phone(3)
        alert = self.proxy.note_watch_seen(10.0)
        self.assertEqual(alert.vibrations, 3)
        self.assertEqual(alert.message, "Missed MarioPhone on third floor")
        self.assertIsNone(self.proxy.note_watch_seen(11.0))

    def test_this_proxy_floor_is_used_when_the_phone_is_seen(self):
        self.proxy.note_phone(None)
        self.assertEqual(self.proxy.state.floor, 2)

    def test_dry_run_deliver_returns_wpp_hex(self):
        alert = self.proxy.force_missed(1)
        result = asyncio.run(self.proxy.deliver(alert))
        self.assertTrue(result["dry_run"])
        self.assertEqual(result["vibrations"], 1)
        self.assertEqual(result["frame_hex"], vibrator_frame(1).to_bytes().hex())

    def test_force_without_floor_fails(self):
        lonely = ScanwatchProxy(
            mac=None,
            secret="gUf8Np69A4GvJxjY1XOcIHKQm2HcPZnO",
            account_id=1,
            cooldown_s=0,
            ble_connect=None,
        )
        with self.assertRaises(WppError):
            lonely.force_missed()

    def test_listen_urls_include_localhost(self):
        urls = listen_urls(8787)
        self.assertIn("http://127.0.0.1:8787", urls)
        self.assertTrue(all("<" not in url for url in urls))

    def test_http_taps_do_not_deliver(self):
        delivered = []

        async def ble(_proxy, alert):
            delivered.append(alert.vibrations)

        proxy = ScanwatchProxy(
            mac=None,
            secret="gUf8Np69A4GvJxjY1XOcIHKQm2HcPZnO",
            account_id=1,
            cooldown_s=0,
            ble_connect=ble,
            proxy_floor=2,
        )
        loop = asyncio.new_event_loop()
        threading.Thread(target=loop.run_forever, daemon=True).start()
        server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(proxy, loop))
        port = server.server_address[1]
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.shutdown)
        self.addCleanup(lambda: loop.call_soon_threadsafe(loop.stop))

        def post(path, body):
            req = urllib.request.Request(
                f"http://127.0.0.1:{port}{path}",
                data=json.dumps(body).encode(),
                headers={"content-type": "application/json"},
                method="POST",
            )
            with urllib.request.urlopen(req) as resp:
                return json.loads(resp.read().decode())

        missed = post("/missed", {"floor": 3})
        phone = post("/phone", {"floor": 1, "watch_connected": False})
        self.assertFalse(missed["buzzed"])
        self.assertFalse(phone["buzzed"])
        self.assertEqual(delivered, [])
        watch = post("/watch-seen", {})
        self.assertTrue(watch["buzzed"])
        self.assertEqual(delivered, [1])


if __name__ == "__main__":
    unittest.main()
