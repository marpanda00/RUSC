# Copyright 2024 Morse Micro
# SPDX-License-Identifier: Apache-2.0
set(src "mmosal_shim_freertos_esp32.c"
        "mmhal_core.c"
        "mmhal_os.c"
        "mmhal_wlan_binaries.c"
        "mmhal_wlan.c"
        "crypto_mbedtls_mm.c")

# Define architecture specific paths and variables
if(${IDF_TARGET} STREQUAL "esp32s3")
    set(inc "include/esp32s3")
    set(BFDNAME "elf32-xtensa-le")
    set(ARCH "xtensa")
elseif(${IDF_TARGET} STREQUAL "esp32c6")
    set(inc "include/esp32c6")
    set(BFDNAME "elf32-littleriscv")
    set(ARCH "riscv")
elseif(${IDF_TARGET} STREQUAL "esp32c3")
    set(inc "include/esp32c3")
    set(BFDNAME "elf32-littleriscv")
    set(ARCH "riscv")
elseif(${IDF_TARGET} STREQUAL "esp32p4")
    set(inc "include/esp32p4")
    set(BFDNAME "elf32-littleriscv")
    set(ARCH "riscv")
else()
    message(FATAL_ERROR "Target ${IDF_TARGET} not known. Exiting...")
endif()


# As of ESP-IDF v5.3 specific `esp_driver_xxx` components were added and the `driver` components was
# kept for compatibility. As of ESP-IDF v6.0 the legacy `driver` component will not contain public
# dependencies for most driver components
# https://github.com/espressif/esp-idf/commit/22a0f9aecb86e930f0904bdc0dbc8862510cd84a
if((IDF_VERSION_MAJOR GREATER 5) OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
    SET(ESP_DRIVER_COMPONENTS "esp_driver_gpio esp_driver_uart esp_driver_spi")
else()
    SET(ESP_DRIVER_COMPONENTS "driver")
endif()

idf_component_register(INCLUDE_DIRS ${inc}
                       SRCS ${src}
                       PRIV_REQUIRES morselib spi_flash app_update log mbedtls esp_timer
                                     ${ESP_DRIVER_COMPONENTS}
                       WHOLE_ARCHIVE)

# Kconfig variables are used to determine which bcf file to add to the final application. The file
# can be in one of two location.
#   - In `morsefirmware/` in the root of the application being built.
#   - In `framework/morsefirmware/` in the mm-iot-esp32 sdk.
set(APPLICATION_BLOB_DIR "${CMAKE_SOURCE_DIR}/morsefirmware")
set(MMIOT_BLOB_DIR "${CMAKE_CURRENT_LIST_DIR}/../morsefirmware")

function(find_binary_blob file_var config_var)
    # Clear the CMAKE cache to force it to search for the file every time.
    unset(${file_var} CACHE)

    set(_requested_name "${${config_var}}")
    set(_found_path "")

    get_filename_component(_requested_basename "${_requested_name}" NAME)
    if(NOT _requested_basename STREQUAL _requested_name)
        message(WARNING
            "${config_var} expects a filename only; ignoring path in '${_requested_name}' and \
            falling back to just '${_requested_basename}'.")
        set(_requested_name "${_requested_basename}")
    endif()

    # Search recursively under the application directory first, then the SDK directory.
    foreach(_root IN ITEMS "${APPLICATION_BLOB_DIR}" "${MMIOT_BLOB_DIR}")
        if(NOT _found_path AND EXISTS "${_root}")
            file(GLOB_RECURSE _all_candidates
                LIST_DIRECTORIES false
                CONFIGURE_DEPENDS
                "${_root}/*"
            )
            foreach(_candidate IN LISTS _all_candidates)
                get_filename_component(_candidate_name "${_candidate}" NAME)
                if(_candidate_name STREQUAL "${_requested_name}")
                    set(_found_path "${_candidate}")
                    break()
                endif()
            endforeach()
        endif()
    endforeach()

    if(NOT _found_path)
        message(WARNING
            "Invalid ${file_var} specified: ${${config_var}}\n"
            "File must be present in one of the following:\n"
            " - ${APPLICATION_BLOB_DIR}\n"
            " - ${MMIOT_BLOB_DIR}"
        )
    endif()

    # Cache the result so callers can access it
    set(${file_var} "${_found_path}" CACHE FILEPATH "" FORCE)

    message(STATUS "${file_var}: ${${file_var}}")
endfunction()

find_binary_blob(BCF_MBIN CONFIG_MM_BCF_FILE)
find_binary_blob(FW_MBIN CONFIG_MM_FW_FILE)

function(make_binary_object file prefix obj_var)
    get_filename_component(_file_name ${file} NAME)
    # Emit the generated object into the current component's binary dir.
    set(_object "${CMAKE_CURRENT_BINARY_DIR}/${_file_name}.o")
    string(MAKE_C_IDENTIFIER ${file} _file_c_identifier)
    add_custom_command(
        OUTPUT ${_object}
        COMMAND ${CMAKE_OBJCOPY} -I binary -O ${BFDNAME} -B ${ARCH} ${file} ${_object}
            --redefine-sym _binary_${_file_c_identifier}_start=${prefix}_start
            --redefine-sym _binary_${_file_c_identifier}_size=${prefix}_size
            --redefine-sym _binary_${_file_c_identifier}_end=${prefix}_end
            --rename-section .data=.rodata._fw_mbin,contents,alloc,load,readonly,data
        DEPENDS ${file}
        COMMENT "Running objcopy on ${file} to generate ${_object}"
    )

    # Populate the caller-specified output variable with the object path.
    set(${obj_var} "${_object}" PARENT_SCOPE)
endfunction()

make_binary_object(${BCF_MBIN} bcf_binary BCF_OBJ)
make_binary_object(${FW_MBIN} firmware_binary FW_OBJ)
add_custom_target(morsefirmware ALL DEPENDS ${FW_OBJ} ${BCF_OBJ})

add_dependencies(${COMPONENT_TARGET} morsefirmware)

target_link_libraries(${COMPONENT_TARGET} PRIVATE
    "${FW_OBJ}"
    "${BCF_OBJ}"
)
