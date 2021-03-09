#include <inc/x86.h>
#include <inc/elf.h>

/**********************************************************************
 * This a dirt simple boot loader, whose sole job is to boot
 * an ELF kernel image from the first IDE hard disk.
 *
 * DISK LAYOUT
 *  * This program(boot.S and main.c) is the bootloader.  It should
 *    be stored in the first sector of the disk.
 *
 *  * The 2nd sector onward holds the kernel image.
 *
 *  * The kernel image must be in ELF format.
 *
 * BOOT UP STEPS
 *  * when the CPU boots it loads the BIOS into memory and executes it
 *
 *  * the BIOS intializes devices, sets of the interrupt routines, and
 *    reads the first sector of the boot device(e.g., hard-drive)
 *    into memory and jumps to it.
 *
 *  * Assuming this boot loader is stored in the first sector of the
 *    hard-drive, this code takes over...
 *
 *  * control starts in boot.S -- which sets up protected mode,
 *    and a stack so C code then run, then calls bootmain()
 *
 *  * bootmain() in this file takes over, reads in the kernel and jumps to it.
 **********************************************************************/

#define SECTSIZE	512
#define ELFHDR		((struct Elf *) 0x10000) // scratch space

void readsect(void*, uint32_t);
void readseg(uint32_t, uint32_t, uint32_t);

void
bootmain(void)
{
	struct Proghdr *ph, *eph;

	// read 1st page off disk
	//读取4096个字节放到0x10000处
	//使用readelf -l kernel可以看到，第一个program table entry开始的offset在文件中是
	//0x1000=4096,所以也就是说，前面的4kb数据都是ELF header,所以接下来我们先将ELF header读到
	//0x10000处,接下来根据ELF header的数据读剩下的文件到内存的当中
	readseg((uint32_t) ELFHDR, SECTSIZE*8, 0);

	// is this a valid ELF?
	if (ELFHDR->e_magic != ELF_MAGIC)
		goto bad;

	// load each program segment (ignores ph flags)
	//获得program table的offset，ELFHDR->e_phoff得到的值应该是0x34
	//相加后 ph = 0x10034
	ph = (struct Proghdr *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff); 

	//eph是program table entry的个数, ELFHDR->e_phnum
	//下面的运算得到了program table 一共有多少字节
	eph = ph + ELFHDR->e_phnum;
	for (; ph < eph; ph++)
		// p_pa is the load address of this segment (as well
		// as the physical address)
		//根据program header entry得到的数据，我们要从ph->p_offset中
		//加载ph->p_memsz 字节数据到ph->p_pa
		//在lab1中,第一次执行的时候，我观察了debug
		//ph->p_pa = 0x100000,ph->p_memsz = 0x07dac,ph->p_offset = 0x1000,
		//在readseg中，offset经过扇区计算后=9
		readseg(ph->p_pa, ph->p_memsz, ph->p_offset);

	// call the entry point from the ELF header
	// note: does not return!
	((void (*)(void)) (ELFHDR->e_entry))();

bad:
	outw(0x8A00, 0x8A00);
	outw(0x8A00, 0x8E00);
	while (1)
		/* do nothing */;
}

// Read 'count' bytes at 'offset' from kernel into physical address 'pa'.
// Might copy more than asked
void
readseg(uint32_t pa, uint32_t count, uint32_t offset)
{
	uint32_t end_pa;

	end_pa = pa + count;

	// round down to sector boundary
	//这里是因为pa不一定都是512字节对齐的，我们将pa做一个512字节对齐
	//下面进行一个举例，例如pa=700，~(512-1) = 0x1110_0000_0000
	//700 & 0x1110_0000_0000 = 0x200 = 512，这样我们就做到了对齐
	pa &= ~(SECTSIZE - 1);

	// translate from bytes to sectors, and kernel starts at sector 1
	// 根据program table entry中的offset来获得对应的扇区。0号扇区是Boot,1号开始是
	//kernel，所以假设某个program header的offset=800的时候，800/512+1=2，所以从第二个扇区开始读数据
	offset = (offset / SECTSIZE) + 1;

	// If this is too slow, we could read lots of sectors at a time.
	// We'd write more to memory than asked, but it doesn't matter --
	// we load in increasing order.
	while (pa < end_pa) {
		// Since we haven't enabled paging yet and we're using
		// an identity segment mapping (see boot.S), we can
		// use physical addresses directly.  This won't be the
		// case once JOS enables the MMU.
		readsect((uint8_t*) pa, offset);

		//物理地址+512字节
		pa += SECTSIZE;

		//读下一个扇区
		offset++;
	}
}

void
waitdisk(void)
{
	// wait for disk reaady
	while ((inb(0x1F7) & 0xC0) != 0x40)
		/* do nothing */;
}

void
readsect(void *dst, uint32_t offset)
{
	// wait for disk to be ready
	waitdisk();

	//使用LBA模式的逻辑扇区方式来寻找扇区
	outb(0x1F2, 1);		// count = 1
	outb(0x1F3, offset);
	outb(0x1F4, offset >> 8);
	outb(0x1F5, offset >> 16);
	outb(0x1F6, (offset >> 24) | 0xE0);
	outb(0x1F7, 0x20);	// cmd 0x20 - read sectors

	// wait for disk to be ready
	waitdisk();

	// read a sector
	//一次读取的单位是四个字节，所以循环sectorsize/4 = 128次
	insl(0x1F0, dst, SECTSIZE/4);
}

