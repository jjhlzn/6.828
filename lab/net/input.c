#include "ns.h"
#include <inc/error.h>

extern union Nsipc nsipcbuf;

#define debug 0

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	
	char data[2048];
	struct jif_pkt pkt_send;
	struct jif_pkt pkt_recv;
	while (1) {
		uint32_t whom;
		if (debug)
			cprintf("[%08x]: start read packet from device driver ... \n", thisenv->env_id);
		int packet_size = 0;
		//make a pagefault, so nsipcbuf.pkt.jp_data can PASS PTE_W check 
		//when call sys_net_recv()
		nsipcbuf.pkt.jp_data[0] = 1;
		int r;
		while ((r = sys_net_recv(pkt_recv->jp_data, sizeof(struct jif_pkt) - sizeof(int), &packet_size)) < 0) {
			if (r != -E_NO_DATA)
				panic("INPUT: sys_net_recv return error!");
			if (debug)
				printf("waiting for packet from device driver...\n");
		}
		if (debug) {
			cprintf("[%08x]: read a packet from device driver, ready to send to NS \n",
					thisenv->env_id);
			cprintf("nsipcbuf.pkt.jp_len = %d\n",nsipcbuf.pkt.jp_len);
			cprintf("nsipcbuf.pkt.jp_data = %s\n",nsipcbuf.pkt.jp_data);
		}
		
		//nsipc_send((int)ns_envid, data, packet_size, PTE_U | PTE_P);
		memmove(pkt_send.jp_data, pkt_recv.jd_data, packet_size);
		pkt_send.jp_len = pkt_recv.jp_len;
		cprintf("input: first db  %08x\n", *(uint32_t *)pkt_send.jp_data);
		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U | PTE_P);
	} 
}
