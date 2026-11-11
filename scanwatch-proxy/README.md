# Missed MarioPhone — ScanWatch floor proxy

When MarioPhone is out of BLE range of the ScanWatch, a house BLE host can still
reach the watch. This proxy then runs Withings `CMD_VIBRATOR` with
`nb_loop = floor`:

| Report | Vibrations |
|---|---|
| Missed MarioPhone on first floor | 1 |
| Missed MarioPhone on second floor | 2 |
| Missed MarioPhone on third floor | 3 |

The watch must already be associated with this proxy (same 32-character
`klSecret` as [withouthings](https://github.com/DavidVentura/withouthings)). A
watch still owned by Health Mate will refuse the probe.

## Open the floor buttons

Do **not** type a placeholder like `<pi-ip>`. Use one of these:

1. **On the iPhone:** run `pythonista_reporter.py` in Pythonista. Tap First / Second / Third floor. That screen does not need a web address.
2. **From Cursor:** open the Missed MarioPhone app pill on this chat (published from `web/`).
3. **On home Wi-Fi after `python3 proxy.py`:** the process prints real URLs, for example `http://192.168.1.23:8787`. Open the printed address, not a template.

Buzzing the watch still needs `proxy.py` on a computer with Bluetooth near the watch. The phone UI only chooses the floor.

## Run the house proxy

On a Raspberry Pi or Linux laptop with Bluetooth:

```bash
cd scanwatch-proxy
python3 -m pip install -r requirements.txt
export SCANWATCH_SECRET='xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'   # 32 ASCII chars
export SCANWATCH_MAC='aa:bb:cc:dd:ee:ff'                    # optional
python3 proxy.py
```

`python3 proxy.py --dry-run` serves the same HTTP API and prints the WPP frame
without opening BLE. `python3 proxy.py --demo 2` prints the two-vibration frame.

## Tests

```bash
cd scanwatch-proxy
python3 -m unittest discover -s tests -v
```
