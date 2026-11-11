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

## Run the house proxy

On a Raspberry Pi or Linux laptop with Bluetooth:

```bash
cd scanwatch-proxy
python3 -m pip install -r requirements.txt
export SCANWATCH_SECRET='xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'   # 32 ASCII chars
export SCANWATCH_MAC='aa:bb:cc:dd:ee:ff'                    # optional
python3 proxy.py
```

Open `http://<pi-ip>:8787` on any phone on the LAN. Mark the floor, then tap
**Phone is out of reach — buzz the watch**.

`python3 proxy.py --dry-run` serves the same HTTP API and prints the WPP frame
without opening BLE. `python3 proxy.py --demo 2` prints the two-vibration frame.

## Phone reporter (Pythonista)

Edit `PROXY` and `FLOOR` in `pythonista_reporter.py`, then run it on the iPhone.
It posts whether the ScanWatch is still in `retrieveConnectedPeripherals`. When
that flips to disconnected, the proxy vibrates once per stored floor.

```bash
curl -s http://<pi-ip>:8787/phone \
  -H 'content-type: application/json' \
  -d '{"floor":2,"watch_connected":false}'
```

## Tests

```bash
cd scanwatch-proxy
python3 -m unittest discover -s tests -v
```
