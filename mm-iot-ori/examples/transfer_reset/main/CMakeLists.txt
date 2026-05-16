set(src
    "src/mm_app_loadconfig.c"
    "src/mm_app_common.c"
    "src/transfer_reset.c")

set(inc
    "src")

idf_component_register(SRCS ${src}
                       INCLUDE_DIRS ${inc}
                       PRIV_REQUIRES morselib mm_shims mmipal mmiperf lwip mmregdb)
