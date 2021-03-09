#include <inc/fs.h>
#include <inc/string.h>
#include <inc/lib.h>

#define debug 0


/*
	定义了一个Fsipc变量，但是没有初始化，如果没有修改的话
	编译器会将里面的内容都都默认设置为0。包括数组里面的内容。
*/
union Fsipc fsipcbuf __attribute__((aligned(PGSIZE)));

// Send an inter-environment request to the file server, and wait for
// a reply.  The request body should be in fsipcbuf, and parts of the
// response may be written back to fsipcbuf.
// type: request code, passed as the simple integer IPC value.
// dstva: virtual address at which to receive reply page, 0 if none.
// Returns result from the file server.
static int
fsipc(unsigned type, void *dstva)
{
	static envid_t fsenv;
	
	//找到文件系统对应的envid
	if (fsenv == 0)
		fsenv = ipc_find_env(ENV_TYPE_FS);

	static_assert(sizeof(fsipcbuf) == PGSIZE);

	if (debug)
		cprintf("[%08x] fsipc %d %08x\n", thisenv->env_id, type, *(uint32_t *)&fsipcbuf);

	//向文件系统发送数据,type用于指明在文件系统当中使用何种系统调用
	//需要发送给文件系统的数据放在fsinbuf当中,这个页将会在文件系统和普通进程之间共享
	ipc_send(fsenv, type, &fsipcbuf, PTE_P | PTE_W | PTE_U);

	//接受来自文件系统返回的响应
	//hi,把需要发送给我的数据放到dstva处吧~
	return ipc_recv(NULL, dstva, NULL);
}

static int devfile_flush(struct Fd *fd);
static ssize_t devfile_read(struct Fd *fd, void *buf, size_t n);
static ssize_t devfile_write(struct Fd *fd, const void *buf, size_t n);
static int devfile_stat(struct Fd *fd, struct Stat *stat);
static int devfile_trunc(struct Fd *fd, off_t newsize);

struct Dev devfile =
{
	.dev_id =	'f',
	.dev_name =	"file",
	.dev_read =	devfile_read,
	.dev_close =	devfile_flush,
	.dev_stat =	devfile_stat,
	.dev_write =	devfile_write,
	.dev_trunc =	devfile_trunc
};

// Open a file (or directory).
//
// Returns:
// 	The file descriptor index on success
// 	-E_BAD_PATH if the path is too long (>= MAXPATHLEN)
// 	< 0 for other errors.
int
open(const char *path, int mode)
{
	// Find an unused file descriptor page using fd_alloc.
	// Then send a file-open request to the file server.
	// Include 'path' and 'omode' in request,
	// and map the returned file descriptor page
	// at the appropriate fd address.
	// FSREQ_OPEN returns 0 on success, < 0 on failure.
	//
	// (fd_alloc does not allocate a page, it just returns an
	// unused fd address.  Do you need to allocate a page?)
	//
	// Return the file descriptor index.
	// If any step after fd_alloc fails, use fd_close to free the
	// file descriptor.

	int r;
	struct Fd *fd;

	if (strlen(path) >= MAXPATHLEN)
		return -E_BAD_PATH;

	//分配一个空闲的struct Fd放到指针fd处
	//此时分配的struct Fd的内容还没有被初始化．
	if ((r = fd_alloc(&fd)) < 0)
		return r;

	//下面两行代码是设置设置参数的,在open操作中
	//我们要设置需要open的路径以及open的mode
	strcpy(fsipcbuf.open.req_path, path);
	fsipcbuf.open.req_omode = mode;

	//然后调用fsipc()函数,在fsipc()函数当中,将fsciocbuf作为参数
	//要传给下ipc_send()函数.最后将会调用serve_open()函数
	//serve_open()函数将会返回数据到fd中，fd的初始化就完成了
	if ((r = fsipc(FSREQ_OPEN, fd)) < 0) {
		//如果失败close对应的struct Fd
		fd_close(fd, 0);
		return r;
	}

	//将结构struct Fd fd转为file descriptor返回
	return fd2num(fd);
}

// Flush the file descriptor.  After this the fileid is invalid.
//
// This function is called by fd_close.  fd_close will take care of
// unmapping the FD page from this environment.  Since the server uses
// the reference counts on the FD pages to detect which files are
// open, unmapping it is enough to free up server-side resources.
// Other than that, we just have to make sure our changes are flushed
// to disk.
static int
devfile_flush(struct Fd *fd)
{
	fsipcbuf.flush.req_fileid = fd->fd_file.id;
	return fsipc(FSREQ_FLUSH, NULL);
}

// Read at most 'n' bytes from 'fd' at the current position into 'buf'.
//
// Returns:
// 	The number of bytes successfully read.
// 	< 0 on error.
static ssize_t
devfile_read(struct Fd *fd, void *buf, size_t n)
{
	// Make an FSREQ_READ request to the file system server after
	// filling fsipcbuf.read with the request arguments.  The
	// bytes read will be written back to fsipcbuf by the file
	// system server.
	int r;

	fsipcbuf.read.req_fileid = fd->fd_file.id;
	fsipcbuf.read.req_n = n;
	//fsipc会去调用serve_read函数，serve_read读取的到的数据放到了fsipcbuf.readRet.ret_buf
	if ((r = fsipc(FSREQ_READ, NULL)) < 0)
		return r;
	
	// cprintf("readRet.buf:%x\n",&fsipcbuf.readRet.ret_buf);
	// cprintf("size of fscipc: %d\n",sizeof(union Fsipc));
	//fsipcbuf.readRet.ret_buf的大小是PGSIZE
	//serve_read的返回值是读取的字节数，不知道为什么assert(r <= PGSZIE)
	//我认为如果成功的话，r == PGSIZE
	assert(r <= n);
	assert(r <= PGSIZE);
	//fsipcbuf.readRet.ret_buf的数据在复制到buf当中，读取数据完毕
	/*
		我之前好奇，我们是否需要初始化制定readRet.ret_buf的位置。后来想想其实是不需要的
		因为我们定义的是一个全局变量，里面的内容都会有由编译器来初始化。所以直接用就行了
		对应的ret_buf已经有一块自己的内存空间了。
	*/
	memmove(buf, fsipcbuf.readRet.ret_buf, r);
	return r;
}


// Write at most 'n' bytes from 'buf' to 'fd' at the current seek position.
//
// Returns:
//	 The number of bytes successfully written.
//	 < 0 on error.
static ssize_t
devfile_write(struct Fd *fd, const void *buf, size_t n)
{
	// Make an FSREQ_WRITE request to the file system server.  Be
	// careful: fsipcbuf.write.req_buf is only so large, but
	// remember that write is always allowed to write *fewer*
	// bytes than requested.
	// LAB 5: Your code here
	int r;
	//如果超过了buffer的大小，那么读取的数据也只能是buffer的最大值
	if (n > sizeof(fsipcbuf.write.req_buf)) {
		n = sizeof(fsipcbuf.write.req_buf);
	}
	fsipcbuf.write.req_fileid = fd->fd_file.id; //写入的目标文件
	//要写入的字节数
	fsipcbuf.write.req_n = n;
	
	//和读反过来的是，我们在写入的时候，现将数据从缓冲区(buf)当中复制到fsipcbuf.write.req_buf
	//然后在去调用serve_write()函数，该函数再去读取fsipcbuf.write.req_buf中的数据
	//serve_write()函数再将fsipcbuf.write.req_buf内的数据写入到文件当中去
	memmove(fsipcbuf.write.req_buf,buf,n);
	//fsipc返回的是成功写入的字节数，如果成功的话应该写入
	r = fsipc(FSREQ_WRITE,NULL);

	assert(r <= n);
	assert(r <= PGSIZE);
	//返回成功读取的字节数．
	return r;
	// panic("devfile_write not implemented");
}

static int
devfile_stat(struct Fd *fd, struct Stat *st)
{
	int r;

	fsipcbuf.stat.req_fileid = fd->fd_file.id;
	if ((r = fsipc(FSREQ_STAT, NULL)) < 0)
		return r;
	strcpy(st->st_name, fsipcbuf.statRet.ret_name);
	st->st_size = fsipcbuf.statRet.ret_size;
	st->st_isdir = fsipcbuf.statRet.ret_isdir;
	return 0;
}

// Truncate or extend an open file to 'size' bytes
static int
devfile_trunc(struct Fd *fd, off_t newsize)
{
	fsipcbuf.set_size.req_fileid = fd->fd_file.id;
	fsipcbuf.set_size.req_size = newsize;
	return fsipc(FSREQ_SET_SIZE, NULL);
}


// Synchronize disk with buffer cache
int
sync(void)
{
	// Ask the file server to update the disk
	// by writing any dirty blocks in the buffer cache.

	return fsipc(FSREQ_SYNC, NULL);
}

