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

// Battery monitoring - DISABLED (ADC driver not available in build)
// #define BATTERY_PIN ADC1_CHANNEL_2  // GPIO 2 for battery
// #define BATTERY_MIN_MV 2800         // Empty battery (3.0V)
// #define BATTERY_MAX_MV 4200         // Full battery (4.2V)
// #define BATTERY_UPDATE_INTERVAL_MS 5000

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
// Task configuration
#define IMPROV_BLE_APP_ID 0
#define IMPROV_TASK_STACK_SIZE 4096
#define IMPROV_UART_PORT UART_NUM_0
#define IMPROV_BAUD_RATE 115200

// Improv Serial Protocol Constants
enum ImprovCommand { UNKNOWN = 0x00, WIFI_SETTINGS = 0x01, IDENTIFY = 0x02, GET_CURRENT_STATE = 0x03, GET_DEVICE_INFO = 0x04 };
enum ImprovState { STATE_STOPPED = 0x00, STATE_AWAITING_AUTHORIZATION = 0x01, STATE_AUTHORIZED = 0x02, STATE_PROVISIONING = 0x03, STATE_PROVISIONED = 0x04 };
enum ImprovError { ERROR_NONE = 0x00, ERROR_INVALID_RPC = 0x01, ERROR_UNKNOWN_RPC = 0x02, ERROR_UNABLE_TO_CONNECT = 0x03, ERROR_NOT_AUTHORIZED = 0x04, ERROR_UNKNOWN = 0x05 };

static const char* IMPROV_PREFIX = "IMPROV";

// Improv BLE UUIDs (Official Specification - 16-bit)
// Service UUID: 0x184E (Improv WiFi Service)
// Characteristic UUIDs:
// - Current State (0x0001): State of provisioning (Notify, Read)
// - Error (0x0002): Error information (Notify, Read)
// - RPC Command (0x0003): Commands from client (Write)
// - RPC Result (0x0004): Results from commands (Notify, Read)
// Service Data UUID: 0x184E (used in advertisement)

static const uint16_t improv_service_uuid = 0x184E;
static const uint16_t improv_current_state_uuid = 0x0001;
static const uint16_t improv_error_uuid = 0x0002;
static const uint16_t improv_rpc_command_uuid = 0x0003;
static const uint16_t improv_rpc_result_uuid = 0x0004;

enum {
    IDX_SVC,
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
static uint16_t improv_handle_table[IDX_CHAR_VAL_MAX];
static uint16_t improv_conn_id = 0xFFFF;
static esp_gatt_if_t improv_gatts_if = ESP_GATT_IF_NONE;

// CRITICAL: Ensure this defaults to STATE_AUTHORIZED (0x02) if you want it 
// to be auto-discovered by clients immediately without a button press!
static ImprovState s_improv_state = STATE_AUTHORIZED; 
static ImprovError s_improv_error = ERROR_NONE;

static uint8_t improv_adv_data[31];  // Main advertisement buffer
static uint8_t improv_scan_rsp[31];  // Scan response buffer
static uint16_t improv_adv_len = 0;
static uint16_t improv_scan_rsp_len = 0;

// 1. Build the primary Advertisement Data (Strictly core Improv telemetry)
static void build_improv_adv_data(uint8_t *adv_data, uint16_t *adv_len) {
    uint8_t *p = adv_data;
    
    // Flags: LE General Discoverable, BR/EDR not supported
    *p++ = 0x02; // Length
    *p++ = 0x01; // Type: Flags
    *p++ = 0x06; // Value
    
    // Service Data (UUID 0x184E with state, capabilities, and 4 reserved bytes)
    // Total data size = 1 (Type) + 2 (UUID) + 1 (State) + 1 (Cap) + 4 (Reserved) = 9 bytes
    *p++ = 0x09; // Length field
    *p++ = 0x16; // Type: Service Data - 16-bit UUID
    *p++ = 0x4E; // Service UUID 0x184E (little-endian: 4E 18)
    *p++ = 0x18;
    *p++ = (uint8_t)s_improv_state;    // 0x02 = Authorized / Ready
    *p++ = 0x03;                        // Capabilities: 0x03 = Identify & Device Info
    *p++ = 0x00; // Reserved 1
    *p++ = 0x00; // Reserved 2
    *p++ = 0x00; // Reserved 3
    *p++ = 0x00; // Reserved 4 (Fills out the standard 6-byte payload requirement)
    
    // Complete List of 16-bit Service UUIDs
    *p++ = 0x03; // Length
    *p++ = 0x03; // Type: Complete List of 16-bit Service UUIDs
    *p++ = 0x4E; // Service UUID 0x184E (little-endian)
    *p++ = 0x18;
    
    *adv_len = (uint16_t)(p - adv_data); // Total: 16 bytes. Leaves plenty of margin.
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

static int battery_percent = 100;
static uint32_t total_packets_received = 0;
static uint32_t total_devices_connected = 0;

static uint32_t last_halow_check_ms = 0;
static uint32_t last_log_ms = 0;

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
    uint32_t last_seen_ms;
    uint32_t packet_count;
} device_info_t;

// Map for connected devices (simple array-based implementation)
#define MAX_DEVICES 10
static device_info_t connected_devices[MAX_DEVICES];
static int num_connected_devices = 0;

// ============ Battery Monitoring - DISABLED ============
// Battery monitoring disabled due to ADC driver build issues
// Can be re-enabled when ADC component is properly available
// For now, battery_percent is set to constant 100%

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
        LOG_WARN("WiFi disconnected, retrying...");
        esp_wifi_connect();
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
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
            if (sats && sats->type == cJSON_Number) {
                connected_devices[i].satellites = sats->valueint;
            }
            
            break;
        }
    }
    
    // Forward to backend if connected
    if (backend_connected && backend_sock >= 0) {
        ssize_t sent = send(backend_sock, (const uint8_t *)json_data, strlen(json_data), 0);
        if (sent > 0) {
            send(backend_sock, (const uint8_t *)"\n", 1, 0);
            LOG_DEBUG("Forwarded to backend: %s", device_id);
        } else {
            LOG_WARN("Failed to send to backend (errno=%d) - reconnecting", errno);
            close(backend_sock);
            backend_sock = -1;
            backend_connected = false;
        }
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

// ============ Main Application Loop ============

/**
 * Dedicated task to monitor and ensure BLE advertising is active
 */
static void ble_advertising_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(3000)); // Wait for BLE stack to fully initialize
    
    while (1) {
        if (improv_gatts_if != ESP_GATT_IF_NONE) {
            // Rebuild advertisement data with current state
            uint16_t adv_len;
            build_improv_adv_data(improv_adv_data, &improv_adv_len);
            build_improv_scan_rsp_data(improv_scan_rsp, &improv_scan_rsp_len);

            // Load the primary advertisement tracking info
            esp_ble_gap_config_adv_data_raw(improv_adv_data, improv_adv_len);

            // Load the companion string name packet 
            //esp_ble_gap_config_scan_rsp_data_raw(improv_scan_rsp, improv_scan_rsp_len);
            //build_improv_adv_data(improv_adv_data, &adv_len);
            //esp_ble_gap_config_adv_data_raw(improv_adv_data, (uint32_t)adv_len);
            //esp_ble_gap_start_advertising(&improv_adv_params);
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
    }
}

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
        
        // Log metrics periodically
        log_device_metrics();
        
        // Monitor heap health - detect memory leaks early
        uint32_t now_ms = mmosal_get_time_ms();
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
            buffer[num_recv] = 0;
            
            // Extract device_id from sender IP for logging
            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &remote_addr.sin_addr, sender_ip, sizeof(sender_ip));
            
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

// ============ Improv BLE Implementation ============

static void send_ble_improv_state_notification(ImprovState state) {
    if (improv_conn_id != 0xFFFF && improv_gatts_if != ESP_GATT_IF_NONE) {
        uint8_t value = (uint8_t)state;
        esp_ble_gatts_send_indicate(improv_gatts_if, improv_conn_id,
                                    improv_handle_table[IDX_CHAR_STATE_VAL],
                                    sizeof(value), &value, false); // false for notification
    }
}

static void send_ble_improv_error_notification(ImprovError error) {
    if (improv_conn_id != 0xFFFF && improv_gatts_if != ESP_GATT_IF_NONE) {
        uint8_t value = (uint8_t)error;
        esp_ble_gatts_send_indicate(improv_gatts_if, improv_conn_id,
                                    improv_handle_table[IDX_CHAR_ERROR_VAL],
                                    sizeof(value), &value, false); // false for notification
    }
}

static void send_ble_improv_rpc_result_notification(const uint8_t* data, uint16_t len) {
    if (improv_conn_id != 0xFFFF && improv_gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(improv_gatts_if, improv_conn_id,
                                    improv_handle_table[IDX_CHAR_RPC_RESULT_VAL],
                                    len, (uint8_t*)data, false); // false for notification
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
             // After setting raw adv data, start advertising
             esp_ble_gap_start_advertising(&improv_adv_params);
             break;
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&improv_adv_params);
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                LOG_ERROR("BLE advertising start failed: %d", param->adv_start_cmpl.status);
            }
            break;
        default:
            break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    // Static UUIDs for characteristics and descriptors (16-bit)
    static esp_bt_uuid_t uuid_state = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = 0x0001 } };     // Current State
    static esp_bt_uuid_t uuid_error = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = 0x0002 } };     // Error
    static esp_bt_uuid_t uuid_rpc_command = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = 0x0003 } }; // RPC Command
    static esp_bt_uuid_t uuid_rpc_result = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = 0x0004 } }; // RPC Result
    static esp_bt_uuid_t uuid_cccd = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = 0x2902 } };    // Client Characteristic Configuration Descriptor
    static esp_gatt_srvc_id_t service_id = { .id = { .uuid = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = 0x184E } }, .inst_id = 0 }, .is_primary = true };
    
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            improv_gatts_if = gatts_if;
            uint16_t adv_len;
            build_improv_adv_data(improv_adv_data, &improv_adv_len);
            build_improv_scan_rsp_data(improv_scan_rsp, &improv_scan_rsp_len);
  
            // Load the primary advertisement tracking info
            esp_ble_gap_config_adv_data_raw(improv_adv_data, improv_adv_len);
     
            // Load the companion string name packet 
            //esp_ble_gap_config_scan_rsp_data_raw(improv_scan_rsp, improv_scan_rsp_len);
            //build_improv_adv_data(improv_adv_data, &adv_len);
            //esp_ble_gap_config_adv_data_raw(improv_adv_data, (uint32_t)adv_len);
            // Device name is embedded in raw advertisement data
            esp_ble_gatts_create_service(gatts_if, &service_id, IDX_CHAR_VAL_MAX);
        } else {
            LOG_ERROR("GATTS registration failed: %x", param->reg.status);
        }
    } else if (event == ESP_GATTS_CREATE_EVT) {
        if (param->create.status == ESP_GATT_OK) {
            improv_handle_table[IDX_SVC] = param->create.service_handle;
            esp_ble_gatts_start_service(improv_handle_table[IDX_SVC]);

            // Add Characteristics (0x0001-0x0004 per Improv WiFi spec)
            esp_ble_gatts_add_char(improv_handle_table[IDX_SVC],
                                   &uuid_state,
                                   ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY, NULL, NULL);
            esp_ble_gatts_add_char_descr(improv_handle_table[IDX_SVC],
                                         &uuid_cccd,
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
            esp_ble_gatts_add_char(improv_handle_table[IDX_SVC],
                                   &uuid_error,
                                   ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY, NULL, NULL);
            esp_ble_gatts_add_char_descr(improv_handle_table[IDX_SVC],
                                         &uuid_cccd,
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
            esp_ble_gatts_add_char(improv_handle_table[IDX_SVC],
                                   &uuid_rpc_command,
                                   ESP_GATT_PERM_WRITE, ESP_GATT_CHAR_PROP_BIT_WRITE, NULL, NULL);
            esp_ble_gatts_add_char(improv_handle_table[IDX_SVC],
                                   &uuid_rpc_result,
                                   ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY, NULL, NULL);
            esp_ble_gatts_add_char_descr(improv_handle_table[IDX_SVC],
                                         &uuid_cccd,
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        } else {
            LOG_ERROR("Improv Service creation failed, error code = %x", param->create.status);
        }
    } else if (event == ESP_GATTS_ADD_CHAR_EVT) {
        if (param->add_char.status == ESP_GATT_OK) {
            if (param->add_char.char_uuid.len == ESP_UUID_LEN_16) {
                uint16_t uuid = param->add_char.char_uuid.uuid.uuid16;
                if (uuid == 0x0001) improv_handle_table[IDX_CHAR_STATE_VAL] = param->add_char.attr_handle;       // Current State
                else if (uuid == 0x0002) improv_handle_table[IDX_CHAR_ERROR_VAL] = param->add_char.attr_handle;   // Error
                else if (uuid == 0x0003) improv_handle_table[IDX_CHAR_RPC_COMMAND_VAL] = param->add_char.attr_handle; // RPC Command
                else if (uuid == 0x0004) improv_handle_table[IDX_CHAR_RPC_RESULT_VAL] = param->add_char.attr_handle;  // RPC Result
            }
        } else {
            LOG_ERROR("Add characteristic failed: %x", param->add_char.status);
        }
    } else if (event == ESP_GATTS_ADD_CHAR_DESCR_EVT) {
        if (param->add_char_descr.status == ESP_GATT_OK) {
            if (param->add_char_descr.descr_uuid.uuid.uuid16 == 0x2902) { // CCCD UUID
                static uint8_t cccd_count = 0;
                if (cccd_count == 0) improv_handle_table[IDX_CHAR_STATE_CFG] = param->add_char_descr.attr_handle;
                else if (cccd_count == 1) improv_handle_table[IDX_CHAR_ERROR_CFG] = param->add_char_descr.attr_handle;
                else if (cccd_count == 2) improv_handle_table[IDX_CHAR_RPC_RESULT_CFG] = param->add_char_descr.attr_handle;
                cccd_count++;
            }
        } else {
            LOG_ERROR("Add descriptor failed: %x", param->add_char_descr.status);
        }
    } else if (event == ESP_GATTS_READ_EVT) {
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        
        if (param->read.handle == improv_handle_table[IDX_CHAR_STATE_VAL]) {
            uint8_t state = (uint8_t)s_improv_state;
            rsp.attr_value.len = 1;
            rsp.attr_value.value[0] = state;
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        } else if (param->read.handle == improv_handle_table[IDX_CHAR_ERROR_VAL]) {
            uint8_t error = (uint8_t)s_improv_error;
            rsp.attr_value.len = 1;
            rsp.attr_value.value[0] = error;
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        } else {
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_READ_NOT_PERMIT, NULL);
        }
    } else if (event == ESP_GATTS_WRITE_EVT) {
        if (param->write.handle == improv_handle_table[IDX_CHAR_RPC_COMMAND_VAL]) {
            uint8_t command = param->write.value[0];
            uint16_t data_len = param->write.len - 1;
            const uint8_t* data = param->write.value + 1;

            if (command == WIFI_SETTINGS && data_len > 2) {
                send_improv_state(STATE_PROVISIONING);

                uint8_t ssid_len = data[0];
                char new_ssid[33] = {0}; // Already safe in context, but ensuring null termination
                memcpy(new_ssid, &data[1], ssid_len);

                uint8_t pass_len = data[1 + ssid_len];
                char new_pass[65] = {0}; // Already safe in context
                memcpy(new_pass, &data[2 + ssid_len], pass_len);

                LOG_INFO("WiFi provisioned via BLE, restarting device...");
                save_wifi_credentials(new_ssid, new_pass);

                send_improv_state(STATE_PROVISIONED);
                
                // RPC Result: Command + Length + [String length + String]... (no URL return)
                uint8_t rpc_result[] = {WIFI_SETTINGS, 0x00}; // Command, no strings
                send_ble_improv_rpc_result_notification(rpc_result, sizeof(rpc_result));

                // Restart after successful provisioning
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            } else if (command == GET_DEVICE_INFO) {
                // Device Info format per spec: Command + Length + [String length + String]... + Checksum
                // Returns: Firmware name, Firmware version, Hardware chip, Device name
                const char* firmware_name = "MorseMicro";
                const char* firmware_version = "1.0";
                const char* hardware_chip = "ESP32";
                const char* device_name = "HaLow Gateway";
                
                uint8_t rpc_result[128];
                uint8_t *p = rpc_result;
                
                *p++ = GET_DEVICE_INFO; // Command ID
                uint8_t *len_pos = p++; // Placeholder for data length
                uint8_t data_start = p - rpc_result;
                
                // String 1: Firmware name
                *p++ = strlen(firmware_name);
                memcpy(p, firmware_name, strlen(firmware_name));
                p += strlen(firmware_name);
                
                // String 2: Firmware version
                *p++ = strlen(firmware_version);
                memcpy(p, firmware_version, strlen(firmware_version));
                p += strlen(firmware_version);
                
                // String 3: Hardware chip
                *p++ = strlen(hardware_chip);
                memcpy(p, hardware_chip, strlen(hardware_chip));
                p += strlen(hardware_chip);
                
                // String 4: Device name
                *p++ = strlen(device_name);
                memcpy(p, device_name, strlen(device_name));
                p += strlen(device_name);
                
                *len_pos = p - (rpc_result + data_start); // Set data length
                *p++ = 0x00; // Checksum (placeholder, will be calculated if needed)
                
                send_ble_improv_rpc_result_notification(rpc_result, p - rpc_result);
            } else {
                send_improv_error(ERROR_UNKNOWN_RPC);
                uint8_t rpc_result[] = {command, ERROR_UNKNOWN_RPC};
                send_ble_improv_rpc_result_notification(rpc_result, sizeof(rpc_result));
            }
        } else if (param->write.handle == improv_handle_table[IDX_CHAR_STATE_CFG] ||
                   param->write.handle == improv_handle_table[IDX_CHAR_ERROR_CFG] ||
                   param->write.handle == improv_handle_table[IDX_CHAR_RPC_RESULT_CFG]) {
            // Client Characteristic Configuration Descriptor write (enable/disable notifications)
        }
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
    } else if (event == ESP_GATTS_CONNECT_EVT) {
        improv_conn_id = param->connect.conn_id;
        LOG_INFO("BLE client connected");
        // DO NOT change state on connect - keep advertising state 0x02 (AUTHORIZED)
        // so commissioning apps can discover and provision the device
    } else if (event == ESP_GATTS_DISCONNECT_EVT) {
        improv_conn_id = 0xFFFF;
        esp_ble_gap_start_advertising(&improv_adv_params);
    }
}

static esp_err_t initialize_ble_improv(void) {
    esp_err_t ret;

    // Release Classic BT memory if not needed to save RAM
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        LOG_ERROR("%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        LOG_ERROR("%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        LOG_ERROR("%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        LOG_ERROR("%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret){
        LOG_ERROR("gatts register error, error code = %x", ret);
        return ret;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret){
        LOG_ERROR("gap register error, error code = %x", ret);
        return ret;
    }

    ret = esp_ble_gatts_app_register(IMPROV_BLE_APP_ID);
    if (ret){
        LOG_ERROR("gatts app register error, error code = %x", ret);
        return ret;
    }

    esp_ble_gatt_set_local_mtu(500); // Set MTU for potentially larger RPC results

    return ESP_OK;
}

/**
 * Task to handle Improv Serial provisioning
 */
static void improv_task(void *pvParameters) {
    uart_config_t uart_config = {
        .baud_rate = IMPROV_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(IMPROV_UART_PORT, &uart_config);
    uart_driver_install(IMPROV_UART_PORT, 256, 0, 0, NULL, 0);

    uint8_t data[128];

    while (1) {
        int len = uart_read_bytes(IMPROV_UART_PORT, data, sizeof(data), 100 / pdMS_TO_TICKS(1));
        if (len >= 9 && memcmp(data, IMPROV_PREFIX, 6) == 0) { // Minimum packet size for header + 1 byte payload + checksum
            uint8_t version = data[6]; // Should be 1
            uint8_t packet_type = data[7]; // This is the packet type (Command, Error, State, RPC Result)
            uint8_t payload_len = data[8]; // Length of the payload (data after payload_len byte)
            
            // Basic checksum validation
            if (len < (9 + payload_len + 1)) { // 6 prefix + 1 version + 1 type + 1 payload_len + payload_len + 1 checksum
                // No error response here, as the packet structure is fundamentally broken or incomplete.
                continue;
            }

            uint8_t received_checksum = data[8 + payload_len + 1]; // Checksum is the last byte
            uint8_t calculated_checksum = 0;
            for (int i = 0; i < (8 + payload_len + 1); i++) { // Sum up to the byte before checksum
                calculated_checksum += data[i];
            }

            if (calculated_checksum != received_checksum) {
                // For checksum errors, just drop the packet
                continue;
            }

            if (packet_type == 0x01) { // This is a Command packet
                if (payload_len < 1) { // A command packet must have at least 1 byte for the command ID
                    send_improv_error(ERROR_INVALID_RPC);
                    continue;
                }
                uint8_t command_id = data[9]; // This is the actual ImprovCommand enum value

                if (command_id == WIFI_SETTINGS) {
                    // Expected payload_len for WIFI_SETTINGS: 1 (ssid_len) + ssid_len + 1 (pass_len) + pass_len
                    if (payload_len < 3) { // Minimum: 1 (ssid_len=1) + 1 (ssid) + 1 (pass_len=0) + 0 (pass) = 3
                        send_improv_error(ERROR_INVALID_RPC);
                        continue;
                    }
                    
                    send_improv_state(STATE_PROVISIONING);
                    
                    uint8_t ssid_len = data[10]; // ssid_len is at data[9 + 1]
                    if (ssid_len > 32 || (10 + ssid_len) > (9 + payload_len)) { // Check bounds for ssid_len
                        send_improv_error(ERROR_INVALID_RPC);
                        continue;
                    }
                    char new_ssid[33] = {0};
                    memcpy(new_ssid, &data[11], ssid_len); // SSID starts at data[10 + 1]
                    
                    uint8_t pass_len_idx = 11 + ssid_len; // pass_len is at data[10 + ssid_len + 1]
                    if (pass_len_idx >= (9 + payload_len)) { // Check bounds for pass_len_idx
                        send_improv_error(ERROR_INVALID_RPC);
                        continue;
                    }
                    uint8_t pass_len = data[pass_len_idx];
                    if (pass_len > 64 || (pass_len_idx + 1 + pass_len) > (9 + payload_len)) { // Check bounds for pass_len
                        send_improv_error(ERROR_INVALID_RPC);
                        continue;
                    }
                    char new_pass[65] = {0};
                    memcpy(new_pass, &data[pass_len_idx + 1], pass_len);
                    
                    save_wifi_credentials(new_ssid, new_pass);
                    
                    send_improv_state(STATE_PROVISIONED);
                    // Restart after successful provisioning
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                } else if (command_id == GET_DEVICE_INFO) {
                    if (payload_len != 1) { // GET_DEVICE_INFO command has no additional payload
                        send_improv_error(ERROR_INVALID_RPC);
                        continue;
                    }
                const char* info = "MorseMicro-ESP32-Gateway";
                uint8_t info_len = strlen(info);
                // RPC Result packet: IMPROV (6) + V (1) + Type=0x04 (1) + Length (1) + Data (info_len) + Checksum (1)
                uint8_t resp[128]; // Max 128 bytes for response
                if (info_len + 10 > sizeof(resp)) { // 6+1+1+1+info_len+1
                    // Cannot send response, skip
                    continue;
                }
                memcpy(resp, IMPROV_PREFIX, 6);
                resp[6] = 1; // Version
                resp[7] = 0x04; // Packet Type: RPC Result
                resp[8] = info_len; // Payload Length
                memcpy(&resp[9], info, info_len); // Payload
                uint8_t checksum = 0;
                for (int i = 0; i < 9 + info_len; i++) checksum += resp[i];
                resp[9 + info_len] = checksum;
                uart_write_bytes(IMPROV_UART_PORT, (const char*)resp, 9 + info_len + 1);
                } else if (command_id == IDENTIFY) {
                    if (payload_len != 1) { // IDENTIFY command has no additional payload
                        send_improv_error(ERROR_INVALID_RPC);
                        continue;
                    }
                    // Respond with current state to acknowledge identity
                    send_improv_state(s_improv_state);
                } else if (command_id == GET_CURRENT_STATE) {
                    if (payload_len != 1) { // GET_CURRENT_STATE command has no additional payload
                        send_improv_error(ERROR_INVALID_RPC);
                        continue;
                    }
                    wifi_ap_record_t info;
                    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
                        // If WiFi is connected, assume provisioned
                        send_improv_state(STATE_PROVISIONED);
                    } else {
                        // If not connected, but credentials are saved, it's authorized.
                        // If no credentials, it's awaiting authorization.
                        // The global s_improv_state is updated by load_wifi_credentials in app_main.
                        // So, just send the current global state.
                        send_improv_state(s_improv_state);
                    }
                } else {
                    send_improv_error(ERROR_UNKNOWN_RPC);
                }
            } else { // Not a Command packet type (e.g., Error, State, RPC Result received from host)
                // No error response, as the host shouldn't send these types.
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
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
    
    // Start BLE advertising task FIRST (will keep advertising active)
    xTaskCreate(ble_advertising_task, "ble_adv", 4096, NULL, 2, NULL);
    
    // Create gateway task with dedicated stack to avoid stack overflow
    xTaskCreate(gateway_task, "gateway", GATEWAY_TASK_STACK_SIZE, NULL, GATEWAY_TASK_PRIORITY, NULL);
    // BLE Improv only - Serial Improv disabled
    // xTaskCreate(improv_task, "improv", IMPROV_TASK_STACK_SIZE, NULL, 5, NULL);
    
    // app_main returns here - other initialization is complete
    // Gateway task runs independently
}