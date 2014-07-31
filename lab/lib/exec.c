// implement exec from user space

#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/lib.h>

int 
execve(const char *filename, char *const argv[], char *const envp[])
{
	// check whether filename exist
	
	// umap all the page under UTOP except user & exception stack,
	// and the page where execve is in. (Assume execve is in one page, and
	// it doesn't do any function call but system call)
	
	// setup stack for 'new' process. Fist, for simplity, we just make 
	// a new empty stack for new process.
	
	// read the file contents from fs. the addr of file context may conflict
	// with the addr of execve. How to deal this situation? First, we just
	// assume the situation won't happen.
	
	// setup entry point for new process.
	
	// request kernel to set new trap frame, so we can jump to entry point
	// of new image.
	// before that, we should unmap the page where execve is in. How to
	// umap? If I unmap, I think we will encounter page fault. So, ...,
	// first, just left the page.
	
	
	
	
} 
