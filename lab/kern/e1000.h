#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#define E1000_PCI_VENDOR  0x8086
#define E1000_PCI_PRODUCT  0x100e

int e1000_attach(struct pci_func *pcif);
int e1000_tx(uint8_t *buf, int len);
int e1000_rx(uint8_t *buf, int bufsize, int *packet_size);

#endif	// JOS_KERN_E1000_H
