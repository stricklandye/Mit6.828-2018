// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint: 
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).
	
	// LAB 4: Your code here.
	uint32_t err = utf->utf_err;
	uint32_t addr = utf->utf_fault_va;
	int cur_proc = sys_getenvid(),result;
	if(!(err & FEC_WR) && (uvpt[PGNUM(addr)] & PTE_COW)) {
		/*
			根据lab网页中里面说的,pgfault()如果不是因为写入一个PTE_COW的page而引发的错误的话，就panic
			所以前面 !,取反
			PGNUM这个宏的作用是根据给出的虚拟地址获得，来获得他是第几个页表项,具体的意思看一下注释就可以
			uvpt[]这个数组定义在memlayout.h中，它的作用是获得page table entry,注释里面说到
			第N个page table entry就是uvpt[N],这样一来两者就可以很好的结合了。获得page table entry我们就可以判断
			是不是PTE_COW
		*/
		panic("pgfault():the fault is neither caused by write nor accessed the page that marked PTE_COW\n");
	}
	// panic("pgfault");
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.

	// 课程网页中的描述，为一个暂时的虚拟地址(PFTEMP)分配一个page
	result = sys_page_alloc(cur_proc,(void*)PFTEMP,PTE_U | PTE_W | PTE_P);
	if(result < 0) {
		panic("pgfault():allocating page for temporary location of current running process failed\n");
	} 
	//为暂时的地址分配页后，将造成page fault的地址对应的内容复制到临时地址去
	memcpy(PFTEMP,(const void*)ROUNDDOWN(addr,PGSIZE),PGSIZE);
	
	//这里要做的就是将虚拟地址以及引起错误的地址映射到相同的地方去，sys_page_map()函数完成的就是做这件事
	//另外，我们要让page是read/write的
	result = sys_page_map(cur_proc,(void *)PFTEMP,cur_proc,(void*)ROUNDDOWN(addr,PGSIZE),PTE_U | PTE_W | PTE_P);
	if(result < 0) {
		panic ("pgfault():mapping failed\n");
	}

	//然后，这个虚拟地址我们未来还是要用的，所以很自然地，我们需要取消mapping
	result = sys_page_unmap(cur_proc,PFTEMP);
	if (result < 0)
	{
		panic("pgfault():unmap virtual address PFTEMP failed\n");
	}
	
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{	// envid:子进程的id, pn: page number,页号,并不是真正的页地址
	//这个函数用于将page pn的内容复制到目标进程envid的address space当中去
	// LAB 4: Your code here.
	int result;
	void* addr = (void *)(pn * PGSIZE); //将页号转为地址
	envid_t cur_proc = sys_getenvid(); 	//父进程
	if(uvpt[pn] & PTE_COW || uvpt[pn] & PTE_W) {
		/*
		这里的逻辑是:一开始,我们的父进程中肯定有一部分的内存是可写入的,比如说栈.
		当我们创建子进程后,这一块内存对应的物理页不再是父进程独有了.所以原来在父进程中writable的page
		都要被标记为copy-on-write.
		于是,下面的代码就非常好解释了.
		*/

		//将父进程中addr对应的映射关系复制到envid(子进程)去,并且标记为copy-on-write
		result = sys_page_map(cur_proc,addr,envid,addr,PTE_COW | PTE_U | PTE_P);
		if(result < 0) {
			panic("duppage():fail to map the copy-on-write into address space of the child\n");
		}
		//父进程中可写入的页不再是它独有了,所以也要在父进程中标记为copy-on-wirte
		result = sys_page_map(cur_proc,addr,cur_proc,addr,PTE_COW | PTE_U | PTE_P);
		if(result < 0) {
			panic("duppage():fail to remap page copy-on-write in parent address space\n");
		}
	} else {
		//如果不是writeable的,那么简单了,直接复制就行
		result = sys_page_map(cur_proc,addr,envid,addr,PTE_U | PTE_P);
		if (result < 0)
		{
			panic("duppage():fail to copy mapping at addr of parent's address space to child\n");
		}
		
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
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	set_pgfault_handler(pgfault);
	envid_t child = sys_exofork();
	uint32_t addr;
	int result;

	if(child < 0 ) 
		panic("fork():failed to create child process");
	if(child == 0) {
		/*
			至于为什么会有两个不同的返回值，已经讲过了。这里我们需要修改thisenv,
			因为这个代码是会在父进程和子进程中分别执行的，所以thisenv会代表不同的进程。
		*/
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	
	for(addr = 0; addr < USTACKTOP; addr += PGSIZE) {
		//并不是0-USTACKTOP所有的地址内容都要被复制到子子进程当中去，我们只复制PTE_P且PTE_U的
		if((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_U))
			duppage(child,PGNUM(addr));
	}

	
	//根据页面描述，exception stack不能复制，要重新申请一个page
	void* stack_addr = (void *)(UXSTACKTOP - PGSIZE);
	int perm = PTE_W | PTE_U | PTE_P;
	if((result = sys_page_alloc(child,stack_addr,perm) < 0)) {
		panic("fork():failed to allocate exception stack for child process");
	}

	//设置子进程的page fault handler
	extern void _pgfault_upcall();
	sys_env_set_pgfault_upcall(child, _pgfault_upcall);

	//标记子进程为runnable
	if((result = sys_env_set_status(child,ENV_RUNNABLE))) {
		panic("sys_env_set_status: %e", result);
	}
	return child;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
