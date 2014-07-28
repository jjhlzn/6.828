// implement fork from user space

#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800
extern void _pgfault_upcall(void);
//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	pte_t pte = vpt[(uint32_t)addr >> PGSHIFT];
	if (!(err & FEC_WR))
		panic("not caused by write access, addr = %08x, eip = %08x, err = %08x!", addr, utf->utf_eip, err);
	
	if (!(pte & PTE_COW))
		panic("the page of 0x%08x is not PTE_COW, eip = %08x\n", addr, utf->utf_eip);
	
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// invoke sys_page_map() which will invoke page_insert(), and in page_insert(), 
	// If there is already a page mapped at 'va', it should be page_remove()d.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	if ((r = sys_page_alloc(0, (void *)PFTEMP, PTE_P | PTE_U | PTE_W)) < 0) 
		panic("pgfault: sys_page_alloc return error - %e", r);
		
	//copy the copy-on-write page to private page
	memmove((void *)PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	
	//TODO: delete the old page's mapping, whys it's not needed??
	//if ((r = sys_page_unmap(0, ROUNDDOWN(addr, PGSIZE))) < 0)
	//	panic("pgfault: sys_page_unmap return error = %e", r);
	
	//map new page to old address
	if ((r = sys_page_map(0, (void *)PFTEMP, 0, ROUNDDOWN(addr, PGSIZE), 
				PTE_P | PTE_U | PTE_W)) < 0)
		panic("pgfault: sys_page_map return error - %e", r);
	
	//unmap new page to PFTEMP
	if ((r = sys_page_unmap(0, (void *)PFTEMP)) < 0)
		panic("pgfault: sys_page_unmap return error = %e", r);
	//cprintf("pgfault: done\n");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
// answer: my guess. because of concurrency, now it's in user space. maybe other "thread"
// of the env or kernel will change the mapping not copy-on-write. so do it again is safe.
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	uintptr_t addr = pn * PGSIZE;
	//cprintf("copy %08x page table mapping\n", addr);
	pte_t pte = vpt[pn];
	
	if (addr + PGSIZE == UXSTACKTOP)  //user exception stack is a except
		return 0; 

	if (pte & PTE_SHARE) {
		if ((r = sys_page_map(0, (void *)addr, envid, (void *)addr, pte & PTE_SYSCALL) < 0))
			panic("sys_page_map return eror - %e", r);
	} else if ((pte & PTE_W) || (pte & PTE_COW)) {
		if ((r = sys_page_map(0, (void *)addr, envid, (void *)addr, PTE_U | PTE_P | PTE_COW)) < 0)
			panic("sys_page_map (map child) return error - %e", r);
			
		if ((r = sys_page_map(0, (void *)addr, 0, (void *)addr, PTE_U | PTE_P | PTE_COW)) < 0)
			panic("sys_page_map (map father) return error - %e", r);
	} else {
		if ((r = sys_page_map(0, (void *)addr, envid, (void *)addr, pte & PTE_SYSCALL)) < 0)
			panic("duppage: sys_page_map (map child) return error - %e", r);
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
extern void (*_pgfault_handler)(struct UTrapframe *utf);
envid_t
fork(void)
{
	// LAB 4: Your code here.
	int i, r;
	
	set_pgfault_handler(pgfault);
	
	if ((r = sys_exofork()) < 0)
		panic("fork: sys_exofork return error - %e", r);
	envid_t child_envid = (envid_t)r;
	
	if (child_envid > 0) { //father
		// copy page table mapping below UTOP if pte is present
		//cprintf("[%08x] setup child env\n", thisenv->env_id);
		for (i = 0; i < UTOP / PGSIZE; i++) 
			if ((vpd[i/1024] & PTE_P) && (vpt[i] & PTE_P))
				duppage(child_envid, i);
		
		if ((r = sys_page_alloc(child_envid, (void *)(UXSTACKTOP - PGSIZE), PTE_P | PTE_U | PTE_W)) < 0)
			panic("fork: sys_page_alloc - %e",r);
			
		if ((r = sys_env_set_pgfault_upcall(child_envid, _pgfault_upcall)) < 0)
			panic("fork: sys_env_set_pgfault_upcall (for child) return error - %e", r);
					
		if ((r = sys_env_set_status(child_envid, ENV_RUNNABLE)) < 0)
			panic("fork: sys_env_set_status return error - %e", r);
	} else {  //child
		thisenv = &envs[ENVX(sys_getenvid())];
		//cprintf("[%08x] child start running\n", thisenv->env_id);
	}
	
	return child_envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
