# Copyright 2024 Morse Micro
# SPDX-License-Identifier: Apache-2.0
set(inc
    ".")
set(src
    "mmbuf.c"
    "mmcrc.c")

idf_component_register(INCLUDE_DIRS ${inc}
                       SRCS ${src}
                       PRIV_REQUIRES morselib mm_shims)
