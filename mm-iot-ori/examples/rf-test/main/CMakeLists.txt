set(src
    "src/rf-test.c"
    "src/mmhal_uart.c")

set(inc
    "src")

# As of ESP-IDF v5.3 specific `esp_driver_xxx` components were added and the `driver` components was
# kept for compatibility. As of ESP-IDF v6.0 the legacy `driver` component will not contain public
# dependencies for most driver components
# https://github.com/espressif/esp-idf/commit/22a0f9aecb86e930f0904bdc0dbc8862510cd84a
if((IDF_VERSION_MAJOR GREATER 5) OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
    SET(ESP_DRIVER_COMPONENTS "esp_driver_gpio esp_driver_uart")
else()
    SET(ESP_DRIVER_COMPONENTS "driver")
endif()

idf_component_register(INCLUDE_DIRS ${inc}
                       SRCS ${src}
                       PRIV_REQUIRES morselib mm_shims mmutils mmregdb ${ESP_DRIVER_COMPONENTS})
