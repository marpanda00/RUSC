import asyncio
import unittest

from policy import UnknownFloor, missed_alert, missed_message, parse_floor, vibrations_for_floor, PhoneSighting
from proxy import ScanwatchProxy, listen_urls
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


class ProxyAlertTests(unittest.TestCase):
    def setUp(self):
        self.proxy = ScanwatchProxy(
            mac="aa:bb:cc:dd:ee:ff",
            secret="gUf8Np69A4GvJxjY1XOcIHKQm2HcPZnO",
            account_id=1,
            cooldown_s=0,
            ble_connect=None,
        )

    def test_losing_the_phone_link_queues_floor_vibrations(self):
        self.assertIsNone(self.proxy.note_phone(1, watch_connected=True))
        alert = self.proxy.note_phone(1, watch_connected=False)
        self.assertEqual(alert.vibrations, 1)
        self.assertEqual(alert.message, "Missed MarioPhone on first floor")

    def test_second_and_third_floor(self):
        self.proxy.note_phone(2, True)
        self.assertEqual(self.proxy.note_phone(2, False).vibrations, 2)
        self.proxy.note_phone(3, True)
        self.assertEqual(self.proxy.note_phone(3, False).vibrations, 3)

    def test_force_missed_uses_last_floor(self):
        self.proxy.note_phone(2, True)
        alert = self.proxy.force_missed()
        self.assertEqual(alert.vibrations, 2)

    def test_dry_run_deliver_returns_wpp_hex(self):
        alert = self.proxy.force_missed(1)
        result = asyncio.run(self.proxy.deliver(alert))
        self.assertTrue(result["dry_run"])
        self.assertEqual(result["vibrations"], 1)
        self.assertEqual(result["frame_hex"], vibrator_frame(1).to_bytes().hex())

    def test_repeated_offline_heartbeats_do_not_requeue(self):
        first = self.proxy.note_phone(2, watch_connected=False)
        self.assertEqual(first.vibrations, 2)
        self.assertIsNone(self.proxy.note_phone(2, watch_connected=False))

    def test_changing_floor_while_offline_requeues_the_new_count(self):
        self.proxy.note_phone(1, watch_connected=False)
        alert = self.proxy.note_phone(3, watch_connected=False)
        self.assertEqual(alert.vibrations, 3)

    def test_force_without_floor_fails(self):
        with self.assertRaises(WppError):
            self.proxy.force_missed()

    def test_listen_urls_include_localhost(self):
        urls = listen_urls(8787)
        self.assertIn("http://127.0.0.1:8787", urls)
        self.assertTrue(all("<" not in url for url in urls))


if __name__ == "__main__":
    unittest.main()
