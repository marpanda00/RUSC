/*
 * Shared HaLow link settings for RUSC Device1 (STA) and Device2 (AP).
 * Edit this file only — both firmware images include it.
 *
 * BCF must match the profile (sdkconfig.defaults CONFIG_MM_BCF_FILE):
 *   RUSC_HALOW_USE_EU=0 -> bcf_mf08651_us.mbin  (915 MHz, HT-HC33 / MF08651)
 *   RUSC_HALOW_USE_EU=1 -> bcf_mf08551.mbin      (863 MHz EU, MF08551 module)
 */
#pragma once

#define RUSC_HALOW_USE_EU 1

#define RUSC_HALOW_SSID "MorseMicro"
#define RUSC_HALOW_PASSPHRASE "12345678"

#if RUSC_HALOW_USE_EU
/* Italy/EU: 863-868 MHz. Requires EU-capable module, antenna, and bcf_mf08551.mbin. */
#define RUSC_HALOW_EU_LOCK_SINGLE_CHANNEL 1
#define RUSC_HALOW_COUNTRY_CODE "EU"
#define RUSC_HALOW_OP_CLASS 6
#define RUSC_HALOW_S1G_CHANNEL 1 /* 863.5 MHz centre */
#define RUSC_HALOW_BW_MHZ 1
#define RUSC_HALOW_MAX_TX_EIRP_DBM 25
#define RUSC_HALOW_CONNECT_TIMEOUT_MS 90000
#define RUSC_HALOW_SCAN_DWELL_MS 500
#else
/* US 902-928 MHz ISM ("915 MHz" band). Lock to one channel when RUSC_HALOW_US_LOCK_915MHZ. */
#define RUSC_HALOW_US_LOCK_915MHZ 1
#define RUSC_HALOW_COUNTRY_CODE "US"
#define RUSC_HALOW_OP_CLASS 1
#define RUSC_HALOW_S1G_CHANNEL 27 /* 915.5 MHz centre, 1 MHz BW */
#define RUSC_HALOW_BW_MHZ 1
#define RUSC_HALOW_MAX_TX_EIRP_DBM 36 /* mmregdb US limit for op class 1 */
#define RUSC_HALOW_CONNECT_TIMEOUT_MS 30000
#define RUSC_HALOW_SCAN_DWELL_MS 100
#endif

#define RUSC_HALOW_RECONNECT_INTERVAL_MS 5000
#define RUSC_HALOW_SCAN_PASSES 12
#define RUSC_HALOW_SCAN_PASS_INTERVAL_MS 5000

#define RUSC_HALOW_NETMASK "255.255.255.0"
#define RUSC_HALOW_GATEWAY_IP "192.168.1.1"
#define RUSC_HALOW_DEVICE1_IP "192.168.1.2"
#define RUSC_HALOW_UDP_PORT 5001

/* Device1 (STA) aliases */
#define HALOW_SSID RUSC_HALOW_SSID
#define HALOW_PASSWORD RUSC_HALOW_PASSPHRASE
#define COUNTRY_CODE RUSC_HALOW_COUNTRY_CODE
#define TARGET_OP_CLASS RUSC_HALOW_OP_CLASS
#define TARGET_S1G_CHANNEL RUSC_HALOW_S1G_CHANNEL
#define GATEWAY_IP RUSC_HALOW_GATEWAY_IP
#define GATEWAY_PORT RUSC_HALOW_UDP_PORT
#define DEVICE_STATIC_IP RUSC_HALOW_DEVICE1_IP

/* Device2 (AP) aliases */
#define AP_SSID RUSC_HALOW_SSID
#define SAE_PASSPHRASE RUSC_HALOW_PASSPHRASE
#define OP_CLASS RUSC_HALOW_OP_CLASS
#define S1G_CHANNEL RUSC_HALOW_S1G_CHANNEL
#define STATIC_LOCAL_IP RUSC_HALOW_GATEWAY_IP
#define STATIC_NETMASK RUSC_HALOW_NETMASK
#define STATIC_GATEWAY RUSC_HALOW_GATEWAY_IP
#define UDP_PORT RUSC_HALOW_UDP_PORT
