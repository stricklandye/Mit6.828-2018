/*
 * File system server main loop -
 * serves IPC requests from other environments.
 */

#include <inc/x86.h>
#include <inc/string.h>

#include "fs.h"


#define debug 0

// The file system server maintains three structures
// for each open file.
//
// 1. The on-disk 'struct File' is mapped into the part of memory
//    that maps the disk.  This memory is kept private to the file
//    server.
// 2. Each open file has a 'struct Fd' as well, which sort of
//    corresponds to a Unix file descriptor.  This 'struct Fd' is kept
//    on *its own page* in memory, and it is shared with any
//    environments that have the file open.
// 3. 'struct OpenFile' links these other two structures, and is kept
//    private to the file server.  The server maintains an array of
//    all open files, indexed by "file ID".  (There can be at most
//    MAXOPEN files open concurrently.)  The client uses file IDs to
//    communicate with the server.  File IDs are a lot like
//    environment IDs in the kernel.  Use openfile_lookup to translate
//    file IDs to struct OpenFile.

struct OpenFile {
	uint32_t o_fileid;	// file id
	struct File *o_file;	// mapped descriptor for open file
	int o_mode;		// open mode
	struct Fd *o_fd;	// Fd page
};

// Max number of open files in the file system at once
#define MAXOPEN		1024
#define FILEVA		0xD0000000

// initialize to force into data section
struct OpenFile opentab[MAXOPEN] = {
	{ 0, 0, 1, 0 }
};

// Virtual address at which to receive page mappings containing client requests.
union Fsipc *fsreq = (union Fsipc *)0x0ffff000;

void
serve_init(void)
{
	int i;
	uintptr_t va = FILEVA;
	for (i = 0; i < MAXOPEN; i++) {
		opentab[i].o_fileid = i;
		opentab[i].o_fd = (struct Fd*) va;
		va += PGSIZE;
	}
}

// Allocate an open file.
int
openfile_alloc(struct OpenFile **o)
{
	int i, r;

	// Find an available open-file table entry
	for (i = 0; i < MAXOPEN; i++) {
		switch (pageref(opentab[i].o_fd)) {
		case 0:
			if ((r = sys_page_alloc(0, opentab[i].o_fd, PTE_P|PTE_U|PTE_W)) < 0)
				return r;
			/* fall through */
		case 1:
			opentab[i].o_fileid += MAXOPEN;
			*o = &opentab[i];
			memset(opentab[i].o_fd, 0, PGSIZE);
			return (*o)->o_fileid;
		}
	}
	return -E_MAX_OPEN;
}

// Look up an open file for envid.
int
openfile_lookup(envid_t envid, uint32_t fileid, struct OpenFile **po)
{
	struct OpenFile *o;

	o = &opentab[fileid % MAXOPEN];
	if (pageref(o->o_fd) <= 1 || o->o_fileid != fileid)
		return -E_INVAL;
	*po = o;
	return 0;
}

// Open req->req_path in mode req->req_omode, storing the Fd page and
// permissions to return to the calling environment in *pg_store and
// *perm_store respectively.
int
serve_open(envid_t envid, struct Fsreq_open *req, void **pg_store, int *perm_store)
{
	char path[MAXPATHLEN];
	struct File *f;
	int fileid;
	int r;
	struct OpenFile *o;

	if (debug)
		cprintf("serve_open %08x %s 0x%x\n", envid, req->req_path, req->req_omode);

	// Copy in the path, making sure it's null-terminated
	memmove(path, req->req_path, MAXPATHLEN);
	path[MAXPATHLEN-1] = 0;

	// Find an open file ID
	//分配一个OpenFile结构，待会就可以用sturct Fd中的fileid将这个结构
	//绑定起来，从而就可以用struct Fd的fileid来寻找对应的struct File了
	//返回值为OpenFile的fileod　field.
	if ((r = openfile_alloc(&o)) < 0) {
		if (debug)
			cprintf("openfile_alloc failed: %e", r);
		return r;
	}


	fileid = r;

	// Open the file
	if (req->req_omode & O_CREAT) {
		if ((r = file_create(path, &f)) < 0) {
			if (!(req->req_omode & O_EXCL) && r == -E_FILE_EXISTS)
				goto try_open;
			if (debug)
				cprintf("file_create failed: %e", r);
			return r;
		}
	} else {
try_open:
		if ((r = file_open(path, &f)) < 0) {
			if (debug)
				cprintf("file_open failed: %e", r);
			return r;
		}
	}

	// Truncate
	if (req->req_omode & O_TRUNC) {
		if ((r = file_set_size(f, 0)) < 0) {
			if (debug)
				cprintf("file_set_size failed: %e", r);
			return r;
		}
	}
	if ((r = file_open(path, &f)) < 0) {
		if (debug)
			cprintf("file_open failed: %e", r);
		return r;
	}

	// Save the file pointer
	o->o_file = f;

	// Fill out the Fd structure
	//通过struct Fd中的fd_file.id和OpenFile中的o_fileid
	//将file descriptor和某个文件关联起来(即struct File)
	o->o_fd->fd_file.id = o->o_fileid;
	o->o_fd->fd_omode = req->req_omode & O_ACCMODE;
	o->o_fd->fd_dev_id = devfile.dev_id;
	o->o_mode = req->req_omode;

	if (debug)
		cprintf("sending success, page %08x\n", (uintptr_t) o->o_fd);

	// Share the FD page with the caller by setting *pg_store,
	// store its permission in *perm_store
	//*pg_store返回到fsipc(FSREQ_OPEN, fd)的fd当中
	// pg_store和perm_store都将会经过ipc_send发送回给调用者
	// 权限PTE_P|PTE_U|PTE_W|PTE_SHARE的意思就是说明struct Fd fd
	// 将会是在子进程和父进程共享
	*pg_store = o->o_fd;
	*perm_store = PTE_P|PTE_U|PTE_W|PTE_SHARE;

	return 0;
}

// Set the size of req->req_fileid to req->req_size bytes, truncating
// or extending the file as necessary.
int
serve_set_size(envid_t envid, struct Fsreq_set_size *req)
{
	struct OpenFile *o;
	int r;
	
	if (debug)
		cprintf("serve_set_size %08x %08x %08x\n", envid, req->req_fileid, req->req_size);

	// Every file system IPC call has the same general structure.
	// Here's how it goes.

	// First, use openfile_lookup to find the relevant open file.
	// On failure, return the error code to the client with ipc_send.
	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;

	// Second, call the relevant file system function (from fs/fs.c).
	// On failure, return the error code to the client.
	return file_set_size(o->o_file, req->req_size);
}

// Read at most ipc->read.req_n bytes from the current seek position
// in ipc->read.req_fileid.  Return the bytes read from the file to
// the caller in ipc->readRet, then update the seek position.  Returns
// the number of bytes successfully read, or < 0 on error.
int
serve_read(envid_t envid, union Fsipc *ipc)
{	
	/*
		我们可能会好奇我们在哪里初始化了这些Fsipc中的各个参数
		一个文件读取的逻辑是,以ls程序为例子，ls中调用read()
		read()函数中有参数buf(缓冲区的地址)，n(要读取的字节数)
		接着read()函数最后调用了dev_read函数，这里以读取硬盘文件为例子
		接着跳转到了file.c/devfile_read函数，在这里将接收到的buf,和n都
		赋值给了Fsipc，这样一来就就完成了参数初始化
	*/

	struct Fsreq_read *req = &ipc->read;
	struct Fsret_read *ret = &ipc->readRet;
	struct OpenFile *o;
	int r;
	if (debug)
		cprintf("serve_read %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// Lab 5: Your code here:
	//根据file_id来寻找对应的openfile
	if((r = openfile_lookup(envid,req->req_fileid,&o)) < 0) {
		return r;
	}

	//调用file_read来读取文件，file_read每一次读取文件都会修改offset
	//file_read的是读取了多少字节，读取到的内容放到了ret->ret_buf当中
	if((r = file_read(o->o_file,ret->ret_buf,req->req_n,o->o_fd->fd_offset)) < 0) {
		return r;
	}
	//修改当前 file descriptor对应的offset
	o->o_fd->fd_offset += r;
	return r;
}


// Write req->req_n bytes from req->req_buf to req_fileid, starting at
// the current seek position, and update the seek position
// accordingly.  Extend the file if necessary.  Returns the number of
// bytes written, or < 0 on error.
int
serve_write(envid_t envid, struct Fsreq_write *req)
{	
	if (debug)
		cprintf("serve_write %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// LAB 5: Your code here.
	struct OpenFile *o;
	int r;
	if((r = openfile_lookup(envid,req->req_fileid,&o)) < 0) {
		return r;
	}
	
	//将输入从req->req_buf写入req->req_n字节的数据到文件o->o_file中，写入完毕修改o->o_fd->fd_offset
	if((r = file_write(o->o_file,req->req_buf,req->req_n,o->o_fd->fd_offset)) < 0) {
		return r;
	}
	o->o_fd->fd_offset += r;

	//返回读取了多少字节
	return r;
	// panic("serve_write not implemented");
}

// Stat ipc->stat.req_fileid.  Return the file's struct Stat to the
// caller in ipc->statRet.
int
serve_stat(envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_stat *req = &ipc->stat;
	struct Fsret_stat *ret = &ipc->statRet;
	struct OpenFile *o;
	int r;

	if (debug)
		cprintf("serve_stat %08x %08x\n", envid, req->req_fileid);

	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;

	strcpy(ret->ret_name, o->o_file->f_name);
	ret->ret_size = o->o_file->f_size;
	ret->ret_isdir = (o->o_file->f_type == FTYPE_DIR);
	return 0;
}

// Flush all data and metadata of req->req_fileid to disk.
int
serve_flush(envid_t envid, struct Fsreq_flush *req)
{
	struct OpenFile *o;
	int r;

	if (debug)
		cprintf("serve_flush %08x %08x\n", envid, req->req_fileid);

	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;
	file_flush(o->o_file);
	return 0;
}


int
serve_sync(envid_t envid, union Fsipc *req)
{
	fs_sync();
	return 0;
}

//定义了一个名字叫做fshandler的结构体指针,返回值为int,参数为envid 以及一个 union Fsipc
typedef int (*fshandler)(envid_t envid, union Fsipc *req);


/*
//初始化了一个上面那个函数指针的数组,[FSREQ_READ] =		serve_read,
的意思就是根据某个下标来设置其对应的值,上面就是说[FSREQ_READ]对应的函数指针设置为serve_read
handlers中的每个函数有不一样的参数,union Fsipc的作用就是可以兼容,以至于每一个不同的函数都可以用相同的
参数传入.
*/
fshandler handlers[] = {
	// Open is handled specially because it passes pages
	/* [FSREQ_OPEN] =	(fshandler)serve_open, */
	[FSREQ_READ] =		serve_read,
	[FSREQ_STAT] =		serve_stat,
	[FSREQ_FLUSH] =		(fshandler)serve_flush,
	[FSREQ_WRITE] =		(fshandler)serve_write,
	[FSREQ_SET_SIZE] =	(fshandler)serve_set_size,
	[FSREQ_SYNC] =		serve_sync
};

void
serve(void)
{
	uint32_t req, whom;
	int perm, r;
	void *pg;

	while (1) {
		perm = 0;
		/*
			在file.c中的fsipc()函数参数将会做ipc_send()中的参数value传到这里
			ipc_recv()的返回值就是这个value,这里也就是说这里的req就是RPC的类型
			对应上面的FSREQ_READ,FSREQ_STAT,FSREQ_FLUSH等等

			进程whom发送的数据放到fsreq
		*/
		req = ipc_recv((int32_t *) &whom, fsreq, &perm);
		if (debug)
			cprintf("fs req %d from %08x [page %08x: %s]\n",
				req, whom, uvpt[PGNUM(fsreq)], fsreq);

		// All requests must contain an argument page
		if (!(perm & PTE_P)) {
			cprintf("Invalid request from %08x: no argument page\n",
				whom);
			continue; // just leave it hanging...
		}

		pg = NULL;
		if (req == FSREQ_OPEN) {
			//serve_open的参数有点不一样,所以单独处理
			//如果成功，serve_open的返回值0，如果没成功，返回值<0
			r = serve_open(whom, (struct Fsreq_open*)fsreq, &pg, &perm);
		} else if (req < ARRAY_SIZE(handlers) && handlers[req]) {
			//根据req作为索引来从handerls这个数组中选择对应的handlers
			r = handlers[req](whom, fsreq);
		} else {
			cprintf("Invalid request code %d from %08x\n", req, whom);
			r = -E_INVAL;
		}
		//向发送者发送数据r,表示当前程序已经接受到消息.
		//在写入或者读取的时候，这个ｒ表示成功写入或者读取的数据字节数
		ipc_send(whom, r, pg, perm);
		
		//将fsreq所对应的地址取消映射,留给下次使用
		//在env2envid中,0表示当前进程
		sys_page_unmap(0, fsreq);
	}
}

void
umain(int argc, char **argv)
{
	static_assert(sizeof(struct File) == 256);
	binaryname = "fs";
	cprintf("FS is running\n");

	// Check that we are able to do I/O
	outw(0x8A00, 0x8A00);
	cprintf("FS can do I/O\n");

	serve_init();
	fs_init();
	serve();
}

