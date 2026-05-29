/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "rusc_halow_regulatory.h"
#include "rusc_halow_config.h"
#include "mmregdb.h"
#include <stddef.h>
#include <string.h>

#if RUSC_HALOW_USE_EU && RUSC_HALOW_EU_LOCK_SINGLE_CHANNEL

/* EU op class 6, S1G channel 1 — 863.5 MHz centre, 1 MHz BW, 25 dBm EIRP (from mmregdb + project limit). */
static const struct mmwlan_s1g_channel rusc_eu_locked_channel = {
    863500000,
    280,
    false,
    66,
    RUSC_HALOW_OP_CLASS,
    RUSC_HALOW_S1G_CHANNEL,
    RUSC_HALOW_BW_MHZ,
    RUSC_HALOW_MAX_TX_EIRP_DBM,
    0,
    0,
    0
};

static struct mmwlan_s1g_channel_list rusc_eu_single_channel_list = {
    .country_code = { 'E', 'U', '\0' },
    .num_channels = 1,
    .channels = &rusc_eu_locked_channel,
};

#elif !RUSC_HALOW_USE_EU && RUSC_HALOW_US_LOCK_915MHZ

/* US op class 1, S1G channel 27 — 915.5 MHz centre, 1 MHz BW. */
static const struct mmwlan_s1g_channel rusc_us_915mhz_channel = {
    915500000,
    10000,
    false,
    68,
    RUSC_HALOW_OP_CLASS,
    RUSC_HALOW_S1G_CHANNEL,
    RUSC_HALOW_BW_MHZ,
    RUSC_HALOW_MAX_TX_EIRP_DBM,
    0,
    0,
    0
};

static struct mmwlan_s1g_channel_list rusc_us_915mhz_only_list = {
    .country_code = { 'U', 'S', '\0' },
    .num_channels = 1,
    .channels = &rusc_us_915mhz_channel,
};

#endif

const struct mmwlan_s1g_channel_list *rusc_halow_get_channel_list(void)
{
    const struct mmwlan_s1g_channel_list *full =
        mmwlan_lookup_regulatory_domain(get_regulatory_db(), RUSC_HALOW_COUNTRY_CODE);

#if RUSC_HALOW_USE_EU && RUSC_HALOW_EU_LOCK_SINGLE_CHANNEL
    if (full != NULL) {
        return &rusc_eu_single_channel_list;
    }
#elif !RUSC_HALOW_USE_EU && RUSC_HALOW_US_LOCK_915MHZ
    if (full != NULL) {
        return &rusc_us_915mhz_only_list;
    }
#endif

    return full;
}
