/** @file listen_worker.c
 *  Implements thread worker operations.
 */

#include "listen_worker.h"
#include "custom_defs.h"
#include <stdio.h>
#include <arpa/inet.h>
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


#define PRINT_BUFFER_SIZE 128


/** Get the VLAN offset.
 *  @param p_eth_hdr The ethernet header.
 * 
 */ 
static inline size_t get_vlan_offset(struct ether_hdr *__restrict__ p_eth_hdr, uint16_t *proto)
{
	size_t vlan_offset = 0;

	struct vlan_hdr *vlan_hdr = (struct vlan_hdr *)(p_eth_hdr + 1);
	vlan_offset = sizeof(struct vlan_hdr);
	*proto = vlan_hdr->eth_proto;

	if (rte_cpu_to_be_16(ETHER_TYPE_VLAN) == *proto) {
		vlan_hdr = vlan_hdr + 1;
		*proto = vlan_hdr->eth_proto;
		vlan_offset += sizeof(struct vlan_hdr);
	}

	return vlan_offset;
}



/** Convert ethernet address to printable format.
 *  @param p_buf The buffer to store the printed result. Must be size ETHER_ADDR_FMT_SIZE.
 *  @param p_eth_addr The ethernet address to print.
 */
static void print_ether_addr(char *__restrict__ p_buf, struct ether_addr *__restrict__ p_eth_addr)
{
	ether_format_addr(p_buf, ETHER_ADDR_FMT_SIZE, p_eth_addr);
}



/** Processes an ARP frame
 *  @param p_frame_num Number of the frame.
 */
inline static void process_arp(const uint16_t p_frame_num)
{
	printf("Frame %u is ARP\n", p_frame_num);
}



/** Processes an ICMP frame
 *  @param p_frame_num Number of the frame.
 */
inline static void process_ip6(const uint16_t p_frame_num)
{
	printf("Frame %u is IPv6\n", p_frame_num);
}




/** Start listening on a NIC port for traffic.
 *  @param ptr_data Pointer to struct app_port which contains al required information to start a job on an lcore listening to a NIC port.
 *  @return Always returns 0.
 */
int worker_do_listen(__attribute__((unused)) void *ptr_data)
{
	struct app_port *ptr_port = (struct app_port*)ptr_data;
	struct rte_mbuf **ptr_frame = (struct rte_mbuf**)ptr_port->buf_frames;
	int *control = &(ptr_port->control);
	struct ether_hdr *__restrict__ eth_hdr;
	struct ipv4_hdr *__restrict__ ip_hdr;
	char src_eth[ETHER_ADDR_FMT_SIZE], dst_eth[ETHER_ADDR_FMT_SIZE], print_buf[PRINT_BUFFER_SIZE];

	const uint16_t port = ptr_port->port_id;
	const unsigned lcore = ptr_port->core_id;
	uint16_t cnt_recv_frames;
	uint16_t i, ether_type, offset;
	struct in_addr src_ip, dst_ip;
	int print_buf_size;


	printf("Launching listen to port %u on lcore %u\n", port, lcore);
	while(*control == 1) {	
		/* Incoming frames */
		cnt_recv_frames = rte_eth_rx_burst(port, 0, ptr_frame, MAX_BURST_LENGTH);
		if(cnt_recv_frames > 0) {
			printf("lcore %u received %u frames\n", lcore, cnt_recv_frames);
			
			for(i=0; i<cnt_recv_frames; i++) {
				eth_hdr = rte_pktmbuf_mtod(ptr_frame[i], struct ether_hdr *);
				ether_type = eth_hdr->ether_type;
				offset = 0;
				if (eth_hdr->ether_type == rte_cpu_to_be_16(ETHER_TYPE_VLAN)) {
					offset = get_vlan_offset(eth_hdr, &ether_type);
					printf("Frame %u has VLAN tag. Offset: %u\n", i+1, offset);
				}
				
				print_ether_addr(src_eth, &eth_hdr->s_addr); /* Get the Ethernet addresses. */
				print_ether_addr(dst_eth, &eth_hdr->d_addr);
				print_buf_size = snprintf(print_buf, PRINT_BUFFER_SIZE, "src eth: %s | dst eth: %s\n", src_eth, dst_eth);


				switch (rte_be_to_cpu_16(ether_type)) {
					case ETHER_TYPE_ARP:
						process_arp(i+1);
						break;
					case ETHER_TYPE_IPv6:
						process_ip6(i+1);
						break;					
					case ETHER_TYPE_IPv4:
						break;
					default:
						break;
				}
								
				if (ether_type == rte_cpu_to_be_16(ETHER_TYPE_IPv4)) {
					ip_hdr = (struct ipv4_hdr *)((char *)(eth_hdr + 1) + offset); /* Get the IP4 addresses. */
					src_ip.s_addr = ip_hdr->src_addr;
					dst_ip.s_addr = ip_hdr->dst_addr;
					snprintf(print_buf+print_buf_size, PRINT_BUFFER_SIZE-print_buf_size, "src ip: %s | dst ip: %s\nProto id: %u\n", inet_ntoa(src_ip), inet_ntoa(dst_ip), ip_hdr->next_proto_id);
				}
				printf(print_buf);
				rte_pktmbuf_free(ptr_frame[i]);
			}
		}
	}
	

	return 0;
}
