/*
 * Minimal PIO-based (non-interrupt-driven) IDE driver code.
 * For information about what all this IDE/ATA magic means,
 * see the materials available on the class references page.
 */

#include "fs.h"
#include <inc/x86.h>

#define IDE_BSY		0x80 // => 1000_0000b,判断硬盘是否在执行一些命令,bsy
#define IDE_DRDY	0x40 // => 0100_0000b,RDY应该是ready的意思,这个标志位表示硬盘是否准备好接受命令
#define IDE_DF		0x20 // => 0010_0000b,this bit, if set, indicates a write fault has occured
#define IDE_ERR		0x01 // => 0000_0001b,这个bit如果被设置的话,说明之前执行的命令出现了错误

static int diskno = 1;

static int
ide_wait_ready(bool check_error)
{
	int r;

	//从0x1F7端口硬盘的状态.判断硬盘的状态是不是为IDE_DRDY(ready),如果是IDE_BSY(busy),那么无限循环等待
	while (((r = inb(0x1F7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY)
		/* do nothing */;

	if (check_error && (r & (IDE_DF|IDE_ERR)) != 0)
		return -1;
	return 0;
}

bool
ide_probe_disk1(void)
{
	int r, x;

	// wait for Device 0 to be ready
	ide_wait_ready(0);

	// switch to Device 1
	outb(0x1F6, 0xE0 | (1<<4));

	// check for Device 1 to be ready for a while
	for (x = 0;
	     x < 1000 && ((r = inb(0x1F7)) & (IDE_BSY|IDE_DF|IDE_ERR)) != 0;
	     x++)
		/* do nothing */;

	// switch back to Device 0
	outb(0x1F6, 0xE0 | (0<<4));

	cprintf("Device 1 presence: %d\n", (x < 1000));
	return (x < 1000);
}

void
ide_set_disk(int d)
{
	if (d != 0 && d != 1)
		panic("bad disk number");
	diskno = d;
}


int
ide_read(uint32_t secno, void *dst, size_t nsecs)
{
	int r;

	assert(nsecs <= 256);

	ide_wait_ready(0);


	/*
		下面是各个端口的解释,下面采用的是LBA28的方式来读取硬盘数据,LBA(Linear Block Address).
		LBA28的意思就是用28位来寻址,因此LBA28支持的硬盘大小就是: 2^28-1*512byte
		LBA相较于CHS(cylinder-header-sector,i.e. 柱面-磁头-扇区)来说就是简化了寻址,不需要去
		计算CHS了,下面是LBA读取的几个端口号的意思.
		0x1F2,需要读取的扇区数目,也就是说要读取多少个扇区
		0x1F3,扇区号,即从那个扇区开始读? 0x1F3持有的是LBA的低8位
		0x1F4,LBA的中间8位
		0x1F5,LBA的高8位.目前只发送了3个Byte,这也就是说还有4bit没有发送
		0x1F6,决定使用的是使用主盘(master)还是从盘(slave),0xE0表示使用的是master,剩下的低四位是LBA的最高4位
		diskno会在fs.c中的fs_init中初始化来决定使用是主盘还是从盘.这里的static int diskno = 1不是最终决定
		用哪个硬盘的

		然后bit 6表明我们使用的是LBA模式.注意,这里说的Bit 6是从0开始计数的.bit 7 and bit 5 must be 
		set for backwards compabitility.

		0x1F7,这是一个多功能端口.在写入的时候是command register,在读取时候是status register
		我们要去读取数据,因此肯定要写入, 所以往这个端口写入0x20表示我们从硬盘当中读取数据.
	*/
	outb(0x1F2, nsecs);
	outb(0x1F3, secno & 0xFF);
	outb(0x1F4, (secno >> 8) & 0xFF);
	outb(0x1F5, (secno >> 16) & 0xFF);
	outb(0x1F6, 0xE0 | ((diskno&1)<<4) | ((secno>>24)&0x0F));
	outb(0x1F7, 0x20);	// CMD 0x20 means read sector

	//接下来要做的就是数据的读取,每次读取的数据大小是一个扇区(SECTSIZE=512)
	for (; nsecs > 0; nsecs--, dst += SECTSIZE) {
		if ((r = ide_wait_ready(1)) < 0)
			return r;
		//读取数据,0x1F0是数据端口,我们将数据从0x1F0读取到目标地址dst处.
		//在32位机器中,一次读取32bit=4byte.所以要从端口中循环的读取数据,循环的次数就是
		//512/4=128次
		insl(0x1F0, dst, SECTSIZE/4);
	}

	return 0;
}

int
ide_write(uint32_t secno, const void *src, size_t nsecs)
{
	int r;

	assert(nsecs <= 256);

	ide_wait_ready(0);

	outb(0x1F2, nsecs);
	outb(0x1F3, secno & 0xFF);
	outb(0x1F4, (secno >> 8) & 0xFF);
	outb(0x1F5, (secno >> 16) & 0xFF);
	outb(0x1F6, 0xE0 | ((diskno&1)<<4) | ((secno>>24)&0x0F));
	
	//在读操作中,端口的意思和read 没有区别.我们只需要向0x1F7端口写入0x30
	//来表示我们次数需要写入数据
	outb(0x1F7, 0x30);	// CMD 0x30 means write sector

	for (; nsecs > 0; nsecs--, src += SECTSIZE) {
		if ((r = ide_wait_ready(1)) < 0)
			return r;
		outsl(0x1F0, src, SECTSIZE/4);
	}

	return 0;
}

