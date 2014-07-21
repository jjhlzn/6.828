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
	
	while (1) {
		uint32_t whom;
		if (debug)
			cprintf("[%08x]: start receive message ... \n", thisenv->env_id);
		//sys_page_alloc(0, (void *)0x00600000, PTE_P | PTE_U | PTE_W);
		//union Nsipc 
		ipc_recv((int32_t *) &whom, &nsipcbuf, 0);
		if (whom != ns_envid) {
				cprintf("NS OUTPUT: output thread got IPC message from env %x not NS\n", whom);
				continue;
		}
		if (debug) {
			cprintf("[%08x]: get a message from NS, ready to send \n", thisenv->env_id);
			cprintf("addr of nsipcbuf = %08x\n",&nsipcbuf);
			cprintf("nsipcbuf.pkt.jp_len = %d\n",nsipcbuf.pkt.jp_len);
			cprintf("nsipcbuf.pkt.jp_data = %s\n",nsipcbuf.pkt.jp_data);
		}
		if (sys_net_send(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len) , 0) {
			panic("NS OUTPUT: send error");
			continue;
		}
	} 
}
