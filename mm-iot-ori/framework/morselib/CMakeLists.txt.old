set(inc "include")

idf_component_register(INCLUDE_DIRS ${inc}
                       WHOLE_ARCHIVE)

if(NOT CONFIG_BUILD_SUPPLICANT_FROM_SOURCE)
    set(libmorse_path "lib/${IDF_TARGET}/libmorse_nocrypto.a")
else()
    set(libmorse_path "lib/${IDF_TARGET}/libmorse_nosupplicant.a")
endif()

if(NOT EXISTS ${libmorse_path})
    add_prebuilt_library(libmorse "${libmorse_path}"
                         PRIV_REQUIRES esp_rom mmpktmem)
else()
        message(FATAL_ERROR "ESP-IDF target ${IDF_TARGET} not supported, exiting.")
endif()

if(NOT CONFIG_BUILD_SUPPLICANT_FROM_SOURCE)
    target_link_libraries(${COMPONENT_LIB} INTERFACE libmorse)
else()

    set(PROTECTED_SYMS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../tools/metadata/protected_syms.txt")

    file(STRINGS "${PROTECTED_SYMS_FILE}" _raw_syms
         REGEX "^[^#].*")
    list(TRANSFORM _raw_syms STRIP)
    list(FILTER _raw_syms EXCLUDE REGEX "^$")

    set(protected_syms)
    foreach(sym IN LISTS _raw_syms)
        list(APPEND protected_syms -p "${sym}")
    endforeach()

    string(REGEX REPLACE "objcopy$" "" TOOLCHAIN_BASE "${CMAKE_OBJCOPY}")
    set(AR "${TOOLCHAIN_BASE}ar")
    set(RANLIB "${TOOLCHAIN_BASE}ranlib")
    set(LIBMORSE  "$<TARGET_FILE:libmorse>")
    set(LIBHOSTAP "$<TARGET_FILE:idf::hostap>")
    set(MANGLER "${CMAKE_CURRENT_SOURCE_DIR}/../tools/buildsystem/librarymangler.py")
    set(MERGESCRIPT "${CMAKE_CURRENT_BINARY_DIR}/libmorse.ar")
    set(LIBMORSEMANG "${CMAKE_CURRENT_BINARY_DIR}/libmorse.a")

    file(GENERATE
        OUTPUT "${MERGESCRIPT}"
        CONTENT
        "CREATE ${LIBMORSEMANG}
        ADDLIB ${LIBMORSE}
        ADDLIB ${LIBHOSTAP}
        SAVE
        END")

    add_custom_command(
        OUTPUT ${LIBMORSEMANG}
        COMMAND ${CMAKE_COMMAND} -E echo "Creating mangled libmorse"
        COMMAND ${CMAKE_COMMAND} -E cat ${MERGESCRIPT} | ${AR} -M
        COMMAND ${RANLIB} ${LIBMORSEMANG}
        COMMAND ${MANGLER} -t ${TOOLCHAIN_BASE} -m ${CMAKE_CURRENT_BINARY_DIR} ${protected_syms} ${LIBMORSEMANG}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS libmorse idf::hostap
    )

    add_custom_target(morselib_build ALL DEPENDS ${LIBMORSEMANG})
    add_library(libmorse_mang STATIC IMPORTED GLOBAL)
    set_target_properties(libmorse_mang PROPERTIES IMPORTED_LOCATION "${LIBMORSEMANG}")
    add_dependencies(libmorse_mang morselib_build)

    set_property(TARGET libmorse_mang PROPERTY INTERFACE_LINK_LIBRARIES idf::mmpktmem)
    target_link_libraries(${COMPONENT_LIB} INTERFACE libmorse_mang)
endif()
