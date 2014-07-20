#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/string.h>
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


volatile uint32_t *pci_bar0 = NULL;  //the mermoy address pointed by pci bar0

#define TX_DESC_LEN 64  //must mutiples of 8
static struct e1000_tx_desc tx_descs[TX_DESC_LEN]
__attribute__ ((aligned(16)));  //transmit descriptor ring 
#define TX_DESC_PACKET_SIZE 2048   //memory size pointed by each transmit 
								   //descriptor
static uint8_t tx_packet_buffer[TX_DESC_LEN * TX_DESC_PACKET_SIZE]
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
	uint32_t i;
	pcibar0w(TDT, 0);
	pcibar0w(TDH, 0);
	pcibar0w(TDBAH, 0);
	cprintf("tx_descs = %08x, buffer_addr = %08x\n", tx_descs, tx_packet_buffer);
	pcibar0w(TDBAL, PADDR(&tx_descs[0]));
	pcibar0w(TDLEN, TX_DESC_LEN * sizeof(struct e1000_tx_desc));
	memset(tx_descs, 0, sizeof(struct e1000_tx_desc) * TX_DESC_LEN);
	for (i = 0; i < TX_DESC_LEN; i++) {
		tx_descs[i].buffer_addr = PADDR(tx_packet_buffer 
											+ i * TX_DESC_PACKET_SIZE);
		tx_descs[i].lower.data |= E1000_TXD_CMD_RS;
		//tx_descs[i].upper.data |= E1000_TXD_STAT_DD;
		//cprintf("buffer_addr = %08x\n",tx_descs[i].buffer_addr);
	}
	
	uint32_t reg_data = pcibar0r(E1000_CTRL_EXT/4);
	reg_data &= ~0x000FFC00;
	reg_data |= 0x00010000;
	pcibar0w(E1000_CTRL_EXT/4, reg_data);
	cprintf("ctrl_ex = %08x\n", pcibar0r(E1000_CTRL_EXT/4));

	uint32_t tipg = pcibar0r(TIPG); // (8 << 20) | (12 << 10) | 10; //unit is ns
	tipg &= ~0x000003FF;
	tipg |= 0x8;
	pcibar0w(TIPG, tipg); 

	uint32_t tctl = 0;
	tctl |= E1000_TCTL_EN;
	tctl |= E1000_TCTL_PSP;
	tctl |= 0x00000100; //set TCTL.CT = 10h
	tctl |= 0x00040000; //set TCTL.COLD = 40h
	cprintf("tctl = %08x\n", tctl);
	pcibar0w(TCTL, tctl);
	
	cprintf("tx_desc_base_addr = %08x %08x\n", pcibar0r(TDBAH), pcibar0r(TDBAL));
	cprintf("tx_desc_len = %08x (%d)\n", pcibar0r(TDLEN), 
			pcibar0r(TDLEN)/sizeof(struct e1000_tx_desc));
	cprintf("tdh = %d, tdt = %d\n", pcibar0r(TDH), pcibar0r(TDT));
	cprintf("tctl = %08x\n", pcibar0r(TCTL));
	cprintf("tipg = %08x\n", pcibar0r(TIPG));
	return 0;
}

int
e1000_tx(uint8_t *buf, int len)
{
	//cprintf("e1000_tx: start\n");
	assert(len <= TX_DESC_PACKET_SIZE);
	uint32_t tdh, tdt, next;
	//dh = pcibar0r(E1000_TDH / 4);
	tdt = pcibar0r(TDT);
	
	/*
	while (!((tx_descs[tdt].lower.data & E1000_TXD_CMD_RS) 
	   && (tx_descs[tdt].upper.data & E1000_TXD_STAT_DD)))
	    cprintf("waiting, no free tx descriptor\n");*/
	
	memmove(KADDR((uint32_t)(tx_descs[tdt].buffer_addr)), buf, len);
	tx_descs[tdt].lower.flags.length = len;
	tx_descs[tdt].lower.data |= E1000_TXD_CMD_RS;   //set 1
	tx_descs[tdt].lower.data |= E1000_TXD_CMD_EOP;  // set 1
	tx_descs[tdt].lower.data &= ~E1000_TXD_CMD_DEXT; //set 0
	tx_descs[tdt].upper.data &= ~E1000_TXD_STAT_DD; //clear E1000_TXD_STAT_DD
	tx_descs[tdt].upper.fields.css = 0;

	
	cprintf("buffer_addr = %08x %08x\n", *(uint32_t *)((char *)&tx_descs[tdt].buffer_addr + 4),
					*(uint32_t *)&tx_descs[tdt].buffer_addr);
	cprintf("lower = %08x\n", tx_descs[tdt].lower.data);
	cprintf("uppper = %08x\n", tx_descs[tdt].upper.data);
	pcibar0w(TDT, (tdt + 1) % TX_DESC_LEN);
	//cprintf("e1000_tx: tdh = %d, tdt = %d\n", pcibar0r(TDH), pcibar0r(TDT));
	//cprintf("e1000_tx: finish\n");
	return 0;
}


