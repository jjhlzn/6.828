#include "ns.h"
#include <inc/error.h>

extern union Nsipc nsipcbuf;

#define debug 1

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
	while (1) {
		uint32_t whom;
		if (debug)
			cprintf("[%08x]: start read packet from device driver ... \n", thisenv->env_id);
		int packet_size = 0;
		//make a pagefault, so nsipcbuf.pkt.jp_data can PASS PTE_W check 
		//when call sys_net_recv()
		int r;
		while ((r = sys_net_recv(data, 
							     sizeof(data), 
							     &packet_size)) < 0) {
			if (r != -E_NO_DATA)
				panic("INPUT: sys_net_recv return error!");
			sys_yield();
			//if (debug)
			//	printf("waiting for packet from device driver...\n");
		}
		if (debug) {
			cprintf("[%08x]: read a packet from device driver, ready to send to NS \n",
					thisenv->env_id);
			cprintf("[%08x]: packet_size = %d\n", thisenv->env_id, packet_size);
		}
		
		//nsipc_send((int)ns_envid, data, packet_size, PTE_U | PTE_P);
		memmove(nsipcbuf.pkt.jp_data, data, packet_size);
		nsipcbuf.pkt.jp_len = packet_size;

		//cprintf("input: len  %d\n", nsipcbuf.pkt.jp_len);
		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U | PTE_P);
	} 
}
