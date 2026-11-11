"""Pythonista UI: tap the floor you left MarioPhone on.

No web address is required for the buttons. If PROXY is set and reachable,
the tap is sent to the house BLE host so the ScanWatch vibrates:

  first floor  -> 1 vibration
  second floor -> 2 vibrations
  third floor  -> 3 vibrations
"""

import json
import urllib.error
import urllib.request

import ui

# House computer running `python3 proxy.py`. Leave empty to only show the tap
# locally. Example: "http://192.168.1.23:8787"
PROXY = ""

FLOORS = (
    (1, "First floor", "1 vibration"),
    (2, "Second floor", "2 vibrations"),
    (3, "Third floor", "3 vibrations"),
)


def post_missed(floor):
    if not PROXY:
        return {
            "ok": True,
            "alert": {
                "message": f"Missed MarioPhone on {FLOORS[floor - 1][1].lower()}",
                "vibrations": floor,
            },
            "note": "Set PROXY in this script to a running house host to buzz the watch.",
        }
    payload = json.dumps({"floor": floor, "watch_connected": False}).encode("utf-8")
    req = urllib.request.Request(
        PROXY.rstrip("/") + "/missed",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=8) as resp:
        return json.loads(resp.read().decode("utf-8"))


class FloorScreen(ui.View):
    def __init__(self):
        self.background_color = "#f4f1ea"
        self.name = "Missed MarioPhone"
        self.status = ui.Label()
        self.status.number_of_lines = 0
        self.status.text = "Tap the floor where you left the phone."
        self.status.text_color = "#1c1914"
        self.status.font = ("<system>", 16)
        self.add_subview(self.status)
        self.buttons = []
        for floor, title, count in FLOORS:
            button = ui.Button(title=f"{title}\n{count}")
            button.floor = floor
            button.action = self.tapped
            button.background_color = "#1f4b3a" if floor == 2 else "#fffdf8"
            button.tint_color = "#f4f1ea" if floor == 2 else "#1c1914"
            button.font = ("<system-bold>", 20)
            button.corner_radius = 14
            self.add_subview(button)
            self.buttons.append(button)

    def layout(self):
        pad = 16
        width = self.width - pad * 2
        y = 24
        for button in self.buttons:
            button.frame = (pad, y, width, 88)
            y += 100
        self.status.frame = (pad, y + 8, width, 120)

    def tapped(self, sender):
        try:
            result = post_missed(sender.floor)
            alert = result.get("alert") or {}
            note = result.get("note") or ""
            message = alert.get("message") or "Queued"
            vibrations = alert.get("vibrations") or sender.floor
            extra = f"\n{note}" if note else ""
            self.status.text = f"{message}\nThe watch should vibrate {vibrations} time(s).{extra}"
        except urllib.error.URLError as exc:
            self.status.text = (
                f"Could not reach {PROXY or 'the house proxy'}: {exc}\n"
                "The buttons work on this phone; buzzing still needs proxy.py "
                "running on a computer with Bluetooth."
            )
        except Exception as exc:
            self.status.text = str(exc)


if __name__ == "__main__":
    FloorScreen().present("fullscreen")
