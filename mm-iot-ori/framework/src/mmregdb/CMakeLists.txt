# Copyright 2025 Morse Micro
# SPDX-License-Identifier: Apache-2.0
set(inc
    ".")
set(src
    "mmregdb.c")

idf_component_register(INCLUDE_DIRS ${inc}
                       SRCS ${src}
                       PRIV_REQUIRES morselib mm_shims)