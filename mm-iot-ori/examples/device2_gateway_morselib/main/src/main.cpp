/*
 * RUSC Device 2: HaLow Gateway (MorseMicro + ESP-IDF)
 * Pure HaLow AP mode implementation using MorseMicro MM-IoT-SDK
 * 
 * Architecture:
 * - HaLow AP: Listens at 192.168.4.1:5001 for Device1 GPS data
 * - Backend WiFi: Connects to standard WiFi for data forwarding (192.168.12.126:3001)
 * - Dual network: HaLow for device-to-device, WiFi for cloud backend
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <arpa/inet.h>

// ============ ESP-IDF Includes ============
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h" // Already included, but good to ensure for BLE

#include "driver/uart.h"
#include "cJSON.h"

// ============ MorseMicro HAL/WLAN Includes (via ESP-IDF component system) ============
extern "C" {
    #include "mmhal.h"
    #include "mmosal.h"
    #include "mmutils.h"
    #include "mmipal.h"
    #include "mmregdb.h"
}

// ============ Configuration Constants ============
#define LOG_TAG "RUSC-GW"

// Logging macros
#define LOG_INFO(fmt, ...)   ESP_LOGI(LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   ESP_LOGW(LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  ESP_LOGE(LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)  ESP_LOGD(LOG_TAG, fmt, ##__VA_ARGS__)

// HT-HC33 battery sense: VBAT -> 100K -> ADC_IN/GPIO1 -> 100K -> GND.
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_EMPTY_MV 3000
#define BATTERY_FULL_MV 4200
#define GATEWAY_STATUS_INTERVAL_MS 30000

// UDP/HaLow configuration
#define UDP_PORT 5001
#define UDP_BUFFER_SIZE 512
#define DEVICE_TIMEOUT_MS 30000

// Backend WiFi (separate from HaLow)
//#define BACKEND_SSID "Sorano1975-2"
//#define BACKEND_PASSWORD "bedtolbush"
#define BACKEND_SSID ""
#define BACKEND_PASSWORD ""

#define BACKEND_IP "ec2-34-203-233-210.compute-1.amazonaws.com"
#define BACKEND_PORT 3001
#define BACKEND_RECONNECT_INTERVAL_MS 10000

// Recovery/monitoring
#define HALOW_CHECK_INTERVAL_MS 5000
#define LOG_INTERVAL_MS 5000

// HaLow AP configuration (must match Device1 STA connection)
#define AP_SSID "MorseMicro"
#define SAE_PASSPHRASE "12345678"
#define COUNTRY_CODE "US"
#define OP_CLASS 1                       // US 915MHz band (Op Class 1) - MUST match Device1
#define S1G_CHANNEL 3                    // Channel 3 = 915.000 MHz (matches ap_mode)
#define MAX_STAS 5                       // Maximum stations for AP
#define STATIC_LOCAL_IP "192.168.1.1"
#define STATIC_NETMASK "255.255.255.0"
#define STATIC_GATEWAY "192.168.1.1"

#define RUSC_TELEMETRY_MAGIC 0x5254
#define RUSC_TELEMETRY_VERSION 1
#define RUSC_DEVICE1_ID "device_1_collector"

#pragma pack(push, 1)
typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t device_index;
    uint16_t seq;
    int32_t lat_e7;
    int32_t lon_e7;
    int32_t alt_cm;
    uint16_t speed_centi_knots;
    uint8_t sats;
    uint8_t quality;
    uint16_t battery_mv;
    uint8_t battery_pct;
    uint8_t halow_status;
    uint16_t crc16;
} rusc_telemetry_packet_t;
#pragma pack(pop)

static_assert(sizeof(rusc_telemetry_packet_t) == 28, "Unexpected telemetry packet size");
// Task configuration
#define IMPROV_BLE_APP_ID 0
#define IMPROV_TASK_STACK_SIZE 4096
#define IMPROV_UART_PORT UART_NUM_0
#define IMPROV_BAUD_RATE 115200

enum ImprovState {
    STATE_STOPPED = 0x00,
    STATE_AWAITING_AUTHORIZATION = 0x01,
    STATE_AUTHORIZED = 0x02,
    STATE_PROVISIONING = 0x03,
    STATE_PROVISIONED = 0x04
};

enum ImprovError {
    ERROR_NONE = 0x00,
    ERROR_INVALID_RPC = 0x01,
    ERROR_UNKNOWN_RPC = 0x02,
    ERROR_UNABLE_TO_CONNECT = 0x03,
    ERROR_NOT_AUTHORIZED = 0x04,
    ERROR_UNKNOWN = 0xFF
};


// CRITICAL: Ensure this defaults to STATE_AUTHORIZED (0x02) so that 
// provisioning clients instantly pick it up over the air.
static ImprovState s_improv_state = STATE_AUTHORIZED; 
static ImprovError s_improv_error = ERROR_NONE;

static uint8_t improv_adv_data[31];
static uint8_t improv_scan_rsp[31];
static uint16_t improv_adv_len = 0;
static uint16_t improv_scan_rsp_len = 0;
// Add these globals near your other buffers if they aren't there
static uint8_t raw_adv_data;
static uint16_t raw_adv_len = 0;

// Improv RPC command IDs from the BLE specification.
enum ImprovCommand { UNKNOWN = 0x00, WIFI_SETTINGS = 0x01, IDENTIFY = 0x02, GET_DEVICE_INFO = 0x03, SCAN_WIFI = 0x04 };
//enum ImprovState { STATE_STOPPED = 0x00, STATE_AWAITING_AUTHORIZATION = 0x01, STATE_AUTHORIZED = 0x02, STATE_PROVISIONING = 0x03, STATE_PROVISIONED = 0x04 };
//enum ImprovError { ERROR_NONE = 0x00, ERROR_INVALID_RPC = 0x01, ERROR_UNKNOWN_RPC = 0x02, ERROR_UNABLE_TO_CONNECT = 0x03, ERROR_NOT_AUTHORIZED = 0x04, ERROR_UNKNOWN = 0x05 };

static const char* IMPROV_PREFIX = "IMPROV";

// Improv BLE UUIDs (current public specification)
// Service UUID:        00467768-6228-2272-4663-277478268000
// Current State:       00467768-6228-2272-4663-277478268001
// Error State:         00467768-6228-2272-4663-277478268002
// RPC Command:         00467768-6228-2272-4663-277478268003
// RPC Result:          00467768-6228-2272-4663-277478268004
// Capabilities:        00467768-6228-2272-4663-277478268005
// Advertisement data:  16-bit Service Data UUID 0x4677
static const uint8_t IMPROV_UUID_BASE_LE[16] = {
    0x00, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00
};
static const uint8_t IMPROV_SERVICE_DATA_UUID_LSB = 0x77;
static const uint8_t IMPROV_SERVICE_DATA_UUID_MSB = 0x46;
static const uint8_t IMPROV_CAPABILITY_IDENTIFY = 0x01;
static const uint8_t IMPROV_CAPABILITY_DEVICE_INFO = 0x02;
static const uint8_t IMPROV_CAPABILITIES = IMPROV_CAPABILITY_IDENTIFY | IMPROV_CAPABILITY_DEVICE_INFO;

enum {
    IDX_SVC,
    IDX_CHAR_CAPABILITIES,
    IDX_CHAR_CAPABILITIES_VAL,
    IDX_CHAR_STATE,
    IDX_CHAR_STATE_VAL,
    IDX_CHAR_STATE_CFG, // Client Characteristic Configuration Descriptor
    IDX_CHAR_ERROR,
    IDX_CHAR_ERROR_VAL,
    IDX_CHAR_ERROR_CFG,
    IDX_CHAR_RPC_COMMAND,
    IDX_CHAR_RPC_COMMAND_VAL,
    IDX_CHAR_RPC_RESULT,
    IDX_CHAR_RPC_RESULT_VAL,
    IDX_CHAR_RPC_RESULT_CFG,
    IDX_CHAR_VAL_MAX,
};
// Improv BLE globals
static uint16_t improv_handle_table[IDX_CHAR_VAL_MAX];
static uint16_t improv_conn_id = 0xFFFF;
static esp_gatt_if_t improv_gatts_if = ESP_GATT_IF_NONE;
static bool improv_restart_adv_after_stop = false;
// Improv BLE globals
//static uint16_t improv_handle_table[IDX_CHAR_VAL_MAX];
//static uint16_t improv_conn_id = 0xFFFF;
//static esp_gatt_if_t improv_gatts_if = ESP_GATT_IF_NONE;
//static ImprovState s_improv_state = STATE_STOPPED;
//static ImprovError s_improv_error = ERROR_NONE;

//// Build advertisement data dynamically to update Service Data state
//static void build_improv_adv_data(uint8_t *adv_data, uint16_t *adv_len) {
//    uint8_t *p = adv_data;
    
//    // Flags: LE General Discoverable, BR/EDR not supported
//    *p++ = 0x02; // Length
//    *p++ = 0x01; // Type: Flags
//    *p++ = 0x06; // Value
//    
//    // Service Data (UUID 0x184E with device state, capabilities, and reserved bytes per Improv spec)
//    // Format: [Length] [Type=0x16] [UUID(2 bytes)] [State(1)] [Capabilities(1)] [Reserved(3)]
//    *p++ = 0x09; // Length: 1(type) + 2(UUID) + 1(state) + 1(capabilities) + 3(reserved) = 8
//    *p++ = 0x16; // Type: Service Data - 16-bit UUID
//    *p++ = 0x4E; // Service UUID 0x184E (little-endian: 4E 18)
//    *p++ = 0x18;
//    *p++ = (uint8_t)s_improv_state;    // Current state: 0x01=Awaiting Auth, 0x02=Authorized, 0x03=Provisioning, 0x04=Provisioned
//    *p++ = 0x03;                        // Capabilities: 0x01=Identify, 0x02=Device Info (0x03=both supported)//
//    *p++ = 0x00; // Reserved byte 1
//    *p++ = 0x00; // Reserved byte 2
//    *p++ = 0x00; // Reserved byte 3
//    *p++ = 0x00; // Optional padding to ensure minimum length for BLE scanners (some require at least 10 bytes of data)
 //   // Complete List of 16-bit Service UUIDs
//    *p++ = 0x03; // Length (2 UUID bytes + 1 type)
//    *p++ = 0x03; // Type: Complete List of 16-bit Service UUIDs
//    *p++ = 0x4E; // Service UUID 0x184E (little-endian)
//    *p++ = 0x18;
    
//    // Complete Local Name for BLE scanner visibility
//    const char* device_name = "RUSC Gateway";
//    size_t name_len = strlen(device_name);
//    if (name_len > 20) name_len = 20;  // Limit to avoid exceeding 31-byte max
//    *p++ = (uint8_t)(name_len + 1);    // Length: 1(type) + name_len
//    *p++ = 0x09;                        // Type: Complete Local Name
//    memcpy(p, device_name, name_len);
//    p += name_len;
//    
//    *adv_len = (uint16_t)(p - adv_data);
    
    // DEBUG: Log the advertisement data (first 16 bytes minimum, or all if longer)
//    if (*adv_len >= 16) {
//        LOG_INFO("ADV DATA (len=%u): %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
//                 *adv_len,
//                 adv_data[0], adv_data[1], adv_data[2], adv_data[3],
//                 adv_data[4], adv_data[5], adv_data[6], adv_data[7],
//                 adv_data[8], adv_data[9], adv_data[10], adv_data[11],
//                 adv_data[12], adv_data[13], adv_data[14], adv_data[15]);
//    }
//}
//
//static uint8_t improv_adv_data[31]; // BLE advertisement limited to 31 bytes
// Improv BLE globals

// Forward Declarations
static void build_improv_adv_data(uint8_t *adv_data, uint16_t *adv_len);
static void build_improv_scan_rsp_data(uint8_t *rsp_data, uint16_t *rsp_len);
esp_err_t initialize_ble_improv(void); 
static void send_ble_improv_state_notification(ImprovState new_state);

static void set_improv_uuid(esp_bt_uuid_t *uuid, uint8_t endpoint) {
    uuid->len = ESP_UUID_LEN_128;
    memcpy(uuid->uuid.uuid128, IMPROV_UUID_BASE_LE, sizeof(IMPROV_UUID_BASE_LE));
    uuid->uuid.uuid128[0] = endpoint;
}

static uint8_t improv_checksum(const uint8_t *data, uint16_t len) {
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < len; i++) {
        checksum += data[i];
    }
    return checksum;
}

// Helper function to dynamically change state and cleanly kick the BLE stack
static void update_improv_ble_state(ImprovState new_state) {
    s_improv_state = new_state;
    
    if (improv_gatts_if != ESP_GATT_IF_NONE) {
        build_improv_adv_data(improv_adv_data, &improv_adv_len);

        if (improv_conn_id == 0xFFFF) {
            improv_restart_adv_after_stop = true;
            esp_ble_gap_stop_advertising();
        }

        // Push notification update if a client is actively connected
        send_ble_improv_state_notification(new_state);
    }
}
static void build_improv_adv_data(uint8_t *adv_data, uint16_t *adv_len) {
    // Use the pointer passed by the calling function
    uint8_t *p = adv_data; 
    
    // 1. Flags Block (3 bytes)
    *p++ = 0x02; // Length
    *p++ = 0x01; // Type: Flags
    *p++ = 0x06; // BR/EDR Not Supported & General Discoverable

    // 2. Complete List of 128-bit Service UUIDs. The web client filters on this UUID.
    *p++ = 0x11; // 1(type) + 16(UUID)
    *p++ = 0x07; // Complete List of 128-bit Service UUIDs
    memcpy(p, IMPROV_UUID_BASE_LE, sizeof(IMPROV_UUID_BASE_LE));
    p += sizeof(IMPROV_UUID_BASE_LE);

    // 3. Improv Service Data Block (must be in the primary advertisement)
    *p++ = 0x09; // Length of this block (9 bytes follow)
    *p++ = 0x16; // Type: Service Data - 16-bit UUID
    *p++ = IMPROV_SERVICE_DATA_UUID_LSB; // Improv service data UUID 0x4677
    *p++ = IMPROV_SERVICE_DATA_UUID_MSB;
    *p++ = (uint8_t)s_improv_state; // Wire up your runtime variable state dynamically
    *p++ = IMPROV_CAPABILITIES;
    *p++ = 0x00; // Reserved 1
    *p++ = 0x00; // Reserved 2
    *p++ = 0x00; // Reserved 3
    *p++ = 0x00; // Reserved 4
    
    // Calculate total payload length accurately using pointer arithmetic
    *adv_len = (uint16_t)(p - adv_data);
}
// 2. Build the Scan Response Data (Offloads the device name)
static void build_improv_scan_rsp_data(uint8_t *rsp_data, uint16_t *rsp_len) {
    uint8_t *p = rsp_data;
    
    const char* device_name = "RUSC Gateway";
    size_t name_len = strlen(device_name);
    if (name_len > 29) name_len = 29; // Sanity hard-limit for scan response buffer
    
    *p++ = (uint8_t)(name_len + 1);    // Length field
    *p++ = 0x09;                        // Type: Complete Local Name
    memcpy(p, device_name, name_len);
    p += name_len;
    
    *rsp_len = (uint16_t)(p - rsp_data);
}

static esp_ble_adv_params_t improv_adv_params = {
    .adv_int_min        = 0x20, // 32ms
    .adv_int_max        = 0x40, // 64ms
    .adv_type           = ADV_TYPE_IND, // Connectable and scannable
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

//static esp_ble_adv_params_t improv_adv_params = {
//    .adv_int_min        = 0x20, // 20ms * 1.6 = 32ms
//    .adv_int_max        = 0x40, // 40ms * 1.6 = 64ms
//    .adv_type           = ADV_TYPE_IND, // Connectable and scannable undirected advertising
//    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
//    .channel_map        = ADV_CHNL_ALL,
//    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
//};

static char current_ssid[33] = BACKEND_SSID;
static char current_password[65] = BACKEND_PASSWORD;

#define GATEWAY_TASK_STACK_SIZE 12288    // Increased stack for cJSON parsing + buffer operations
#define GATEWAY_TASK_PRIORITY 3          // Lower priority (Morse tasks likely run at higher priority)
// Opaque argument for callback validation
static uint32_t ap_opaque_argument = 0xDEADBEEF;

// ============ Global State ============
static int udp_sock = -1;
static int backend_sock = -1;
static bool halow_active = false;
static bool backend_connected = false;
static volatile bool backend_wifi_has_ip = false;
static volatile uint8_t backend_wifi_disconnect_reason = 0;
static volatile bool backend_wifi_reconfiguring = false;
static adc_oneshot_unit_handle_t g_adc_handle = NULL;

static int battery_percent = 100;
static uint16_t battery_mv = 0;
static uint32_t total_packets_received = 0;
static uint32_t total_devices_connected = 0;

static uint32_t last_halow_check_ms = 0;
static uint32_t last_log_ms = 0;
static uint32_t last_gateway_status_ms = 0;

// ============ NVS Credentials Management ============

static void save_wifi_credentials(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    if (nvs_open("storage", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_str(nvs_handle, "wifi_ssid", ssid);
        nvs_set_str(nvs_handle, "wifi_password", password);
        nvs_set_u8(nvs_handle, "wifi_provision", 1);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        LOG_INFO("Credentials saved to NVS");
    }
}

static bool load_wifi_credentials() {
    nvs_handle_t nvs_handle;
    if (nvs_open("storage", NVS_READONLY, &nvs_handle) == ESP_OK) {
        uint8_t provisioned = 0;
        nvs_get_u8(nvs_handle, "wifi_provision", &provisioned);
        if (provisioned) {
            size_t ssid_len = sizeof(current_ssid);
            size_t pass_len = sizeof(current_password);
            nvs_get_str(nvs_handle, "wifi_ssid", current_ssid, &ssid_len);
            nvs_get_str(nvs_handle, "wifi_password", current_password, &pass_len);
            nvs_close(nvs_handle);
            return true;
        }
        nvs_close(nvs_handle);
    }
    return false;
}

// Forward declarations for BLE notifications
static void send_ble_improv_state_notification(ImprovState state);
static void send_ble_improv_error_notification(ImprovError error);
static void send_ble_improv_rpc_result_notification(const uint8_t* data, uint16_t len);
static void ble_wifi_provision_task(void *pvParameters);

static void send_improv_state(ImprovState state) {
    s_improv_state = state; // Update global state
    // BLE-only: Send only BLE notification (UART driver not initialized)
    send_ble_improv_state_notification(state);
}

static void send_improv_error(ImprovError error) {
    s_improv_error = error; // Update global error
    // BLE-only: Send only BLE notification (UART driver not initialized)
    send_ble_improv_error_notification(error);
}

// Removed send_improv_serial_error - Serial Improv is disabled (BLE only)


// Device tracking structure
typedef struct {
    char device_id[32];
    int signal_strength;
    int satellites;
    int quality;
    double latitude;
    double longitude;
    double altitude;
    uint16_t battery_mv;
    uint8_t battery_pct;
    uint8_t halow_status;
    uint32_t last_seen_ms;
    uint32_t packet_count;
} device_info_t;

// Map for connected devices (simple array-based implementation)
#define MAX_DEVICES 10
static device_info_t connected_devices[MAX_DEVICES];
static int num_connected_devices = 0;

// ============ Battery Monitoring ============

static uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint8_t battery_percent_from_mv(uint16_t mv) {
    if (mv <= BATTERY_EMPTY_MV) return 0;
    if (mv >= BATTERY_FULL_MV) return 100;
    return (uint8_t)(((mv - BATTERY_EMPTY_MV) * 100) / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

static void init_battery_adc(void) {
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&init_config, &g_adc_handle) != ESP_OK) {
        LOG_WARN("Battery ADC init failed");
        g_adc_handle = NULL;
        return;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(g_adc_handle, BATTERY_ADC_CHANNEL, &channel_config) != ESP_OK) {
        LOG_WARN("Battery ADC channel config failed");
    }
}

static uint16_t read_battery_mv(void) {
    if (g_adc_handle == NULL) {
        return 0;
    }

    int raw = 0;
    if (adc_oneshot_read(g_adc_handle, BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
        return 0;
    }

    uint32_t adc_mv = ((uint32_t)raw * 3300U) / 4095U;
    uint32_t mv = adc_mv * 2U;
    return mv > UINT16_MAX ? UINT16_MAX : (uint16_t)mv;
}

static void update_gateway_battery(void) {
    battery_mv = read_battery_mv();
    battery_percent = battery_percent_from_mv(battery_mv);
}

// ============ HaLow AP Mode Handlers ============

/**
 * Handle HaLow AP station status changes (connect/disconnect)
 */
static void handle_ap_sta_status(const struct mmwlan_ap_sta_status *sta_status, void *arg) {
    if (sta_status == NULL) return;
    
    // Validate opaque argument
    if ((uintptr_t)arg != ap_opaque_argument) {
        LOG_WARN("AP callback received unexpected opaque argument");
    }
    
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             sta_status->mac_addr[0], sta_status->mac_addr[1],
             sta_status->mac_addr[2], sta_status->mac_addr[3],
             sta_status->mac_addr[4], sta_status->mac_addr[5]);
    
    // Check STA state: AUTHORIZED means fully connected and transmitting
    if (sta_status->state == MMWLAN_AP_STA_AUTHORIZED) {
        LOG_INFO("[AP] STA AUTHORIZED: %s (AID=%u)", mac_str, sta_status->aid);
        total_devices_connected++;
    } else if (sta_status->state == MMWLAN_AP_STA_UNKNOWN || sta_status->state == MMWLAN_AP_STA_ASSOCIATED) {
        LOG_DEBUG("[AP] STA state change: %s (state=%d)", mac_str, sta_status->state);
        if (total_devices_connected > 0) total_devices_connected--;
    }
}

/**
 * Link status callback for network up/down events
 */
static void link_status_callback(const struct mmipal_link_status *link_status) {
    uint32_t time_ms = mmosal_get_time_ms();
    if (link_status->link_state == MMIPAL_LINK_UP) {
        LOG_INFO("[HaLow] Link UP at %lu ms: IP=%s", time_ms, link_status->ip_addr);
    } else {
        LOG_INFO("[HaLow] Link DOWN at %lu ms", time_ms);
    }
}

// ============ HaLow Initialization ============

// Stringify macro helpers
#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

void load_mmwlan_ap_args(struct mmwlan_ap_args *ap_args) {
    // SSID
    strncpy((char *)ap_args->ssid, AP_SSID, sizeof(ap_args->ssid) - 1);
    ap_args->ssid_len = strlen((char *)ap_args->ssid);
    
    // Passphrase (SAE/WPA3)
    strncpy(ap_args->passphrase, SAE_PASSPHRASE, sizeof(ap_args->passphrase) - 1);
    ap_args->passphrase_len = strlen(ap_args->passphrase);
    
    // Security
    ap_args->security_type = MMWLAN_SAE;
    ap_args->pmf_mode = MMWLAN_PMF_REQUIRED;
    
    // Channel
    ap_args->op_class = OP_CLASS;
    ap_args->s1g_chan_num = S1G_CHANNEL;
    
    // Max stations
    ap_args->max_stas = MAX_STAS;
}

/**
 * Initialize MorseMicro HaLow AP stack
 */
static esp_err_t initialize_halow_ap(void) {
    enum mmwlan_status status;
    const struct mmwlan_s1g_channel_list *channel_list;
    struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
    struct mmwlan_ap_args ap_args = MMWLAN_AP_ARGS_INIT;
    struct mmipal_init_args mmipal_init_args = MMIPAL_INIT_ARGS_DEFAULT;
    
    LOG_INFO("Initializing MorseMicro HaLow stack...");
    
    // 1. Power Management: Enable Vext (Heltec specific) - BEFORE mmhal_init
    gpio_set_direction((gpio_num_t)18, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)18, 0);  // Vext on
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 2. Initialize HAL and WLAN stacks (this sets up GPIO interrupts)
    LOG_INFO("[DEBUG] Calling mmhal_init()...");
    mmhal_init();
    LOG_INFO("[DEBUG] mmhal_init() complete");
    
    LOG_INFO("[DEBUG] Calling mmwlan_init()...");
    mmwlan_init();
    LOG_INFO("[DEBUG] mmwlan_init() complete");
    
    // 3. Set regulatory domain BEFORE boot
    LOG_INFO("[DEBUG] Looking up regulatory domain for %s...", COUNTRY_CODE);
    channel_list = mmwlan_lookup_regulatory_domain(get_regulatory_db(), COUNTRY_CODE);
    if (channel_list == NULL) {
        LOG_ERROR("Could not find regulatory domain for %s", COUNTRY_CODE);
        return ESP_FAIL;
    }
    LOG_INFO("[DEBUG] Regulatory domain found");
    
    LOG_INFO("[DEBUG] Setting channel list...");
    status = mmwlan_set_channel_list(channel_list);
    if (status != MMWLAN_SUCCESS) {
        LOG_ERROR("Failed to set regulatory domain (status %d)", status);
        return ESP_FAIL;
    }
    LOG_INFO("[DEBUG] Channel list set successfully. OP Class=%d, Channel=%d", OP_CLASS, S1G_CHANNEL);
    
    // 4. CRITICAL: Boot the MorseMicro radio chip
    LOG_INFO("[DEBUG] Calling mmwlan_boot() - chip reset and SPI initialization...");
    status = mmwlan_boot(&boot_args);
    LOG_INFO("[DEBUG] mmwlan_boot() returned with status %d", status);
    if (status != MMWLAN_SUCCESS) {
        LOG_ERROR("Failed to boot HaLow stack (status %d)", status);
        LOG_ERROR("Cannot proceed without functional radio. Halting.");
        return ESP_FAIL;
    }
    LOG_INFO("Radio boot successful!");
    
    // 5. Initialize network interface with static IP
    mmipal_init_args.mode = MMIPAL_STATIC;
    mmipal_init_args.ip6_mode = MMIPAL_IP6_DISABLED;
    (void)mmosal_safer_strcpy(mmipal_init_args.ip_addr,
                              STATIC_LOCAL_IP,
                              sizeof(mmipal_init_args.ip_addr));
    (void)mmosal_safer_strcpy(mmipal_init_args.netmask,
                              STATIC_NETMASK,
                              sizeof(mmipal_init_args.netmask));
    (void)mmosal_safer_strcpy(mmipal_init_args.gateway_addr,
                              STATIC_GATEWAY,
                              sizeof(mmipal_init_args.gateway_addr));
    
    if (mmipal_init(&mmipal_init_args) != MMIPAL_SUCCESS) {
        LOG_ERROR("Failed to initialize network interface");
        return ESP_FAIL;
    }
    LOG_INFO("Network interface initialized");
    
    mmipal_set_link_status_callback(link_status_callback);
    
    // 6. Configure and enable AP mode
    LOG_INFO("Configuring HaLow AP for SSID: %s", AP_SSID);
    load_mmwlan_ap_args(&ap_args);
    ap_args.sta_status_cb = handle_ap_sta_status;
    ap_args.sta_status_cb_arg = (void *)(uintptr_t)ap_opaque_argument;
    ap_args.max_stas = MAX_STAS;
    
    status = mmwlan_ap_enable(&ap_args);
    if (status == MMWLAN_SUCCESS) {
        LOG_INFO("HaLow AP Mode started successfully!");
        LOG_INFO("  SSID: %s", AP_SSID);
        LOG_INFO("  Channel: %d (Op Class %d)", S1G_CHANNEL, OP_CLASS);
        LOG_INFO("  IP: %s", STATIC_LOCAL_IP);
        LOG_INFO("  Max STAs: %d", MAX_STAS);
        halow_active = true;
        return ESP_OK;
    } else {
        LOG_ERROR("Failed to enable HaLow AP Mode (status %d)", status);
        return ESP_FAIL;
    }
}

// ============ UDP Socket Management ============

/**
 * Initialize UDP listener on port 5001
 */
static esp_err_t initialize_udp_listener(void) {
    struct sockaddr_in udp_addr;
    
    // Create UDP socket
    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock < 0) {
        LOG_ERROR("Failed to create UDP socket");
        return ESP_FAIL;
    }
    
    // Bind to UDP port
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    udp_addr.sin_port = htons(UDP_PORT);
    
    if (bind(udp_sock, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
        LOG_ERROR("Failed to bind UDP socket to port %d", UDP_PORT);
        close(udp_sock);
        udp_sock = -1;
        return ESP_FAIL;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(udp_sock, F_GETFL, 0);
    fcntl(udp_sock, F_SETFL, flags | O_NONBLOCK);
    
    LOG_INFO("UDP listener started on port %d", UDP_PORT);
    return ESP_OK;
}

// ============ WiFi Event Handlers ============

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        LOG_INFO("WiFi STA started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        backend_wifi_has_ip = false;
        backend_wifi_disconnect_reason = disconnected ? disconnected->reason : 0;
        LOG_WARN("WiFi disconnected, retrying... reason=%u", backend_wifi_disconnect_reason);
        if (!backend_wifi_reconfiguring) {
            esp_wifi_connect();
        }
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        backend_wifi_has_ip = true;
        LOG_INFO("WiFi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        // Note: backend_connected is only set in connect_backend_server() after TCP connection succeeds
    }
}

// ============ Backend WiFi Connection ============

/**
 * Initialize standard WiFi for backend connection
 * Dual-processor design: ESP32-S3 handles 2.4 GHz WiFi (backend)
 * while separate HT-HC01 (Morse Micro) handles HaLow (902-928 MHz).
 * No interference due to frequency isolation.
 */
static esp_err_t initialize_backend_wifi(void) {
    // Load credentials from NVS if available
    if (load_wifi_credentials()) {
        LOG_INFO("Using stored credentials from NVS");
    } else {
        LOG_INFO("No NVS credentials, using defaults");
    }

    LOG_INFO("Connecting to backend WiFi: %s", current_ssid);

    // Initialize NVS (needed for WiFi calibration data)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    
    // Create default event loop (ignore if already exists)
    esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(loop_err);
    }
    
    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default WiFi STA in namespace
    esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi subsystem
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    
    wifi_config_t wifi_config = {};
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    
    // Configure STA mode
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    // Set SSID and password using string copy
    strncpy((char *)wifi_config.sta.ssid, current_ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = 0;
    strncpy((char *)wifi_config.sta.password, current_password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = 0;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    return ESP_OK;
}

/**
 * Connect to backend TCP server for data forwarding
 * Uses getaddrinfo() to support both IP addresses and DNS hostnames
 */
static esp_err_t connect_backend_server(void) {
    struct sockaddr_in backend_addr;
    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;
    char port_str[6];
    
    backend_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (backend_sock < 0) {
        LOG_ERROR("Failed to create backend socket");
        return ESP_FAIL;
    }
    
    // Convert port number to string for getaddrinfo
    snprintf(port_str, sizeof(port_str), "%d", BACKEND_PORT);
    
    // Configure address resolution hints
    hints.ai_family = AF_INET;           // IPv4 only
    hints.ai_socktype = SOCK_STREAM;     // TCP
    hints.ai_protocol = IPPROTO_TCP;
    
    // Resolve hostname (works with both DNS names and IP addresses)
    int res = getaddrinfo(BACKEND_IP, port_str, &hints, &result);
    if (res != 0 || result == NULL) {
        LOG_ERROR("Failed to resolve backend address %s (error: %d)", BACKEND_IP, res);
        if (result != NULL) {
            freeaddrinfo(result);
        }
        close(backend_sock);
        backend_sock = -1;
        return ESP_FAIL;
    }
    
    // Use the first result (lwIP should have resolved it)
    memcpy(&backend_addr, (struct sockaddr_in *)result->ai_addr, sizeof(backend_addr));
    backend_addr.sin_port = htons(BACKEND_PORT);
    
    if (connect(backend_sock, (struct sockaddr *)&backend_addr, sizeof(backend_addr)) < 0) {
        LOG_ERROR("Failed to connect to backend at %s:%d (errno: %d)", BACKEND_IP, BACKEND_PORT, errno);
        close(backend_sock);
        backend_sock = -1;
        freeaddrinfo(result);
        return ESP_FAIL;
    }
    
    freeaddrinfo(result);
    backend_connected = true;
    LOG_INFO("Connected to backend at %s:%d", BACKEND_IP, BACKEND_PORT);
    return ESP_OK;
}

// ============ Data Processing ============

static bool send_backend_line(const char *line) {
    if (!backend_connected || backend_sock < 0) {
        return false;
    }

    ssize_t sent = send(backend_sock, (const uint8_t *)line, strlen(line), 0);
    if (sent > 0) {
        send(backend_sock, (const uint8_t *)"\n", 1, 0);
        return true;
    }

    LOG_WARN("Failed to send to backend (errno=%d) - reconnecting", errno);
    close(backend_sock);
    backend_sock = -1;
    backend_connected = false;
    return false;
}

static const char *device_id_from_index(uint8_t device_index) {
    switch (device_index) {
        case 1:
            return RUSC_DEVICE1_ID;
        default:
            return NULL;
    }
}

static device_info_t *upsert_device_info(const char *device_id) {
    for (int i = 0; i < num_connected_devices; i++) {
        if (strcmp(connected_devices[i].device_id, device_id) == 0) {
            return &connected_devices[i];
        }
    }

    if (num_connected_devices >= MAX_DEVICES) {
        return NULL;
    }

    device_info_t *device = &connected_devices[num_connected_devices++];
    memset(device, 0, sizeof(*device));
    strncpy(device->device_id, device_id, sizeof(device->device_id) - 1);
    return device;
}

static bool process_compact_telemetry(const uint8_t *data, int len, const char *sender_ip) {
    if (len < 2) {
        return false;
    }

    uint16_t magic = 0;
    memcpy(&magic, data, sizeof(magic));
    if (magic != RUSC_TELEMETRY_MAGIC) {
        return false;
    }

    if (len != sizeof(rusc_telemetry_packet_t)) {
        LOG_WARN("Invalid compact telemetry length from %s: %d", sender_ip, len);
        return true;
    }

    rusc_telemetry_packet_t packet;
    memcpy(&packet, data, sizeof(packet));
    if (packet.version != RUSC_TELEMETRY_VERSION) {
        LOG_WARN("Unsupported compact telemetry version from %s: %u", sender_ip, packet.version);
        return true;
    }

    uint16_t received_crc = packet.crc16;
    packet.crc16 = 0;
    uint16_t calculated_crc = crc16_ccitt((const uint8_t *)&packet, sizeof(packet));
    if (received_crc != calculated_crc) {
        LOG_WARN("Compact telemetry CRC mismatch from %s: rx=0x%04x calc=0x%04x",
                 sender_ip, received_crc, calculated_crc);
        return true;
    }

    const char *device_id = device_id_from_index(packet.device_index);
    if (device_id == NULL) {
        LOG_WARN("Unknown compact telemetry device index from %s: %u", sender_ip, packet.device_index);
        return true;
    }

    double lat = packet.lat_e7 / 10000000.0;
    double lon = packet.lon_e7 / 10000000.0;
    double alt = packet.alt_cm / 100.0;
    double speed = packet.speed_centi_knots / 100.0;

    device_info_t *device = upsert_device_info(device_id);
    if (device != NULL) {
        device->last_seen_ms = mmosal_get_time_ms();
        device->packet_count++;
        device->satellites = packet.sats;
        device->quality = packet.quality;
        device->latitude = lat;
        device->longitude = lon;
        device->altitude = alt;
        device->battery_mv = packet.battery_mv;
        device->battery_pct = packet.battery_pct;
        device->halow_status = packet.halow_status;
    }

    total_packets_received++;

    char json_data[256];
    snprintf(json_data, sizeof(json_data),
             "{\"device_id\":\"%s\",\"lat\":%.7f,\"lon\":%.7f,\"alt\":%.2f,\"speed\":%.2f,"
             "\"sats\":%u,\"quality\":%u,\"packet\":%u,\"battery_mv\":%u,\"battery\":%u,"
             "\"halow_status\":%u}",
             device_id,
             lat,
             lon,
             alt,
             speed,
             packet.sats,
             packet.quality,
             packet.seq,
             packet.battery_mv,
             packet.battery_pct,
             packet.halow_status);

    send_backend_line(json_data);
    LOG_DEBUG("Decoded compact telemetry: %s seq=%u", device_id, packet.seq);
    return true;
}

static void send_gateway_status_if_due(uint32_t now_ms) {
    if (now_ms - last_gateway_status_ms < GATEWAY_STATUS_INTERVAL_MS) {
        return;
    }
    last_gateway_status_ms = now_ms;

    update_gateway_battery();

    char status_json[160];
    snprintf(status_json, sizeof(status_json),
             "{\"type\":\"gateway_status\",\"device_id\":\"device2\",\"battery_mv\":%u,\"battery\":%d}",
             battery_mv,
             battery_percent);
    send_backend_line(status_json);
}

/**
 * Process GPS JSON data from Device1
 */
static void process_device_data(const char *device_id, const char *json_data) {
    // Parse JSON
    cJSON *doc = cJSON_Parse(json_data);
    if (doc == NULL) {
        LOG_ERROR("Failed to parse JSON from %s", device_id);
        return;
    }
    
    total_packets_received++;
    
    // Update device info if we have it
    for (int i = 0; i < num_connected_devices; i++) {
        if (strcmp(connected_devices[i].device_id, device_id) == 0) {
            connected_devices[i].last_seen_ms = mmosal_get_time_ms();
            connected_devices[i].packet_count++;
            
            // Extract data from JSON
            cJSON *signal = cJSON_GetObjectItem(doc, "signal_strength");
            if (signal && signal->type == cJSON_Number) {
                connected_devices[i].signal_strength = signal->valueint;
            }
            
            cJSON *sats = cJSON_GetObjectItem(doc, "satellites");
            if (sats == NULL) {
                sats = cJSON_GetObjectItem(doc, "sats");
            }
            if (sats && sats->type == cJSON_Number) {
                connected_devices[i].satellites = sats->valueint;
            }
            
            break;
        }
    }
    
    // Forward to backend if connected
    if (send_backend_line(json_data)) {
        LOG_DEBUG("Forwarded to backend: %s", device_id);
    }
    
    cJSON_Delete(doc);
}

// ============ Monitoring and Logging ============

static void log_device_metrics(void) {
    uint32_t now_ms = mmosal_get_time_ms();
    if (now_ms - last_log_ms < LOG_INTERVAL_MS) {
        return;
    }
    last_log_ms = now_ms;
    
    LOG_INFO("[Status] HaLow=%s Backend=%s Battery=%d%% Devices=%lu Packets=%lu",
             halow_active ? "ON" : "OFF",
             backend_connected ? "OK" : "DISC",
             battery_percent,
             total_devices_connected,
             total_packets_received);
}

static void check_halow_status(void) {
    uint32_t now_ms = mmosal_get_time_ms();
    if (now_ms - last_halow_check_ms < HALOW_CHECK_INTERVAL_MS) {
        return;
    }
    last_halow_check_ms = now_ms;
    
    // Check HaLow connectivity via link status (no dedicated AP status function in MorseMicro API)
    // The halow_active flag is managed by link_status_callback() which monitors MMIPAL_LINK_UP/DOWN
    // If HaLow appears to be disconnected, we rely on auto-recovery through the link callback
}

// ============ Main Application Loops & Tasks ============

static void gateway_task(void *pvParameters) {
    char buffer[UDP_BUFFER_SIZE];
    struct sockaddr_in remote_addr;
    socklen_t remote_addr_len;
    int num_recv;
    uint32_t last_heap_check = 0;
    uint32_t last_backend_retry = 0;
    
    LOG_INFO("Gateway task started (stack=%d bytes)", GATEWAY_TASK_STACK_SIZE);
    
    while (1) {
        // Check HaLow status periodically
        check_halow_status();
        
        // Battery monitoring disabled - ADC driver not available in build
        // update_battery();
        
        uint32_t now_ms = mmosal_get_time_ms();
        
        // Log metrics periodically
        log_device_metrics();
        send_gateway_status_if_due(now_ms);
        
        // Monitor heap health - detect memory leaks early
        if (now_ms - last_heap_check >= 10000) {  // Check every 10 seconds
            size_t free_heap = esp_get_free_heap_size();
            if (free_heap < 50000) {  // Less than 50KB remaining
                LOG_WARN("HEAP WARNING: Only %d bytes free! Memory leak suspected.", free_heap);
            }
            last_heap_check = now_ms;
        }
        
        // Retry backend connection if disconnected
        if (!backend_connected && (now_ms - last_backend_retry >= BACKEND_RECONNECT_INTERVAL_MS)) {
            LOG_INFO("[RETRY] Attempting to reconnect to backend server...");
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                connect_backend_server();
            } else {
                LOG_WARN("[RETRY] Backend WiFi not connected, skipping server connection");
            }
            last_backend_retry = now_ms;
        }
        
        // Receive UDP packets (non-blocking)
        remote_addr_len = sizeof(remote_addr);
        num_recv = recvfrom(udp_sock, (uint8_t *)buffer, UDP_BUFFER_SIZE - 1, 0,
                           (struct sockaddr *)&remote_addr, &remote_addr_len);
        
        if (num_recv > 0) {
            // Extract device_id from sender IP for logging
            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &remote_addr.sin_addr, sender_ip, sizeof(sender_ip));

            if (process_compact_telemetry((const uint8_t *)buffer, num_recv, sender_ip)) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

            buffer[num_recv] = 0;
            
            // Try to parse device_id from JSON (with memory safety)
            cJSON *doc = cJSON_Parse(buffer);
            if (doc != NULL) {
                cJSON *device_id_item = cJSON_GetObjectItem(doc, "device_id");
                if (device_id_item && device_id_item->type == cJSON_String) {
                    process_device_data(device_id_item->valuestring, buffer);
                } else {
                    LOG_WARN("No device_id in JSON from %s", sender_ip);
                }
                cJSON_Delete(doc);  // CRITICAL: Always delete to prevent heap leak
            } else {
                LOG_WARN("Invalid JSON from %s: %.100s", sender_ip, buffer);
            }
        }
        
        // Small delay to prevent watchdog timeout and allow MorseMicro tasks to run
        // Also prevents overwhelming the radio TX buffer
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============ Improv BLE Event Handling System ============

static void send_ble_improv_state_notification(ImprovState state) {
    if (improv_conn_id != 0xFFFF && improv_gatts_if != ESP_GATT_IF_NONE) {
        uint8_t value = (uint8_t)state;
        esp_ble_gatts_send_indicate(improv_gatts_if, improv_conn_id,
                                    improv_handle_table[IDX_CHAR_STATE_VAL],
                                    sizeof(value), &value, false); 
    }
}

static void send_ble_improv_error_notification(ImprovError error) {
    if (improv_conn_id != 0xFFFF && improv_gatts_if != ESP_GATT_IF_NONE) {
        uint8_t value = (uint8_t)error;
        esp_ble_gatts_send_indicate(improv_gatts_if, improv_conn_id,
                                    improv_handle_table[IDX_CHAR_ERROR_VAL],
                                    sizeof(value), &value, false); 
    }
}

static void send_ble_improv_rpc_result_notification(const uint8_t* data, uint16_t len) {
    if (improv_conn_id != 0xFFFF && improv_gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(improv_gatts_if, improv_conn_id,
                                    improv_handle_table[IDX_CHAR_RPC_RESULT_VAL],
                                    len, (uint8_t*)data, false); 
    }
}

typedef struct {
    char ssid[33];
    char password[65];
} wifi_provision_request_t;

static void send_empty_improv_rpc_result(uint8_t command) {
    uint8_t rpc_result[] = {command, 0x00, command};
    send_ble_improv_rpc_result_notification(rpc_result, sizeof(rpc_result));
}

static void send_gatt_write_response_if_needed(esp_gatt_if_t gatts_if,
                                               const esp_ble_gatts_cb_param_t *param) {
    if (param->write.need_rsp) {
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
    }
}

static bool apply_backend_wifi_credentials(const char *ssid, const char *password, uint32_t timeout_ms) {
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = 0;
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = 0;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    LOG_INFO("Applying provisioned backend WiFi SSID: %s", ssid);
    backend_wifi_has_ip = false;
    backend_wifi_disconnect_reason = 0;
    backend_connected = false;

    backend_wifi_reconfiguring = true;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        backend_wifi_reconfiguring = false;
        LOG_ERROR("Failed to apply provisioned WiFi config: %s", esp_err_to_name(err));
        return false;
    }

    backend_wifi_reconfiguring = false;
    err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        LOG_ERROR("Failed to start provisioned WiFi connection: %s", esp_err_to_name(err));
        return false;
    }

    uint32_t start_ms = mmosal_get_time_ms();
    while (mmosal_get_time_ms() - start_ms < timeout_ms) {
        if (backend_wifi_has_ip) {
            LOG_INFO("Provisioned WiFi connected successfully");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    LOG_WARN("Provisioned WiFi failed to connect within %lu ms (last reason=%u)",
             timeout_ms, backend_wifi_disconnect_reason);
    return false;
}

static void ble_wifi_provision_task(void *pvParameters) {
    wifi_provision_request_t *request = (wifi_provision_request_t *)pvParameters;

    update_improv_ble_state(STATE_PROVISIONING);

    if (apply_backend_wifi_credentials(request->ssid, request->password, 20000)) {
        strncpy(current_ssid, request->ssid, sizeof(current_ssid) - 1);
        current_ssid[sizeof(current_ssid) - 1] = 0;
        strncpy(current_password, request->password, sizeof(current_password) - 1);
        current_password[sizeof(current_password) - 1] = 0;
        save_wifi_credentials(current_ssid, current_password);

        update_improv_ble_state(STATE_PROVISIONED);
        send_empty_improv_rpc_result(WIFI_SETTINGS);
    } else {
        send_improv_error(ERROR_UNABLE_TO_CONNECT);
        update_improv_ble_state(STATE_AUTHORIZED);
    }

    free(request);
    vTaskDelete(NULL);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            // Once the main packet is registered, register the Companion Scan Response Name
            esp_ble_gap_config_scan_rsp_data_raw(improv_scan_rsp, improv_scan_rsp_len);
            break;
            
        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
            // Both packets are cleanly staged in memory. Fire up advertising.
            esp_ble_gap_start_advertising(&improv_adv_params);
            break;
            
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                LOG_ERROR("BLE advertising start failed: %d", param->adv_start_cmpl.status);
            } else {
                LOG_INFO("Improv Wi-Fi BLE advertising successfully initiated.");
            }
            break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (improv_restart_adv_after_stop && improv_conn_id == 0xFFFF) {
                improv_restart_adv_after_stop = false;
                esp_ble_gap_config_adv_data_raw(improv_adv_data, improv_adv_len);
            }
            break;
            
        default:
            break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    static esp_bt_uuid_t uuid_capabilities;
    static esp_bt_uuid_t uuid_state;
    static esp_bt_uuid_t uuid_error;
    static esp_bt_uuid_t uuid_rpc_command;
    static esp_bt_uuid_t uuid_rpc_result;
    static esp_bt_uuid_t uuid_cccd = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = 0x2902 } }; 
    static esp_gatt_srvc_id_t service_id = {};
    
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            improv_gatts_if = gatts_if;

            set_improv_uuid(&uuid_capabilities, 0x05);
            set_improv_uuid(&uuid_state, 0x01);
            set_improv_uuid(&uuid_error, 0x02);
            set_improv_uuid(&uuid_rpc_command, 0x03);
            set_improv_uuid(&uuid_rpc_result, 0x04);

            service_id.is_primary = true;
            service_id.id.inst_id = 0;
            set_improv_uuid(&service_id.id.uuid, 0x00);
            esp_ble_gap_set_device_name("RUSC Gateway");
            
            // Build structures cleanly into globals
            build_improv_adv_data(improv_adv_data, &improv_adv_len);
            build_improv_scan_rsp_data(improv_scan_rsp, &improv_scan_rsp_len);
            
            // Start sequential cascade setup via GAP handler
            esp_ble_gap_config_adv_data_raw(improv_adv_data, improv_adv_len);
            
            esp_ble_gatts_create_service(gatts_if, &service_id, IDX_CHAR_VAL_MAX);
        } else {
            LOG_ERROR("GATTS registration failed: %x", param->reg.status);
        }
    } else if (event == ESP_GATTS_CREATE_EVT) {
        if (param->create.status == ESP_GATT_OK) {
            improv_handle_table[IDX_SVC] = param->create.service_handle;
            esp_ble_gatts_start_service(improv_handle_table[IDX_SVC]);

            esp_ble_gatts_add_char(improv_handle_table[IDX_SVC], &uuid_capabilities,
                                   ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, NULL, NULL);

            esp_ble_gatts_add_char(improv_handle_table[IDX_SVC], &uuid_state,
                                   ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY, NULL, NULL);
            esp_ble_gatts_add_char_descr(improv_handle_table[IDX_SVC], &uuid_cccd,
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
            
            esp_ble_gatts_add_char(improv_handle_table[IDX_SVC], &uuid_error,
                                   ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY, NULL, NULL);
            esp_ble_gatts_add_char_descr(improv_handle_table[IDX_SVC], &uuid_cccd,
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
            
            esp_ble_gatts_add_char(improv_handle_table[IDX_SVC], &uuid_rpc_command,
                                   ESP_GATT_PERM_WRITE, ESP_GATT_CHAR_PROP_BIT_WRITE, NULL, NULL);
            
            esp_ble_gatts_add_char(improv_handle_table[IDX_SVC], &uuid_rpc_result,
                                   ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY, NULL, NULL);
            esp_ble_gatts_add_char_descr(improv_handle_table[IDX_SVC], &uuid_cccd,
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        } else {
            LOG_ERROR("Improv Service creation failed, error code = %x", param->create.status);
        }
    } else if (event == ESP_GATTS_ADD_CHAR_EVT) {
        if (param->add_char.status == ESP_GATT_OK) {
            if (param->add_char.char_uuid.len == ESP_UUID_LEN_128) {
                uint8_t endpoint = param->add_char.char_uuid.uuid.uuid128[0];
                if (endpoint == 0x05) improv_handle_table[IDX_CHAR_CAPABILITIES_VAL] = param->add_char.attr_handle;
                else if (endpoint == 0x01) improv_handle_table[IDX_CHAR_STATE_VAL] = param->add_char.attr_handle;       
                else if (endpoint == 0x02) improv_handle_table[IDX_CHAR_ERROR_VAL] = param->add_char.attr_handle;   
                else if (endpoint == 0x03) improv_handle_table[IDX_CHAR_RPC_COMMAND_VAL] = param->add_char.attr_handle; 
                else if (endpoint == 0x04) improv_handle_table[IDX_CHAR_RPC_RESULT_VAL] = param->add_char.attr_handle;  
            }
        }
    } else if (event == ESP_GATTS_ADD_CHAR_DESCR_EVT) {
        if (param->add_char_descr.status == ESP_GATT_OK) {
            if (param->add_char_descr.descr_uuid.uuid.uuid16 == 0x2902) { 
                static uint8_t cccd_count = 0;
                if (cccd_count == 0) improv_handle_table[IDX_CHAR_STATE_CFG] = param->add_char_descr.attr_handle;
                else if (cccd_count == 1) improv_handle_table[IDX_CHAR_ERROR_CFG] = param->add_char_descr.attr_handle;
                else if (cccd_count == 2) improv_handle_table[IDX_CHAR_RPC_RESULT_CFG] = param->add_char_descr.attr_handle;
                cccd_count++;
            }
        }
    } else if (event == ESP_GATTS_READ_EVT) {
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        
        if (param->read.handle == improv_handle_table[IDX_CHAR_CAPABILITIES_VAL]) {
            rsp.attr_value.len = 1;
            rsp.attr_value.value[0] = IMPROV_CAPABILITIES;
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        } else if (param->read.handle == improv_handle_table[IDX_CHAR_STATE_VAL]) {
            rsp.attr_value.len = 1;
            rsp.attr_value.value[0] = (uint8_t)s_improv_state;
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        } else if (param->read.handle == improv_handle_table[IDX_CHAR_ERROR_VAL]) {
            rsp.attr_value.len = 1;
            rsp.attr_value.value[0] = (uint8_t)s_improv_error;
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        } else {
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_READ_NOT_PERMIT, NULL);
        }
    } else if (event == ESP_GATTS_WRITE_EVT) {
        if (param->write.handle == improv_handle_table[IDX_CHAR_RPC_COMMAND_VAL]) {
            LOG_INFO("Improv RPC write received: len=%u", param->write.len);
            if (param->write.len < 3) {
                send_improv_error(ERROR_INVALID_RPC);
                send_gatt_write_response_if_needed(gatts_if, param);
                return;
            }

            uint8_t command = param->write.value[0];
            uint8_t data_len = param->write.value[1];
            const uint8_t* data = param->write.value + 2;
            uint8_t expected_len = data_len + 3;
            uint8_t expected_checksum = improv_checksum(param->write.value, expected_len - 1);
            LOG_INFO("Improv RPC command=0x%02x data_len=%u", command, data_len);

            if (param->write.len != expected_len || param->write.value[expected_len - 1] != expected_checksum) {
                LOG_WARN("Invalid Improv RPC packet: actual_len=%u expected_len=%u checksum=0x%02x expected=0x%02x",
                         param->write.len, expected_len, param->write.value[expected_len - 1], expected_checksum);
                send_improv_error(ERROR_INVALID_RPC);
                uint8_t rpc_result[] = {command, 0x00, command};
                send_ble_improv_rpc_result_notification(rpc_result, sizeof(rpc_result));
            } else if (command == WIFI_SETTINGS && data_len >= 2) {
                if (s_improv_state != STATE_AUTHORIZED) {
                    send_improv_error(ERROR_NOT_AUTHORIZED);
                    uint8_t rpc_result[] = {WIFI_SETTINGS, 0x00, WIFI_SETTINGS};
                    send_ble_improv_rpc_result_notification(rpc_result, sizeof(rpc_result));
                    send_gatt_write_response_if_needed(gatts_if, param);
                    return;
                }

                update_improv_ble_state(STATE_PROVISIONING);

                uint8_t ssid_len = data[0];
                if (ssid_len > 32 || (uint16_t)ssid_len + 2 > data_len) {
                    send_improv_error(ERROR_INVALID_RPC);
                    send_gatt_write_response_if_needed(gatts_if, param);
                    return;
                }

                char new_ssid[33] = {0}; 
                memcpy(new_ssid, &data[1], ssid_len);

                uint8_t pass_len = data[1 + ssid_len];
                if (pass_len > 64 || (uint16_t)ssid_len + pass_len + 2 > data_len) {
                    send_improv_error(ERROR_INVALID_RPC);
                    send_gatt_write_response_if_needed(gatts_if, param);
                    return;
                }

                char new_pass[65] = {0}; 
                memcpy(new_pass, &data[2 + ssid_len], pass_len);

                LOG_INFO("WiFi credentials received via BLE, testing connection...");

                wifi_provision_request_t *request = (wifi_provision_request_t *)calloc(1, sizeof(wifi_provision_request_t));
                if (request == NULL) {
                    send_improv_error(ERROR_UNKNOWN);
                    send_gatt_write_response_if_needed(gatts_if, param);
                    return;
                }

                strncpy(request->ssid, new_ssid, sizeof(request->ssid) - 1);
                strncpy(request->password, new_pass, sizeof(request->password) - 1);

                if (xTaskCreate(ble_wifi_provision_task, "improv_wifi", 4096, request, 4, NULL) != pdPASS) {
                    free(request);
                    send_improv_error(ERROR_UNKNOWN);
                    send_gatt_write_response_if_needed(gatts_if, param);
                    return;
                }
            } else if (command == IDENTIFY) {
                LOG_INFO("Improv identify requested");
            } else if (command == GET_DEVICE_INFO) {
                const char* firmware_name = "MorseMicro";
                const char* firmware_version = "1.0";
                const char* hardware_chip = "ESP32";
                const char* device_name = "HaLow Gateway";
                
                uint8_t rpc_result[128];
                uint8_t *p = rpc_result;
                
                *p++ = GET_DEVICE_INFO; 
                uint8_t *len_pos = p++; 
                uint8_t data_start = p - rpc_result;
                
                *p++ = strlen(firmware_name);
                memcpy(p, firmware_name, strlen(firmware_name));
                p += strlen(firmware_name);
                
                *p++ = strlen(firmware_version);
                memcpy(p, firmware_version, strlen(firmware_version));
                p += strlen(firmware_version);
                
                *p++ = strlen(hardware_chip);
                memcpy(p, hardware_chip, strlen(hardware_chip));
                p += strlen(hardware_chip);
                
                *p++ = strlen(device_name);
                memcpy(p, device_name, strlen(device_name));
                p += strlen(device_name);
                
                *len_pos = p - (rpc_result + data_start); 
                *p = improv_checksum(rpc_result, p - rpc_result);
                p++;
                
                send_ble_improv_rpc_result_notification(rpc_result, p - rpc_result);
            } else {
                send_improv_error(ERROR_UNKNOWN_RPC);
                uint8_t rpc_result[] = {command, 0x00, command};
                send_ble_improv_rpc_result_notification(rpc_result, sizeof(rpc_result));
            }
        }
        send_gatt_write_response_if_needed(gatts_if, param);
    } else if (event == ESP_GATTS_CONNECT_EVT) {
        improv_conn_id = param->connect.conn_id;
        LOG_INFO("BLE client connected, conn_id=%u", improv_conn_id);
    } else if (event == ESP_GATTS_DISCONNECT_EVT) {
        improv_conn_id = 0xFFFF;
        LOG_INFO("BLE client disconnected, restarting Improv advertising");
        build_improv_adv_data(improv_adv_data, &improv_adv_len);
        esp_ble_gap_config_adv_data_raw(improv_adv_data, improv_adv_len);
    }
}
esp_err_t initialize_ble_improv(void) {
    esp_err_t ret;

    // 1. Initialize Non-Volatile Storage (NVS) - Required for BLE pairing bonding data
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Release Classic Bluetooth memory since we only care about BLE 
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // 3. Initialize and enable the hardware BT Controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE("BLE_INIT", "initialize controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE("BLE_INIT", "enable controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Initialize and enable Bluedroid (The host software BLE stack)
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE("BLE_INIT", "init bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE("BLE_INIT", "enable bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 5. Register your GAP (Advertising/Scanning) event handler
    // Make sure this matches your actual GAP callback function name!
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE("BLE_INIT", "gap register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 6. Register your GATTS (Services/Characteristics) event handler
    // Make sure this matches your actual GATTS callback function name!
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE("BLE_INIT", "gatts register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 7. Register your application profile ID (arbitrarily using ID 0 here)
    // This triggers the profile registration event where your GATT table gets built
    ret = esp_ble_gatts_app_register(0);
    if (ret != ESP_OK) {
        ESP_LOGE("BLE_INIT", "gatts app register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}
// ============ Main Entry Point ============

extern "C" void app_main(void) {
    printf("\n\n");
    printf("=================================================================\n");
    printf("RUSC Device 2: HaLow Gateway (MorseMicro + ESP-IDF)\n");
    printf("=================================================================\n\n");
    
    esp_err_t err;
    
    // 1. Initialize NVS FIRST (Critical for WiFi, BLE, and stored credentials)
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    init_battery_adc();
    update_gateway_battery();

    // 2. Load credentials and set initial Improv state
    // IMPORTANT: Always boot as AUTHORIZED for reprovisioning capability
    // Only change to PROVISIONED after WiFi backend connects successfully
    if (load_wifi_credentials()) {
        LOG_INFO("Provisioning found in NVS - but starting as AUTHORIZED for reprovisioning capability");
    } else {
        LOG_INFO("No provisioning found in NVS");
    }
    s_improv_state = STATE_AUTHORIZED;

    // Initialize HaLow AP (must come before other GPIO initialization)
    err = initialize_halow_ap();
    if (err != ESP_OK) {
        LOG_ERROR("Failed to initialize HaLow AP!");
        return;
    }
    
    // Battery ADC initialization disabled - ADC driver not available in build
    // adc1_config_width(ADC_WIDTH_BIT_12);
    // adc1_config_channel_atten(BATTERY_PIN, ADC_ATTEN_DB_11);
    
    // Initialize UDP listener
    err = initialize_udp_listener();
    if (err != ESP_OK) {
        LOG_ERROR("Failed to initialize UDP listener!");
        return;
    }
    
    // Initialize backend WiFi
    err = initialize_backend_wifi();
    if (err != ESP_OK) {
        LOG_WARN("Backend WiFi initialization failed - will retry");
    }
    
    // Wait for WiFi to connect and connect to backend
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        LOG_INFO("Backend WiFi connected!");
        err = connect_backend_server();
        if (err == ESP_OK) {
            // Successfully connected to backend - mark as PROVISIONED
            s_improv_state = STATE_PROVISIONED;
            LOG_INFO("Backend connection successful - device is now PROVISIONED");
        } else {
            LOG_WARN("Failed to connect to backend server - staying in AUTHORIZED for reprovisioning");
        }
    } else {
        LOG_WARN("Backend WiFi connection failed - staying in AUTHORIZED for reprovisioning");
    }
    
    // Initialize BLE Improv
    err = initialize_ble_improv();
    if (err != ESP_OK) {
        LOG_ERROR("Failed to initialize BLE Improv! Halting.");
        return;
    }
    
    // Wait for BLE to stabilize before starting main tasks
    vTaskDelay(pdMS_TO_TICKS(1000));

    LOG_INFO("Gateway initialization complete - starting gateway task");
    printf("=================================================================\n\n");
    
    // Create gateway task with dedicated stack to avoid stack overflow
    xTaskCreate(gateway_task, "gateway", GATEWAY_TASK_STACK_SIZE, NULL, GATEWAY_TASK_PRIORITY, NULL);
    // BLE Improv only - Serial Improv disabled
    // xTaskCreate(improv_task, "improv", IMPROV_TASK_STACK_SIZE, NULL, 5, NULL);
    
    // app_main returns here - other initialization is complete
    // Gateway task runs independently
}