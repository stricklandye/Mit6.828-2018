// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

struct {
  //buf锁，最多只能有一个线程可以访问该buf
  struct spinlock lock;

  //一个持有disk buffer的数组
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

//PAGEBREAK!
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  
  //初始化buffer，将bcache中的buf结构通过双向循环链串联起来
  //在需要使用buffer的时候，都是通过bcache.head，而不是buf数组。
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    //头插法插入双向循环链表
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.

//Bread (4502) calls bget to get a buffer for the given sector (4506). page 79
//bget()：给定某个扇区，来获得对应的buffer
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  //因为bcache中的struct buf是一个双向循环链表，我们遍历一个双向循环链表的条件就是
  //判断当前节点的下一个是不是head,因为我们都是从head开始遍历的。
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    //遍历整个buffer链表，通过条件b->dev == dev && b->blockno == blockno
    //判断是否是我们想要的buffer
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached; recycle an unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.

  /*
    如果上面的代码没有找到blockno对应的缓存，那么应该从bcache新找到一个可用的
    因为bcache是一个双向循环链表，所以遍历的条件是判断是；
    只要判断下一个或者前一个块是否等于head就行。这里的遍历方向和上面是相反的，
    但是没有多大的区别。
  */
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    
    //为blockno新分配一个buffer，并且初始化好buffer的内容
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      //flags=0的话，我的理解就是
      //此时这个Buffer还是空的，只是被初始化好了，当下一次读取的时候
      //需要判断flag,如果b->flags & B_VALID == 0才从硬盘读取
      //所以这里flag这样设置，我们才会从硬盘中读取对应的数据（和bread中的
      //(b->flags & B_VALID) == 0相对应）
      b->flags = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
// bread()：传入扇区号，返回该扇区号对应的buffer。因为有时候某个扇区不一定在
// buffer当中。所以必然涉及到，从硬盘中读取扇区的操作。
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;
  //calls bget to get a buffer for the given sector 
  //bget是通过给定扇区来获得buffer的
  b = bget(dev, blockno);
  
  // 如果已经在buffer当中的话，b->flags & B_VALID应该是=1的
  // 这个(b->flags & B_VALID) == 0，说明不在缓存当中，因此调用iderw()函数从硬盘中读取
  if((b->flags & B_VALID) == 0) { 
    //the buffer
    //needs to be read from disk, bread calls iderw to do that before returning the buffer  
    // 读取数据到(缓冲区)struct buf
    iderw(b);
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  
  /*
    这里我的猜想是，写入硬盘的请求并不是马上写入的，iderw中会先将
    硬盘操作插入到队列中，所以当我们写入数据到缓存的时候（因为调用这个bwrite的时候
    在内存当中数据已经被修改，但是还没有写入到硬盘当中，所以应该标记为dirty），
    先将缓存标记为dirty等待将来的写入到硬盘
  */
  b->flags |= B_DIRTY;
  iderw(b);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    //释放缓存，这里的意思就是将参数放回到双向循环链表当中
    //这里使用的是头插法
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    //头插法更新头节点
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}
//PAGEBREAK!
// Blank page.

