/** @file listen_worker.c
 *  Implements thread worker operations.
 */

#include "listen_worker.h"
#include <stdio.h>


/** Start listening on a NIC port for traffic.
 *  @param ptr_data Pointer to struct app_port which contains al required information to start a job on an lcore listening to a NIC port.
 *  @return Always returns 0.
 */
int worker_do_listen(__attribute__((unused)) void *ptr_data)
{
	//struct app_port *ptr_port = (struct app_port*)ptr_data;
	//struct rte_mbuf **ptr_frame = (struct rte_mbuf**)ptr_port->buf_frames;
	//int *control = &(ptr_port->control);
	//struct ether_hdr *__restrict__ eth_hdr;
	//struct ipv4_hdr *__restrict__ ip_hdr;
	//char src_eth[ETHER_ADDR_FMT_SIZE], dst_eth[ETHER_ADDR_FMT_SIZE];

	//const uint16_t port = ptr_port->port_id;
	//uint16_t cnt_recv_frames;
	//uint16_t cnt_total_recv_frames = 0;
	//uint16_t i;
	//struct in_addr src_ip, dst_ip;


	//printf("Launching listen to port %u on lcore %u\n", port, ptr_port->core_id);
	//while(*control == 1) {	
		///* Incoming frames */
		//cnt_recv_frames = rte_eth_rx_burst(port, 0, ptr_frame, MAX_BURST_LENGTH);
		//if(cnt_recv_frames > 0) {
			//cnt_total_recv_frames += cnt_recv_frames;
			//printf("Received frames: %u\n", cnt_total_recv_frames);
			
			//for(i=0; i<cnt_recv_frames; i++) {
				//eth_hdr = rte_pktmbuf_mtod(ptr_frame[i], struct ether_hdr *);
				//ip_hdr = rte_pktmbuf_mtod_offset(ptr_frame[i], struct ipv4_hdr *, sizeof(struct ether_hdr));
				//print_ether_addr(src_eth, &eth_hdr->s_addr);
				//print_ether_addr(dst_eth, &eth_hdr->d_addr);
				//src_ip.s_addr = ip_hdr->src_addr;
				//dst_ip.s_addr = ip_hdr->dst_addr;

				//printf("src eth: %s | dst eth: %s\nsrc ip: %s | dst ip: %s\nNext proto: %u\n", src_eth, dst_eth, inet_ntoa(src_ip), inet_ntoa(dst_ip), ip_hdr->next_proto_id);
				//rte_pktmbuf_free(ptr_frame[i]);
			//}
		//}
	//}
	

	return 0;
}
