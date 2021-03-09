//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  //根据file descriptor,获得对应的file对象，用于接下来的读取内容
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd] == 0){
      //当前进程中
      //找到最小可用的file descriptor
      //让当前file descriptor对应的file对象为参数f
      curproc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
sys_dup(void)
{
  //复制一个file descriptor
  //并不是只是返回一个数字看起来那么简单
  struct file *f;
  int fd;

  //从栈当中获得file descriptor
  //并且从当前进程的ofile fields当中
  //找到对应的sturct file
  if(argfd(0, 0, &f) < 0)
    return -1;
  
  //为这个struct file 分配一个file descriptor
  
  if((fd=fdalloc(f)) < 0)
    return -1;
  
  //此时有两个file descriptor都指向f，
  //ref count ++;
  filedup(f);
  return fd;
}

int
sys_read(void)
{
  struct file *f; //fiel descriptor对应的file结构
  int n; //要读取的字节数
  char *p; //buffer

  //下面代码的意思和sys_write的意思比较相近,不再过多赘述
  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  //调用fileread来读取，p:数据将要被写入的地址， n:要读取的字节数
  return fileread(f, p, n);
}

int
sys_write(void)
{
  // write(fd,buf,n)
  // 从buf往fd写入n字节的数据
  struct file *f; //fd对应的file结构
  int n;  //要写入的字节数
  char *p; //buffer

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;

  // agrfd能够从栈当中获得file descriptor,
  //然后找到file descriptor对应的struct file
  //然后作为参数传给filewrite函数
  return filewrite(f, p, n);
}

int
sys_close(void)
{
  int fd;
  struct file *f;
  
  /*
    在外层调用的是close(int fd)，参数会穿入到栈当中
    然后argfd从栈当中获得fd,存到变量fd当中，并且根据fd
    获得对应的struct file;
  */
  if(argfd(0, &fd, &f) < 0)
    return -1;
  //清除当前进程中opfile[fd]，这样就直接关闭了
  //注意，这里只是释放进程中open file table对应的索引，还没有释放file结构
  myproc()->ofile[fd] = 0;

  /**
   * 释放对应的结构，在这个流程中，比较关键的操作是：
   * 指向f的references--,如果ref == 0,才会去释放这个这个file对象
  */
  fileclose(f);
  return 0;
}

int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;

  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  /*
    这个系统调用完成的是Unix中的link的实现。
    比如说：ln foo.txt abr ，意思就是说建立一个hard link,名字叫做bar，
    连接到foo.txt这个文件。两者指向相同的inode。

  */
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  //argstr()函数用于从栈当中取得参数
  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;

  begin_op();
  // 调用namei()函数，找到原来文件名的所对应的inode
  // ip就是原来文件指向的inode
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  /*
    hard link不能用于目录当中，原因见下面网址:
    https://askubuntu.com/questions/210741/why-are-hard-links-not-allowed-for-directories
    所以，在sys_link这个系统调用当中。如果遇到文件类型是目录，那么就结束，返回-1
  */
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  /*
    现在有一个新的目录指向这个inode，所以inode的nlink++。  
  */
  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  //调用nameiparent来找到new这个路径的父路径。因为我们在一个路劲创建了一个新的
  //hard link，对应的父路径下的数据要发生改变，我们调用nameiparent()函数
  //来找到新路径的父路径的inode,写到dp当中
  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);

  //一个hard link就是让两个文件名都指向相同的文件，我们调用dirlink()函数
  //来将老的文件的inode(就是ip->inum)写入到新路径名(dp,name)
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], *path;
  uint off;

  //将路径加入到栈当中
  if(argstr(0, &path) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  // path：路径，就是相当于文件名
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;
  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && ip->type == T_FILE)
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    // 调用dirlink()函数将inode和某个文件名连接起来
    // 要给当前目录设置好它.(当前目录)和..(父目录)对应 inode number
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;

  //从栈当中获得路径和操作文件的方式
  //比如说，O_RDONLY（只读）,O_CREATE(创建)等等
  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  //如果omode是O_CREATE，说明要创建文件
  if(omode & O_CREATE){
    //调用create()函数来创建文件
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    //namei函数最终会返回路径path对应的inode
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  //filealloc()在系统当中分配一个空闲的file slot的给f
  //一个系统可以打开的文件是有限的，比如说在终端输入ulimit -n 可以看到最大可以打开的文件
  //接着为这个file分配一个file descriptor
  //在以后的操作当中，我们都已这个file descriptor来获得struct file
  //fdalloc()函数将filealloc()得到的struct file对象放到进程自己(struct proc)的file中
  //然后返回file descriptor。
  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  /*
    struct file中有一个成员变量时pipe,只有在pipealloc()才用得到
    这里用不到
  */
  f->type = FD_INODE; //用FD_INODE来表示是普通文件
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  //在最后返回file descriptor
  return fd;
}

int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mknod(void)
{
  struct inode *ip;
  char *path;
  int major, minor;

  begin_op();
  if((argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  struct inode *ip;
  struct proc *curproc = myproc();
  
  begin_op();
  if(argstr(0, &path) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(curproc->cwd);
  end_op();
  curproc->cwd = ip;
  return 0;
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      myproc()->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}
