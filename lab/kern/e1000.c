#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <kern/pmap.h>
#include <kern/pci.h>
#include <kern/e1000_hw.h>
#include <kern/e1000.h>


#define debug 1

volatile uint32_t *pci_bar0 = NULL;

static uint32_t
pcibar0r(int index)
{
	return pci_bar0[index]; 
}

// LAB 6: Your driver code here
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
	
	return 0;
}


