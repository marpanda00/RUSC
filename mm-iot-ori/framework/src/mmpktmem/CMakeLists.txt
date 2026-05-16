# Copyright 2024 Morse Micro
# SPDX-License-Identifier: Apache-2.0
set(inc
    ".")
set(src
    "mmpktmem_heap.c")

idf_component_register(INCLUDE_DIRS ${inc}
                       SRCS ${src}
                       PRIV_REQUIRES mm_shims mmutils morselib)

add_compile_definitions(MMPKTMEM_TX_POOL_N_BLOCKS=CONFIG_MMPKTMEM_TX_POOL_N_BLOCKS)
add_compile_definitions(MMPKTMEM_RX_POOL_N_BLOCKS=CONFIG_MMPKTMEM_RX_POOL_N_BLOCKS)
