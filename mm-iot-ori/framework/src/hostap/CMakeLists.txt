#
# Copyright 2025 Morse Micro
#
# SPDX-License-Identifier: Apache-2.0
#

#
# Configuration options
#
# ************************************************************************
# *                             Warning                                  *
# ************************************************************************
# * Modifying the configuration below is not recommended. The MM IoT SDK *
# * WPA Supplicant driver has only been tested with the supplied         *
# * configuration. Modifying it may result in compilation failures or    *
# * undefined behaviour.                                                 *
# ************************************************************************
#

set(inc
    "."
    "src"
    "src/common"
    "src/utils"
    "wpa_supplicant")

set(src
    "drivers_morse.c"
    "os_mmosal.c"
    "src/common/dragonfly.c"
    "src/common/hw_features_common.c"
    "src/common/ieee802_11_common.c"
    "src/common/ptksa_cache.c"
    "src/common/sae.c"
    "src/common/wpa_common.c"
    "src/crypto/aes-unwrap.c"
    "src/crypto/dh_groups.c"
    "src/crypto/sha256-kdf.c"
    "src/crypto/sha384-kdf.c"
    "src/crypto/tls_none.c"
    "src/crypto/sha512-kdf.c"
    "src/drivers/driver_common.c"
    "src/eap_common/eap_common.c"
    "src/eap_peer/eap.c"
    "src/eap_peer/eap_methods.c"
    "src/eapol_supp/eapol_supp_sm.c"
    "src/rsn_supp/preauth.c"
    "src/rsn_supp/pmksa_cache.c"
    "src/rsn_supp/wpa.c"
    "src/rsn_supp/wpa_ie.c"
    "src/utils/crc32.c"
    "src/utils/bitfield.c"
    "src/utils/common.c"
    "src/utils/wpa_debug.c"
    "src/utils/wpabuf.c"
    "wpa_supplicant/bgscan.c"
    "wpa_supplicant/bgscan_simple.c"
    "wpa_supplicant/autoscan.c"
    "wpa_supplicant/autoscan_exponential.c"
    "wpa_supplicant/bss.c"
    "wpa_supplicant/config.c"
    "wpa_supplicant/eap_register.c"
    "wpa_supplicant/events.c"
    "wpa_supplicant/notify.c"
    "wpa_supplicant/op_classes.c"
    "wpa_supplicant/scan.c"
    "wpa_supplicant/sme.c"
    "wpa_supplicant/wmm_ac.c"
    "wpa_supplicant/wpa_supplicant.c"
    "wpa_supplicant/wpas_glue.c"
    "wpa_supplicant/bssid_ignore.c"
    "wpa_supplicant/wnm_sta.c")

set(src
    "${src}"
    "morse_stubs.c"
    "wpa_supplicant/ap.c"
    "src/ap/authsrv.c"
    "src/ap/bss_load.c"
    "src/ap/wmm.c"
    "src/ap/ap_list.c"
    "src/ap/comeback_token.c"
    "src/pasn/pasn_responder.c"
    "src/ap/hw_features.c"
    "src/ap/dfs.c"
    "src/ap/tkip_countermeasures.c"
    "src/ap/hostapd.c"
    "src/ap/ieee802_1x.c"
    "src/ap/ieee802_11_auth.c"
    "src/ap/ieee802_11_s1g.c"
    "src/ap/sta_info.c"
    "src/ap/ap_config.c"
    "src/ap/ap_drv_ops.c"
    "src/ap/ap_mlme.c"
    "src/ap/beacon.c"
    "src/ap/drv_callbacks.c"
    "src/ap/eap_user_db.c"
    "src/ap/ieee802_11.c"
    "src/ap/ieee802_11_ht.c"
    "src/ap/ieee802_11_shared.c"
    "src/ap/neighbor_db.c"
    "src/ap/pmksa_cache_auth.c"
    "src/ap/rrm.c"
    "src/ap/utils.c"
    "src/ap/wpa_auth.c"
    "src/ap/wpa_auth_glue.c"
    "src/ap/wpa_auth_ie.c"
    "src/eapol_auth/eapol_auth_sm.c"
    "src/eap_server/eap_server.c"
    "src/eap_server/eap_server_methods.c")

idf_component_register(INCLUDE_DIRS ${inc}
                       SRCS ${src})

# morselib's CMakeLists.txt will be responsible for triggering the compilation
# and mangling of hostap, as they need to be mangled together. Because mangling
# is implemented by way of add_custom_command, its output is of UTILITY type,
# and PRIV_REQUIRES here, on other components interface headers, would cause a
# circular dependency failure.
# Instead, explicitly include the interface includes as below.
target_include_directories(${COMPONENT_LIB} PRIVATE
  "$<TARGET_PROPERTY:idf::morselib,INTERFACE_INCLUDE_DIRECTORIES>"
  "$<TARGET_PROPERTY:idf::mm_shims,INTERFACE_INCLUDE_DIRECTORIES>"
)

add_compile_definitions(IEEE8021X_EAPOL=1)
add_compile_definitions(CONFIG_SME=1)
add_compile_definitions(CONFIG_SAE=1)
add_compile_definitions(CONFIG_OWE=1)
add_compile_definitions(CONFIG_ECC=1)
add_compile_definitions(CONFIG_SHA256=1)
add_compile_definitions(CONFIG_SHA512=1)
add_compile_definitions(CONFIG_SHA384=1)
add_compile_definitions(CONFIG_FIPS=1)
add_compile_definitions(CONFIG_NO_ACCOUNTING=1)
add_compile_definitions(CONFIG_NO_BSS_TRANS_MGMT=1)
add_compile_definitions(CONFIG_NO_CONFIG_BLOBS=1)
add_compile_definitions(CONFIG_NO_RADIUS=1)
add_compile_definitions(CONFIG_NO_RANDOM_POOL=1)
add_compile_definitions(CONFIG_NO_RC4=1)
add_compile_definitions(CONFIG_NO_ROBUST_AV=1)
add_compile_definitions(CONFIG_NO_RRM=1)
add_compile_definitions(CONFIG_NO_VLAN=1)
add_compile_definitions(CONFIG_WNM=1)
add_compile_definitions(CONFIG_IEEE80211AH=1)
add_compile_definitions(CONFIG_NO_CONFIG_WRITE=1)
add_compile_definitions(OS_NO_C_LIB_DEFINES=1)
add_compile_definitions(CONFIG_BGSCAN=1)
add_compile_definitions(CONFIG_BGSCAN_SIMPLE=1)
add_compile_definitions(CONFIG_AUTOSCAN=1)
add_compile_definitions(CONFIG_AUTOSCAN_EXPONENTIAL=1)
add_compile_definitions(CONFIG_S1G_TWT=1)
add_compile_definitions(MAX_NUM_MLD_LINKS=1)
add_compile_definitions(MAX_NUM_MLO_LINKS=1)
add_compile_definitions(WPA_SUPPLICANT_CLEANUP_INTERVAL=120)
add_compile_definitions(MM_IOT)

add_compile_definitions(CONFIG_OPENSSL_INTERNAL_AES_WRAP=1)

add_compile_definitions(CONFIG_AP=1)
add_compile_definitions(NEED_AP_MLME=1)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-c++-compat")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-unused-but-set-variable")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-unused-function")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-unused-parameter")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-unused-variable")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-maybe-uninitialized")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-dangling-else")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-type-limits")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-sign-compare")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-parentheses")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-packed-not-aligned")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-misleading-indentation")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-address")
