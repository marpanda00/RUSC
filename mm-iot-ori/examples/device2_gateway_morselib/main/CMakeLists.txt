# Copyright 2025 Morse Micro
# SPDX-License-Identifier: Apache-2.0
set(src
    "src/main.cpp"
    "../../shared/rusc_halow_regulatory.c")

set(inc
    "src"
    "../../shared")

idf_component_register(SRCS ${src}
                       INCLUDE_DIRS ${inc}
                       REQUIRES bt esp_adc esp_driver_gpio esp_driver_uart log json esp_wifi esp_event esp_netif nvs_flash lwip morselib mm_shims mmipal mmiperf mmregdb)
