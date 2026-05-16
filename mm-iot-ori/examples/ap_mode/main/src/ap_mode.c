#include <string.h>
#include "mmhal.h"
#include "mmosal.h"
#include "mmutils.h"
#include "mmipal.h"
#include "mmregdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

/* --- 1. Configuration Constants --- */
#define COUNTRY_CODE "US"
#define STATIC_LOCAL_IP "192.168.1.1"
#define STATIC_GATEWAY  "192.168.1.1"
#define STATIC_NETMASK  "255.255.255.0"
#define AP_SSID         "MorseMicro"
#define SAE_PASSPHRASE  "12345678"
#define MAX_STAS        4

/* --- 2. Global Variables & Callbacks --- */
static uint32_t opaque_argument_value;

static void handle_ap_sta_status(const struct mmwlan_ap_sta_status *sta_status, void *arg)
{
    (void)sta_status;
    printf("STA status updated\n");
}

static void link_status_callback(const struct mmipal_link_status *link_status)
{
    if (link_status->link_state == MMIPAL_LINK_UP) {
        printf("Link is up. IP: %s\n", link_status->ip_addr);
    } else {
        printf("Link is down.\n");
    }
}

/* --- 3. Helper Functions --- */

void audit_morse_pins(void) {
    printf("\n--- MORSE HARDWARE PIN AUDIT ---\n");
    int pins[] = {2, 3, 4, 5, 6, 7, 8, 9, 18};
    const char* names[] = {"CS", "MOSI", "SCK", "MISO", "IRQ", "BUSY", "RESET", "WAKE", "VEXT"};

    for (int i = 0; i < 9; i++) {
        gpio_num_t pin = (gpio_num_t)pins[i];
        int level = gpio_get_level(pin);
        printf("GPIO [%02d] (%-5s) | Level: %d\n", pin, names[i], level);
    }
    printf("-----------------------------------\n\n");
}

void load_mmwlan_ap_args(struct mmwlan_ap_args *ap_args)
{
    strncpy((char *)ap_args->ssid, AP_SSID, sizeof(ap_args->ssid));
    ap_args->ssid_len = strlen((char *)ap_args->ssid);
    strncpy(ap_args->passphrase, SAE_PASSPHRASE, sizeof(ap_args->passphrase));
    ap_args->passphrase_len = strlen(ap_args->passphrase);
    ap_args->security_type = MMWLAN_SAE;
    ap_args->pmf_mode = MMWLAN_PMF_REQUIRED;
    ap_args->op_class = 1;
    ap_args->s1g_chan_num = 3;   // freq=915.000 MHz
    //ap_args->pri_bw_mhz = 2;      // 2MHz bandwidth
}

void app_print_version_info(void)
{
    struct mmwlan_version version = { 0 };
    if (mmwlan_get_version(&version) == MMWLAN_SUCCESS) {
        printf("Morse chip ID: 0x%04lx (%s)\n", version.morse_chip_id, version.morse_chip_id_string);
    }
}

/* --- 4. Main Application Entry --- */
void app_main(void)
{
    enum mmwlan_status status;
    struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
    struct mmwlan_ap_args ap_args = MMWLAN_AP_ARGS_INIT;
    /* Re-declaring the missing init args */
    struct mmipal_init_args mmipal_init_args = MMIPAL_INIT_ARGS_DEFAULT;

    printf("\n--- MANUAL HARDWARE RESET SEQUENCE ---\n");

    // 1. Vext Power ON (Heltec specific)
    gpio_set_direction((gpio_num_t)18, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)18, 0); 
    vTaskDelay(pdMS_TO_TICKS(100));

    // 2. FORCE MANUAL RESET TOGGLE
    // We are doing this manually because the chip is stuck in reset (Level 0)
    gpio_reset_pin((gpio_num_t)8);
    gpio_set_direction((gpio_num_t)8, GPIO_MODE_OUTPUT);
    
    // Hold LOW to clear state
    gpio_set_level((gpio_num_t)8, 0); 
    vTaskDelay(pdMS_TO_TICKS(50));

    // Pull HIGH and hold it with max drive strength
    gpio_set_level((gpio_num_t)8, 1); 
    gpio_set_drive_capability((gpio_num_t)8, GPIO_DRIVE_CAP_3);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 3. Init WLAN 
    mmhal_init();
    mmwlan_init();
    
    // 4. Audit to see if our manual HIGH level survived the Init call
    audit_morse_pins();

    printf("Attempting mmwlan_boot...\n");
    const struct mmwlan_s1g_channel_list *channel_list = mmwlan_lookup_regulatory_domain(get_regulatory_db(), COUNTRY_CODE);
    if (channel_list != NULL) {
        mmwlan_set_channel_list(channel_list);
    }

    status = mmwlan_boot(&boot_args);
    
    if (status != MMWLAN_SUCCESS) {
        printf("!! BOOT FAILED !!\n");
        // If Audit showed RESET as 0, something in the SDK is still hijacking GPIO 8
        while(1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 5. Network Interface Setup
    mmipal_init_args.mode = MMIPAL_STATIC;
    strncpy(mmipal_init_args.ip_addr, STATIC_LOCAL_IP, sizeof(mmipal_init_args.ip_addr));
    strncpy(mmipal_init_args.netmask, STATIC_NETMASK, sizeof(mmipal_init_args.netmask));
    strncpy(mmipal_init_args.gateway_addr, STATIC_GATEWAY, sizeof(mmipal_init_args.gateway_addr));

    if (mmipal_init(&mmipal_init_args) == MMIPAL_SUCCESS) {
        mmipal_set_link_status_callback(link_status_callback);
        load_mmwlan_ap_args(&ap_args);
        ap_args.sta_status_cb = handle_ap_sta_status;
        ap_args.sta_status_cb_arg = &opaque_argument_value;
        ap_args.max_stas = MAX_STAS;

        if (mmwlan_ap_enable(&ap_args) == MMWLAN_SUCCESS) {
            printf("\n--- AP ACTIVE: %s ---\n", AP_SSID);
        }
    }
}