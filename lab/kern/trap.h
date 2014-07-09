/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_TRAP_H
#define JOS_KERN_TRAP_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/trap.h>
#include <inc/mmu.h>

/* The kernel's interrupt descriptor table */
extern struct Gatedesc idt[];
extern struct Pseudodesc idt_pd;

void trap_init(void);
void trap_init_percpu(void);
void print_regs(struct PushRegs *regs);
void print_trapframe(struct Trapframe *tf);
void page_fault_handler(struct Trapframe *);
void breakpoint_exception_handler(struct Trapframe *);
void backtrace(struct Trapframe *);

void handler0(void);
void handler1(void);
void handler2(void);
void handler3(void);
void handler4(void);
void handler5(void);
void handler6(void);
void handler7(void);
void handler8(void);
//void handler9(void);
void handler10(void);
void handler11(void);
void handler12(void);
void handler13(void);
void handler14(void);
//void handler15(void);
void handler16(void);
void handler17(void);
void handler18(void);
void handler19(void);
void handler48(void);

#endif /* JOS_KERN_TRAP_H */
