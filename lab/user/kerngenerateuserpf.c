#include <inc/lib.h>

struct List {
	int member;
	struct List *next;
};

#define MAX_CHAT_COUNT (PGSIZE / sizeof(struct List))
struct List free_list_elems[MAX_CHAT_COUNT]
__attribute__ ((aligned(PGSIZE)));


volatile int a;
void child_loop()
{
	
	while(1) {
		/* nothing */;
		envid_t who;
		//ipc_recv(&who, 0, 0);
	    a = free_list_elems[0].member;
		cprintf("[%08x]: free_list_elems[0].member  = %d\n", thisenv->env_id, a);
	}
}

void
umain(int argc, char **argv)
{
	int pid;
	if ((pid = fork()) < 0)
		panic("fork error");
		
	if (pid == 0) {
		child_loop();
	}
	
	cprintf("free_list_elems = %08x\n",free_list_elems);
	free_list_elems[0].member = 10; 
	//cancle page-on-write on free_list_elems
	if (sys_page_map(0, free_list_elems, 0, 
			free_list_elems, PTE_U | PTE_P | PTE_W) < 0)
		panic("sys_page_map return error!"); 
	if (sys_page_map(0, free_list_elems, pid, 
			free_list_elems, PTE_U | PTE_P) < 0)
		panic("sys_page_map return error!");
	
	
	//asm volatile ("int $3");
	free_list_elems[0].member = 11; 
	cprintf("[%08x]: free_list_elems[0].member  = %d\n", thisenv->env_id, free_list_elems[0].member);
	//ipc_send(pid, 0, 0, 0);
	//asm volatile ("int $3");
	while(1)
		;
}
