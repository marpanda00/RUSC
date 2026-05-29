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
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "rusc_halow_config.h"
#include "rusc_halow_regulatory.h"

// MorseMicro HALow Stack (wrapped in extern "C" for C++ compatibility)
extern "C" {
    #include "mmhal.h"
    #include "mmosal.h"
    #include "mmutils.h"
    #include "mmipal.h"
    #include "mmregdb.h"
    #include "mmwlan.h"
}

static const char *TAG = "Device1_GPS";

// UART Configuration for GPS
#define GPS_UART_NUM UART_NUM_1
#define GPS_RX_PIN 18
#define GPS_TX_PIN 17
#define GPS_BAUD 9600
#define GPS_BUF_SIZE 256

// I2C compass configuration for HiLetgo GY-9250 / MPU9250.
#define COMPASS_I2C_PORT I2C_NUM_0
#define COMPASS_SDA_PIN 13
#define COMPASS_SCL_PIN 14
#define COMPASS_I2C_FREQ_HZ 100000
#define MPU9250_ADDR_LOW 0x68
#define MPU9250_ADDR_HIGH 0x69
#define MPU9250_REG_WHO_AM_I 0x75
#define MPU9250_REG_PWR_MGMT_1 0x6B
#define MPU9250_REG_GYRO_CONFIG 0x1B
#define MPU9250_REG_ACCEL_CONFIG 0x1C
#define MPU9250_REG_INT_PIN_CFG 0x37
#define MPU9250_REG_ACCEL_XOUT_H 0x3B
#define AK8963_ADDR 0x0C
#define AK8963_REG_WHO_AM_I 0x00
#define AK8963_REG_ST1 0x02
#define AK8963_REG_HXL 0x03
#define AK8963_REG_CNTL1 0x0A
#define AK8963_REG_ASAX 0x10
#define AK8963_MODE_POWER_DOWN 0x00
#define AK8963_MODE_FUSE_ROM 0x0F
#define AK8963_MODE_CONTINUOUS_100HZ_16BIT 0x16
#define COMPASS_INVALID_CDEG UINT16_MAX

#define DEVICE_ID "device_1_collector"
#define SEND_INTERVAL_MS 1000
#define HALOW_CONNECT_TIMEOUT_MS RUSC_HALOW_CONNECT_TIMEOUT_MS
#define HALOW_RECONNECT_INTERVAL_MS RUSC_HALOW_RECONNECT_INTERVAL_MS

// HT-HC33 battery sense: VBAT -> 100K -> ADC_IN/GPIO1 -> 100K -> GND.
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_EMPTY_MV 3000
#define BATTERY_FULL_MV 4200

#define RUSC_TELEMETRY_MAGIC 0x5254  // "TR" little-endian on the wire
#define RUSC_TELEMETRY_VERSION 4
#define RUSC_DEVICE_INDEX 1
#define RUSC_COMMAND_MAGIC 0x4343  // "CC" little-endian on the wire
#define RUSC_COMMAND_VERSION 1

enum RuscHalowStatus : uint8_t {
    RUSC_HALOW_STATUS_STA_CONNECTED = 0x01,
    RUSC_HALOW_STATUS_LINK_READY = 0x02,
    RUSC_HALOW_STATUS_LAST_SEND_OK = 0x04,
    RUSC_HALOW_STATUS_LAST_SEND_ERROR = 0x08,
};

enum RuscCalibrationCommand : uint8_t {
    RUSC_CAL_CMD_NONE = 0,
    RUSC_CAL_CMD_GYRO = 1,
    RUSC_CAL_CMD_SET_LEVEL = 2,
};

enum RuscCalibrationState : uint8_t {
    RUSC_CAL_STATE_IDLE = 0,
    RUSC_CAL_STATE_GYRO_RUNNING = 1,
    RUSC_CAL_STATE_GYRO_DONE = 2,
    RUSC_CAL_STATE_LEVEL_SET = 3,
    RUSC_CAL_STATE_ERROR = 255,
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
    uint16_t compass_cdeg;
    int16_t roll_cdeg;
    int16_t pitch_cdeg;
    int16_t yaw_rate_cdeg_s;
    uint8_t calibration_state;
    uint8_t calibration_progress;
    uint16_t last_command_id;
    uint8_t device_mac[6];
    uint8_t sats;
    uint8_t quality;
    uint16_t battery_mv;
    uint8_t battery_pct;
    uint8_t halow_status;
    uint16_t crc16;
} rusc_telemetry_packet_t;

typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t device_index;
    uint16_t command_id;
    uint8_t command;
    uint16_t duration_ms;
    uint16_t crc16;
} rusc_command_packet_t;
#pragma pack(pop)

static_assert(sizeof(rusc_telemetry_packet_t) == 46, "Unexpected telemetry packet size");
static_assert(sizeof(rusc_command_packet_t) == 11, "Unexpected command packet size");

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
static uint8_t g_mpu9250_addr = MPU9250_ADDR_LOW;
static i2c_master_bus_handle_t g_compass_i2c_bus = NULL;
static i2c_master_dev_handle_t g_mpu9250_i2c = NULL;
static i2c_master_dev_handle_t g_ak8963_i2c = NULL;
static float g_mag_adjust[3] = {1.0f, 1.0f, 1.0f};
static bool g_compass_ready = false;
static float g_gyro_bias_dps[3] = {0.0f, 0.0f, 0.0f};
static float g_level_roll_offset_deg = 0.0f;
static float g_level_pitch_offset_deg = 0.0f;
static volatile uint8_t g_calibration_state = RUSC_CAL_STATE_IDLE;
static volatile uint8_t g_calibration_progress = 0;
static uint16_t g_last_command_id = 0;
static uint8_t g_device_mac[6] = {0};

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

static int16_t clamp_i16(int value) {
    if (value < INT16_MIN) return INT16_MIN;
    if (value > INT16_MAX) return INT16_MAX;
    return (int16_t)value;
}

static i2c_master_dev_handle_t compass_device_handle(uint8_t address) {
    if (address == g_mpu9250_addr) {
        return g_mpu9250_i2c;
    }
    if (address == AK8963_ADDR) {
        return g_ak8963_i2c;
    }
    return NULL;
}

static esp_err_t i2c_write_byte(uint8_t address, uint8_t reg, uint8_t value) {
    uint8_t buffer[2] = {reg, value};
    i2c_master_dev_handle_t device = compass_device_handle(address);
    if (device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(device, buffer, sizeof(buffer), 100);
}

static esp_err_t i2c_read_bytes(uint8_t address, uint8_t reg, uint8_t *buffer, size_t len) {
    i2c_master_dev_handle_t device = compass_device_handle(address);
    if (device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(device, &reg, 1, buffer, len, 100);
}

static bool init_compass(void) {
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = COMPASS_I2C_PORT;
    bus_config.sda_io_num = (gpio_num_t)COMPASS_SDA_PIN;
    bus_config.scl_io_num = (gpio_num_t)COMPASS_SCL_PIN;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_config, &g_compass_i2c_bus);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Compass I2C bus init failed: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t detected_mpu_addr = 0;
    if (i2c_master_probe(g_compass_i2c_bus, MPU9250_ADDR_LOW, 100) == ESP_OK) {
        detected_mpu_addr = MPU9250_ADDR_LOW;
    } else if (i2c_master_probe(g_compass_i2c_bus, MPU9250_ADDR_HIGH, 100) == ESP_OK) {
        detected_mpu_addr = MPU9250_ADDR_HIGH;
    } else {
        ESP_LOGW(TAG, "MPU9250 not found on I2C pins SDA=%d SCL=%d", COMPASS_SDA_PIN, COMPASS_SCL_PIN);
        return false;
    }
    g_mpu9250_addr = detected_mpu_addr;

    i2c_device_config_t mpu_config = {};
    mpu_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    mpu_config.device_address = g_mpu9250_addr;
    mpu_config.scl_speed_hz = COMPASS_I2C_FREQ_HZ;
    if (i2c_master_bus_add_device(g_compass_i2c_bus, &mpu_config, &g_mpu9250_i2c) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add MPU9250 I2C device");
        return false;
    }

    uint8_t who_am_i = 0;
    i2c_read_bytes(g_mpu9250_addr, MPU9250_REG_WHO_AM_I, &who_am_i, 1);
    ESP_LOGI(TAG, "MPU9250 found at 0x%02x WHO_AM_I=0x%02x", g_mpu9250_addr, who_am_i);

    i2c_write_byte(g_mpu9250_addr, MPU9250_REG_PWR_MGMT_1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Use full-resolution defaults: accelerometer +/-2g, gyro +/-250 dps.
    i2c_write_byte(g_mpu9250_addr, MPU9250_REG_ACCEL_CONFIG, 0x00);
    i2c_write_byte(g_mpu9250_addr, MPU9250_REG_GYRO_CONFIG, 0x00);

    // Enable bypass so the ESP32-S3 can talk directly to the AK8963 magnetometer.
    i2c_write_byte(g_mpu9250_addr, MPU9250_REG_INT_PIN_CFG, 0x02);
    vTaskDelay(pdMS_TO_TICKS(10));

    if (i2c_master_probe(g_compass_i2c_bus, AK8963_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "AK8963 magnetometer not found through MPU9250 bypass");
        return false;
    }

    i2c_device_config_t ak_config = {};
    ak_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    ak_config.device_address = AK8963_ADDR;
    ak_config.scl_speed_hz = COMPASS_I2C_FREQ_HZ;
    if (i2c_master_bus_add_device(g_compass_i2c_bus, &ak_config, &g_ak8963_i2c) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add AK8963 I2C device");
        return false;
    }

    uint8_t ak_who_am_i = 0;
    if (i2c_read_bytes(AK8963_ADDR, AK8963_REG_WHO_AM_I, &ak_who_am_i, 1) != ESP_OK) {
        ESP_LOGW(TAG, "AK8963 magnetometer not found through MPU9250 bypass");
        return false;
    }

    i2c_write_byte(AK8963_ADDR, AK8963_REG_CNTL1, AK8963_MODE_POWER_DOWN);
    vTaskDelay(pdMS_TO_TICKS(10));
    i2c_write_byte(AK8963_ADDR, AK8963_REG_CNTL1, AK8963_MODE_FUSE_ROM);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t asa[3] = {128, 128, 128};
    if (i2c_read_bytes(AK8963_ADDR, AK8963_REG_ASAX, asa, sizeof(asa)) == ESP_OK) {
        for (int i = 0; i < 3; i++) {
            g_mag_adjust[i] = (((float)asa[i] - 128.0f) / 256.0f) + 1.0f;
        }
    }

    i2c_write_byte(AK8963_ADDR, AK8963_REG_CNTL1, AK8963_MODE_POWER_DOWN);
    vTaskDelay(pdMS_TO_TICKS(10));
    i2c_write_byte(AK8963_ADDR, AK8963_REG_CNTL1, AK8963_MODE_CONTINUOUS_100HZ_16BIT);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Compass ready: AK8963 WHO_AM_I=0x%02x ASA=%u/%u/%u", ak_who_am_i, asa[0], asa[1], asa[2]);
    return true;
}

static bool read_compass_heading(float *heading_deg) {
    if (!g_compass_ready || heading_deg == NULL) {
        return false;
    }

    uint8_t st1 = 0;
    if (i2c_read_bytes(AK8963_ADDR, AK8963_REG_ST1, &st1, 1) != ESP_OK || (st1 & 0x01) == 0) {
        return false;
    }

    uint8_t data[7] = {0};
    if (i2c_read_bytes(AK8963_ADDR, AK8963_REG_HXL, data, sizeof(data)) != ESP_OK) {
        return false;
    }

    if (data[6] & 0x08) {
        ESP_LOGW(TAG, "Compass magnetic sensor overflow");
        return false;
    }

    int16_t mag_x = (int16_t)((data[1] << 8) | data[0]);
    int16_t mag_y = (int16_t)((data[3] << 8) | data[2]);

    float adjusted_x = (float)mag_x * g_mag_adjust[0];
    float adjusted_y = (float)mag_y * g_mag_adjust[1];
    float heading = atan2f(adjusted_y, adjusted_x) * 180.0f / 3.14159265f;
    if (heading < 0.0f) {
        heading += 360.0f;
    }

    *heading_deg = heading;
    return true;
}

typedef struct {
    float accel_g[3];
    float gyro_dps[3];
    float roll_deg;
    float pitch_deg;
    float yaw_rate_dps;
} imu_motion_t;

static bool read_mpu_motion(imu_motion_t *motion) {
    if (!g_compass_ready || motion == NULL) {
        return false;
    }

    uint8_t raw[14] = {0};
    if (i2c_read_bytes(g_mpu9250_addr, MPU9250_REG_ACCEL_XOUT_H, raw, sizeof(raw)) != ESP_OK) {
        return false;
    }

    int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t ay = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t az = (int16_t)((raw[4] << 8) | raw[5]);
    int16_t gx = (int16_t)((raw[8] << 8) | raw[9]);
    int16_t gy = (int16_t)((raw[10] << 8) | raw[11]);
    int16_t gz = (int16_t)((raw[12] << 8) | raw[13]);

    motion->accel_g[0] = (float)ax / 16384.0f;
    motion->accel_g[1] = (float)ay / 16384.0f;
    motion->accel_g[2] = (float)az / 16384.0f;
    motion->gyro_dps[0] = ((float)gx / 131.0f) - g_gyro_bias_dps[0];
    motion->gyro_dps[1] = ((float)gy / 131.0f) - g_gyro_bias_dps[1];
    motion->gyro_dps[2] = ((float)gz / 131.0f) - g_gyro_bias_dps[2];

    float raw_roll = atan2f(motion->accel_g[1], motion->accel_g[2]) * 180.0f / 3.14159265f;
    float raw_pitch = atan2f(
        -motion->accel_g[0],
        sqrtf((motion->accel_g[1] * motion->accel_g[1]) + (motion->accel_g[2] * motion->accel_g[2]))
    ) * 180.0f / 3.14159265f;

    motion->roll_deg = raw_roll - g_level_roll_offset_deg;
    motion->pitch_deg = raw_pitch - g_level_pitch_offset_deg;
    motion->yaw_rate_dps = motion->gyro_dps[2];
    return true;
}

static bool calibrate_gyro_bias(uint16_t duration_ms) {
    const uint16_t actual_duration_ms = duration_ms < 1000 ? 5000 : duration_ms;
    const int sample_interval_ms = 20;
    const int sample_count = actual_duration_ms / sample_interval_ms;
    float gyro_sum[3] = {0.0f, 0.0f, 0.0f};
    int good_samples = 0;

    g_calibration_state = RUSC_CAL_STATE_GYRO_RUNNING;
    g_calibration_progress = 0;

    for (int i = 0; i < sample_count; i++) {
        uint8_t raw[14] = {0};
        if (i2c_read_bytes(g_mpu9250_addr, MPU9250_REG_ACCEL_XOUT_H, raw, sizeof(raw)) == ESP_OK) {
            int16_t gx = (int16_t)((raw[8] << 8) | raw[9]);
            int16_t gy = (int16_t)((raw[10] << 8) | raw[11]);
            int16_t gz = (int16_t)((raw[12] << 8) | raw[13]);
            gyro_sum[0] += (float)gx / 131.0f;
            gyro_sum[1] += (float)gy / 131.0f;
            gyro_sum[2] += (float)gz / 131.0f;
            good_samples++;
        }
        g_calibration_progress = (uint8_t)(((i + 1) * 100) / sample_count);
        vTaskDelay(pdMS_TO_TICKS(sample_interval_ms));
    }

    if (good_samples < sample_count / 2) {
        g_calibration_state = RUSC_CAL_STATE_ERROR;
        g_calibration_progress = 0;
        return false;
    }

    g_gyro_bias_dps[0] = gyro_sum[0] / good_samples;
    g_gyro_bias_dps[1] = gyro_sum[1] / good_samples;
    g_gyro_bias_dps[2] = gyro_sum[2] / good_samples;
    g_calibration_state = RUSC_CAL_STATE_GYRO_DONE;
    g_calibration_progress = 100;
    ESP_LOGI(TAG, "Gyro bias calibrated: x=%.3f y=%.3f z=%.3f dps",
             g_gyro_bias_dps[0], g_gyro_bias_dps[1], g_gyro_bias_dps[2]);
    return true;
}

static bool set_level_offsets(void) {
    imu_motion_t motion = {};
    g_calibration_state = RUSC_CAL_STATE_IDLE;
    g_calibration_progress = 0;

    if (!read_mpu_motion(&motion)) {
        g_calibration_state = RUSC_CAL_STATE_ERROR;
        return false;
    }

    g_level_roll_offset_deg += motion.roll_deg;
    g_level_pitch_offset_deg += motion.pitch_deg;
    g_calibration_state = RUSC_CAL_STATE_LEVEL_SET;
    g_calibration_progress = 100;
    ESP_LOGI(TAG, "Boat level set: roll_offset=%.2f pitch_offset=%.2f",
             g_level_roll_offset_deg, g_level_pitch_offset_deg);
    return true;
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

static bool process_command_packet(const uint8_t *data, int len) {
    if (len != sizeof(rusc_command_packet_t)) {
        return false;
    }

    rusc_command_packet_t command;
    memcpy(&command, data, sizeof(command));
    if (command.magic != RUSC_COMMAND_MAGIC || command.version != RUSC_COMMAND_VERSION ||
        command.device_index != RUSC_DEVICE_INDEX) {
        return false;
    }

    uint16_t received_crc = command.crc16;
    command.crc16 = 0;
    uint16_t calculated_crc = crc16_ccitt((const uint8_t *)&command, sizeof(command));
    if (received_crc != calculated_crc) {
        ESP_LOGW(TAG, "Command CRC mismatch: rx=0x%04x calc=0x%04x", received_crc, calculated_crc);
        return true;
    }

    g_last_command_id = command.command_id;
    ESP_LOGI(TAG, "Received calibration command id=%u command=%u duration=%u",
             command.command_id, command.command, command.duration_ms);

    switch (command.command) {
        case RUSC_CAL_CMD_GYRO:
            calibrate_gyro_bias(command.duration_ms);
            return true;
        case RUSC_CAL_CMD_SET_LEVEL:
            set_level_offsets();
            return true;
        default:
            ESP_LOGW(TAG, "Unsupported calibration command: %u", command.command);
            g_calibration_state = RUSC_CAL_STATE_ERROR;
            return true;
    }
}

static void receive_gateway_commands(int socket_fd) {
    uint8_t command_buffer[64];
    while (1) {
        int received = recvfrom(socket_fd, command_buffer, sizeof(command_buffer), MSG_DONTWAIT, NULL, NULL);
        if (received <= 0) {
            return;
        }
        if (!process_command_packet(command_buffer, received)) {
            ESP_LOGW(TAG, "Ignored unknown command packet length=%d", received);
        }
    }
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

static void sta_status_callback(mmwlan_sta_state state);

typedef struct {
    SemaphoreHandle_t done;
    bool found_target;
    uint8_t target_bssid[MMWLAN_MAC_ADDR_LEN];
    uint32_t target_freq_hz;
    int16_t target_rssi;
    int ap_count;
} halow_scan_ctx_t;

static halow_scan_ctx_t g_scan_ctx = {};

static void halow_log_bcf_metadata(void) {
    struct mmwlan_bcf_metadata metadata = {};
    if (mmwlan_get_bcf_metadata(&metadata) == MMWLAN_SUCCESS) {
        ESP_LOGI(TAG, "BCF: %s build=%s", metadata.board_desc, metadata.build_version);
    }
}

static void halow_apply_scan_config(void) {
    struct mmwlan_scan_config scan_config = MMWLAN_SCAN_CONFIG_INIT;
    scan_config.dwell_time_ms = RUSC_HALOW_SCAN_DWELL_MS;
    scan_config.home_channel_dwell_time_ms = RUSC_HALOW_SCAN_DWELL_MS;
    enum mmwlan_status status = mmwlan_set_scan_config(&scan_config);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGW(TAG, "mmwlan_set_scan_config returned %d", status);
    }
}

static void halow_scan_rx_cb(const struct mmwlan_scan_result *result, void *arg) {
    (void)arg;
    if (result == NULL || result->ssid_len == 0) {
        return;
    }

    char ssid[MMWLAN_SSID_MAXLEN + 1] = {0};
    size_t ssid_copy = result->ssid_len;
    if (ssid_copy > MMWLAN_SSID_MAXLEN) {
        ssid_copy = MMWLAN_SSID_MAXLEN;
    }
    memcpy(ssid, result->ssid, ssid_copy);
    g_scan_ctx.ap_count++;

    ESP_LOGI(TAG, "Scan: SSID=%s RSSI=%d freq=%lu Hz bw=%u MHz",
             ssid, result->rssi,
             (unsigned long)result->channel_freq_hz, result->bw_mhz);

    if (result->ssid_len == strlen(HALOW_SSID) &&
        memcmp(result->ssid, HALOW_SSID, result->ssid_len) == 0) {
        g_scan_ctx.found_target = true;
        g_scan_ctx.target_freq_hz = result->channel_freq_hz;
        g_scan_ctx.target_rssi = result->rssi;
        if (result->bssid != NULL) {
            memcpy(g_scan_ctx.target_bssid, result->bssid, MMWLAN_MAC_ADDR_LEN);
        }
        ESP_LOGI(TAG, "Target AP %s found at %lu Hz RSSI=%d",
                 HALOW_SSID, (unsigned long)result->channel_freq_hz, result->rssi);
    }
}

static void halow_scan_complete_cb(enum mmwlan_scan_state state, void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Scan finished state=%d APs=%d target_found=%d",
             state, g_scan_ctx.ap_count, g_scan_ctx.found_target);
    if (g_scan_ctx.done != NULL) {
        xSemaphoreGive(g_scan_ctx.done);
    }
}

static bool halow_run_scan_pass(bool directed) {
    g_scan_ctx.ap_count = 0;
    g_scan_ctx.done = xSemaphoreCreateBinary();
    if (g_scan_ctx.done == NULL) {
        ESP_LOGE(TAG, "Failed to allocate scan semaphore");
        return false;
    }

    mmwlan_sta_disable();
    vTaskDelay(pdMS_TO_TICKS(200));

    struct mmwlan_scan_req scan_req = MMWLAN_SCAN_REQ_INIT;
    scan_req.scan_rx_cb = halow_scan_rx_cb;
    scan_req.scan_complete_cb = halow_scan_complete_cb;
    scan_req.args.dwell_time_ms = RUSC_HALOW_SCAN_DWELL_MS;
    if (directed) {
        scan_req.args.ssid_len = strlen(HALOW_SSID);
        memcpy(scan_req.args.ssid, HALOW_SSID, scan_req.args.ssid_len);
    }

    ESP_LOGI(TAG, "Scan pass (%s, dwell=%d ms)...",
             directed ? HALOW_SSID : "all SSIDs", RUSC_HALOW_SCAN_DWELL_MS);
    enum mmwlan_status status = mmwlan_scan_request(&scan_req);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGW(TAG, "mmwlan_scan_request returned %d", status);
        vSemaphoreDelete(g_scan_ctx.done);
        g_scan_ctx.done = NULL;
        return false;
    }

    xSemaphoreTake(g_scan_ctx.done, pdMS_TO_TICKS(120000));
    vSemaphoreDelete(g_scan_ctx.done);
    g_scan_ctx.done = NULL;
    return true;
}

static bool halow_scan_for_gateway(void) {
    memset(&g_scan_ctx, 0, sizeof(g_scan_ctx));

    for (int pass = 1; pass <= RUSC_HALOW_SCAN_PASSES; pass++) {
        bool directed = (pass % 3) != 0;
        if (!halow_run_scan_pass(directed)) {
            return false;
        }
        ESP_LOGI(TAG, "Scan pass %d/%d: APs=%d target_found=%d",
                 pass, RUSC_HALOW_SCAN_PASSES, g_scan_ctx.ap_count, g_scan_ctx.found_target);
        if (g_scan_ctx.found_target) {
            return true;
        }
        if (pass < RUSC_HALOW_SCAN_PASSES) {
            vTaskDelay(pdMS_TO_TICKS(RUSC_HALOW_SCAN_PASS_INTERVAL_MS));
        }
    }

    ESP_LOGW(TAG, "AP %s not heard after %d passes (other APs seen: %d)",
             HALOW_SSID, RUSC_HALOW_SCAN_PASSES, g_scan_ctx.ap_count);
    return false;
}

static void halow_drain_connection_signals(void) {
    while (xSemaphoreTake(g_wifi_connected, 0) == pdTRUE) {
    }
    while (xSemaphoreTake(g_link_up, 0) == pdTRUE) {
    }
}

static void halow_fill_sta_args(struct mmwlan_sta_args *sta_args) {
    memset(sta_args, 0, sizeof(*sta_args));
    strncpy((char *)sta_args->ssid, HALOW_SSID, sizeof(sta_args->ssid) - 1);
    sta_args->ssid_len = strlen(HALOW_SSID);
    strncpy(sta_args->passphrase, HALOW_PASSWORD, sizeof(sta_args->passphrase) - 1);
    sta_args->passphrase_len = strlen(HALOW_PASSWORD);
    sta_args->security_type = MMWLAN_SAE;
    sta_args->pmf_mode = MMWLAN_PMF_REQUIRED;
}

static bool halow_wait_connected(uint32_t timeout_ms) {
    ESP_LOGI(TAG, "Waiting for HaLow link (timeout %lu ms)...", timeout_ms);
    TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    enum mmwlan_sta_state last_logged = MMWLAN_STA_DISABLED;

    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        enum mmwlan_sta_state sta_state = mmwlan_get_sta_state();

        if (g_halow_link_ready) {
            g_halow_sta_connected = true;
            ESP_LOGI(TAG, "HaLow link ready (mmipal UP)");
            return true;
        }

        if (sta_state != last_logged) {
            ESP_LOGI(TAG, "STA state: %d", sta_state);
            last_logged = sta_state;
        }

        if (sta_state == MMWLAN_STA_CONNECTED) {
            g_halow_sta_connected = true;
        }

        if (xSemaphoreTake(g_link_up, pdMS_TO_TICKS(1000)) == pdTRUE && g_halow_link_ready) {
            g_halow_sta_connected = true;
            ESP_LOGI(TAG, "HaLow link ready (mmipal UP)");
            return true;
        }

        if (xSemaphoreTake(g_wifi_connected, 0) == pdTRUE) {
            g_halow_sta_connected = true;
        }
    }

    ESP_LOGW(TAG, "HaLow connect timeout (sta=%d link_ready=%d sta_connected=%d)",
             mmwlan_get_sta_state(), g_halow_link_ready, g_halow_sta_connected);
    return false;
}

static bool halow_attempt_connect(void) {
    struct mmwlan_sta_args sta_args;

    g_halow_sta_connected = false;
    g_halow_link_ready = false;
    g_last_send_ok = false;
    halow_drain_connection_signals();

    halow_scan_for_gateway();

    halow_fill_sta_args(&sta_args);
    if (g_scan_ctx.found_target) {
        memcpy(sta_args.bssid, g_scan_ctx.target_bssid, MMWLAN_MAC_ADDR_LEN);
        ESP_LOGI(TAG, "Using BSSID from scan for %s", HALOW_SSID);
    }
    ESP_LOGI(TAG, "Connecting HaLow STA to %s...", HALOW_SSID);
    enum mmwlan_status sta_status = mmwlan_sta_enable(&sta_args, sta_status_callback);
    if (sta_status != MMWLAN_SUCCESS) {
        ESP_LOGW(TAG, "mmwlan_sta_enable returned %d", sta_status);
    }

    return halow_wait_connected(HALOW_CONNECT_TIMEOUT_MS);
}

static bool halow_reconnect(void) {
    ESP_LOGW(TAG, "HaLow reconnect: resetting STA...");
    mmwlan_sta_disable();
    vTaskDelay(pdMS_TO_TICKS(500));
    return halow_attempt_connect();
}

static bool halow_link_usable(void) {
    return g_halow_sta_connected && g_halow_link_ready;
}

static bool halow_connect_with_retry(void) {
    while (!halow_attempt_connect()) {
        mmwlan_sta_disable();
        ESP_LOGW(TAG, "HaLow connect failed, retrying in %d ms...", HALOW_RECONNECT_INTERVAL_MS);
        vTaskDelay(pdMS_TO_TICKS(HALOW_RECONNECT_INTERVAL_MS));
    }
    return true;
}

static void halow_wait_until_connected(void) {
    while (!halow_link_usable()) {
        if (halow_reconnect()) {
            return;
        }
        ESP_LOGW(TAG, "HaLow reconnect failed, retrying in %d ms...", HALOW_RECONNECT_INTERVAL_MS);
        vTaskDelay(pdMS_TO_TICKS(HALOW_RECONNECT_INTERVAL_MS));
    }
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
        g_last_send_ok = false;
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
        g_last_send_ok = false;
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
    g_compass_ready = init_compass();
    esp_read_mac(g_device_mac, ESP_MAC_WIFI_STA);
    
    ESP_LOGI(TAG, "=== RUSC Device 1: GPS + Compass Collector + HALow UDP ===");
    ESP_LOGI(TAG, "Device MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             g_device_mac[0], g_device_mac[1], g_device_mac[2],
             g_device_mac[3], g_device_mac[4], g_device_mac[5]);
    
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
    const struct mmwlan_s1g_channel_list *channel_list = rusc_halow_get_channel_list();
    if (channel_list != NULL) {
        mmwlan_set_channel_list(channel_list);
        halow_apply_scan_config();
#if RUSC_HALOW_USE_EU && RUSC_HALOW_EU_LOCK_SINGLE_CHANNEL
        ESP_LOGI(TAG, "Regulatory domain EU: op class %d, ch %d @ 863.5 MHz, %d MHz BW, %d dBm EIRP",
                 TARGET_OP_CLASS, TARGET_S1G_CHANNEL, RUSC_HALOW_BW_MHZ, RUSC_HALOW_MAX_TX_EIRP_DBM);
#elif !RUSC_HALOW_USE_EU && RUSC_HALOW_US_LOCK_915MHZ
        ESP_LOGI(TAG, "Regulatory domain %s: op class %d, ch %d (915.5 MHz only)",
                 COUNTRY_CODE, TARGET_OP_CLASS, TARGET_S1G_CHANNEL);
#else
        ESP_LOGI(TAG, "Regulatory domain %s: op class %d, ch %d",
                 COUNTRY_CODE, TARGET_OP_CLASS, TARGET_S1G_CHANNEL);
#endif
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
    halow_log_bcf_metadata();
    
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

    // Disable power save to prevent ping loss/latency issues
    enum mmwlan_status ps_status = mmwlan_set_power_save_mode(MMWLAN_PS_DISABLED);
    ESP_LOGI(TAG, "Power save disabled (status=%d)", ps_status);

    halow_connect_with_retry();
    
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
        if (!halow_link_usable()) {
            halow_wait_until_connected();
            continue;
        }

        rusc_telemetry_packet_t packet = {};
        gps_data_t gps_snapshot;
        float compass_heading = 0.0f;
        bool compass_valid = read_compass_heading(&compass_heading);
        imu_motion_t motion = {};
        bool motion_valid = read_mpu_motion(&motion);
        
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
        packet.compass_cdeg = compass_valid
            ? clamp_u16((int)lroundf(compass_heading * 100.0f), 35999)
            : COMPASS_INVALID_CDEG;
        packet.roll_cdeg = motion_valid ? clamp_i16((int)lroundf(motion.roll_deg * 100.0f)) : 0;
        packet.pitch_cdeg = motion_valid ? clamp_i16((int)lroundf(motion.pitch_deg * 100.0f)) : 0;
        packet.yaw_rate_cdeg_s = motion_valid ? clamp_i16((int)lroundf(motion.yaw_rate_dps * 100.0f)) : 0;
        packet.calibration_state = g_calibration_state;
        packet.calibration_progress = g_calibration_progress;
        packet.last_command_id = g_last_command_id;
        memcpy(packet.device_mac, g_device_mac, sizeof(packet.device_mac));
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
            g_halow_link_ready = false;
            g_halow_sta_connected = false;
            ESP_LOGW(TAG, "Failed to send UDP packet, reconnecting HaLow...");
            halow_wait_until_connected();
        } else {
            g_last_send_ok = true;
            ESP_LOGI(TAG, "Sent telemetry: seq=%u lat=%.6f lon=%.6f heading=%s%.2f roll=%.2f pitch=%.2f yaw_rate=%.2f cal=%u/%u bat=%umV/%u%% status=0x%02x",
                     packet.seq,
                     gps_snapshot.latitude,
                     gps_snapshot.longitude,
                     compass_valid ? "" : "invalid/",
                     compass_valid ? compass_heading : -1.0f,
                     motion_valid ? motion.roll_deg : 0.0f,
                     motion_valid ? motion.pitch_deg : 0.0f,
                     motion_valid ? motion.yaw_rate_dps : 0.0f,
                     packet.calibration_state,
                     packet.calibration_progress,
                     packet.battery_mv,
                     packet.battery_pct,
                     packet.halow_status);
        }

        receive_gateway_commands(socket_fd);
        
        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}
