/** @file my_dpdk_hello.c
 *  Simple demonstration of running a thread on each core using DPDK.
 * 
 *  Usage:
 * 	./my_dpdk_hello -l 0-1
 **/

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>
//#include <inttypes.h>

#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_ethdev.h>


static int say_hello(void *p_arp)
{	
	printf("Hello from core %u\n", rte_lcore_id());
	return 0;
}


int main(int argc, char *argv[])
{
	unsigned i, ports;
	
	if(rte_eal_init(argc, argv) < 0) /* Init environment. */
		rte_panic("Cannot init EAL\n");

	printf("Number of cores %u\n", rte_lcore_count()); /* Get number of cores. */

	RTE_LCORE_FOREACH_SLAVE(i) /* Execute on each slave core. */
		rte_eal_remote_launch(say_hello, NULL, i);

	say_hello(NULL); /* Execute on master thread/core. */
	ports = rte_eth_dev_count_avail();
	printf("No. of ports: %u\n", ports);
	
	rte_eal_mp_wait_lcore(); /* Wait for all slave cores to stop their jobs. */
	return 0;
}
