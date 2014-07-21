#include "ns.h"
#include <netif/etharp.h>

static envid_t output_envid;
static envid_t input_envid;

static struct jif_pkt *pkt = (struct jif_pkt*)REQVA;
extern union Nsipc nsipcbuf;
#define debug 1
static void
announce(void)
{
	// We need to pre-announce our IP so we don't have to deal
	// with ARP requests.  Ideally, we would use gratuitous ARP
	// for this, but QEMU's ARP implementation is dumb and only
	// listens for very specific ARP requests, such as requests
	// for the gateway IP.

	uint8_t mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
	uint32_t myip = inet_addr(IP);
	uint32_t gwip = inet_addr(DEFAULT);
	int r;

	if ((r = sys_page_alloc(0, pkt, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);

	struct etharp_hdr *arp = (struct etharp_hdr*)pkt->jp_data;
	pkt->jp_len = sizeof(*arp);

	memset(arp->ethhdr.dest.addr, 0xff, ETHARP_HWADDR_LEN);
	memcpy(arp->ethhdr.src.addr,  mac,  ETHARP_HWADDR_LEN);
	arp->ethhdr.type = htons(ETHTYPE_ARP);
	arp->hwtype = htons(1); // Ethernet
	arp->proto = htons(ETHTYPE_IP);
	arp->_hwlen_protolen = htons((ETHARP_HWADDR_LEN << 8) | 4);
	arp->opcode = htons(ARP_REQUEST);
	memcpy(arp->shwaddr.addr,  mac,   ETHARP_HWADDR_LEN);
	memcpy(arp->sipaddr.addrw, &myip, 4);
	memset(arp->dhwaddr.addr,  0x00,  ETHARP_HWADDR_LEN);
	memcpy(arp->dipaddr.addrw, &gwip, 4);

	//ipc_send(output_envid, NSREQ_OUTPUT, pkt, PTE_P|PTE_W|PTE_U);
	if (sys_net_send(pkt->jp_data, pkt->jp_len) , 0) {
		panic("NS OUTPUT: send error");
	}
	sys_page_unmap(0, pkt);
}

struct jif_pkt pkt2;
static void
sendtestpacket(void)
{
	// We need to pre-announce our IP so we don't have to deal
	// with ARP requests.  Ideally, we would use gratuitous ARP
	// for this, but QEMU's ARP implementation is dumb and only
	// listens for very specific ARP requests, such as requests
	// for the gateway IP.

	//uint8_t mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
	//uint32_t myip = inet_addr(IP);
	//uint32_t gwip = inet_addr(DEFAULT);
	int r;

	char msg[1024] = "hello                                         abd";
	msg[0] = 0x52;
	msg[1] = 0x54;
	msg[2] = 0x00;
	msg[3] = 0x12;
	msg[4] = 0x34;
	msg[5] = 0x56;
	
	msg[6] = 0x52;
	msg[7] = 0x55;
	msg[8] = 0x0a;
	msg[9] = 0x00;
	msg[10] = 0x02;
	msg[11] = 0x02;
	
	int len = 64;
	memmove(pkt2.jp_data, msg, len);
    pkt2.jp_len = len;
	//ipc_send(output_envid, NSREQ_OUTPUT, pkt, PTE_P|PTE_W|PTE_U);
	if (sys_net_send(pkt2.jp_data, pkt2.jp_len) < 0) {
		panic("NS OUTPUT: send error");
	}
	
}

static void
hexdump(const char *prefix, const void *data, int len)
{
	int i;
	char buf[80];
	char *end = buf + sizeof(buf);
	char *out = NULL;
	for (i = 0; i < len; i++) {
		if (i % 16 == 0)
			out = buf + snprintf(buf, end - buf,
					     "%s%04x   ", prefix, i);
		out += snprintf(out, end - out, "%02x", ((uint8_t*)data)[i]);
		if (i % 16 == 15 || i == len - 1)
			cprintf("%.*s\n", out - buf, buf);
		if (i % 2 == 1)
			*(out++) = ' ';
		if (i % 16 == 7)
			*(out++) = ' ';
	}
}

void
umain(int argc, char **argv)
{
	envid_t ns_envid = sys_getenvid();
	int i, r, first = 1;

	binaryname = "testinput";

	output_envid = fork();
	if (output_envid < 0)
		panic("error forking");
	else if (output_envid == 0) {
		while (1) {
			announce();
			sendtestpacket();
			int count = 1000000000;
			while(count--);
			sys_yield();
		}
		return;
	}

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
		//ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U | PTE_P);
	} 
}
