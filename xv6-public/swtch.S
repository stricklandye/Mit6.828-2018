# Context switch
#
#   void swtch(struct context **old, struct context *new);
# 
# Save the current registers on the stack, creating
# a struct context, and save its address in *old.
# Switch stacks to new and pop previously-saved registers.

.globl swtch
swtch:
  movl 4(%esp), %eax # address of old context,old context is empty now 
  movl 8(%esp), %edx # address of new context

  # Save old callee-saved registers
  # because call instrucation will push eip into stack,so 
  # we do not push eip here
  pushl %ebp
  pushl %ebx
  pushl %esi
  pushl %edi

  # Switch stacks
  movl %esp, (%eax) # save the old address to the (%eax), now we have created the old context
  movl %edx, %esp   # 将new context的起始地址给esp,那么弹出寄存器就到了新进程

  # Load new callee-saved registers
  popl %edi
  popl %esi
  popl %ebx
  popl %ebp
  ret # 跳转到新进程
