#include "ns.h"
#include <inc/error.h>

//extern union Nsipc nsipcbuf;

#define debug 0

static int turn = 0;
static union Nsipc _nsipcbuf[2]
__attribute__ ((aligned(PGSIZE)));

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
	//char data[2048];
	
	//why we need the flowing initializaiton? becasuse if we don't do this,
	//we will encounter user_mem_assert failure when call sys_net_recv().
	//INPUT is forked by other env, and _nsicbuf is PTE_COW, 
	//we must make sure input has private copy.
	_nsipcbuf[0].pkt.jp_len = 0;
	_nsipcbuf[1].pkt.jp_len = 0;
	while (1) {
		uint32_t whom;
		//if (debug)
		//	cprintf("[%08x]: reading from NIC ... \n", thisenv->env_id);
		int packet_size = 0;
		int r;
		while ((r = sys_net_recv(_nsipcbuf[turn].pkt.jp_data, 
							     sizeof(union Nsipc) - sizeof(int), 
							     &packet_size)) < 0) {
			if (r != -E_NO_DATA)
				panic("INPUT: sys_net_recv return error!");
			sys_yield();
		}
		if (debug) {
			cprintf("[%08x]: read a packet, ready to send to NS \n",
					thisenv->env_id);
			cprintf("[%08x]: packet_size = %d\n", thisenv->env_id, packet_size);
		}
		
		_nsipcbuf[turn].pkt.jp_len = packet_size;

		//cprintf("input: len  %d\n", nsipcbuf.pkt.jp_len);
		ipc_send(ns_envid, NSREQ_INPUT, &_nsipcbuf[turn], PTE_U | PTE_P);
		
		turn = (turn + 1) % 2;
	} 
}
