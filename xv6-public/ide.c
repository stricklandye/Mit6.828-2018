// Simple PIO-based (non-DMA) IDE driver code.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define SECTOR_SIZE   512
#define IDE_BSY       0x80
#define IDE_DRDY      0x40
#define IDE_DF        0x20
#define IDE_ERR       0x01

#define IDE_CMD_READ  0x20
#define IDE_CMD_WRITE 0x30
#define IDE_CMD_RDMUL 0xc4
#define IDE_CMD_WRMUL 0xc5

// idequeue points to the buf now being read/written to the disk.
// idequeue->qnext points to the next buf to be processed.
// You must hold idelock while manipulating queue.

static struct spinlock idelock;
static struct buf *idequeue;

static int havedisk1;
static void idestart(struct buf*);

// Wait for IDE disk to become ready.
static int
idewait(int checkerr)
{
  int r;

  //0x1f7端口表示读取硬盘状态的，下面代码的意思
  //判断硬盘状态是否为ready
  while(((r = inb(0x1f7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY)
    ;
  if(checkerr && (r & (IDE_DF|IDE_ERR)) != 0)
    return -1;
  return 0;
}

void
ideinit(void)
{
  int i;

  initlock(&idelock, "ide");


  /*
    enable IDE_IRQ interrupt
    开启硬盘中断 
    The call to ioapicenable enables the interrupt only on the last CPU (ncpu-1): on a two-
    processor system, CPU 1 handles disk interrupt
  */
  ioapicenable(IRQ_IDE, ncpu - 1);
  idewait(0);

  // Check if disk 1 is present
  /*
    After ideinit, the disk is not used again until the buffer cache calls iderw,
  which updates a locked buffer as indicated by the flags. If B_DIRTY is set, iderw
  writes the buffer to the disk; if B_VALID is not set, iderw reads the buffer from the
  disk
  */
  outb(0x1f6, 0xe0 | (1<<4));
  for(i=0; i<1000; i++){
    if(inb(0x1f7) != 0){
      havedisk1 = 1;
      break;
    }
  }

  // Switch back to disk 0.
  outb(0x1f6, 0xe0 | (0<<4));
}

// Start the request for b.  Caller must hold idelock.
static void
idestart(struct buf *b)
{
  //issues either a read or a write for the buffer’s device and sector,according to the flags
  if(b == 0)
    panic("idestart");
  if(b->blockno >= FSSIZE)
    panic("incorrect blockno");

  /*
    int sector_per_block =  BSIZE/SECTOR_SIZE;
    int sector = b->blockno * sector_per_block;
    在一般系统当中，每一个block并不等于一个扇区的大小。有时候可能是一个4096个字节
    在传入到硬盘读写函数的时候，传入的往往是block，我们要将block转为sector,
    b->blockno * sector_per_block得到我们需要读取的扇区数
    假设我们的block size是4096字节，block 0对应的硬盘扇区就是0~7。
    int sector = b->blockno * sector_per_block;得到就是0
  */
  int sector_per_block =  BSIZE/SECTOR_SIZE;
  int sector = b->blockno * sector_per_block;
  int read_cmd = (sector_per_block == 1) ? IDE_CMD_READ :  IDE_CMD_RDMUL;
  int write_cmd = (sector_per_block == 1) ? IDE_CMD_WRITE : IDE_CMD_WRMUL;

  if (sector_per_block > 7) panic("idestart");

  idewait(0);
  /*
    关于0x3f6端口以及其他的IO port的解释看这个网站
    https://wiki.osdev.org/ATA_PIO_Mode
    如果0x3f6适用于控制ATA bus的，这个寄存器叫做Device control Registers
    向0x3f6输入0，表示允许硬盘发送中断。
  */
  outb(0x3f6, 0);  // generate interrupt
  outb(0x1f2, sector_per_block);  // number of sectors
  outb(0x1f3, sector & 0xff);
  outb(0x1f4, (sector >> 8) & 0xff);
  outb(0x1f5, (sector >> 16) & 0xff);
  outb(0x1f6, 0xe0 | ((b->dev&1)<<4) | ((sector>>24)&0x0f));
  if(b->flags & B_DIRTY){
    outb(0x1f7, write_cmd); //写入的话，向0x1f7端口发送0x20
    outsl(0x1f0, b->data, BSIZE/4);
  } else {
    outb(0x1f7, read_cmd); //读取的话，向0x1f7端口发送0x30
  }
}

// Interrupt handler.
void
ideintr(void)
{
  //Eventually, the disk will finish its operation and trigger an interrupt. 
  //trap will call ideintr to handle it
  struct buf *b;

  // First queued buffer is the active request.
  acquire(&idelock);

  if((b = idequeue) == 0){
    release(&idelock);
    return;
  }
  idequeue = b->qnext;

  // Read data if needed.
  // 这里我没有十分明白这个条件的意思
  if(!(b->flags & B_DIRTY) && idewait(1) >= 0)
    //read the data into buffer
    insl(0x1f0, b->data, BSIZE/4);

  // Wake process waiting for this buf.
  //ideintr sets B_VALID, clears B_DIRTY
  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  wakeup(b);

  // Start disk on next buf in queue.
  if(idequeue != 0)
    idestart(idequeue);

  release(&idelock);
}

//PAGEBREAK!
// Sync buf with disk.
// If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
// Else if B_VALID is not set, read buf from disk, set B_VALID.
void
iderw(struct buf *b)
{ 
  /*
    keeping the list of pending disk requests in a queue and
    using interrupts to find out when each request has fin ished
  */
  struct buf **pp;

  if(!holdingsleep(&b->lock))
    panic("iderw: buf not locked");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("iderw: nothing to do");
  if(b->dev != 0 && !havedisk1)
    panic("iderw: ide disk 1 not present");

  acquire(&idelock);  //DOC:acquire-lock

  // Append b to idequeue.
  b->qnext = 0;
  for(pp=&idequeue; *pp; pp=&(*pp)->qnext)  //DOC:insert-queue
    ;
  *pp = b;

  // Start disk if necessary.
  if(idequeue == b)
    //进行硬盘操作
    idestart(b);

  // Wait for request to finish.
  while((b->flags & (B_VALID|B_DIRTY)) != B_VALID){
    sleep(b, &idelock);
  }


  release(&idelock);
}
