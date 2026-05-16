# Copyright 2025 Morse Micro
# SPDX-License-Identifier: Apache-2.0
set(src
    "src/ap_mode.c")

set(inc
    "src")

idf_component_register(SRCS ${src}
                       INCLUDE_DIRS ${inc}
                       PRIV_REQUIRES esp_driver_gpio morselib mm_shims mmipal mmiperf lwip mmregdb)
