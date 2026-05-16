set(src
    "src/sta_connect.c")

idf_component_register(SRCS ${src} PRIV_REQUIRES morselib mm_shims)
