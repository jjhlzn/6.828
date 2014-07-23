#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/string.h>
#include <inc/error.h>

#include <inc/memlayout.h>
#include <kern/pmap.h>
#include <kern/pci.h>
#include <kern/e1000_hw.h>
#include <kern/e1000.h>

#define debug 1

#define TDBAH (E1000_TDBAH / 4)
#define TDBAL (E1000_TDBAL / 4)
#define TDLEN (E1000_TDLEN / 4)
#define TCTL  (E1000_TCTL / 4)
#define TDT	  (E1000_TDT / 4)
#define TDH   (E1000_TDH / 4)
#define TIPG  (E1000_TIPG / 4)

#define RDBAH (E1000_RDBAH / 4)
#define RDBAL (E1000_RDBAL / 4)
#define RDLEN (E1000_RDLEN / 4)
#define RCTL  (E1000_RCTL / 4)
#define RDT   (E1000_RDT / 4)
#define RDH   (E1000_RDH / 4)
#define IMS   (E1000_IMS / 4)
#define RA    (E1000_RA / 4)
#define RAL    (E1000_RA / 4)
#define RAH    (E1000_RA / 4 + 1)
#define MTA   (E1000_MTA / 4)

#define EERD  (E1000_EERD / 4)


volatile uint32_t *pci_bar0 = NULL;  //the mermoy address pointed by pci bar0

#define TX_DESC_LEN 64  //must mutiples of 8
#define TX_DESC_PACKET_SIZE 2048   //memory size pointed by each transmit 
								   //descriptor
#define RECV_DESC_LEN 64 //must mutiples of 8
#define RECV_DESC_PACKET_SIZE 2048

								   
static struct e1000_tx_desc tx_descs[TX_DESC_LEN]
__attribute__ ((aligned(16)));  //transmit descriptor ring 

static uint8_t tx_packet_buffer[TX_DESC_LEN * TX_DESC_PACKET_SIZE]
__attribute__ ((aligned(2048)));


static struct e1000_rx_desc rx_descs[RECV_DESC_LEN]
__attribute__ ((aligned(16))); 
static uint8_t recv_packet_buffer[RECV_DESC_LEN * RECV_DESC_PACKET_SIZE]
__attribute__ ((aligned(2048)));


static uint32_t
pcibar0r(int index)
{
	return pci_bar0[index]; 
}

static void
pcibar0w(int index, int value)
{
	pci_bar0[index] = value;
	pci_bar0[index];  // wait for write to finish, by reading
	//cprintf("value = %08x, value1 = %08x\n", value, pci_bar0[index]);
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

// Tansimit packet initialization
// alloc memmory transmit descriptors array and the packet buffer pointed
// by the descriptor. Initialize every transimit descriptor. Set corresponding
// registers.
// 0. NIC use PHYSICAL address!
// 1. transmit descriptors array should be 16-bytes aligned.
// 2. initialize Transmit descriptors base address(TDBAH/TDBAL).
// 3. set transmit descriptors(TDLEN) register to the size of transmit
//	  descriptors array in bytes. this register must be 128-byte aligned.
//    because a transmit descriptor is 16-byte, so TDLEN must be mutiples 
//    of 8.
// 4. Set Transmit Descriptor Head And Tail (TDH/TDT) registers to 0.
// 5. Initialzie the Transmit Control Register (TCTL)
//    5.1 Set the Enable (TCTL.EN) bit to 0 for normal operation
//    5.2 Set the Pad Short Packets (TCTL.PSP) bit to 1
//    5.3 Configure the Collision Threshold (TCTL.CT) to the desired 
//        value. Ethernet standard is 10h.This setting only has meaning
//        in half duplex mode.
//    5.4 Configure the Collision Distance (TCTL.COLD) to its expected 
//        value. For full duplex operation, this value should be set 
//        to 40h. For gigabit half duplex, this value should be set to
//         200h. For 10/100 half duplex, this value should be set to 40h.
// 6. Set Transmit Inter Packet Gap (TIPG) register according to table
//    13-77 of section 13-4-34
static void 
e1000_tx_init()
{
	uint32_t i;
	pcibar0w(TDT, 0);
	pcibar0w(TDH, 0);
	pcibar0w(TDBAH, 0);
	//cprintf("tx_descs = %08x, buffer_addr = %08x\n", tx_descs, tx_packet_buffer);
	pcibar0w(TDBAL, PADDR(&tx_descs[0]));
	pcibar0w(TDLEN, sizeof(tx_descs));
	memset(tx_descs, 0, sizeof(struct e1000_tx_desc) * TX_DESC_LEN);
	for (i = 0; i < TX_DESC_LEN; i++) {
		tx_descs[i].buffer_addr = PADDR(tx_packet_buffer 
											+ i * TX_DESC_PACKET_SIZE);
		tx_descs[i].lower.data |= E1000_TXD_CMD_RS;
		tx_descs[i].upper.data |= E1000_TXD_STAT_DD;
		//cprintf("buffer_addr = %08x\n",tx_descs[i].buffer_addr);
	}

	uint32_t tipg = pcibar0r(TIPG); // (8 << 20) | (12 << 10) | 10; //unit is ns
	tipg &= ~0x000003FF;
	tipg |= 0x8;
	pcibar0w(TIPG, tipg); 

	uint32_t tctl = 0;
	tctl |= E1000_TCTL_EN;
	tctl |= E1000_TCTL_PSP;
	tctl |= 0x00000100; //set TCTL.CT = 10h
	tctl |= 0x00040000; //set TCTL.COLD = 40h
	//cprintf("tctl = %08x\n", tctl);
	pcibar0w(TCTL, tctl);
	
	if (debug) {
		cprintf("tx_desc_base_addr = %08x %08x\n", pcibar0r(TDBAH), pcibar0r(TDBAL));
		cprintf("tx_desc_len = %08x (%d)\n", pcibar0r(TDLEN), 
				pcibar0r(TDLEN)/sizeof(struct e1000_tx_desc));
		cprintf("tdh = %d, tdt = %d\n", pcibar0r(TDH), pcibar0r(TDT));
		cprintf("tctl = %08x\n", pcibar0r(TCTL));
		cprintf("tipg = %08x\n", pcibar0r(TIPG));
	}	
}

// Receive Packet Initialization. 
// NOTE: Must use PHYSICAL address for NIC buffer (descriptors and packet bufer).
// 1. Alloc receive descriptor array (actually we have allocted). 
//    And alloc packet buffer for every descriptor.
// 2. Set RDBAL/RDBAH, RDLEN, RDH, RDT according to the receive descriptor
//    array.
// 3. Set Receive Address Regsiters(RAL/RAH). The registers store MAC address.
// 4. Set MTA to 0
// 5. Close Interrupt Set/Read Regiseter (IMS) now. When we need intterupt,
//    we can set the bit for needed interrupt.
// 6. Set Receive Control Register:
//	  6.1 Close long packet by set RCTL.LPE to 0
//    6.2 Set loopback mode bit (RCTL.LBM) to 00
//    6.3 Set RCTL.BSIZE to the packet buffer size. If packet buffer size
//        is larger than 2048 bytes, configure the Buffer Extension Size
//       (RCTL.BSEX) bits 
//    6.4 Set strip CRS bit (RCTL.SECRC) to 1.
//    6.5 Set enable receive bit (RCTL.EN) to 1.
static void 
e1000_rx_init()
{
	pcibar0w(RDBAH, 0);
	pcibar0w(RDBAL, PADDR(rx_descs));
	pcibar0w(RDLEN, sizeof(rx_descs));
	pcibar0w(RDH, 0);
	pcibar0w(RDT, RECV_DESC_LEN - 1);
	uint32_t i;
	for (i = 0; i < RECV_DESC_LEN; i++) {
		rx_descs[i].buffer_addr = PADDR(recv_packet_buffer 
											+ i * RECV_DESC_PACKET_SIZE);
		rx_descs[i].status = 0;
	}
	
	uint8_t mac_addr[6];
	e1000_read_mac_addr(mac_addr);

    pcibar0w(RAL, *(uint32_t *)mac_addr);
    pcibar0w(RAH, (*((uint32_t *)mac_addr + 1) & 0x0000FFFF) | 0x80000000);
    
    pcibar0w(MTA, 0);
    pcibar0w(IMS, 0);
    
    uint32_t reg_data = pcibar0r(RCTL);
    if (RECV_DESC_PACKET_SIZE != 2048)
		panic("recv packet size isn't 2048");
    reg_data &= ~E1000_RCTL_LPE;
    reg_data &= ~E1000_RCTL_LBM_MASK;
    reg_data &= ~E1000_RCTL_SZ_MASK;
    reg_data &= ~E1000_RCTL_BSEX;
    reg_data |= E1000_RCTL_SECRC;
    reg_data |= E1000_RCTL_EN;

    pcibar0w(RCTL, reg_data);
		
}

// Read MAC address from NIC
// Software can use the EEPROM Read register (EERD) to cause the 
// Ethernet controller to read a word from the EEPROM that the 
// software can then use. STEPS:
// 1. software writes the address to read the Read Address (EERD.ADDR) 
//    field and then simultaneously writes a 1b to the Start Read
//    bit (EERD.START). 
// 2. The Ethernet controller then reads the word from the EEPROM, 
//    sets the Read Done bit (EERD.DONE), and puts the data in the 
//    Read Data field (EERD.DATA). 
// 3. Software can poll the EEPROM Read register until it sees the 
//    EERD.DONE bit set, then use the data from the EERD.DATA field. 
// Any words read this way are not written to hardware’s internal registers.
int 
e1000_read_mac_addr(uint8_t *buf)
{
	int i;
	uint32_t reg_data = 0;
	uint16_t *addrbuf = (uint16_t *)buf;
	
	for (i = 0; i < 3; i++) {
		reg_data = 0;
		reg_data |= i << E1000_EEPROM_RW_ADDR_SHIFT;
		reg_data |= E1000_EEPROM_RW_REG_START;
		reg_data &= ~E1000_EEPROM_RW_REG_DONE;
		
		pcibar0w(EERD, reg_data);
		
		while (!(pcibar0r(EERD) & E1000_EEPROM_RW_REG_DONE))
			/* waiting */ ;
		
		addrbuf[i] = pcibar0r(EERD) >> E1000_EEPROM_RW_REG_DATA;
	}
	return 0;
}

int  
e1000_attach(struct pci_func *pcif) 
{
	// create a virtual memory mappng for the E1000's BAR (Base Address
	// Register) 0.
	if (debug) 
		cprintf("start e1000_attach\n");
	pci_func_enable(pcif);
	
	assert(pcif->reg_base[0]);
	assert(pcif->reg_size[0] <= 0x0400000);
	
	uintptr_t va_addr = PCI_BAR0;
	assert(va_addr % 0x100000 == 0); //PCI_BAR0 should be 4M aligned.
	uintptr_t end_va_addr = va_addr + pcif->reg_size[0];
	uintptr_t phy_addr = pcif->reg_base[0];
	
	pte_t *ptep = NULL;
	for (; va_addr < end_va_addr; va_addr += PGSIZE, phy_addr += PGSIZE) {
		if (!(ptep = pgdir_walk(kern_pgdir, (void *)va_addr, 1)))
			panic("out of memory!");
		//perm: kernel RW, user None
		*ptep = phy_addr | PTE_P | PTE_W | PTE_PCD | PTE_PWT;
	}
	
	pci_bar0 = (uint32_t *)PCI_BAR0;
	
	uint32_t device_status = pcibar0r(E1000_STATUS/4);
	cprintf("device_status = %08x\n", device_status);
	
	e1000_tx_init();
	e1000_rx_init();
	
	return 0;
}

int
e1000_tx(uint8_t *buf, int len)
{
	assert(len <= TX_DESC_PACKET_SIZE);
	uint32_t tdt;
	tdt = pcibar0r(TDT);
	
	while (!((tx_descs[tdt].lower.data & E1000_TXD_CMD_RS) 
	   && (tx_descs[tdt].upper.data & E1000_TXD_STAT_DD)))
	    cprintf("waiting, no free tx descriptor\n");
	
	memmove(KADDR((uint32_t)(tx_descs[tdt].buffer_addr)), buf, len);
	tx_descs[tdt].lower.flags.length = len;
	tx_descs[tdt].lower.data |= E1000_TXD_CMD_RS;   //set 1
	tx_descs[tdt].lower.data |= E1000_TXD_CMD_EOP;  // set 1
	tx_descs[tdt].lower.data &= ~E1000_TXD_CMD_DEXT; //set 0
	tx_descs[tdt].upper.data &= ~E1000_TXD_STAT_DD; //clear E1000_TXD_STAT_DD
	tx_descs[tdt].upper.fields.css = 0;

	if (debug) {
		hexdump("e1000_tx output:", buf, len);
	}
	pcibar0w(TDT, (tdt + 1) % TX_DESC_LEN);
	return 0;
}


int 
e1000_rx(uint8_t *buf, int bufsize, int *packet_size)
{
	uint32_t rdt, recv_index;
	
	rdt = pcibar0r(RDT);
	recv_index = (rdt + 1) % RECV_DESC_LEN;

	if (!rx_descs[recv_index].status) {
		return -E_NO_DATA;
	} 
	
	if (!(rx_descs[recv_index].status & E1000_RXD_STAT_EOP))
		panic("driver don't suport long packets");
	//copy packets data to buf
	*packet_size = rx_descs[recv_index].length;
	if (rx_descs[recv_index].length > bufsize)
		cprintf("e1000_rx: WARN!!!!! bufsize is smaller than packet_size\n", 
				  bufsize, 
				  *packet_size);
	memmove(buf, 
			KADDR((uint32_t)(rx_descs[recv_index].buffer_addr)), 
		   *packet_size > bufsize ? bufsize : *packet_size);
	rx_descs[recv_index].status = 0; 
	
	if (debug) {
		hexdump("e1000_rx input:", buf, *packet_size > bufsize ? bufsize : *packet_size);
	}
	//update rdt
	cprintf("rdh = %d, rdt = %d\n", pcibar0r(RDH), recv_index);
	pcibar0w(RDT, recv_index);
	return 0;
} 


