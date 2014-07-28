// hello, world
#include <inc/lib.h>
#include <inc/x86.h>

void
umain(int argc, char **argv)
{
	breakpoint();
	cprintf("hello, world\n");
	cprintf("i am environment %08x\n", thisenv->env_id);
}
