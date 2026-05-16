#
# Copyright 2023 Morse Micro
#
# SPDX-License-Identifier: Apache-2.0
#
set(inc
    ".")
set(src
    "slip.c")

idf_component_register(INCLUDE_DIRS ${inc}
                       SRCS ${src})
