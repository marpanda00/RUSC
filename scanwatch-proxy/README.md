# Missed MarioPhone — ScanWatch floor proxy

House Bluetooth proxies record which floor MarioPhone is on from **phone
visibility**, then vibrate the ScanWatch only when **the watch** comes into
range of a proxy.

| Phone last seen on | Vibrations |
|---|---|
| first floor | 1 |
| second floor | 2 |
| third floor | 3 |

Taps never buzz the watch.

The watch must already be associated with this proxy (same 32-character
`klSecret` as [withouthings](https://github.com/DavidVentura/withouthings)). A
watch still owned by Health Mate will refuse the probe.

## What runs where

1. **On MarioPhone:** run `pythonista_reporter.py` in Pythonista. It advertises
   as `MarioPhone` so each house proxy can see which floor the phone is on.
2. **On each floor host:** `python3 proxy.py --floor 1` (or `2` / `3`). The
   process scans BLE. Seeing MarioPhone records that floor. Seeing the
   ScanWatch sends `CMD_VIBRATOR` with `nb_loop = floor`.
3. **Status:** open the Missed MarioPhone app pill, or the URL `proxy.py`
   prints (for example `http://192.168.1.23:8787`).

## Run the house proxy

On a Raspberry Pi or Linux laptop with Bluetooth, one host per floor:

```bash
cd scanwatch-proxy
python3 -m pip install -r requirements.txt
export SCANWATCH_SECRET  # already set: 32-char klSecret from the BLE trace
export SCANWATCH_MAC='aa:bb:cc:dd:ee:ff'     # optional
export PROXY_FLOOR=2
python3 proxy.py
```

`SCANWATCH_SECRET` is the association key already taken from the watch trace;
keep it as an environment variable on the Pi, not a separate website token.
`MARIOPHONE_MAC` is optional if the phone advertises the name `MarioPhone`.

Optional: `MARIOPHONE_INBOX_URL` lets proxies share the last phone floor. That
call uses the same `SCANWATCH_SECRET`; no extra token.

`python3 proxy.py --buzz-now` connects to an advertising ScanWatch and vibrates
immediately (uses `--floor` / `--loops`, default 1).

`python3 proxy.py --dry-run` serves status HTTP without opening BLE.
`python3 proxy.py --demo 2` prints the two-vibration frame.

## Tests

```bash
cd scanwatch-proxy
python3 -m unittest discover -s tests -v
```
