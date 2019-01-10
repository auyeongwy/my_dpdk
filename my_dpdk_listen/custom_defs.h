/** @file custom_defs.h
 *  Common definitions of values and structures used by different files.
 */

#ifndef _CUSTOM_DEFS_
#define _CUSTOM_DEFS_

#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_ethdev.h>
#include <rte_kni.h>
#include <rte_flow.h>
#include <rte_cycles.h>


#define MAX_BURST_LENGTH 32


struct app_port 
{
	int control; /**< Controls the thread. 1=run. 0=stop. */
	struct ether_addr mac_addr; /**< Mac address of the NIC card the thread runs on. */
	struct rte_mbuf *buf_frames[MAX_BURST_LENGTH]; /**< The memory buffer to hold packet frames. */
	rte_spinlock_t lock; /**< Spinlock indicator. */
	int port_active; /**< Port active indicator. */
	int port_dirty; /**< Port dirty indicator. */
	int port_id; /**< NIC port id. */
	unsigned core_id; /**< Slave core id. */
	struct rte_mempool *pkt_pool; /**< Memory pool from where the frame buffers are allocated. */
};


#endif
