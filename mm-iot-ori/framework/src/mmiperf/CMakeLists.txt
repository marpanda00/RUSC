# Copyright 2024 Morse Micro
# SPDX-License-Identifier: Apache-2.0
set(inc
    "lwip"
    ".")
set(src
    "common/mmiperf_common.c"
    "common/mmiperf_data.c"
    "common/mmiperf_list.c"
    "lwip/mmiperf_tcp.c"
    "lwip/mmiperf_udp.c")

idf_component_register(INCLUDE_DIRS ${inc}
                       SRCS ${src}
                       PRIV_REQUIRES driver morselib mm_shims mmipal mmutils lwip)

add_compile_definitions(MMIPERF_STACK_SIZE=CONFIG_MMIPERF_STACK_SIZE)