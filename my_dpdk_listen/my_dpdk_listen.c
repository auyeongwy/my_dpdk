/** @file my_dpdk_init.c
 * 	Demonstrates intilizing a port with DPDK.
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


struct app_port 
{
	int control;
	struct ether_addr mac_addr;
	struct rte_mbuf *buf_frames[MAX_BURST_LENGTH];
	rte_spinlock_t lock;
	int port_active;
	int port_dirty;
	int idx_port;
	unsigned core_id;
	struct rte_mempool *pkt_pool;
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



static struct app_port *v_app_ports = NULL;
static int v_num_ports = 0;



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



static void do_cleanup()
{
	if(v_app_ports != NULL)
		free(v_app_ports);
}



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
	rte_eth_dev_info_get(p_port_id, &dev_info);
	size_pktpool = dev_info.rx_desc_lim.nb_max + dev_info.tx_desc_lim.nb_max + PKTPOOL_EXTRA_SIZE;
	snprintf(str_name, 16, "pkt_pool%i", p_port_id);
	
	
	p_app_port->pkt_pool = rte_pktmbuf_pool_create(str_name, size_pktpool, PKTPOOL_CACHE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (p_app_port->pkt_pool == NULL)
		rte_exit(EXIT_FAILURE, "rte_pktmbuf_pool_create failed");
	else
		printf("Port %u Pkt pool %s poolsize: %d\n", p_port_id, str_name, size_pktpool);
		
	p_app_port->port_active = 1;
	p_app_port->port_dirty = 0;
	p_app_port->idx_port = p_port_id;
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

	if (rte_eth_dev_start(p_port_id) < 0)
		rte_exit(EXIT_FAILURE, "%s:%i: rte_eth_dev_start failed", __FILE__, __LINE__);
	rte_eth_macaddr_get(p_port_id, &(p_app_port->mac_addr));
	rte_spinlock_init(&(p_app_port->lock));		
}



static int do_listen(__attribute__((unused)) void *ptr_data)
{
	struct app_port *ptr_port = (struct app_port*)ptr_data;
	struct rte_mbuf *ptr_frame = (struct rte_mbuf*)ptr_port->buf_frames;
	int *control = &(ptr_port->control);


	const uint16_t port = ptr_port->idx_port;
	uint16_t cnt_recv_frames;
	uint16_t cnt_total_recv_frames = 0;


	printf("Launching listen to port %u on lcore %u\n", port, ptr_port->core_id);
	while(*control == 1) {	
		/* Incoming frames */
		cnt_recv_frames = rte_eth_rx_burst(port, 0, &ptr_frame, MAX_BURST_LENGTH);
		if(cnt_recv_frames > 0) {
			cnt_total_recv_frames += cnt_recv_frames;
			printf("Received frames: %u\n", cnt_total_recv_frames);
		}
	}
	

	//while (app_cfg.exit_now == 0) {
		//for (idx_port = 0; idx_port < app_cfg.cnt_ports; idx_port++) {
			///* Check that port is active and unlocked */
			//ptr_port = &app_cfg.ports[idx_port];
			//lock_result = rte_spinlock_trylock(&ptr_port->lock);
			//if (lock_result == 0)
				//continue;
			//if (ptr_port->port_active == 0) {
				//rte_spinlock_unlock(&ptr_port->lock);
				//continue;
			//}
			//txq = &ptr_port->txq;

			///* MAC address was updated */
			//if (ptr_port->port_dirty == 1) {
				//rte_eth_macaddr_get(ptr_port->idx_port,
					//&ptr_port->mac_addr);
				//ptr_port->port_dirty = 0;
			//}

			///* Incoming frames */
			//cnt_recv_frames = rte_eth_rx_burst(
				//ptr_port->idx_port, 0,
				//&txq->buf_frames[txq->cnt_unsent],
				//RTE_DIM(txq->buf_frames) - txq->cnt_unsent
				//);
			//if (cnt_recv_frames > 0) {
				//for (idx_frame = 0;
					//idx_frame < cnt_recv_frames;
					//idx_frame++) {
					//ptr_frame = txq->buf_frames[
						//idx_frame + txq->cnt_unsent];
					//process_frame(ptr_port, ptr_frame);
				//}
				//txq->cnt_unsent += cnt_recv_frames;
			//}

			///* Outgoing frames */
			//if (txq->cnt_unsent > 0) {
				//cnt_sent = rte_eth_tx_burst(
					//ptr_port->idx_port, 0,
					//txq->buf_frames,
					//txq->cnt_unsent
					//);
				///* Shuffle up unsent frame pointers */
				//for (idx_frame = cnt_sent;
					//idx_frame < txq->cnt_unsent;
					//idx_frame++)
					//txq->buf_frames[idx_frame - cnt_sent] =
						//txq->buf_frames[idx_frame];
				//txq->cnt_unsent -= cnt_sent;
			//}
			//rte_spinlock_unlock(&ptr_port->lock);
		//} /* end for( idx_port ) */
	//} /* end for(;;) */

	return 0;
}



int main(int argc, char *argv[])
{
	unsigned num_core, portid, i;
	int lcore = -1; /* Start at -1. */


	signal(SIGINT, handle_interrupt); /* Register signal handler. */
	
	
	if(rte_eal_init(argc, argv) < 0) /* Init environment. */
		rte_exit(EXIT_FAILURE, "Cannot init EAL\n");
	

	printf("Number of cores %u\n", (num_core = rte_lcore_count())); /* Get number of cores. */
	printf("No. of ports: %u\n", (v_num_ports = rte_eth_dev_count_avail())); /* Get number of ports. */
	if((v_num_ports < 1) || (num_core - v_num_ports < 1)) /* Ensure we always have at least one more core than number of ports. */
		rte_exit(EXIT_FAILURE, "Insufficient ports");
	
	if((v_app_ports = calloc(v_num_ports, sizeof(struct app_port))) == NULL)
		rte_exit(EXIT_FAILURE, "System memory error");
	i = 0;
	RTE_ETH_FOREACH_DEV(portid) {
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
	do_cleanup();
	return 0;
}

