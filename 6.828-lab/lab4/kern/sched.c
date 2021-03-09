#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle;

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
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.

	// LAB 4: Your code here.
	// cprintf("enter yield");
	struct Env* current_proc = thiscpu->cpu_env; //当前cpu正在运行的进程
	int startid = (current_proc) ? ENVX(current_proc->env_id) : 0; //返回在当前CPU运行的进程ID
	int next_procid;
	for(int i = 0; i < NENV; i++) {
		// 找到当前进城之后第一个状态为RUNNABLE的进程
		next_procid = (startid+i) % NENV;
		if(envs[next_procid].env_status == ENV_RUNNABLE) {
			env_run(&envs[next_procid]); //运行新的进程
		}
	}

	//注释里面说到了，不能将当前正在运行的进程运行到别的CPU上。如果之前运行在当前CPU的进程仍然在运行
	//且没有其他可以runnable的进程，那么就继续运行原来的进程
	if(envs[startid].env_status == ENV_RUNNING && envs[startid].env_cpunum == cpunum()) {
		env_run(current_proc); //继续运行原来的进程
	}
	// struct Env *now = thiscpu->cpu_env;
    // int32_t startid = (now) ? ENVX(now->env_id): 0;
    // int32_t nextid;
    // size_t i;
    // // 当前没有任何环境执行,应该从0开始查找
    // for(i = 0; i < NENV; i++) {
    //     nextid = (startid+i)%NENV;
    //     if(envs[nextid].env_status == ENV_RUNNABLE) {
    //             env_run(&envs[nextid]);
    //         }
    // }
    
    // // 循环一圈后，没有可执行的环境
    // if(envs[startid].env_status == ENV_RUNNING && envs[startid].env_cpunum == cpunum()) {
    //     env_run(&envs[startid]);
    // }
    
    // sched_halt never returns
    sched_halt();
}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	int i;

	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		while (1)
			monitor(NULL);
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile (
		"movl $0, %%ebp\n"
		"movl %0, %%esp\n"
		"pushl $0\n"
		"pushl $0\n"
		// Uncomment the following line after completing exercise 13
		"sti\n"
		"1:\n"
		"hlt\n"
		"jmp 1b\n"
	: : "a" (thiscpu->cpu_ts.ts_esp0));
}

