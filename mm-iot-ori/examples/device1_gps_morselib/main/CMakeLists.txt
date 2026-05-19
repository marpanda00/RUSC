# Copyright 2025 Morse Micro
# SPDX-License-Identifier: Apache-2.0
set(src
    "src/main.cpp")

set(inc
    "src")

idf_component_register(SRCS ${src}
                       INCLUDE_DIRS ${inc}
                       PRIV_REQUIRES esp_adc esp_driver_gpio esp_driver_i2c esp_driver_uart log esp_wifi esp_event esp_netif nvs_flash lwip morselib mm_shims mmipal mmiperf mmregdb)
