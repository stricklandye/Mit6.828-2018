
/*
  这个struct file结构并不是真正的在硬盘中的结构
  他只是代表一个open file 
*/
struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE } type;
  int ref; // reference count
  char readable;  // 该文件是否可读
  char writable;  //该文件是否可写入

  /*
    硬盘中的inode 类型是dinoe(在fs.h中)，当硬盘中的dinode读取到内存后，
    再将其转为struct inode。我们要访问内存中的inode,用的是指针。所以一个file对象
    里面的应该持有一个inode指针，来用于访问该文件的inode
  */
  struct pipe *pipe;
  struct inode *ip; // inode pointer
  uint off; //offset
};


// in-memory copy of an inode
// 这个inode是fs.h中的inode的copy
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  
  /*
  ref field counts the number of
  C pointers referring to the in-memory inode, and the kernel discards the inode from
  memory if the reference count drops to zero.
  */
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  
  //nlink表示有多少个directory entries link到了这个inode
  // 如果nlink == 0。那么这个inode就可以释放了。
  // 在unix系统中，没有delete()这个系统调用，只有unlink()
  short nlink;
  // 文件的大小，字节单位
  uint size;
  // 用于保存数据的扇区号，其中有12个direct block,最后一个扇区（就是NDREICT+1）中的1
  // 用于保存indirect block
  uint addrs[NDIRECT+1];
};

// table mapping major device number to
// device functions
struct devsw {
  int (*read)(struct inode*, char*, int);
  int (*write)(struct inode*, char*, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
