struct buf {
  int flags;
  uint dev; //当前buffer对应的device
  uint blockno; //当前buffer对应 扇区号
  struct sleeplock lock;
  uint refcnt;      
  struct buf *prev; // LRU cache list
  struct buf *next; //一个双向环形链表来管理缓冲区
  struct buf *qnext; // disk queue

  uchar data[BSIZE]; //当前内存中的数据，是第blockno扇区数据的copy
};

//B_VALID flag means that  data has been read in
#define B_VALID 0x2  // buffer has been read from disk
//B_DIRTY means that data needs to be written out
#define B_DIRTY 0x4  // buffer needs to be written to disk

