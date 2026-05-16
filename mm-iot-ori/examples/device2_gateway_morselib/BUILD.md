# Device 1: GPS Collector + HALow UDP - Build Instructions

## Prerequisites

Ensure the following are set in your environment:

```powershell
$env:IDF_PATH = "C:\esp\esp-idf"
$env:MMIOT_ROOT = "C:\Code\RUSC\mm-iot-esp32\framework"
```

## Build Command

```powershell
cd C:\Code\RUSC\device1_gps_morselib
cmd /c "set IDF_PATH=C:\esp\esp-idf && set MMIOT_ROOT=C:\Code\RUSC\mm-iot-esp32\framework && C:\esp\esp-idf\export.bat && idf.py build"
```

## Flash and Monitor (COM6)

```powershell
cd C:\Code\RUSC\device1_gps_morselib
cmd /c "set IDF_PATH=C:\esp\esp-idf && set MMIOT_ROOT=C:\Code\RUSC\mm-iot-esp32\framework && C:\esp\esp-idf\export.bat && idf.py -p COM6 flash monitor"
```

## Clean Build

```powershell
cd C:\Code\RUSC\device1_gps_morselib
del sdkconfig
rmdir /s /q build
```

## Hardware

- **Board**: HT-HC33 (Heltec ESP32-S3)
- **GPS**: NEO 7M (UART1: RX=GPIO18, TX=GPIO17)
- **HALow**: MorseMicro module
- **Serial Port**: COM6

## Configuration

- HALow SSID: `RUSC_HALow_AP`
- HALow Password: `rusc2024`
- Gateway IP: `192.168.4.1`
- Gateway Port: `5001`
- Device ID: `device_1_collector`
- GPS Baud: `9600`
- Send Interval: `1000ms`
