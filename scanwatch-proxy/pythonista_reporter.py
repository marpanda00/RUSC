"""Pythonista reporter: tell the house proxy which floor MarioPhone is on.

Paste this into Pythonista on the iPhone. Set PROXY to the Raspberry Pi / laptop
running scanwatch-proxy, and FLOOR to 1, 2 or 3.

When the ScanWatch drops off the phone's connected-BLE list, the proxy buzzes
the watch that many times: first floor = 1, second = 2, third = 3.
"""

import json
import urllib.error
import urllib.request

PROXY = "http://192.168.1.10:8787"
FLOOR = 1  # 1 = first floor, 2 = second, 3 = third


def scanwatch_connected():
    import time
    from objc_util import ObjCClass, create_objc_class

    def centralManagerDidUpdateState_(_self, _cmd, _central):
        pass

    DelegateClass = create_objc_class(
        "PythonBLEDelegate",
        superclass=ObjCClass("NSObject"),
        protocols=["CBCentralManagerDelegate"],
        methods=[centralManagerDidUpdateState_],
    )
    delegate = DelegateClass.alloc().init()
    manager = (
        ObjCClass("CBCentralManager")
        .alloc()
        .initWithDelegate_queue_options_(delegate, None, None)
    )
    time.sleep(0.5)
    connected = manager.retrieveConnectedPeripheralsWithServices_(None)
    if not connected:
        return False
    for i in range(connected.count()):
        name = connected.objectAtIndex_(i).name()
        if name and "scanwatch" in str(name).lower():
            return True
    return False


def report(floor, watch_connected):
    payload = json.dumps(
        {
            "floor": floor,
            "watch_connected": watch_connected,
            "name": "MarioPhone",
        }
    ).encode("utf-8")
    req = urllib.request.Request(
        PROXY.rstrip("/") + "/phone",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=8) as resp:
            print(resp.read().decode("utf-8"))
    except urllib.error.URLError as exc:
        raise SystemExit(f"proxy not reachable: {exc}") from exc


if __name__ == "__main__":
    if FLOOR not in (1, 2, 3):
        raise SystemExit("set FLOOR = 1, 2 or 3")
    connected = scanwatch_connected()
    print(f"floor={FLOOR} watch_connected={connected}")
    report(FLOOR, connected)
