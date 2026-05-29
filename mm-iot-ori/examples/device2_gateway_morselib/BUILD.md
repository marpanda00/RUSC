# Device 2: HaLow Gateway - Build Instructions

## Prerequisites

Ensure the following are set in your environment:

```powershell
$env:IDF_PATH = "C:\esp\esp-idf"
$env:MMIOT_ROOT = "C:\Code\RUSC\mm-iot-ori\framework"
```

## Build Command

```powershell
cd C:\Code\RUSC\mm-iot-ori\examples\device2_gateway_morselib
cmd /c "set IDF_PATH=C:\esp\esp-idf && set MMIOT_ROOT=C:\Code\RUSC\mm-iot-ori\framework && C:\esp\esp-idf\export.bat && idf.py build"
```

## Hardware

- **Board**: HT-HC33 (Heltec ESP32-S3)
- **HALow**: MorseMicro MM6108 module
- **Backend**: ESP32 2.4 GHz Wi-Fi + BLE (Improv provisioning)

## Configuration (HaLow — shared with Device1 via `examples/shared/rusc_halow_config.h`)

Set `RUSC_HALOW_USE_EU` in that header (1 = EU default, 0 = US lab on MF08651).

**Default (EU):** `EU`, op class 6, channel 1 @ 863.5 MHz, **1 MHz**, **25 dBm EIRP**, BCF `bcf_mf08551.mbin`  
**US:** op class 1, channel 27 @ 915.5 MHz, BCF `bcf_mf08651_us.mbin`

- HALow AP SSID: `MorseMicro`
- HALow Password: `12345678`
- HaLow AP IP: `192.168.1.1`
- UDP Port: `5001` (Device1 sends here)
- Wi-Fi country (backend): `IT`
