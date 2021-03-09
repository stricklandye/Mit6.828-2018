// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

// direcy block的数量，说明前6KB的文件数据，可以直接从
// inode中得到他们对应的扇区号
#define NDIRECT 12 

//一个扇区号占据32bit=4byte，我们用一个额外的扇区来存放
//indrect的数据，512 / 4 byte = 128，indrect sector的数量
//如果不在direct中，那么就需要从indirect block中得到扇区号
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
// inode是表示每个文件的基本结构，比如说这个文件的类型（是目录，还是普通文件，还是devices）
// 还有该文件的数据，它表示为在磁盘中的扇区号,其中包括了直接扇区(direct)，间接扇区(indirect)
// 这个是硬盘上的inode，在file.h中的inode是这个inode在内存中的copy
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)

  //addrs是保存文件的数据的，分为direct block和indirect block
  //在这里，direct block的个数是NDIRECT+1=13，其中最后一个用于存放
  //indreict block的数据
  uint addrs[NDIRECT+1];   // Data block addresses
};

// Inodes per block.
// 一个扇区可以持有的inode个数
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
// 哪个扇区持有inode i。计算很简单
// 只要inode i / IPB,这首先得到它相较于inode其实扇区的偏移是多少
// 然后再加上inode的起始扇区，就可以计算到inode位于第几个扇区了
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
// 一个扇区可以持有的bitmap bits的个数，
// 一个byte等于8bit,一个扇区总共有512 byte，所以一个扇区可以持有512 * 8 bits 
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
// 包含这block b对应的bitmap所属的扇区
#define BBLOCK(b, sb) (b/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
// 最大文件名长度
#define DIRSIZ 14

/*
  这个表示目录(directory)的结构，用于存放目录的数据。name表示目录名
  inum表示这个目录对应的inode number。一个目录的数据就是一大堆的目录结构
*/
struct dirent {
  ushort inum; // inode number
  char name[DIRSIZ];
};

