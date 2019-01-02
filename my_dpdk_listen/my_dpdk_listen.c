/** @file my_dpdk_listen.c
 * 	Demonstrates intilizing a port with DPDK, launching a listening thread on it.
 * 
 *  Usage:
 *  ./my_dpdk_listen -l 0-1
 **/


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>
#include <signal.h>


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


#define MBUF_POOL_SIZE 2048
#define MBUF_POOL_CACHE 32
#define NUM_RXTX_D 512
#define PORT_RX_QUEUE_SIZE 1024
#define PORT_TX_QUEUE_SIZE 1024
#define PKTPOOL_EXTRA_SIZE 512
#define PKTPOOL_CACHE 32
#define MAX_BURST_LENGTH 32


/*struct txq_port 
{
	uint16_t cnt_unsent;
	struct rte_mbuf *buf_frames[MAX_BURST_LENGTH];
};*/


/** Structure that defines each thread on a lcore.
 * 
 */
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


/*static struct rte_eth_conf port_conf = {
	.rxmode = {
		.ignore_offload_bitfield = 1,
		.offloads = DEV_RX_OFFLOAD_CRC_STRIP,
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};*/



static struct app_port *v_app_ports = NULL; /**< Array of struct app_port, one object per NIC port. */
static int v_num_ports = 0; /**< Total number of NIC ports. */



/** Implements interrupt handler to catch Ctrl-C.
 *  @param p_option The caught signal.
 */
static void handle_interrupt(int p_option)
{
	int i;
	
	for(i=0; i<v_num_ports; i++) {
		printf("Caught interrupt. Sending stop signal to lcore %d\n", i);
		v_app_ports[i].control = 0;
	}
}



/** Frees allocated  memory prior to exit.
 */
static void do_cleanup()
{
	if(v_app_ports != NULL)
		free(v_app_ports);
}



static void check_link_status(const uint16_t p_port_id)
{
	struct rte_eth_link link;
	uint8_t rep_cnt = 9;

	memset(&link, 0, sizeof(link));
	do { /* Check link status for maximum 9 seconds */
		rte_eth_link_get(p_port_id, &link);
		if (link.link_status == ETH_LINK_UP)
			break;
		rte_delay_ms(1000);
	} while (--rep_cnt);

	if (link.link_status == ETH_LINK_DOWN)
		rte_exit(EXIT_FAILURE, "%u link is still down\n", p_port_id);
}



/** Sets up a NIC port.
 *  @param p_port_id ID of the NIC port.
 *  @param p_core_id Core Id of the CPU core that will control this NIC port.
 *  @param p_app_port struct app_port that contains information for running this NIC port.
 */
static void setup_port(const unsigned p_port_id, const unsigned p_core_id, struct app_port *__restrict__ p_app_port)
{
	int size_pktpool;
	struct rte_eth_conf cfg_port;
	struct rte_eth_dev_info dev_info;
	char str_name[16];
	uint16_t nb_rxd = PORT_RX_QUEUE_SIZE;
	uint16_t nb_txd = PORT_TX_QUEUE_SIZE;
	struct rte_eth_txconf txconf;


	memset(&cfg_port, 0, sizeof(cfg_port));
	cfg_port.txmode.mq_mode = ETH_MQ_TX_NONE;
	cfg_port.rxmode.ignore_offload_bitfield = 1;
	cfg_port.rxmode.max_rx_pkt_len = ETHER_MAX_LEN;
	rte_eth_dev_info_get(p_port_id, &dev_info);
	if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
		cfg_port.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;	
	
	
	size_pktpool = dev_info.rx_desc_lim.nb_max + dev_info.tx_desc_lim.nb_max + PKTPOOL_EXTRA_SIZE;
	snprintf(str_name, 16, "pkt_pool%i", p_port_id);
	
	
	p_app_port->pkt_pool = rte_pktmbuf_pool_create(str_name, size_pktpool, PKTPOOL_CACHE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (p_app_port->pkt_pool == NULL)
		rte_exit(EXIT_FAILURE, "rte_pktmbuf_pool_create failed");
	else
		printf("Port %u Pkt pool %s poolsize: %d\n", p_port_id, str_name, size_pktpool);
		
	p_app_port->port_active = 1;
	p_app_port->port_dirty = 0;
	p_app_port->port_id = p_port_id;
	p_app_port->core_id = p_core_id;
	
	
	if (rte_eth_dev_configure(p_port_id, 1, 1, &cfg_port) < 0)
		rte_exit(EXIT_FAILURE, "rte_eth_dev_configure failed");
	if (rte_eth_dev_adjust_nb_rx_tx_desc(p_port_id, &nb_rxd, &nb_txd) < 0)
		rte_exit(EXIT_FAILURE, "rte_eth_dev_adjust_nb_rx_tx_desc failed");	
	else
		printf("RX size: %u TX size: %u\n", nb_rxd, nb_txd);
	if (rte_eth_rx_queue_setup(p_port_id, 0, nb_rxd, rte_eth_dev_socket_id(p_port_id), NULL, p_app_port->pkt_pool) < 0)
		rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup failed");
	txconf = dev_info.default_txconf;
	txconf.txq_flags = ETH_TXQ_FLAGS_IGNORE;
	if (rte_eth_tx_queue_setup(p_port_id, 0, nb_txd, rte_eth_dev_socket_id(p_port_id), &txconf) < 0)
		rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup failed");

	rte_eth_promiscuous_enable(p_port_id);
	if (rte_eth_dev_start(p_port_id) < 0)
		rte_exit(EXIT_FAILURE, "%s:%i: rte_eth_dev_start failed", __FILE__, __LINE__);
	rte_eth_macaddr_get(p_port_id, &(p_app_port->mac_addr));
	rte_spinlock_init(&(p_app_port->lock));		
	
	check_link_status(p_port_id);
}



/** Start listening on a NIC port for traffic.
 *  @param ptr_data Pointer to struct app_port which contains al required information to start a job on an lcore listening to a NIC port.
 *  @return Always returns 0.
 */
static int do_listen(__attribute__((unused)) void *ptr_data)
{
	struct app_port *ptr_port = (struct app_port*)ptr_data;
	struct rte_mbuf **ptr_frame = (struct rte_mbuf**)ptr_port->buf_frames;
	int *control = &(ptr_port->control);


	const uint16_t port = ptr_port->port_id;
	uint16_t cnt_recv_frames;
	uint16_t cnt_total_recv_frames = 0;
	uint16_t i;


	printf("Launching listen to port %u on lcore %u\n", port, ptr_port->core_id);
	while(*control == 1) {	
		/* Incoming frames */
		cnt_recv_frames = rte_eth_rx_burst(port, 0, ptr_frame, MAX_BURST_LENGTH);
		if(cnt_recv_frames > 0) {
			cnt_total_recv_frames += cnt_recv_frames;
			printf("Received frames: %u\n", cnt_total_recv_frames);
			
			for(i=0; i<cnt_recv_frames; i++)
				rte_pktmbuf_free(ptr_frame[i]);
		}
	}
	

	return 0;
}



int main(int argc, char *argv[])
{
	unsigned num_core, portid, i;
	int lcore = -1; /* Start at -1. */
	struct rte_flow_error error;


	signal(SIGINT, handle_interrupt); /* Register signal handler. */
	
	
	if(rte_eal_init(argc, argv) < 0) /* Init environment. */
		rte_exit(EXIT_FAILURE, "Cannot init EAL\n");
	

	printf("Number of cores %u\n", (num_core = rte_lcore_count())); /* Get number of cores. */
	printf("No. of ports: %u\n", (v_num_ports = rte_eth_dev_count_avail())); /* Get number of ports. */
	if((v_num_ports < 1) || (num_core - v_num_ports < 1)) /* Ensure we always have at least one more core than number of ports. */
		rte_exit(EXIT_FAILURE, "Insufficient ports");
	
	if((v_app_ports = calloc(v_num_ports, sizeof(struct app_port))) == NULL) /* Allocate memory for controlling each job. */
		rte_exit(EXIT_FAILURE, "System memory error");
		
	i = 0;
	RTE_ETH_FOREACH_DEV(portid) { /* Setup each available port. */
		lcore = rte_get_next_lcore(lcore, 1, 0);
		setup_port(portid, lcore, &(v_app_ports[i]));
		printf("Mac: %02x:%02x:%02x:%02x:%02x:%02x\n", v_app_ports[i].mac_addr.addr_bytes[0], v_app_ports[i].mac_addr.addr_bytes[1], v_app_ports[i].mac_addr.addr_bytes[2], v_app_ports[i].mac_addr.addr_bytes[3], v_app_ports[i].mac_addr.addr_bytes[4], v_app_ports[i].mac_addr.addr_bytes[5]);
		++i;
	}
	
	
	for(i=0; i<v_num_ports; i++) { /* Launch threads on the cores. */
		v_app_ports[i].control = 1;
		rte_eal_remote_launch(do_listen, &(v_app_ports[i]), v_app_ports[i].core_id);
	}
	
	rte_eal_mp_wait_lcore();
	RTE_ETH_FOREACH_DEV(portid) { /* Cleanup each port. */
		//if(rte_flow_flush(portid, &error) != 0) {
		//	if(error.message != NULL)
		//		printf("%s\n", error.message);
		//}
		rte_flow_flush(portid, &error);
        rte_eth_dev_stop(portid);
        rte_eth_dev_close(portid);
	}
	
	do_cleanup();
	return 0;
}

