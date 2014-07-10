// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/env.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line

static void get_pte_permission_desc(uint16_t pte_permission, char *msg);
static void show_pte_mappings(int pdx, uintptr_t from_addr, uintptr_t end_addr);
static int parse_str2int(char *str);
static int atoi(char *str, int base);
static void dump_virtual_mem (uintptr_t start_addr, uintptr_t end_addr);
static void dump_virtual_mem_per_pde(uintptr_t start_addr, uintptr_t end_addr);
static void dump_virtual_mem_per_pte(uintptr_t start_addr, uintptr_t end_addr);
static void dump_prefix_empty (uintptr_t start_addr);
static void dump_db (uintptr_t addr, uint32_t dw);
static void dump_phys_mem (physaddr_t start_addr, physaddr_t end_addr);
static pde_t *get_curenv_or_kernel_pgdir();

#define DW_COUNT_PER_LINE  4 //show how many double worlds per line
//4M page offset
#define BIGPGOFF(la)	(((uintptr_t) (la)) & 0x3fffff)

struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Dispaly stack backtrace", mon_backtrace},
	{ "showmappings", "Display virtual memory mapping", mon_showmappings },
	{ "setptpermission", "Set page table permission", mon_setptpermission },
	{ "dump", "Dump memory contents", mon_dump}
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-entry+1023)/1024);
	return 0;
}

int mon_setptpermission(int argc, char **argv, struct Trapframe *tf)
{
	pde_t *kern_pgdir = get_curenv_or_kernel_pgdir();
	//check parameters
	if (argc != 3 && argc != 4) {
		cprintf("usage: setptpermission virtual_addr permission or setptpermission start_virtual end_virtual permission\n");
		return 0;
	}
	
	uintptr_t start_virtual = 0, end_virtual = 0;
	uint16_t permission = 0;
	if (argc == 3) {
		start_virtual = (int32_t) parse_str2int(argv[1]);
		end_virtual = start_virtual;
		permission = (uint16_t)parse_str2int(argv[2]);
	} else if (argc == 4) {
		start_virtual = (int32_t) parse_str2int(argv[1]);
		end_virtual = (int32_t) parse_str2int(argv[2]);
		permission = (uint16_t)parse_str2int(argv[3]);
	}
	
	if (curenv && curenv->env_status != ENV_FREE)
		cprintf("set page table permission for env[%d].\n", curenv->env_id);
	else 
		cprintf("set page table permission for kernel.\n");
	
	int i,j,end_ptx;
	for (i=PDX(start_virtual); i<=PDX(end_virtual); i++) {
		if (kern_pgdir[i] & PTE_PS) {
			kern_pgdir[i] = (kern_pgdir[i] & 0xfffffe00) | (permission & 0x1ff);
		} else {
			if (i==PDX(start_virtual)) 
				j = PTX(start_virtual);
			else
				j = 0;
			if (i==PDX(end_virtual))
				end_ptx = PTX(end_virtual);
			else
				end_ptx = NPTENTRIES - 1;
			uintptr_t base_addr = i * PTSIZE;
			for (; j <= end_ptx; j++) {
				pte_t *ptep = pgdir_walk(kern_pgdir, (void *)(base_addr + j * PGSIZE), 0);
				if (!ptep || *ptep == 0) { //can't find page table, or the pte hasn't been mapped.  
					cprintf("no page table or has not mapped");
					continue;
				}
				*ptep = (*ptep & 0xfffffe00) | (permission & 0x1ff);
			}
		}
	}
	return 0;
}	

int 
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	//check parameters
	if (argc != 3 && argc != 1) {
		cprintf("usage: showmappings or showmappings start_addr end_addr\n");
		return 0;
	}
	
	uintptr_t from_addr = 0, end_addr = 0xffffffff;
	
	if (argc == 3) {
		from_addr = (uintptr_t) parse_str2int(argv[1]);
		end_addr = (uintptr_t) parse_str2int(argv[2]);
	} 
	
	pde_t *kern_pgdir = get_curenv_or_kernel_pgdir();
	if (curenv && curenv->env_status != ENV_FREE)
		cprintf("env[%d] page table mapping:\n", curenv->env_id);
	else 
		cprintf("kernel page table mapping:\n");
	int i,j;
	for (i=PDX(from_addr); i<=PDX(end_addr); ) {
		int start_pdx = i, end_pdx;
		physaddr_t start_phys_addr = 0;
		uint16_t pde_permission;
		
		if (kern_pgdir[i] & PTE_P) {
			end_pdx = i;
			pde_permission = kern_pgdir[i] & 0xfff;
		} else {
			i++;
			continue;
		}
		
		//find the continuous dpes which have the same permission, and their pte's permission
		//should be the same.
		uint16_t pte_permission = 0;
		for (j=i; j<=PDX(end_addr); j++) {
			if ( (kern_pgdir[j] & PTE_P) && ((kern_pgdir[j] & 0xfff) == pde_permission)) {
				end_pdx = j;
				
				if (kern_pgdir[j] & PTE_PS) { //pde used as 4M page
					if (j==i) {
						start_phys_addr = kern_pgdir[j] & 0xffc00000;
						continue;
					}
					if (start_phys_addr + (j-i) * PTSIZE != (kern_pgdir[j] & 0xffc00000)) 
						break;
				} else {
					//check whether pte has the same permisson, and phys memory address
					// should be continuous.
					int k;
					int is_goto_outer = 0;
					for (k=0; k<NPTENTRIES; k++) {
						pte_t ptep = *pgdir_walk(kern_pgdir, (void *)(j * PTSIZE + k * PGSIZE), 0);
						if (!(ptep & PTE_P)) {
							is_goto_outer = 1;
							break;
						}
						if (j == i && k == 0) {
							pte_permission = ptep & 0xfff;
							start_phys_addr = PTE_ADDR(ptep);
							continue;
						}
						if ( ((ptep & 0xfff) != pte_permission) 
							 || ((start_phys_addr+(j-i)*PTSIZE+k*PGSIZE) != PTE_ADDR(ptep))  ) {
							is_goto_outer = 1;
							break;
						}
					}
					if (is_goto_outer)
						break;
				}
			} else {
				break;
			}
		}
		
		//pde show format:
		//[from_virtual-end_virtual]  PDE[from_index-end_index]      --------(privilege bits)
		assert(start_pdx <= end_pdx);
		char permission_desc[10];
		get_pte_permission_desc(pde_permission, permission_desc);
		if (start_pdx < end_pdx) {
			if (kern_pgdir[start_pdx] & PTE_PS) {
				cprintf("[%5.05x-%5.05x]  PDE[%3.03x-%3.03x] %s %5.05x-%5.05x\n", 
						(uint32_t)(start_pdx * PTSIZE) >> PGSHIFT,
						(uint32_t)( (end_pdx + 1) * PTSIZE - PGSIZE) >> PGSHIFT , 
						 start_pdx, end_pdx, permission_desc,
						(uint32_t)start_phys_addr >> PGSHIFT, 
						(uint32_t)(start_phys_addr + (end_pdx-start_pdx+1) * PTSIZE - PGSIZE) >> PGSHIFT);
			} else {
			
				cprintf("[%5.05x-%5.05x]  PDE[%3.03x-%3.03x] %s\n", (uint32_t)(start_pdx * PTSIZE) >> PGSHIFT,
						(uint32_t)( (end_pdx + 1) * PTSIZE - PGSIZE) >> PGSHIFT , start_pdx, end_pdx, permission_desc);
						
				//in this case, the pte's permission should be the the same.
				get_pte_permission_desc(pte_permission, permission_desc);
				cprintf("  [%5.05x-%5.05x]  PTE[%3.03x-%3.03x] %s %5.05x-%5.05x\n", (uint32_t)(start_pdx * PTSIZE) >> PGSHIFT,
					(uint32_t)( (end_pdx + 1) * PTSIZE - PGSIZE) >> PGSHIFT, 0, 0x3ff, permission_desc,
					(uint32_t)start_phys_addr >> PGSHIFT, 
					(uint32_t)(start_phys_addr + (end_pdx-start_pdx+1) * PTSIZE - PGSIZE) >> PGSHIFT);
			}
		} else {
			if (kern_pgdir[start_pdx] & PTE_PS) {
				cprintf("[%5.05x-%5.05x]  PDE[%3.03x] %s %5.05x\n", 
						(uint32_t)(start_pdx * PTSIZE) >> PGSHIFT,
						(uint32_t)( (end_pdx + 1) * PTSIZE - PGSIZE) >> PGSHIFT , 
						 start_pdx, permission_desc,
						(uint32_t)start_phys_addr >> PGSHIFT);
			} else {
				cprintf("[%5.05x-%5.05x]  PDE[%3.03x]     %s\n", (uint32_t)(start_pdx * PTSIZE) >> PGSHIFT
						, (uint32_t)( (end_pdx + 1) * PTSIZE - PGSIZE) >> PGSHIFT , start_pdx, permission_desc);
				show_pte_mappings(start_pdx, from_addr, end_addr);
			}
		}
	
		i = end_pdx + 1;
	}
	
	return 0;
}
		
int 
mon_dump(int argc, char **argv, struct Trapframe *tf)
{
	//parse arguments
	if (argc != 3 && argc != 4) {
		cprintf("usage: dump [-p|-v] start_addr end_addr\n");
		return 0;
	}
	
	int use_virtual = 1;
	uintptr_t start_addr = 0, end_addr = 0;
	
	if (argc == 4) {
		if (argv[1][0] == '-' && argv[1][1] == 'p' && argv[1][2] == 0) {
			use_virtual = 0;
		} else if (argv[1][0] == '-' && argv[1][1] == 'v' && argv[1][2] == 0) {
			use_virtual = 1;
		} else {
			cprintf("usage: dump [-p|-v] start_addr end_addr\n");
			return 0;
		}
		start_addr = (uintptr_t)parse_str2int(argv[2]);
		end_addr = (uintptr_t)parse_str2int(argv[3]);
	} else {
		start_addr = (uintptr_t)parse_str2int(argv[1]);
		end_addr = (uintptr_t)parse_str2int(argv[2]);
	}
	cprintf("start_addr = %8.08x, end_addr = %8.08x\n", start_addr, end_addr);
	end_addr -= 1;
	
	uint32_t mod = start_addr % (uint32_t)4;
	if (mod != 0)
		start_addr -= mod;
    mod = end_addr % (uint32_t)4;
	if ( mod != 3)
		end_addr += (3 - mod);
	
	cprintf("start_addr = %8.08x, end_addr = %8.08x\n", start_addr, end_addr);
	if (use_virtual)
		dump_virtual_mem(start_addr, end_addr);
	else {
		//check whether physaddr is valid
		extern size_t npages;
		uint32_t phys_mem_total = (uint32_t)(npages * PGSIZE);
		if (!(start_addr < phys_mem_total - 1 && end_addr < phys_mem_total - 1)) {
			cprintf("address extends max physical memory[%dK]", phys_mem_total / 1024);
			return 0;
		}
		dump_phys_mem(start_addr, end_addr);
	}
	return 0;
} 		

static void 
dump_virtual_mem (uintptr_t start_addr, uintptr_t end_addr)
{
	uintptr_t addr;
	for (addr = start_addr; addr <= end_addr; addr = (PDX(addr)+1) * PTSIZE) {
		int pdx = PDX(addr);
		uintptr_t base_addr = pdx * PTSIZE;
		uintptr_t pde_end_addr = (uint32_t)((pdx + 1) * PTSIZE - 1);
		dump_virtual_mem_per_pde(addr, pde_end_addr < end_addr ? pde_end_addr : end_addr);
	}
	cprintf("\n");
}

static void 
dump_phys_mem (physaddr_t start_addr, physaddr_t end_addr)
{
	dump_virtual_mem(KERNBASE + start_addr, KERNBASE + end_addr);
}

//dump memory contents for virtual address region [from_virtual, end_virtual].
//NOTE: [from_virtual, end_virtual] should be the same pde
static void 
dump_virtual_mem_per_pde(uintptr_t start_addr, uintptr_t end_addr)
{
	//cprintf("dump_virtual_mem_per_pde: start_addr = %8.08x, end_addr = %8.08x\n", start_addr, end_addr);
	int pdx = PDX(start_addr);
	assert(start_addr < end_addr);
	assert(end_addr < start_addr + PTSIZE);
	
	pde_t *kern_pgdir = get_curenv_or_kernel_pgdir();
	pde_t pde = kern_pgdir[pdx];
	
	if (pde == 0) { //haven't mapped
		cprintf("%8.08x-%8.08x: not mapped\n", start_addr, end_addr);
		return;	
	}

	uintptr_t base_addr = pdx * PTSIZE;
	if (pde & PTE_PS) {
		if (pde & PTE_P) {
			uint32_t start_offset = BIGPGOFF(start_addr), end_offset = BIGPGOFF(end_addr);
			dump_prefix_empty(start_addr);
			uint32_t i = start_offset;
			for (; i <= end_offset; i += 4) {
				dump_db (base_addr+i, *(uint32_t *)(base_addr + i));
			}
		} else {
			cprintf("[%8.08-%8.08x] page present bit is 0\n", start_addr, end_addr);
		}
	} else {
		uintptr_t addr = start_addr;
		dump_prefix_empty(start_addr);
		for (; addr <= end_addr; addr = base_addr+ (PTX(addr) + 1) * PGSIZE) {
			int ptx = PTX(addr);
			uintptr_t pte_end_addr = (uint32_t)(base_addr + (ptx + 1) * PGSIZE - 1);
			dump_virtual_mem_per_pte(addr,  pte_end_addr < end_addr ? pte_end_addr : end_addr);
		}
	}
}


//dump memory contents for virtual address region [start_addr, end_addr]. 
//the arguments are virtual addresses.
//NOTE: [from_virtual, end_virtual] should be in the same pte
		
static void 
dump_virtual_mem_per_pte(uintptr_t start_addr, uintptr_t end_addr)
{
	//cprintf("dump_virtual_mem_per_pte: start_addr = %8.08x, end_addr = %8.08x\n", start_addr, end_addr);
	assert(start_addr % (uint32_t)4 == 0);
	assert((uint32_t)(end_addr % (uint32_t)4) == 3);
	//int ptx = PTX(start_addr);
	//cprintf("(ptx + 1) * PGSIZE - 1 = %8.8x\n",(ptx + 1) * PGSIZE - 1);
	//cprintf("end_addr = %8.8x\n", end_addr);
	//assert( (ptx + 1) * PGSIZE - 1 >= end_addr);

	uintptr_t addr;
	for (addr = start_addr; addr <= end_addr; addr += 4) {
		dump_db(addr, *(uint32_t *)addr);
	}
	
}

static void 
dump_prefix_empty (uintptr_t start_addr)
{
	assert(start_addr % (uint32_t)4 == 0);
	uint32_t pos = start_addr / (uint32_t)4;
	assert(start_addr % (uint32_t)4 == 0);
	
	if (pos % (uint32_t)DW_COUNT_PER_LINE > 0){
		
		int j;
		uint32_t skiped_count = (pos % (uint32_t)DW_COUNT_PER_LINE);
		cprintf("%8.08x-%8.08x: ", start_addr - skiped_count * 4, (uint32_t)(start_addr - skiped_count * 4 + DW_COUNT_PER_LINE * 4));
		for (j=0; j < skiped_count; j++)
			cprintf("          "); //ten spaces
	}
}

static void 
dump_db (uintptr_t addr, uint32_t dw) 
{
	assert(addr % (uint32_t)4 == 0);
	uint32_t pos = addr / (uint32_t)4;
	if (pos % (uint32_t)DW_COUNT_PER_LINE == 0) {
		cprintf("%8.08x-%8.08x: ", addr, addr + DW_COUNT_PER_LINE * 4);
		cprintf("%8.08x  ",  dw);
	} else {
		cprintf("%8.08x  ",  dw);
	}
	
	if ((pos + 1) % (uint32_t)DW_COUNT_PER_LINE == 0) {
		cprintf("\n");
	}
}

static void
get_pte_permission_desc(uint16_t pte_permission, char *msg)
{
	snprintf(msg,
			 10,
			 "%c%c%c%c%c%c%c%c%c",
			 pte_permission & PTE_G ? 'G' : '-',
			 pte_permission & PTE_PS ? 'P' : '-',
			 pte_permission & PTE_D  ? 'D' : '-',
			 pte_permission & PTE_A  ? 'A' : '-',
			 pte_permission & PTE_PCD  ? 'C' : '-',
			 pte_permission & PTE_PWT  ? 'T' : '-',
			 pte_permission & PTE_U  ? 'U' : '-',
			 pte_permission & PTE_W  ? 'W' : '-',
			 pte_permission & PTE_P  ? 'P' : '-'
			 );
}


//print page table mapping for virtual address region [from_virtual, end_virtual] in page dirctory entry 'pdx'.
//NOTE: 1. it's not 4M Page
//      2. pdx memory region should have intersector with [from_virtual, end_virtual]
static void
show_pte_mappings(int pdx, uintptr_t from_virtual, uintptr_t end_virtual) 
{
	pde_t *kern_pgdir = get_curenv_or_kernel_pgdir();
	uintptr_t base_addr = pdx * PTSIZE;
	uint16_t pte_permission;
	
	int k, max_k;
	
	if (base_addr >= from_virtual)
		k = 0;
	else 
		k = PTX(from_virtual);
	if (end_virtual >= (int32_t)(base_addr + PTSIZE))
		max_k = NPTENTRIES-1;
	else 
		max_k = PTX(end_virtual);
	
	//cprintf("k=%d, max_k=%d\n", k, max_k);
	for (; k < NPTENTRIES && k <= max_k ;) {
		int start_ptx = k, end_ptx;
		physaddr_t from_addr, end_addr;
		
		pte_t ptep = *pgdir_walk(kern_pgdir, (void *)(base_addr + k * PGSIZE), 0);
		
		if (!ptep) {
			k++;
			continue;
		}
			
		from_addr = PTE_ADDR(ptep);
		pte_permission = ptep & 0xfff;
		
		int n;
		for (n=k+1; n<NPTENTRIES && n <= max_k; n++) {
			pte_t ptep2 = *pgdir_walk(kern_pgdir, (void *)(base_addr + n * PGSIZE), 0);
			if ( (ptep2 & 0xfff) != pte_permission )
				break;
		}
		
		end_ptx = n-1;
		end_addr = PTE_ADDR(*pgdir_walk(kern_pgdir, (void *)(base_addr + end_ptx * PGSIZE), 0));
		//pte show format:
		// [from_virtual-end_virtual] PTE[from_index-end_index] -------- from_phys-end_phys
		char permission_desc[10];
		get_pte_permission_desc(pte_permission, permission_desc);
		if (start_ptx < end_ptx) {
			cprintf("  [%5.05x-%5.05x]  PTE[%3.03x-%3.03x] %s %5.05x-%5.05x\n", (uint32_t)(base_addr + start_ptx * PGSIZE) >> PGSHIFT,
				(uint32_t)(base_addr + end_ptx * PGSIZE) >> PGSHIFT, start_ptx, end_ptx, permission_desc,
				from_addr >> PGSHIFT, end_addr >> PGSHIFT);
		} else {
			cprintf("  [%5.05x-%5.05x]  PTE[%3.03x]     %s %5.05x\n", (uint32_t)(base_addr + start_ptx * PGSIZE) >> PGSHIFT,
				(uint32_t)(base_addr + end_ptx * PGSIZE) >> PGSHIFT, start_ptx, permission_desc,
				from_addr >> PGSHIFT);
		}
		
		k = end_ptx + 1;
	}
}

static int 
parse_str2int(char *str)
{
	//TODO: should check str format
	int value;
	if (strlen(str) > 2 && str[0] == '0' && str[1] == 'x') 
		value = atoi(str,16); 
	else 
		value = atoi(str,10);
	return (uint32_t)value;
}


//conert to a numberic string to decimal, numberic can be hex string '0x12ff' 
//or decmal '1234', you can use base to speicify.
static int 
atoi(char *str, int base) 
{
	if (base == 16)
		str += 2; //skip 0x
	int value = 0;
	int i, count = strlen(str);
	for (i=0; i<strlen(str); i++) {
		char ch = str[i];
		int ch_num = 0;
		if (ch >= '0' && ch <= '9')
			ch_num = ch - '0';
		else
			ch_num = ch - 'a' + 10;
		
		int index = count - 1 - i;	
		if (base == 16)
			value += (ch_num << (4 * index));
		else if (base == 10) {
			while (index > 0) {
				ch_num *= 10;
				index--;
			}
			value += ch_num;
		};
	}
	return value;
}




/* why mon_backtrace need the three arguments? because mon_* funcitons have the three function.
 * they are the value of 'func' field of struct Command */
int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	//read ebp, eip, and get the arguments for mon_backtrace
	unsigned int *ebp = 0;
	unsigned int eip, last_ebp, arg1, arg2, arg3, arg4, arg5;
	int start = 1; 
	struct Eipdebuginfo eip_debug_info;
	//read the last ebp, and if ebp doesn't equal 0, then print 
	//the information(ebp, eip, and so on).
	do{
		if (start) {
			ebp = (unsigned int *)read_ebp();
			start = 0;
		} else {
			ebp = (unsigned int *)last_ebp;
		}
		last_ebp = *ebp;
		eip = *(ebp + 1);
		arg1 = *(ebp + 2);
		arg2 = *(ebp + 3);
		arg3 = *(ebp + 4);
		arg4 = *(ebp + 5);
		arg5 = *(ebp + 6);
		
		//output format: ebp f0109e58  eip f0100a62  args 00000001 f0109e80 f0109e98 f0100ed2 00000031
		cprintf("ebp %8.08x  eip %8.08x  args %8.08x %8.08x %8.08x %8.08x %8.08x\n",
				ebp,eip,arg1,arg2,arg3,arg4,arg5);
		
		if( debuginfo_eip(eip, &eip_debug_info) == 0 ){
			//output format:        kern/monitor.c:143: monitor+106
			cprintf("       %s:%d: ", eip_debug_info.eip_file, eip_debug_info.eip_line);
			cprintf("%.*s", eip_debug_info.eip_fn_namelen, eip_debug_info.eip_fn_name);
			//cprintf("+%u\n", *((unsigned int *)eip_debug_info.eip_fn_addr));
			cprintf("+%u\n", eip_debug_info.eip_fn_addr);
		} else {
			cprintf("       can't find line info!\n");
		}
	} while (last_ebp != 0);
	cprintf("mon_backtrace end!\n");
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}

static pde_t *
get_curenv_or_kernel_pgdir()
{
	extern pde_t *kern_pgdir;
	if (curenv && curenv->env_status != ENV_FREE)
		return curenv->env_pgdir;
	return kern_pgdir;
}
