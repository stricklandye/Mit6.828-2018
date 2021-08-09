#include "mmu.h"

  # vectors.S sends all traps here.
  # vectors.pl会生成一个vector.S将所有的中断都重定向到
  # alltraps来执行，构建trapframe

  # 用户栈地址esp,中断号，eip等地址会由中断的时候
  # 由处理器压入，下面的内容要我们手动压入到栈当中，注意
  # 当代码执行到这里的时候，已经是内核栈了。我们在内核栈中构建
  # struct trapframe
.globl alltraps
alltraps:
  # Build trap frame.
  pushl %ds
  pushl %es
  pushl %fs
  pushl %gs
  pushal
  
  # Set up data segments.
  # 将数据段寄存器都修改为内核级别的,cpl=0
  movw $(SEG_KDATA<<3), %ax
  movw %ax, %ds
  movw %ax, %es

  # Call trap(tf), where tf=%esp
  # 此时的esp压入到栈中，将作为trap(trapframe *tf)的参数
  pushl %esp
  call trap
  addl $4, %esp

  # Return falls through to trapret...
.globl trapret
trapret:
  popal
  popl %gs
  popl %fs
  popl %es
  popl %ds
  addl $0x8, %esp  # trapno and errcode
  iret
