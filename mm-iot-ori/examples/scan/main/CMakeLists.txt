set(src
    "src/scan.c")

idf_component_register(SRCS ${src} PRIV_REQUIRES morselib mm_shims mmregdb)
