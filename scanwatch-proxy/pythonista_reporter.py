"""Advertise MarioPhone so house proxies can record the floor from BLE.

The ScanWatch vibrates only when a house proxy sees the watch — never from a
tap on this screen.
"""

import ui

SERVICE_UUID = "B10B10B1-0B10-B10B-10B1-0B10B10B10B1"
LOCAL_NAME = "MarioPhone"

_manager = None
_delegate = None


def start_advertising():
    global _manager, _delegate
    try:
        import cb

        cb.start_advertising(
            {
                "kCBAdvDataLocalName": LOCAL_NAME,
                "kCBAdvDataServiceUUIDs": [SERVICE_UUID],
            }
        )
        return
    except Exception:
        pass

    from objc_util import NSObject, ObjCClass, ObjCInstance, create_objc_class

    CBPeripheralManager = ObjCClass("CBPeripheralManager")
    CBUUID = ObjCClass("CBUUID")

    def peripheralManagerDidUpdateState_(_self, _cmd, manager):
        mgr = ObjCInstance(manager)
        if mgr.state() != 5:
            return
        uuid = CBUUID.UUIDWithString_(SERVICE_UUID)
        mgr.startAdvertising_(
            {
                "kCBAdvDataLocalName": LOCAL_NAME,
                "kCBAdvDataServiceUUIDs": [uuid],
            }
        )

    Delegate = create_objc_class(
        "MarioPhoneAdvDelegate",
        NSObject,
        methods=[peripheralManagerDidUpdateState_],
        protocols=["CBPeripheralManagerDelegate"],
    )
    _delegate = Delegate.new()
    _manager = CBPeripheralManager.alloc().initWithDelegate_queue_(_delegate, None)


class StatusScreen(ui.View):
    def __init__(self):
        self.background_color = "#f4f1ea"
        self.name = "Missed MarioPhone"
        self.status = ui.Label()
        self.status.number_of_lines = 0
        self.status.text = (
            "Advertising MarioPhone.\n\n"
            "House proxies record the floor from this signal. "
            "The watch vibrates when a proxy sees the ScanWatch, not from a tap."
        )
        self.status.text_color = "#1c1914"
        self.status.font = ("<system>", 18)
        self.add_subview(self.status)
        try:
            start_advertising()
        except Exception as exc:
            self.status.text = f"Could not advertise MarioPhone: {exc}"

    def layout(self):
        pad = 16
        self.status.frame = (pad, 40, self.width - pad * 2, 220)


if __name__ == "__main__":
    StatusScreen().present("fullscreen")
