# RUSC Developer Setup

Use this guide when cloning the project on a new machine. It reflects the **current** stack: ESP-IDF firmware in `mm-iot-ori/examples/`, Node.js backend, and AWS EC2 for production.

Older docs (`SETUP.md`, `00_START_HERE.md`) describe a legacy MicroPython workflow and may be outdated.

---

## 1. Clone the repository

```bash
git clone https://github.com/marpanda00/RUSC.git
cd RUSC
git pull
```

Latest work is on `main`. Active firmware paths:

| Role | Path |
|------|------|
| Boat node (device1) | `mm-iot-ori/examples/device1_gps_morselib/` |
| Gateway (device2) | `mm-iot-ori/examples/device2_gateway_morselib/` |
| MorseMicro SDK | `mm-iot-ori/` |
| Backend + website | `backend/` |

`build/` folders are **not** in git. Run `idf.py build` locally after clone.

---

## 2. Copy manually (not in git)

| Item | Location on this project | Action |
|------|--------------------------|--------|
| EC2 SSH key | `~/.ssh/RUSC_KEY.pem` | Copy to the new machine |
| Production env | `backend/.env.production` | Copy if you deploy from the new PC; EC2 may already have `.env` |
| Boat display names | EC2: `/home/ubuntu/backend/device-names.json` | Backup from server if you need existing names |

```bash
scp -i ~/.ssh/RUSC_KEY.pem \
  ubuntu@ec2-34-203-233-210.compute-1.amazonaws.com:/home/ubuntu/backend/device-names.json \
  ./device-names.json.backup
```

Optional (Cursor IDE only):

- `.cursor/rules/project-high-level-spec.mdc`
- `.cursor/rules/firmware-flash-partitions.mdc`

---

## 3. Firmware build environment

### Prerequisites

- **ESP-IDF 5.4.x** (tested with v5.4.4)
- **ESP-IDF Python environment** (e.g. `idf5.4_py3.11_env`)
- USB driver for the board (CP2102 / CH340 — see `CP2102_INSTALL.md`)

### Environment variables (Windows PowerShell)

Adjust paths to your install locations:

```powershell
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.4_py3.11_env"
$env:MMIOT_ROOT = "C:\Code\RUSC\mm-iot-ori"
& "C:\Espressif\frameworks\esp-idf-v5.4.4\export.ps1"
```

> **Note:** Some `BUILD.md` files under examples still reference `mm-iot-esp32\framework`. The correct `MMIOT_ROOT` is the **`mm-iot-ori`** repo root (same folder that contains `framework/`).

### Build and flash

```powershell
# Device 1 — boat (GPS + MPU9250 + HaLow)
cd mm-iot-ori\examples\device1_gps_morselib
idf.py build
idf.py -p COMx flash monitor

# Device 2 — gateway (HaLow AP + cloud TCP)
cd ..\device2_gateway_morselib
idf.py build
idf.py -p COMy flash monitor
```

Replace `COMx` / `COMy` with your USB serial ports (they change per PC).

### Flash partition warning

HT-HC33 boards have **16 MB flash**. `device1` firmware is close to the current app partition limit (~4% free after build). Before adding large features, enlarge the app partition in `sdkconfig` / `partitions.csv`. See `.cursor/rules/firmware-flash-partitions.mdc` if using Cursor.

---

## 4. Hardware and firmware constants

| Item | Value |
|------|--------|
| Board | Heltec HT-HC33 (ESP32-S3) |
| GPS (NEO-7M) UART | RX = GPIO 18, TX = GPIO 17, 9600 baud |
| MPU9250 I2C | SDA = GPIO 13, SCL = GPIO 14 |
| HaLow SSID / password | `MorseMicro` / `12345678` |
| Gateway HaLow IP | `192.168.1.1` |
| Device1 static IP | `192.168.1.2` |
| Telemetry UDP port | `5001` |
| Telemetry packet version | `4` (includes MAC, IMU, calibration fields) |

Cloud target (in gateway `main.cpp`):

- Host: `ec2-34-203-233-210.compute-1.amazonaws.com`
- TCP port: `3001`

---

## 5. Backend (local development)

```powershell
cd backend
npm install
copy .env.example .env
npm start
```

Default ports (from `.env.example`):

| Service | Port |
|---------|------|
| HTTP API + static site | 3000 |
| TCP (gateway telemetry) | 3001 |

Do **not** commit `.env`, `node_modules/`, or `build/` — they are in `.gitignore`.

---

## 6. Production (AWS EC2)

Already running; you do not need to move the server when changing dev machines.

| Item | Value |
|------|--------|
| Host | `ec2-34-203-233-210.compute-1.amazonaws.com` |
| Website / REST API | `http://ec2-34-203-233-210.compute-1.amazonaws.com:3000` |
| Gateway TCP ingest | port `3001` |
| SSH | `ssh -i ~/.ssh/RUSC_KEY.pem ubuntu@ec2-34-203-233-210.compute-1.amazonaws.com` |
| Process manager | `pm2` — app name `rusc-backend` |

### Deploy backend / website from Windows

```powershell
cd backend
.\deploy.bat
```

Or manually with `scp` (see `backend/deploy.sh`).

Restart on server:

```bash
cd ~/backend && pm2 restart rusc-backend
```

---

## 7. Verify the full stack

1. Power gateway (device2) and boat node (device1).
2. Confirm gateway connects to EC2 TCP (`3001`).
3. Open the live map:  
   `http://ec2-34-203-233-210.compute-1.amazonaws.com:3000`
4. Use the **hamburger menu** → device should appear as communicating.
5. Set a boat name → **Save** → label updates on the map marker.

API check:

```bash
curl http://ec2-34-203-233-210.compute-1.amazonaws.com:3000/api/positions
```

Devices are keyed by **Wi-Fi MAC**; display names are stored in `device-names.json` on the server.

---

## 8. What you can ignore on a fresh clone

The repo root contains many untracked legacy scripts (`device1_*.py`, `device2_*.py`, old `web/` copy, MicroPython trees, etc.). They are **not** required for the current ESP-IDF + `backend/public` workflow.

---

## 9. Quick reference — repo layout

```
RUSC/
├── DEVELOPER_SETUP.md          ← this file
├── backend/
│   ├── server.js               ← API, TCP gateway, device naming
│   └── public/
│       ├── index.html          ← live map + device drawer
│       └── technical-solution.html
└── mm-iot-ori/
    ├── framework/              ← MorseMicro (via MMIOT_ROOT)
    └── examples/
        ├── device1_gps_morselib/
        └── device2_gateway_morselib/
```
