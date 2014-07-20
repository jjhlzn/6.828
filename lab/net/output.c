#include "ns.h"
#include <inc/lib.h>
extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	cprintf("output is running!!!!!!!!!!!!!!!!!!!!\n");
	//binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	
	while (1) {
		cprintf("output is running!!!!!!!!!!!!!!!!!!!!\n");
		uint32_t whom;
		ipc_recv((int32_t *) &whom, &nsipcbuf, 0);
		if (whom != ns_envid) {
				cprintf("NS OUTPUT: output thread got IPC message from env %x not NS\n", whom);
				continue;
		}
		if (sys_net_send(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len) , 0) {
			panic("NS OUTPUT: send error");
			continue;
		}
	} 
}
