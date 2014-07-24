#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>


// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle;
	int i;

	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING) and never choose an
	// idle environment (env_type == ENV_TYPE_IDLE).  If there are
	// no runnable environments, simply drop through to the code
	// below to switch to this CPU's idle environment.

	// LAB 4: Your code here.
	struct Env *next_env = NULL;
	int find_start_i = -1;
	if (!curenv)
		find_start_i = 0;
	else 
		find_start_i = (curenv - envs) + 1;
		
	//cprintf("find_sart_i = %d\n", find_start_i);
	
	int find_count;
	for (find_count = 0, i = find_start_i; find_count < NENV - 1; i++, find_count++) {
		i = i % NENV;
		if (envs[i].env_status == ENV_RUNNABLE 
		 && envs[i].env_type != ENV_TYPE_IDLE) {
			next_env = envs + i;
			break;
		}
	}
	
	if (next_env == NULL && curenv && curenv->env_status == ENV_RUNNING 
			&& curenv->env_cpunum == thiscpu->cpu_id) 
		next_env = curenv;
		
	if (next_env) 
		env_run(next_env);  //not return
	
	//cprintf("not found next env\n");

	// For debugging and testing purposes, if there are no
	// runnable environments other than the idle environments,
	// drop into the kernel monitor.
	// NOTE: because of receive interrupt, we must jump into ENV_TYPE_IDLE.
	// Otherwise, when there is no env running, and packet receive, but in
	// kernel mode, we can't receive hardware interrupt.
	for (i = 0; i < NENV; i++) {
		if ( //envs[i].env_type != ENV_TYPE_IDLE &&
		    (envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING))
			break;
	}
	if (i == NENV) {
		cprintf("No more runnable environments!\n");
		while (1)
			monitor(NULL);
	}

	// Run this CPU's idle environment when nothing else is runnable.
	idle = &envs[cpunum()];
	if (!(idle->env_status == ENV_RUNNABLE || idle->env_status == ENV_RUNNING))
		panic("CPU %d: No idle environment!", cpunum());
	env_run(idle);
}
