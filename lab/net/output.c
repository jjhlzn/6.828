#include "ns.h"
#include <inc/lib.h>
extern union Nsipc nsipcbuf;

#define debug 0

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	//cprintf("[%08x]: I am OUTPUT\n", thisenv->env_id);
	while (1) {
		uint32_t whom;
		//if (debug)
		//	cprintf("[%08x]: start receive message ... \n", thisenv->env_id);
		//sys_page_alloc(0, (void *)0x00600000, PTE_P | PTE_U | PTE_W);
		//union Nsipc 
		ipc_recv((int32_t *) &whom, &nsipcbuf, 0);
		if (whom != ns_envid) {
				cprintf("NS OUTPUT: output thread got IPC message from env %x not NS\n", whom);
				continue;
		}
		if (debug) {
			cprintf("[%08x]: get a message, ready to send to NIC \n", thisenv->env_id);
			cprintf("[%08x]: packet_size = %d\n", thisenv->env_id, nsipcbuf.pkt.jp_len);
			//cprintf("[%08x]: nsipcbuf.pkt.jp_data = %s\n", thisenv->env_id, nsipcbuf.pkt.jp_data);
		}
		if (sys_net_send(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len) , 0) {
			panic("NS OUTPUT: send error");
			continue;
		}
	} 
}
