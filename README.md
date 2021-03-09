# Mit6.828-2018

过去很长一段时间都想自己写一个操作系统，走了不少弯路。花了很多时间在学习《30天自制操作系统》，学到后面发现作者讲述的有点绕了，代码也没有讲明白。到最后还是选择了MIT6.828，这门课的难度比较大，要求对x86保护模式下的变成要比较了解。慢慢跟下来，还是受益匪浅的。**我没有做完所有的实现，我就做到了lab5。不知道何种原因，lab6代码无法运行，所以我就没做了,而且lab6是实现内核与一个web server通信的，和内核的其他内容比起来（比如说进程，管道,shell等等）不怎么相关**。几个lab做下来，对于系统如何引导启动，页式内存的管理，文件系统，进程相关内容都有了更深刻的了解。

### 课程介绍

课程地址:[MIT6.828](https://pdos.csail.mit.edu/6.828/2018/schedule.html)

通过这个课可以加深对内核的理解，并且还需要实现lab。每一个lab都有给出代码，其中预留了代码来填空。

* 为什么学的是2018而不是2020？

  2020我看了下好像用的是RISC-V，我不了解这个。所以还是选择老版本的课程。

* 如何学习？

  建议事先先稍微学习下x86的保护模式以及x86的汇编。准备工作做好了以后，就直接按照课程schedule来做就行。多阅读xv6 book，xv6 book主要是对xv6代码的讲解。和lab不是非常相关，但是原理是通用的，xv6 book多看看。（不过xv6 book有些地方好难懂，可能是我水平太低了）。

**PS：**
学习x86保护模式的内容可以参考《x86-实模式到保护模式》这本书，网上有它的pdf和源代码。mit6.828中涉及到的保护模式只是很多都提到了。但是APIC什么的没有说到，不过我认为这本书仍然值得一看。《30天实现操作系统》这本我觉得不咋地，首先作者的代码质量一般，越到后面对于代码的解释也有点模棱两可。可以直接选择不看，不过其中的一些保护模式的知识，ELF文件的解释可以稍微参考下。看前几章我感觉差不多了。另外一本书比较知名的就是《操作系统真相还原》了，我没有看过，暂且不做评论。

### XV6 以及JOS

xv6是一个正儿八经的UNIX-like内核，学习xv6的代码能够加深我们对UNIX内核的理解。比如说文件描述符的实现，管道，文件系统等等内容。而在实验当中，我们用的是JOS的内核。**我在xv6中加入了一些注释，重点是在文件系统这块，因为lab中所使用的JOS的文件系统不是很贴近现实文件系统设计，所以我花了不少时间来研究xv6的源代码。**

### 环境

1.  电脑系统：任意的Linux系统都可以。我最开始用的是ubuntu,后来换到deepin去了。不过问题不大

2.  虚拟机：课程里面用的是QEMU，另外一个比较有名的就是bochs了。推荐安装[这里](https://pdos.csail.mit.edu/6.828/2018/tools.html)提到的patched QEMU，在后面实验中更好一些。

#### 笔记

下面是一些所有lab的地址，我就做到了Lab5，然后有些作业没做。下面是lab的一些记录：\
**Lab1:**\
[Lab1 PartA Bootstrap](https://www.jianshu.com/p/21ed0b5fa390) \
[Lab1 PartB The Boot Loader](https://www.jianshu.com/p/57b0d65db7ff)\
[Lab1 PartC The kernel](https://www.jianshu.com/p/3835ecaa42ff) \
**Lab2:**\
[Lab2 PartA Physical Memory Management](https://www.jianshu.com/p/fabf1b8bba60) \
[Lab2 PartB Virtual Memory](https://www.jianshu.com/p/fabf1b8bba60) \
[Lab2 PartC Kernel Address Space](https://www.jianshu.com/p/2f1c2431fefe) \
**Lab3:**\
[Lab3 PartA User Environments and Exception Handling](https://www.jianshu.com/p/8218a35a3660) \
[Lab3 PartB  Page Faults, Breakpoints Exceptions, and System Calls](https://www.jianshu.com/p/cd46deeca049) \
**Lab4:**\
[Lab4 PartA Multiprocessor Support and Cooperative Multitasking](https://www.jianshu.com/p/7fe917d603cd) \
[Lab4 PartB Copy-on-Write fork](https://www.jianshu.com/p/baf21b2abb54)\
[Preeptive Multitasking and Inter-Process communication](https://www.jianshu.com/p/dc08a11296e8)\
**Lab5:**\
[Mit6.828 lab5: File system,Spawn  and Shell](https://www.jianshu.com/p/a20a9a0f9a2e)

HomeWork没有全部做完，memory barriers什么的没做.\
**HomeWork:**\
[HomeWork1 Boot Xv6](https://www.jianshu.com/p/6b7b9ac26308)\
[HomeWork2 Shell](https://www.jianshu.com/p/6aa1df3a7ebb)\
[HomeWork3 System call](https://www.jianshu.com/p/c4f3bbf00957)\
[HomeWork4 Lazy Page Allocation](https://www.jianshu.com/p/3df405ed382c)\
[HomeWork5 System Alarm](https://www.jianshu.com/p/c75ead0ad973)