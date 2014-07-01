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

#define CMDBUF_SIZE	80	// enough for one VGA text line

static void get_pte_permission_desc(uint16_t pte_permission, char *msg);
static void show_pte_mappings(int pdx);

struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "showmappings", "Display virtual memory mapping", mon_showmappings }
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

int 
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	//check parameters
	if (argc != 3 && argc != 1) {
		cprintf("usage: showmappings or showmappings start_addr end_addr\n");
		return -1;
	}
	
	uint32_t from_addr, end_addr;
	if (argc == 0) {
		from_addr = 0;
		end_addr = 0xffffffff;
	} else if (argc == 2) {
		//TODO: parse parameters
		panic("not implemented!\n");
		return -1;
	}
	
	extern pde_t *kern_pgdir;
	int i,j;
	for (i=PDX(from_addr); i<=PDX(end_addr); ) {
		int start_pdx = i, end_pdx;
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
				
				//check whether pte has the same permisson 
				int k;
				for (k=0; k<NPTENTRIES; k++) {
					pte_t *ptep = pgdir_walk(kern_pgdir, (void *)(j * PTSIZE + k * PGSIZE), 0);
					assert(ptep);
					if (j == i && k == 0) {
						pte_permission = *ptep & 0xfff;
						continue;
					}
					if ( (*ptep & 0xfff) != pte_permission  ) 
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
			cprintf("[%5.5x-%5.5x]  PDE[%3.3x-%3.3x] %s\n", (uint32_t)(start_pdx * PTSIZE) >> PGSHIFT
				    , (uint32_t)( (end_pdx + 1) * PTSIZE - PGSIZE) >> PGSHIFT , start_pdx, end_pdx, permission_desc);
				    
			//in this case, the pte's permission should be the the same.
			int k;
			for (k=start_pdx; k<=end_pdx; k++)
				show_pte_mappings(k);
		} else {
			cprintf("[%5.5x-%5.5x]  PDE[%3.3x]     %s\n", (uint32_t)(start_pdx * PTSIZE) >> PGSHIFT
				    , (uint32_t)( (end_pdx + 1) * PTSIZE - PGSIZE) >> PGSHIFT , start_pdx, permission_desc);
			show_pte_mappings(start_pdx);
		}
		
		
		
		i = end_pdx + 1;
	}
	
	return 0;
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

static void
show_pte_mappings(int pdx) 
{
	extern pde_t *kern_pgdir;
	uintptr_t base_addr = pdx * PTSIZE;
	uint16_t pte_permission;
	int k;
	for (k=0; k<NPTENTRIES; k++) {
		int start_ptx = k, end_ptx;
		physaddr_t from_addr, end_addr;
		
		pte_t *ptep = pgdir_walk(kern_pgdir, (void *)(base_addr + k * PGSIZE), 0);
		assert(ptep);
		
		if (!(*ptep & PTE_P))
			continue;
			
		from_addr = PTE_ADDR(*ptep);
		pte_permission = *ptep & 0xfff;
		
		int n;
		for (n=k+1; n<NPTENTRIES; n++) {
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
			cprintf(" [%5.5x-%5.5x] PTE[%3.3x-%3.3x] %s\n %5.5x-%5.5x", (uint32_t)(base_addr + start_ptx * PGSIZE) >> PGSHIFT,
				(uint32_t)(base_addr + end_ptx * PGSIZE) >> PGSHIFT, start_ptx, end_ptx, permission_desc,
				from_addr, end_addr);
		} else {
			cprintf(" [%5.5x-%5.5x] PTE[%3.3x]     %s\n %5.5x", (uint32_t)(base_addr + start_ptx * PGSIZE) >> PGSHIFT,
				(uint32_t)(base_addr + end_ptx * PGSIZE) >> PGSHIFT, start_ptx, permission_desc,
				from_addr);
		}
		
		k = end_ptx + 1;
	}
}

/* why mon_backtrace need the three arguments? */
int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	//read ebp, eip, and get the arguments for mon_backtrace
	unsigned int *ebp = 0;
	unsigned int eip, last_ebp, arg1, arg2, arg3, arg4, arg5;
	int start = 1; 
	struct Eipdebuginfo eip_debug_info;
	//read the last ebp, and if ebp doesn't equal 0, then print the information(ebp, eip, and so on).
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
		cprintf("ebp %8.08x  eip %8.08x  args %8.08x %8.08x %8.08x %8.08x %8.08x\n",ebp,eip,arg1,arg2,arg3,arg4,arg5);
		
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
	//mon_backtrace(0, 0, 0);

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
