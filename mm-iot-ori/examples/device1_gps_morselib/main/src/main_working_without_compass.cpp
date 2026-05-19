#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

// MorseMicro HALow Stack (wrapped in extern "C" for C++ compatibility)
extern "C" {
    #include "mmhal.h"
    #include "mmosal.h"
    #include "mmutils.h"
    #include "mmipal.h"
    #include "mmregdb.h"
}

static const char *TAG = "Device1_GPS";

// UART Configuration for GPS
#define GPS_UART_NUM UART_NUM_1
#define GPS_RX_PIN 18
#define GPS_TX_PIN 17
#define GPS_BAUD 9600
#define GPS_BUF_SIZE 256

// HALow WiFi Configuration
//#define HALOW_SSID "RUSC_HaLow_AP"
#define HALOW_SSID "MorseMicro"
//#define HALOW_PASSWORD "rusc2024"
#define HALOW_PASSWORD "12345678"

#define GATEWAY_IP "192.168.1.1"
#define GATEWAY_PORT 5001
#define DEVICE_ID "device_1_collector"
#define SEND_INTERVAL_MS 1000
#define DEVICE_STATIC_IP "192.168.1.2"

// HT-HC33 battery sense: VBAT -> 100K -> ADC_IN/GPIO1 -> 100K -> GND.
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_EMPTY_MV 3000
#define BATTERY_FULL_MV 4200

// HALow Channel Configuration (must match Device2 AP settings)
#define COUNTRY_CODE "US"           // Regulatory domain: US (Op Class 68)
#define TARGET_OP_CLASS 1           // US 915MHz band (Op Class 1) - MUST match gateway
#define TARGET_S1G_CHANNEL 3        // Channel 3 = 915.000 MHz (matches ap_mode example)

#define RUSC_TELEMETRY_MAGIC 0x5254  // "TR" little-endian on the wire
#define RUSC_TELEMETRY_VERSION 1
#define RUSC_DEVICE_INDEX 1

enum RuscHalowStatus : uint8_t {
    RUSC_HALOW_STATUS_STA_CONNECTED = 0x01,
    RUSC_HALOW_STATUS_LINK_READY = 0x02,
    RUSC_HALOW_STATUS_LAST_SEND_OK = 0x04,
    RUSC_HALOW_STATUS_LAST_SEND_ERROR = 0x08,
};

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

// GPS Data Structure
typedef struct {
    float latitude;
    float longitude;
    float altitude;
    float speed;
    int satellites;
    int quality;
} gps_data_t;

static gps_data_t g_gps_data = {0, 0, 0, 0, 0, 0};
static SemaphoreHandle_t g_gps_mutex = NULL;
static SemaphoreHandle_t g_wifi_connected = NULL;
static SemaphoreHandle_t g_link_up = NULL;
static adc_oneshot_unit_handle_t g_adc_handle = NULL;
static bool g_halow_sta_connected = false;
static bool g_halow_link_ready = false;
static bool g_last_send_ok = false;
static uint16_t g_packet_seq = 0;

static int32_t clamp_i32(int64_t value, int32_t min_value, int32_t max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return (int32_t)value;
}

static uint16_t clamp_u16(int value, uint16_t max_value) {
    if (value < 0) return 0;
    if (value > max_value) return max_value;
    return (uint16_t)value;
}

static uint8_t battery_percent_from_mv(uint16_t battery_mv) {
    if (battery_mv <= BATTERY_EMPTY_MV) return 0;
    if (battery_mv >= BATTERY_FULL_MV) return 100;
    return (uint8_t)(((battery_mv - BATTERY_EMPTY_MV) * 100) / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

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

static void init_battery_adc(void) {
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&init_config, &g_adc_handle) != ESP_OK) {
        ESP_LOGW(TAG, "Battery ADC init failed");
        g_adc_handle = NULL;
        return;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(g_adc_handle, BATTERY_ADC_CHANNEL, &channel_config) != ESP_OK) {
        ESP_LOGW(TAG, "Battery ADC channel config failed");
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

    // GPIO1 sees half of VBAT through the onboard 100K/100K divider.
    uint32_t adc_mv = ((uint32_t)raw * 3300U) / 4095U;
    uint32_t battery_mv = adc_mv * 2U;
    return battery_mv > UINT16_MAX ? UINT16_MAX : (uint16_t)battery_mv;
}

static uint8_t build_halow_status(void) {
    uint8_t status = 0;
    if (g_halow_sta_connected) status |= RUSC_HALOW_STATUS_STA_CONNECTED;
    if (g_halow_link_ready) status |= RUSC_HALOW_STATUS_LINK_READY;
    if (g_last_send_ok) {
        status |= RUSC_HALOW_STATUS_LAST_SEND_OK;
    } else {
        status |= RUSC_HALOW_STATUS_LAST_SEND_ERROR;
    }
    return status;
}

// ==================== GPS PARSING ====================

bool parse_gprmc(const char *sentence, gps_data_t *data) {
    if (strncmp(sentence, "$GPRMC", 6) != 0) return false;
    
    int commas[10] = {0};
    int comma_count = 0;
    
    for (int i = 0; sentence[i] != '\0' && comma_count < 10; i++) {
        if (sentence[i] == ',') {
            commas[comma_count++] = i;
        }
    }
    
    if (comma_count < 8) return false;
    
    // Check status (field 1: A=active, V=void)
    if (sentence[commas[1] + 1] != 'A') return false;
    
    // Parse latitude (field 2)
    char lat_str[16] = {0};
    int lat_len = commas[3] - commas[2] - 1;
    if (lat_len > 0 && lat_len < sizeof(lat_str)) {
        strncpy(lat_str, &sentence[commas[2] + 1], lat_len);
        
        if (lat_len > 5) {
            double lat_val = atof(lat_str);
            float lat_deg = (int)(lat_val / 100.0);  // Extract exactly first 2 digits as degrees
            float lat_min = fmod(lat_val, 100.0) / 60.0;
            float latitude = lat_deg + lat_min;
            if (sentence[commas[3] + 1] == 'S') latitude = -latitude;
            
            // Parse longitude (field 3)
            char lon_str[16] = {0};
            int lon_len = commas[5] - commas[4] - 1;
            if (lon_len > 0 && lon_len < sizeof(lon_str)) {
                strncpy(lon_str, &sentence[commas[4] + 1], lon_len);
                
                double lon_val = atof(lon_str);
                float lon_deg = (int)(lon_val / 100.0);  // Extract exactly first 2-3 digits as degrees
                float lon_min = fmod(lon_val, 100.0) / 60.0;
                float longitude = lon_deg + lon_min;
                if (sentence[commas[5] + 1] == 'W') longitude = -longitude;
                
                // Parse speed (field 4)
                char speed_str[16] = {0};
                int speed_len = commas[7] - commas[6] - 1;
                if (speed_len > 0 && speed_len < sizeof(speed_str)) {
                    strncpy(speed_str, &sentence[commas[6] + 1], speed_len);
                    
                    xSemaphoreTake(g_gps_mutex, portMAX_DELAY);
                    data->latitude = latitude;
                    data->longitude = longitude;
                    data->speed = atof(speed_str);
                    data->quality = 1;
                    xSemaphoreGive(g_gps_mutex);
                    
                    ESP_LOGD(TAG, "[GPS_PARSED] lat=%.6f lon=%.6f speed=%.2f", latitude, longitude, atof(speed_str));
                    
                    return true;
                }
            }
        }
    }
    
    return false;
}

bool parse_gpgga(const char *sentence, gps_data_t *data) {
    if (strncmp(sentence, "$GPGGA", 6) != 0) return false;
    
    int commas[15] = {0};
    int comma_count = 0;
    
    for (int i = 0; sentence[i] != '\0' && comma_count < 15; i++) {
        if (sentence[i] == ',') {
            commas[comma_count++] = i;
        }
    }
    
    if (comma_count < 9) return false;
    
    // Parse quality (field 5: 0=invalid, 1=GPS, 2=DGPS)
    int quality = sentence[commas[5] + 1] - '0';
    if (quality < 1) return false;
    
    // Parse satellite count (field 6)
    char sat_str[4] = {0};
    int sat_len = commas[7] - commas[6] - 1;
    if (sat_len > 0 && sat_len < sizeof(sat_str)) {
        strncpy(sat_str, &sentence[commas[6] + 1], sat_len);
        int satellites = atoi(sat_str);
        
        // Parse altitude (field 8)
        char alt_str[16] = {0};
        int alt_len = commas[9] - commas[8] - 1;
        if (alt_len > 0 && alt_len < sizeof(alt_str)) {
            strncpy(alt_str, &sentence[commas[8] + 1], alt_len);
            
            xSemaphoreTake(g_gps_mutex, portMAX_DELAY);
            data->altitude = atof(alt_str);
            data->satellites = satellites;
            xSemaphoreGive(g_gps_mutex);
            
            return true;
        }
    }
    
    return false;
}

// ==================== GPS UART TASK ====================

void gps_uart_task(void *arg) {
    // Allocate buffers statically to avoid stack pressure
    static uint8_t data[GPS_BUF_SIZE];
    static char line_buffer[256];
    int line_idx = 0;
    
    // Configure UART
    uart_config_t uart_config = {};
    uart_config.baud_rate = GPS_BAUD;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    
    uart_param_config(GPS_UART_NUM, &uart_config);
    uart_set_pin(GPS_UART_NUM, GPS_TX_PIN, GPS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(GPS_UART_NUM, GPS_BUF_SIZE, 0, 0, NULL, 0);
    
    ESP_LOGI(TAG, "GPS UART initialized on pins RX=%d TX=%d", GPS_RX_PIN, GPS_TX_PIN);
    
    while (1) {
        int len = uart_read_bytes(GPS_UART_NUM, data, GPS_BUF_SIZE, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            ESP_LOGD(TAG, "[UART_RX] Got %d bytes", len);
            for (int i = 0; i < len; i++) {
                char c = (char)data[i];
                
                if (c == '\n') {
                    if (line_idx > 0) {
                        line_buffer[line_idx] = '\0';
                        
                        // Log raw NMEA sentences for debugging coordinate issues
                        if (strncmp(line_buffer, "$GPRMC", 6) == 0 || strncmp(line_buffer, "$GPGGA", 6) == 0) {
                            ESP_LOGD(TAG, "[GPS_RAW] %s", line_buffer);
                        }
                        
                        // Try to parse GPRMC and GPGGA sentences
                        if (!parse_gprmc(line_buffer, &g_gps_data)) {
                            parse_gpgga(line_buffer, &g_gps_data);
                        }
                    }
                    line_idx = 0;
                } else if (c != '\r' && line_idx < sizeof(line_buffer) - 1) {
                    line_buffer[line_idx++] = c;
                }
            }
        }
    }
    
    vTaskDelete(NULL);
}

// ==================== HALow WIFI SCANNING ====================

/**
 * Scan for available HaLow access points
 * Returns the SSID of the first HaLow AP found, or NULL if none found
 */
static char scanned_ssid[33] = {0};  // Store scanned SSID (max 32 chars + null)
static char scanned_password[64] = {0};  // Store scanned password if needed

const char* scan_for_halow_ap(void) {
    ESP_LOGI(TAG, "Scanning for HaLow Access Points...");
    
    // Perform WiFi scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {.min = 100, .max = 300},
            .passive = 0
        }
    };
    
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);  // true = blocking
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return NULL;
    }
    
    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    if (ap_count == 0) {
        ESP_LOGI(TAG, "No HaLow APs found");
        return NULL;
    }
    
    ESP_LOGI(TAG, "Found %d networks. Looking for HaLow APs:", ap_count);
    
    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(ap_count * sizeof(wifi_ap_record_t));
    if (ap_list == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for AP list");
        return NULL;
    }
    
    // Get all scan records
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    
    // Look for HaLow APs (common SSIDs: RUSC_HaLow_AP, MorseMicroIoT, etc.)
    const char *halow_ssids[] = {
        "RUSC_HaLow_AP",      // Device 2 gateway
        "MorseMicroIoT",      // Default MorseMicro AP
        "HaLow",              // Generic HaLow network
        "S1G_AP",             // S1G access point
        NULL
    };
    
    for (int i = 0; i < ap_count; i++) {
        char ssid_str[33];
        // SSID is null-terminated, use strlen to get length
        uint8_t ssid_len = strnlen((const char *)ap_list[i].ssid, sizeof(ap_list[i].ssid));
        memcpy(ssid_str, ap_list[i].ssid, ssid_len);
        ssid_str[ssid_len] = '\0';
        
        // Log all found networks
        ESP_LOGI(TAG, "  [%d] SSID: %s | RSSI: %d | Channel: %d",
                 i + 1, ssid_str, ap_list[i].rssi, ap_list[i].primary);
        
        // Check if this is a known HaLow SSID
        for (int j = 0; halow_ssids[j] != NULL; j++) {
            if (strcmp(ssid_str, halow_ssids[j]) == 0) {
                // Found a HaLow AP - store its SSID
                strncpy(scanned_ssid, ssid_str, sizeof(scanned_ssid) - 1);
                scanned_ssid[sizeof(scanned_ssid) - 1] = 0;
                
                // Set password based on known AP
                if (strcmp(ssid_str, "RUSC_HaLow_AP") == 0) {
                    strncpy(scanned_password, "rusc2024", sizeof(scanned_password) - 1);
                } else if (strcmp(ssid_str, "MorseMicroIoT") == 0) {
                    strncpy(scanned_password, "12345678", sizeof(scanned_password) - 1);
                }
                
                ESP_LOGI(TAG, "✓ HaLow AP found: %s (RSSI: %d dBm)", scanned_ssid, ap_list[i].rssi);
                free(ap_list);
                return scanned_ssid;
            }
        }
    }
    
    // If no known SSID found, use the first one (strongest signal)
    if (ap_count > 0) {
        char default_ssid[33];
        uint8_t ssid_len = strnlen((const char *)ap_list[0].ssid, sizeof(ap_list[0].ssid));
        memcpy(default_ssid, ap_list[0].ssid, ssid_len);
        default_ssid[ssid_len] = '\0';
        
        strncpy(scanned_ssid, default_ssid, sizeof(scanned_ssid) - 1);
        scanned_ssid[sizeof(scanned_ssid) - 1] = 0;
        
        ESP_LOGI(TAG, "⚠ No known HaLow SSID found. Using first AP: %s", scanned_ssid);
        free(ap_list);
        return scanned_ssid;
    }
    
    free(ap_list);
    return NULL;
}

// ==================== HALow WIFI EVENT ====================

void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected to HALow AP, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xSemaphoreGive(g_wifi_connected);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected from HALow AP");
        esp_wifi_connect();
    }
}
// mmwlan link state callback - fires when radio layer link establishes
static void mmwlan_link_state_callback(enum mmwlan_link_state link_state, void *arg) {
    ESP_LOGI(TAG, "[CALLBACK] mmwlan_link_state_callback fired! link_state=%d (UP=%d)",
             link_state, MMWLAN_LINK_UP);
    if (link_state == MMWLAN_LINK_UP) {
        ESP_LOGI(TAG, "Link went Up");
        g_halow_sta_connected = true;
        xSemaphoreGive(g_link_up);
    } else {
        ESP_LOGW(TAG, "Link went DOWN (state=%d)", link_state);
        g_halow_sta_connected = false;
        g_halow_link_ready = false;
    }
}

// STA status callback - fires on STA connection state changes
static void sta_status_callback(mmwlan_sta_state state) {
    ESP_LOGI(TAG, "[CALLBACK] sta_status_callback fired! state=%d", state);
    if (state == MMWLAN_STA_CONNECTING) {
        ESP_LOGI(TAG, "STA state: CONNECTING (%d)", state);
    } else if (state == MMWLAN_STA_CONNECTED) {
        ESP_LOGI(TAG, "STA state: CONNECTED (%d) - SAE auth successful!", state);
        g_halow_sta_connected = true;
        xSemaphoreGive(g_wifi_connected);
    } else {
        ESP_LOGI(TAG, "STA state: %d", state);
        if (state != MMWLAN_STA_CONNECTING) {
            g_halow_sta_connected = false;
        }
    }
}

// Link state callback for mmipal (LWIP interface) - signals when data link is ready
static void mmipal_link_status_callback(const struct mmipal_link_status *link_status) {
    ESP_LOGI(TAG, "[CALLBACK] mmipal_link_status_callback fired! link_state=%d (UP=%d)",
             link_status->link_state, MMIPAL_LINK_UP);
    if (link_status->link_state == MMIPAL_LINK_UP) {
        ESP_LOGI(TAG, "✓ LWIP LINK IS UP - Ready to send UDP packets! IP: %s", link_status->ip_addr);
        g_halow_link_ready = true;
        xSemaphoreGive(g_link_up);
    } else {
        ESP_LOGI(TAG, "LWIP Link went DOWN");
        g_halow_link_ready = false;
    }
}

// ==================== MAIN APPLICATION ====================

extern "C" void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Create mutexes and semaphores
    g_gps_mutex = xSemaphoreCreateMutex();
    g_wifi_connected = xSemaphoreCreateBinary();
    g_link_up = xSemaphoreCreateBinary();
    init_battery_adc();
    
    ESP_LOGI(TAG, "=== RUSC Device 1: GPS Collector + HALow UDP ===");
    
    // ============ CRITICAL: MorseMicro Radio Boot Sequence ============
    ESP_LOGI(TAG, "Starting MorseMicro HALow radio initialization...");
    
    // 1. Power Management: Enable Vext (Heltec specific) - BEFORE mmhal_init
    gpio_set_direction((gpio_num_t)18, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)18, 0);  // Vext on
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 2. Initialize HAL and WLAN stacks (this sets up GPIO interrupts)
    mmhal_init();
    mmwlan_init();
    
    // Register mmwlan link callback BEFORE boot (like sta_connect.c does)
    enum mmwlan_status link_cb_status = mmwlan_register_link_state_cb(mmwlan_link_state_callback, NULL);
    if (link_cb_status != MMWLAN_SUCCESS) {
        ESP_LOGW(TAG, "Warning: mmwlan link callback registration returned %d", link_cb_status);
    }
    
    // 3. Set regulatory domain BEFORE boot
    ESP_LOGI(TAG, "Setting regulatory domain to %s...", COUNTRY_CODE);
    const struct mmwlan_s1g_channel_list *channel_list = 
        mmwlan_lookup_regulatory_domain(get_regulatory_db(), COUNTRY_CODE);
    if (channel_list != NULL) {
        mmwlan_set_channel_list(channel_list);
        ESP_LOGI(TAG, "Regulatory domain set to %s (Op Class %d, Channel %d)",
                 COUNTRY_CODE, TARGET_OP_CLASS, TARGET_S1G_CHANNEL);
    } else {
        ESP_LOGW(TAG, "Could not find regulatory domain for %s", COUNTRY_CODE);
    }
    
    // 4. CRITICAL: Boot the MorseMicro radio chip
    enum mmwlan_status status;
    struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
    
    ESP_LOGI(TAG, "Attempting mmwlan_boot...");
    status = mmwlan_boot(&boot_args);
    
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "!! RADIO BOOT FAILED !! Status: %d", status);
        ESP_LOGE(TAG, "Cannot proceed without functional radio. Halting.");
        while(1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "Radio boot successful!");
    
    // 5. Initialize network interface with static IP
    struct mmipal_init_args mmipal_init_args = MMIPAL_INIT_ARGS_DEFAULT;
    mmipal_init_args.mode = MMIPAL_STATIC;
    strncpy(mmipal_init_args.ip_addr, DEVICE_STATIC_IP, sizeof(mmipal_init_args.ip_addr) - 1);
    strncpy(mmipal_init_args.netmask, "255.255.255.0", sizeof(mmipal_init_args.netmask) - 1);
    strncpy(mmipal_init_args.gateway_addr, GATEWAY_IP, sizeof(mmipal_init_args.gateway_addr) - 1);
    
    if (mmipal_init(&mmipal_init_args) != MMIPAL_SUCCESS) {
        ESP_LOGE(TAG, "Failed to initialize network interface");
        while(1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // Register LWIP link status callback AFTER mmipal_init()
    // This callback signals when the LWIP interface is ready (replaces mmwlan link callbacks)
    mmipal_set_link_status_callback(mmipal_link_status_callback);
    
    ESP_LOGI(TAG, "Network interface initialized");
    
    // Configure HaLow STA connection using MorseMicro API
    ESP_LOGI(TAG, "Configuring HaLow STA connection for AP: %s", HALOW_SSID);
    
    // Prepare STA connection arguments - use memset to ensure ALL fields initialized
    struct mmwlan_sta_args sta_args;
    memset(&sta_args, 0, sizeof(sta_args));
    
    strncpy((char *)sta_args.ssid, HALOW_SSID, sizeof(sta_args.ssid) - 1);
    sta_args.ssid_len = strlen(HALOW_SSID);
    strncpy(sta_args.passphrase, HALOW_PASSWORD, sizeof(sta_args.passphrase) - 1);
    sta_args.passphrase_len = strlen(HALOW_PASSWORD);
    sta_args.security_type = MMWLAN_SAE;
    sta_args.pmf_mode = MMWLAN_PMF_REQUIRED;
    
    // Enable STA mode
    ESP_LOGI(TAG, "Attempting MorseMicro STA mode to connect to %s...", HALOW_SSID);
    ESP_LOGI(TAG, "[DEBUG] STA connection parameters:");
    ESP_LOGI(TAG, "  SSID: %s (len=%d)", sta_args.ssid, sta_args.ssid_len);
    ESP_LOGI(TAG, "  Security: %d (MMWLAN_SAE=%d)", sta_args.security_type, MMWLAN_SAE);
    ESP_LOGI(TAG, "  PMF: %d (REQUIRED=%d)", sta_args.pmf_mode, MMWLAN_PMF_REQUIRED);
    
    enum mmwlan_status sta_status = mmwlan_sta_enable(&sta_args, sta_status_callback);
    ESP_LOGI(TAG, "[DEBUG] mmwlan_sta_enable(with callback) returned: %d (SUCCESS=%d)", sta_status, MMWLAN_SUCCESS);
    
    if (sta_status != MMWLAN_SUCCESS) {
        ESP_LOGW(TAG, "STA enable returned status: %d (connection may be pending)", sta_status);
    } else {
        ESP_LOGI(TAG, "[DEBUG] STA enable SUCCESS - connection should be initiating");
    }
    
    // Disable power save to prevent ping loss/latency issues
    // MorseMicro community note: older SDK versions had issues with AP support for PS STAs,
    // causing packets to be buffered when device sleeps. Disabling PS keeps device always awake.
    enum mmwlan_status ps_status = mmwlan_set_power_save_mode(MMWLAN_PS_DISABLED);
    ESP_LOGI(TAG, "Power save disabled (status=%d) - device will stay awake for lower latency/no ping loss", ps_status);
    
    // Note: With mpipal STA mode, we need BOTH callbacks to succeed:
    // 1. sta_status_callback fires when SAE authentication completes (CONNECTED state)
    // 2. mpipal_link_status_callback fires when LWIP layer is ready
    
    // First wait for SAE authentication (STA connection state)
    ESP_LOGI(TAG, "Waiting for SAE authentication (STA CONNECTED state)...");
    if (xSemaphoreTake(g_wifi_connected, pdMS_TO_TICKS(10000))) {
        ESP_LOGI(TAG, "✓ SAE authentication successful");
    } else {
        ESP_LOGW(TAG, "SAE authentication timeout - connection not establishing");
    }
    
    // Then wait for LWIP layer to be ready
    ESP_LOGI(TAG, "Waiting for LWIP link to be ready...");
    if (xSemaphoreTake(g_link_up, pdMS_TO_TICKS(10000))) {
        ESP_LOGI(TAG, "✓ LWIP link is UP - ready to send data!");
    } else {
        ESP_LOGW(TAG, "LWIP link timeout - may not be able to transmit");
    }
    
    // Start GPS UART task (significantly increased stack size for mmipal callback safety)
    ESP_LOGI(TAG, "Starting GPS UART task with 16KB stack...");
    xTaskCreate(gps_uart_task, "gps_uart_task", 16384, NULL, 5, NULL);
    
    // Main UDP data sending loop
    int socket_fd = -1;
    struct sockaddr_in server_addr;
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Create UDP socket
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(GATEWAY_PORT);
    server_addr.sin_addr.s_addr = inet_addr(GATEWAY_IP);
    
    ESP_LOGI(TAG, "UDP socket created, ready to send to %s:%d", GATEWAY_IP, GATEWAY_PORT);
    
    while (1) {
        rusc_telemetry_packet_t packet = {};
        gps_data_t gps_snapshot;
        
        xSemaphoreTake(g_gps_mutex, portMAX_DELAY);
        gps_snapshot = g_gps_data;
        xSemaphoreGive(g_gps_mutex);

        uint16_t battery_mv = read_battery_mv();
        packet.magic = RUSC_TELEMETRY_MAGIC;
        packet.version = RUSC_TELEMETRY_VERSION;
        packet.device_index = RUSC_DEVICE_INDEX;
        packet.seq = g_packet_seq++;
        packet.lat_e7 = clamp_i32((int64_t)llround((double)gps_snapshot.latitude * 10000000.0), INT32_MIN, INT32_MAX);
        packet.lon_e7 = clamp_i32((int64_t)llround((double)gps_snapshot.longitude * 10000000.0), INT32_MIN, INT32_MAX);
        packet.alt_cm = clamp_i32((int64_t)llround((double)gps_snapshot.altitude * 100.0), INT32_MIN, INT32_MAX);
        packet.speed_centi_knots = clamp_u16((int)lroundf(gps_snapshot.speed * 100.0f), UINT16_MAX);
        packet.sats = (uint8_t)clamp_u16(gps_snapshot.satellites, UINT8_MAX);
        packet.quality = (uint8_t)clamp_u16(gps_snapshot.quality, UINT8_MAX);
        packet.battery_mv = battery_mv;
        packet.battery_pct = battery_percent_from_mv(battery_mv);
        packet.halow_status = build_halow_status();
        packet.crc16 = 0;
        packet.crc16 = crc16_ccitt((const uint8_t *)&packet, sizeof(packet));
        
        // Send UDP packet
        if (sendto(socket_fd, &packet, sizeof(packet), 0,
                   (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            g_last_send_ok = false;
            ESP_LOGW(TAG, "Failed to send UDP packet");
        } else {
            g_last_send_ok = true;
            ESP_LOGI(TAG, "Sent compact GPS: seq=%u lat=%.6f lon=%.6f bat=%umV/%u%% status=0x%02x",
                     packet.seq,
                     gps_snapshot.latitude,
                     gps_snapshot.longitude,
                     packet.battery_mv,
                     packet.battery_pct,
                     packet.halow_status);
        }
        
        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}
