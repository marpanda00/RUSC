/*
 * Copyright 2022-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Transfer a specified amount of data and then reset the device under test (DUT).
 *
 * This application is to test a specific case where some amount of data is transferred and then the
 * DUT is reset. To transfer the data we will be using Iperf, this is not a perfect solution but is
 * an easy way to generate arbitrary data lengths.
 *
 * The Iperf parameters are specified using the defines in the file. Additional defines in
 * @c mm_app_loadconfig.c and @c mm_app_common.c are used to configure the network stack and WLAN
 * interface.
 *
 * @note It is assumed that you have followed the steps in the @ref GETTING_STARTED guide and are
 * therefore familiar with how to build, flash, and monitor an application using the MM-IoT-SDK
 * framework.
 *
 * The basic operation of this application is as follows.
 * 1. Connect to the AP and bring up a network interface.
 * 2. Start the specified Iperf type (client/server, TCP/UDP)
 *      - Note that the AP will need to run the complimentary side
 * 3. Once the transfer is complete the DUT will print a report and then trigger a reset using the
 *    @ref mmhal_reset() function.
 *
 * Below is an example command that you might execute on the AP which the DUT is configured as a TCP
 * server. It will print the attempt number, try to connect to a tcp server and send 2 Mbytes of
 * data, then go around the loop again.
 *
 * @code
 * attempt=0; while :; printf "\n#### Attempt $attempt ####\n"; do iperf -c 192.168.1.2 -i 1 -n 2M; let "attempt++"; sleep 15; done
 * @endcode
 *
 * Example output
 *
 * @code
 * #### Attempt 2327 ####
 * ------------------------------------------------------------
 * Client connecting to 192.168.1.2, TCP port 5001
 * TCP window size: 72.2 KByte (default)
 * ------------------------------------------------------------
 * [  1] local 192.168.1.1 port 45298 connected with 192.168.1.2 port 5001
 * [ ID] Interval       Transfer     Bandwidth
 * [  1] 0.00-1.00 sec   896 KBytes  7.34 Mbits/sec
 * [  1] 1.00-2.00 sec   243 KBytes  1.99 Mbits/sec
 * [  1] 2.00-3.00 sec   114 KBytes   934 Kbits/sec
 * [  1] 3.00-4.00 sec   285 KBytes  2.33 Mbits/sec
 * [  1] 4.00-5.00 sec   392 KBytes  3.21 Mbits/sec
 * [  1] 5.00-7.11 sec   118 KBytes   458 Kbits/sec
 * [  1] 0.00-7.11 sec  2.00 MBytes  2.36 Mbits/sec
 *
 * #### Attempt 2328 ####
 * ------------------------------------------------------------
 * Client connecting to 192.168.1.2, TCP port 5001
 * TCP window size: 72.2 KByte (default)
 * ------------------------------------------------------------
 * [  1] local 192.168.1.1 port 45300 connected with 192.168.1.2 port 5001
 * [ ID] Interval       Transfer     Bandwidth
 * [  1] 0.00-1.00 sec  1.13 MBytes  9.44 Mbits/sec
 * [  1] 1.00-2.00 sec   372 KBytes  3.05 Mbits/sec
 * [  1] 2.00-3.00 sec   186 KBytes  1.52 Mbits/sec
 * [  1] 3.00-4.00 sec   178 KBytes  1.46 Mbits/sec
 * [  1] 4.00-6.34 sec   160 KBytes   562 Kbits/sec
 * [  1] 0.00-6.34 sec  2.00 MBytes  2.65 Mbits/sec
 * @endcode
 */


#include <endian.h>
#include <string.h>
#include "mmhal_os.h"
#include "mmosal.h"
#include "mmwlan.h"
#include "mmipal.h"

#include "mmiperf.h"
#include "mm_app_common.h"

/* ------------------------ Configuration options ------------------------ */

/** Iperf configurations. */
enum iperf_type
{
    IPERF_TCP_SERVER,   /**< TCP server (RX) */
    IPERF_UDP_SERVER,   /**< UDP server (RX) */
    IPERF_TCP_CLIENT,   /**< TCP client (TX) */
    IPERF_UDP_CLIENT,   /**< UDP client (TX) */
};

#ifndef IPERF_TYPE
/** Type of iperf instance to start. */
#define IPERF_TYPE                      IPERF_TCP_SERVER
#endif

#ifndef IPERF_SERVER_IP
/** IP address of server to connect to when in client mode. */
#define IPERF_SERVER_IP                 "192.168.1.1"
#endif

#ifndef IPERF_TIME_AMOUNT
/**
 * Duration for client transfers specified either in seconds or bytes.
 * If this is negative, it specifies a time in seconds; if positive, it
 * specifies the number of bytes to transmit.
 */
#define IPERF_TIME_AMOUNT               2097000
#endif
#ifndef IPERF_SERVER_PORT
/** Specifies the port to listen on in server mode. */
#define IPERF_SERVER_PORT               5001
#endif

/* ------------------------ End of configuration options ------------------------ */

/** Array of power of 10 unit specifiers. */
static const char units[] = {' ', 'K', 'M', 'G', 'T'};

/**
 * Function to format a given number of bytes into an appropriate SI base. I.e if you give it 1400
 * it will return 1 with unit_index set to 1 for Kilo.
 *
 * @warning This uses power of 10 units (kilo, mega, giga, etc). Not to be confused with power of 2
 *          units (kibi, mebi, gibi, etc).
 *
 * @param[in]   bytes       Original number of bytes
 * @param[out]  unit_index  Index into the @ref units array. Must not be NULL
 *
 * @return Number of bytes formatted to the appropriate unit given by the unit index.
 */
static uint32_t format_bytes(uint64_t bytes, uint8_t *unit_index)
{
    MMOSAL_ASSERT(unit_index != NULL);
    *unit_index = 0;

    while (bytes >= 1000 && *unit_index < 4)
    {
        bytes /= 1000;
        (*unit_index)++;
    }

    return bytes;
}
/**
 * Handle a report at the end of an iperf transfer.
 *
 * @param report    The iperf report.
 * @param arg       Opaque argument specified when iperf was started.
 * @param handle    The iperf instance handle returned when iperf was started.
 */
static void iperf_report_handler(const struct mmiperf_report *report, void *arg,
                                 mmiperf_handle_t handle)
{
    (void)handle;
    struct mmosal_semb *iperf_complete = (struct mmosal_semb *)arg;

    uint8_t bytes_transferred_unit_index = 0;
    uint32_t bytes_transferred_formatted = format_bytes(report->bytes_transferred,
                                                        &bytes_transferred_unit_index);

    printf("\nIperf Report\n");
    printf("  Remote Address: %s:%d\n", report->remote_addr, report->remote_port);
    printf("  Local Address:  %s:%d\n", report->local_addr, report->local_port);
    printf("  Transferred: %lu %cBytes, duration: %lu ms, bandwidth: %lu kbps\n",
           bytes_transferred_formatted, units[bytes_transferred_unit_index],
           report->duration_ms, report->bandwidth_kbitpsec);
    printf("\n");

    mmosal_semb_give(iperf_complete);
}

/** Start iperf as a TCP client. */
static void start_tcp_client(void *report_arg)
{
    uint32_t server_port = IPERF_SERVER_PORT;
    struct mmiperf_client_args args = MMIPERF_CLIENT_ARGS_DEFAULT;

    /* Get the Server IP */
    strncpy(args.server_addr, IPERF_SERVER_IP, sizeof(args.server_addr));


    MMOSAL_ASSERT(server_port <= UINT16_MAX);
    args.server_port = server_port;

    int amount = IPERF_TIME_AMOUNT;
    args.amount = amount;
    if (args.amount < 0)
    {
        args.amount *= 100;
    }
    args.report_fn = iperf_report_handler;
    args.report_arg = report_arg;

    mmiperf_start_tcp_client(&args);
    printf("\nIperf TCP client started, waiting for completion...\n");
}

/** Start iperf as a TCP server. */
static void start_tcp_server(void *report_arg)
{
    struct mmiperf_server_args args = MMIPERF_SERVER_ARGS_DEFAULT;

    uint32_t local_port = IPERF_SERVER_PORT;
    args.local_port = (uint16_t) local_port;

    args.report_fn = iperf_report_handler;
    args.report_arg = report_arg;

    mmiperf_handle_t iperf_handle = mmiperf_start_tcp_server(&args);
    if (iperf_handle == NULL)
    {
        printf("Failed to get local address\n");
        return;
    }
    printf("\nIperf TCP server started, waiting for client to connect...\n");
    struct mmipal_ip_config ip_config;
    enum mmipal_status status;
    status = mmipal_get_ip_config(&ip_config);
    if (status == MMIPAL_SUCCESS)
    {
        printf("Execute cmd on AP 'iperf -c %s -p %u -i 1' for IPv4\n",
               ip_config.ip_addr, args.local_port);
    }

    struct mmipal_ip6_config ip6_config;
    status = mmipal_get_ip6_config(&ip6_config);
    if (status == MMIPAL_SUCCESS)
    {
        printf("Execute cmd on AP 'iperf -c %s%%wlan0 -p %u -i 1 -V' for IPv6\n",
               ip6_config.ip6_addr[0], args.local_port);
    }
}

/** Start iperf as a UDP client. */
static void start_udp_client(void *report_arg)
{
    uint32_t server_port = IPERF_SERVER_PORT;
    struct mmiperf_client_args args = MMIPERF_CLIENT_ARGS_DEFAULT;

    strncpy(args.server_addr, IPERF_SERVER_IP, sizeof(args.server_addr));

    MMOSAL_ASSERT(server_port <= UINT16_MAX);
    args.server_port = server_port;

    int amount = IPERF_TIME_AMOUNT;
    args.amount = amount;
    if (args.amount < 0)
    {
        args.amount *= 100;
    }
    args.report_fn = iperf_report_handler;
    args.report_arg = report_arg;

    mmiperf_start_udp_client(&args);
    printf("\nIperf UDP client started, waiting for completion...\n");
}

/** Start iperf as a UDP server. */
static void start_udp_server(void *report_arg)
{
    struct mmiperf_server_args args = MMIPERF_SERVER_ARGS_DEFAULT;

    uint32_t local_port = IPERF_SERVER_PORT;
    args.local_port = (uint16_t) local_port;

    args.report_fn = iperf_report_handler;
    args.report_arg = report_arg;

    mmiperf_handle_t iperf_handle = mmiperf_start_udp_server(&args);
    if (iperf_handle == NULL)
    {
        printf("Failed to start iperf server\n");
        return;
    }

    printf("\nIperf UDP server started, waiting for client to connect...\n");
    struct mmipal_ip_config ip_config;
    enum mmipal_status status;
    status = mmipal_get_ip_config(&ip_config);
    if (status == MMIPAL_SUCCESS)
    {
        printf("Execute cmd on AP 'iperf -c %s -p %u -i 1 -u -b 20M' for IPv4\n",
               ip_config.ip_addr, args.local_port);
    }

    struct mmipal_ip6_config ip6_config;
    status = mmipal_get_ip6_config(&ip6_config);
    if (status == MMIPAL_SUCCESS)
    {
        printf("Execute cmd on AP 'iperf -c %s%%wlan0 -p %u -i 1 -V -u -b 20M' for IPv6\n",
               ip6_config.ip6_addr[0], args.local_port);
    }
}

/**
 * Main entry point to the application. This will be invoked in a thread once operating system
 * and hardware initialization has completed. It may return, but it does not have to.
 */
void app_main(void)
{
    printf("\n\nMorse Iperf Demo (Built " __DATE__ " " __TIME__ ")\n\n");

    /* Initialize and connect to Wi-Fi, blocks till connected */
    app_wlan_init();
    app_wlan_start();

    struct mmosal_semb *iperf_complete = mmosal_semb_create("iperf_complete");;

    printf("(%lu) Start\n", mmosal_get_time_ms());

    enum iperf_type iperf_mode = IPERF_TYPE;

    switch (iperf_mode)
    {
    case IPERF_TCP_SERVER:
        start_tcp_server((void *)iperf_complete);
        break;

    case IPERF_UDP_SERVER:
        start_udp_server((void *)iperf_complete);
        break;

    case IPERF_UDP_CLIENT:
        start_udp_client((void *)iperf_complete);
        break;

    case IPERF_TCP_CLIENT:
        start_tcp_client((void *)iperf_complete);
        break;
    }

    mmosal_semb_wait(iperf_complete, UINT32_MAX);

    printf("(%lu) Reset DUT\n", mmosal_get_time_ms());
    mmhal_reset();
}
