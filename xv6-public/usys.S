#include "syscall.h"
#include "traps.h"

# 将fork()，wait()封装为库文件。给用户程序调用
# .globl 表示全局的，这样才可用让用户程序调用
# 在eax中放的是系统号，在进行拼接，就转为了syscall.h中的宏定义
# 如SYS_fork,在使用int指令来发起中断号为64的系统调用中断
# 每一号的中断都在vectors.pl进行进一步封装

# 用户程序调用中断的基本过程为：
# push args1 
# push argrs2 ... 压入参数
# call syscall_wrapper 这个基本上书封装好的系统调用函数,如果fork()
# syscall_wrapper中执行下面的代码:
#   mov 系统调用号,eax
#   int syscall <---- 在这里之后，进行栈切换，换到内核栈中执行中断代码
# 因此，在用户栈中,esp == 返回的eip，esp + 4 = 第一个参数，esp + 8 = 第二个参数
# xv6中的参数获取函数argint都是基于这个方法来从栈当中获取参数
#define SYSCALL(name) \
  .globl name; \
  name: \
    movl $SYS_ ## name, %eax; \
    int $T_SYSCALL; \
    ret

SYSCALL(fork)
SYSCALL(exit)
SYSCALL(wait)
SYSCALL(pipe)
SYSCALL(read)
SYSCALL(write)
SYSCALL(close)
SYSCALL(kill)
SYSCALL(exec)
SYSCALL(open)
SYSCALL(mknod)
SYSCALL(unlink)
SYSCALL(fstat)
SYSCALL(link)
SYSCALL(mkdir)
SYSCALL(chdir)
SYSCALL(dup)
SYSCALL(getpid)
SYSCALL(sbrk)
SYSCALL(sleep)
SYSCALL(uptime)
