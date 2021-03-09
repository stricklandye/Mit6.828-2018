// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb; 

// Read the super block.
void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
// 该函数用于从dev(设备，比如说磁盘)中获得空闲的扇区
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  //从磁盘扇区中寻找到可用的扇区，0表示可用，1表示空闲
  //sb.size表示硬盘总共可以用的数量,sb.size = 1000，我们要遍历扇区。在通过Bitmap
  //来查看对应的扇区是否被使用。bitmap可能占据多个扇区，所以外循环用于遍历每个用于存放bitmap
  //的扇区
  for(b = 0; b < sb.size; b += BPB){
    //下面这句代码的意思，应该是获取bitmap的扇区内容,BBLOCK()应该得到的是bitmap的扇区号
    bp = bread(dev, BBLOCK(b, sb));

    //接下来的内循环用于，遍历扇区的bitmap元素。一个扇区的bitmap总共可以存放512 * 8扇区。
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      
      /*
        举个例子，下面的代码就非常好理解：比如说我们要使用第7扇区
        1 << (bi % 8) = 0100_0000.那么就将第7bit设置为1就行
      */
      m = 1 << (bi % 8);
      /*
        (bp->data[bi/8],bi/8是得到byte。比如说我们要判断15扇区是否可用
        15/8 = 1，这就是位于数组第二个元素。拿到数组的内容bp->data[bi/8] & m ,如果
        == 0，表示该扇区可用。注意bp->data是一个char类型的数组
      */
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        //将该bit设置为1,说明本扇区已经被使用了。
        bp->data[bi/8] |= m;  // Mark block in use.

        //bitmap被修改后，应该写回到硬盘当中去
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);

        //返回的是新分配的扇区号
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
// 释放扇区b对应的bitmap的内容。也就是将扇区b在bitmap中的
// 设置为0
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  //拿到b对应的bitmap的扇区号，然后读取对应的扇区号内容，放到 struct buf *bp;
  bp = bread(dev, BBLOCK(b, sb));

  //这面这条语句的意思,我认为是获得b对应的bitmap，一个bitmap可以持有512 * 8个扇区
  //如果扇区号b是 512 * 8 +1,b % BPB;此时说明，当前扇区的实在第二个bitmap的第一个byte
  bi = b % BPB;
  
  //老样子，将某byte中的某位bit设置为1
  m = 1 << (bi % 8);
  //如果改扇区已经是free的，再free就panic。
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  // 将这个bit设置为0，表示该扇区空闲了
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a cache entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The icache.lock spin-lock protects the allocation of icache
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold icache.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit(int dev)
{
  int i = 0;
  
  initlock(&icache.lock, "icache");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }

  readsb(dev, &sb);
  cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d\
 inodestart %d bmap start %d\n", sb.size, sb.nblocks,
          sb.ninodes, sb.nlog, sb.logstart, sb.inodestart,
          sb.bmapstart);
}

static struct inode* iget(uint dev, uint inum);

//PAGEBREAK!
// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode.
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  /*
    在mkfs.c中初始化了一个文件系统可以持有的inode数量，
    ialloc函数就是从系统所有的inode当中，分配可用的inode并且返回
  */
  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk, since i-node cache is write-through.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip)
{
  acquiresleep(&ip->lock);
  if(ip->valid && ip->nlink == 0){
    acquire(&icache.lock);
    int r = ip->ref;
    release(&icache.lock);
    if(r == 1){
      // inode has no links and no other references: truncate and free.
      itrunc(ip);
      ip->type = 0;
      iupdate(ip);
      ip->valid = 0;
    }
  }
  releasesleep(&ip->lock);

  acquire(&icache.lock);
  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

//PAGEBREAK!
// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
/*
  虽然注释里面说是disk block address of nth block。但是我的理解
  就是返回返回某个块的扇区号
*/
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  //如果块号 < NDIRECT,说明块号对应的扇区可以直接从inode中获取到
  //块号是从0开始计算的，比如说offset = 500， 500/512 = 0
  // 所以块号11对应的就是第12个块，下面的条件bn < NDIRECT就是判断
  //块号是否在前12个
  if(bn < NDIRECT){
    //如果addr = ip->addrs[bn] ==0，说明块还不存在,重新分配一个块
    if((addr = ip->addrs[bn]) == 0)
      /*
        如果bn th块对应的扇区还不在inode ip之内。那么调用balloc()
        来分配一个扇区。
      */
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  
  /*
    这里的逻辑是，我们通过offset/BSIZE得到块号。当我们的块号超过NDIRECT了
    那么说明这个块号对应的扇区号肯定位于indirect block当中
    比如说我们得到的块是15，这说明该块对应的扇区号在indirect block中的第三个
    所以我们需要15-12=3，来得到在indirect block的块号
  */
  bn -= NDIRECT;

  // 当块号=12的时候，实际上这个已经是第13个block了。
  // 此时已经到了indirect block当中了
  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
static void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

//PAGEBREAK!
// Read data from inode.
// Caller must hold ip->lock.
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  /*
    off > ip->size: 读取的内容不能超过文件的size
    off + n < off：这个条件没有理解什么意思，我猜这个可能和uint的上限有关
    当 off+n > MAX(int)的时候，此时off+n < off
  */
  if(off > ip->size || off + n < off)
    return -1;
  /*
    如果off +n > ip -> size，说明要读取的字节数已经超过了文件大小
    那么就将n = ip->size - off;表示读取off到文件末尾这一段的所有内容
  */
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    /*
      bmap()函数将off/BSIZE来得到inode中的扇区号。inode中持有一大堆扇区数据，我们
      通过offet/BSIZE来判断该offset对应的是inode第几个扇区。
      bread()读取到的数据并不是直接放到直接放到目标地址，而是先放到缓冲区
      bread()函数返回的就是那个缓冲区
    */
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);

    //将缓冲区中的数据复制到dst地址处
    /*
      偏移量与缓冲区的偏移地址相加，来得到最终的数据。比如说，我想从文件的
      offset=100处读取数据，100/512 = 0，用0去inode中找到第一个扇区的扇区号
      然后读取该扇区内容到buffer中,buffer->data + offset就是我们所需要的数据的
      的地址
    */
    memmove(dst, bp->data + off%BSIZE, m);
    //释放缓冲区(struct buf)
    brelse(bp);
  }
  return n;
}

// PAGEBREAK!
// Write data to inode.
// Caller must hold ip->lock.
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  //这里说明目标文件是目录类型
  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  /*
    如果写入的地方大于文件的最大的size，这时候就有问题了。因为一个inode可以持有的扇区的数量
    就是 number of direct block +  number of indirect block 
  */
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    /*
      bmap(ip, off/BSIZE)，用off/BSIZE来从inode得到中某个扇区号
      然后扇区号传给bread()来读取数据到struct buf当中
      如果需要写入到新的扇区，bmap()中会调用balloc()函数来申请扇区
    */
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    /*
      和readi()相反，write的时候，数据先写到缓冲区，而不是直接写到硬盘
    */
    memmove(bp->data + off%BSIZE, src, m);
    log_write(bp);
    brelse(bp);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;
}

//PAGEBREAK!
// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
// 在一个目录里面找一个directory entry。(就是struct dirent，定义在fs.h当中)
// dp是要被查找的路径的inode，name是要找的路径名字，poff用于返回这个路径在
// 被查找文件的offset
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  /*
    根据文件名字找到对应的directory entry。目录也是一种特殊的文件，
    目录的数据就是它的子目录里面的各种数据。当我们需要找到一个目录的对应的inode的时候
    首先先从根目录 /开始。找到/ 对应的inode。这个inode中存放的是子目录的信息
    然后我们去读取这个inode的扇区。比如说/aa/bb/c.txt，那么需要/的数据中找到aa对应的目录结构
    然后再在aa当中找到bb中找到c.txt的inode。差不多就是这样
  */
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  //目录说到底也是一个文件，所以我们要遍历目录的数据区域，来找name所属的struct dirent
  for(off = 0; off < dp->size; off += sizeof(de)){
    //调用readi来从dp这个inode中，从off处，读取sizeof(de)个字节的数据，读取到的数据
    //放到de处
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    
    //如果参数name和硬盘中的某个结构(struct dirent)相同，说明找到了
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      // poff指向目标directory entry在当前目录文件中的偏移
      if(poff)
        *poff = off;
      inum = de.inum;
      //返回de.inum对应的inode
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
// 这个函数用于往inode dp中写入一个目录结构(name, inum)
int
dirlink(struct inode *dp, char *name, uint inum)
{ 
  /*
    一个目录结构体(struct dirent,定义在fs.h当中)中拥有一个inode变量
    它用于保存当前路径的数据。dirlink()函数用于往某个路径结构中写入
    新的子路径。所以，首先我们需要找到当前路径对应的dirent,然后从中拿到
    该路径所属的inode。然后再把数据写入
  */
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  // 判断name所对应的inode是否存在，如果存在hard link就不能执行了
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  // 在目标inode当中找到一个空闲的dirent
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  //上面找到空闲的dirent之后，接下来的代码就是初始化数据了
  //将参数name复制到de.name，然后将参数name对应的inode number复制给de.inum
  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  //创建好dirent之后，我们要将inode dp中。
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

//PAGEBREAK!
// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  // 如果目录是以/开头的，那么就是说明该目录是以从根目录开始的
  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    // 如果不是/开头，那么说明就是当前目录的，myproc()->cwd
    ip = idup(myproc()->cwd);

  //skipelem("a/bb/c", name) = "bb/c", setting name = "a"
  //skipelem循环知道目录名结束
  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    // dirlookup：表示从inode ip中找到路径名name所对应的inode
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);

    //在最后更新inode ip，比如说路径a/bb/c,第一次循环结束后
    // 将ip更新为 a 对应的inode.然后再目录a的inode总寻找bb。
    // 接着将inode ip更新为bb对应的inode,然后在里面寻找c。
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  //得到路径path对应的inode
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
