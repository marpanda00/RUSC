# Device 1: GPS Collector + HALow UDP - Build Instructions

## Prerequisites

Ensure the following are set in your environment:

```powershell
$env:IDF_PATH = "C:\esp\esp-idf"
$env:MMIOT_ROOT = "C:\Code\RUSC\mm-iot-ori\framework"
```

## Build Command

```powershell
cd C:\Code\RUSC\mm-iot-ori\examples\device1_gps_morselib
cmd /c "set IDF_PATH=C:\esp\esp-idf && set MMIOT_ROOT=C:\Code\RUSC\mm-iot-ori\framework && C:\esp\esp-idf\export.bat && idf.py build"
```

## Flash and Monitor (COM6)

```powershell
cd C:\Code\RUSC\mm-iot-ori\examples\device1_gps_morselib
cmd /c "set IDF_PATH=C:\esp\esp-idf && set MMIOT_ROOT=C:\Code\RUSC\mm-iot-ori\framework && C:\esp\esp-idf\export.bat && idf.py -p COM6 flash monitor"
```

## Clean Build

```powershell
cd C:\Code\RUSC\mm-iot-ori\examples\device1_gps_morselib
del sdkconfig
rmdir /s /q build
```

## Hardware

- **Board**: HT-HC33 (Heltec ESP32-S3)
- **GPS**: NEO 7M (UART1: RX=GPIO18, TX=GPIO17)
- **HALow**: MorseMicro module
- **Serial Port**: COM6

## Configuration (HaLow — shared with Device2 via `examples/shared/rusc_halow_config.h`)

Set `RUSC_HALOW_USE_EU` in that header (1 = EU default, 0 = US lab on MF08651).

**Default (EU, `RUSC_HALOW_USE_EU=1`):**
- Regulatory domain: `EU` (863.5 MHz, op class 6, channel 1)
- Bandwidth: **1 MHz**
- Max TX EIRP: **25 dBm** (both Device1 and Device2)
- BCF: `bcf_mf08551.mbin`

**US (`RUSC_HALOW_USE_EU=0`):** op class 1, channel 27 @ 915.5 MHz, BCF `bcf_mf08651_us.mbin`

- HALow SSID: `MorseMicro`
- HALow Password: `12345678`
- Gateway IP: `192.168.1.1`
- Device IP: `192.168.1.2`
- UDP Port: `5001`
