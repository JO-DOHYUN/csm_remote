#pragma once

/*
 * Product application view of the pinned network configuration.
 *
 * The Arduino framework owns the complete PORTENTA_H7_M7 mbed_config.h.
 * Include it first so Arduino core sources and the replacement archive share
 * the same target/feature ABI, then override only the measured network
 * capacities compiled into the pinned replacement objects.
 */
#include <mbed_config.h>

#define CSM_PRODUCT_MBED_PROFILE 1

#undef MBED_ALL_STATS_ENABLED
#define MBED_ALL_STATS_ENABLED 0

#undef MBED_MEM_TRACING_ENABLED
#define MBED_MEM_TRACING_ENABLED 0

#undef MBED_CONF_MBED_TRACE_ENABLE
#define MBED_CONF_MBED_TRACE_ENABLE 0

#undef MBED_CONF_CELLULAR_DEBUG_AT
#define MBED_CONF_CELLULAR_DEBUG_AT 0

#undef MBED_CONF_LWIP_MEM_SIZE
#define MBED_CONF_LWIP_MEM_SIZE 40960

#undef MBED_CONF_LWIP_MEMP_NUM_TCP_SEG
#define MBED_CONF_LWIP_MEMP_NUM_TCP_SEG 40

#undef MBED_CONF_LWIP_TCP_MSS
#define MBED_CONF_LWIP_TCP_MSS 1460

#undef MBED_CONF_LWIP_TCP_SND_BUF
#define MBED_CONF_LWIP_TCP_SND_BUF (8 * TCP_MSS)

#undef MBED_CONF_LWIP_TCPIP_THREAD_STACKSIZE
#define MBED_CONF_LWIP_TCPIP_THREAD_STACKSIZE 4096

/*
 * Arduino's arm_hal_random.c supplies the randLIB platform entropy hooks.
 * The full Mbed CMake graph defines this Nanostack-only switch, but the
 * Arduino application graph must leave it clear so those hooks are emitted.
 */
#ifdef NS_USE_EXTERNAL_MBED_TLS
#undef NS_USE_EXTERNAL_MBED_TLS
#endif
