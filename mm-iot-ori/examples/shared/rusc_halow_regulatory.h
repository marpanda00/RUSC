/*
 * Regulatory channel list helper for RUSC Device1/Device2.
 */
#pragma once

#include "mmwlan.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Channel list to pass to mmwlan_set_channel_list() before mmwlan_boot().
 * For EU/US locked profiles, returns a single channel with configured BW and max EIRP.
 */
const struct mmwlan_s1g_channel_list *rusc_halow_get_channel_list(void);

#ifdef __cplusplus
} // extern "C"
#endif
