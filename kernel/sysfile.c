//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
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
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
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
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
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
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
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
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
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
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
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

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
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
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// mmap(虚拟地址, 映射长度, 权限, 写回, fd, 偏移)
uint64
sys_mmap(void) {
    uint64 va;
    int len, prot, flags, fd, off;

    if(argaddr(0, &va) < 0)    return -1;
    if (argint(1, &len) < 0 || argint(2, &prot) < 0
        || argint(3, &flags) < 0 || argint(4, &fd) < 0)
        return -1;
    if (argint(5, &off) < 0)    return -1;

    // 检查参数
    struct proc *p = myproc();
    if (len <= 0)    return -1;
    if (p->ofile[fd] == 0)    return -1;// fd 代表的打开文件不存在
    else    filedup(p->ofile[fd]);// 否则增加一次引用
    if (p->ofile[fd]->writable == 0// 不可写文件映射可写属性
        && (prot & PROT_WRITE) == PROT_WRITE
        && (flags & MAP_SHARED) == MAP_SHARED)    return -1;

    // 分配 vma 结构体
    int i;
    for (i = 0; i < MAXVMA; ++i) {
        if (p->vma[i].valid == 0) {
            p->vma[i].valid = 1;
            break;
        }
    }
    if (i == MAXVMA)    return -1;// vma 数组没有空闲位置

    // 填充 vma
    p->vma[i].va = p->curend - len;// 分配一个虚拟地址
    p->curend = p->vma[i].va;// 同时修正下一个虚拟地址
    p->vma[i].len = len;
    uint pteflags = 0;// 以下几句赋值 PTE 权限
    if ((prot & PROT_READ) == PROT_READ)    pteflags |= PTE_R;
    if ((prot & PROT_WRITE) == PROT_WRITE)    pteflags |= PTE_W;
    p->vma[i].prot = pteflags;
    p->vma[i].flags = flags;
    p->vma[i].fd = fd;
    p->vma[i].off = off;
    p->vma[i].f = p->ofile[fd];

    return (uint64)p->vma[i].va;
}

uint64
sys_munmap(void) {
    uint64 va;
    int len;

    if(argaddr(0, &va) < 0)    return -1;
    if (argint(1, &len) < 0)    return -1;

    // 因为 exit() 也要 unmap，所以主体逻辑封装为另一个函数
    if (subunmap(va, len) == -1)    return -1;

    return 0; 
}

// 因为头文件问题
// usertrap() 处理逻辑要写在包含 file.h 的文件里
uint64
pgfault(uint64 va) {
    struct proc *p = myproc();
    struct vma_t *v = 0;
    // 寻找出对应的 vma 元素
    for (int i = 0; i < MAXVMA; ++i) {
        if (p->vma[i].valid == 1 && p->vma[i].va <= va
            && va <= p->vma[i].va + p->vma[i].len) {
            v = &p->vma[i];
            break;
        }
    }
    if (v == 0)    return -1;

    // 分配一页
    // 安装映射
    // 将文件 4096B 写入（使用 readi()）
    uint64 pa = (uint64)kalloc();
    if (pa == 0)    return -1;
    memset((char *)pa, 0, PGSIZE);

    // 由于等会 readi() 是从 inode 读数据，输出至用户地址 va
    // 所以要先建立用户地址 va 到物理地址 pa 的映射
    if (mappages(p->pagetable, va, PGSIZE, pa, v->prot|PTE_U) == -1) {
        kfree((void *)pa);
        return -1;
    }
    // inode 数据写入 va
    ilock(v->f->ip);
    int ret = readi(v->f->ip, 1, va, v->off + va - v->va, PGSIZE);
    if (ret == -1) {
        kfree((void *)pa);
        iunlock(v->f->ip);
        return -1;
    }
    iunlock(v->f->ip);

    return 0;
}

uint64
subunmap(uint64 va, int len) {
    if (len == 0)    return 0;
    struct proc *p = myproc();
    struct vma_t *v = 0;
    // 寻找出对应的 vma 元素
    for (int i = 0; i < MAXVMA; ++i) {
        if (p->vma[i].valid == 1 && p->vma[i].va <= va
            && va <= p->vma[i].va + p->vma[i].len) {
            v = &p->vma[i];
            break;
        }
    }
    if (v == 0)    return -1;

    // 检查 va 对应的映射
    va = PGROUNDDOWN(va);
    pte_t *pte = walk(p->pagetable, va, 0);// 第一个取消页的 pte
    if (pte == 0)    return -1;
    uint64 pteflags = PTE_FLAGS(*pte);// 第一个取消页的 pte flags
    uint64 pa;
    if ((pa = walkaddr(p->pagetable, va)) != 0) {
        // 已安装映射，物理地址必存在，需要检查写回，再取消映射
        // 未安装映射，什么都不用做
        if (v->flags & MAP_SHARED) {
            if ((pteflags & PTE_D) == PTE_D) {
                int ret = filewrite(v->f, va, len);
                if (ret == -1)    return -1;
            }
        }

        // 取消映射
        uvmunmap(p->pagetable, va, len / PGSIZE, 1);
    }

    // 依照文件是部分 unmap 还是全部 unmap，情况不同
    // 判断是否是部分 unmap，可对比
    //     1.  vma 起址是否等同本次 unmap 起址
    //     2.  vma 长度是否等同本次 unmap 长度
    // 上述情况两两组合共四种情况
    if (v->va == va && v->len == len) {
        // vma 起址 == un 起址，且 vma 长度 == un 长度
        // 即完全 unmap 文件
        v->len -= len;
    } else if (v->va == va && v->len != len) {
        // vma 起址 == un 起址，且 vma 长度 != un 长度
        // 部分 unmap 文件，un 文件开头部分
        v->va += len;
        v->len -= len;
    } else if (v->va != va && v->len == len) {
        // vma 起址 != un 起址，且 vma 长度 == un 长度
        // 这种情况不存在
    } else if (v->va != va && v->len != len) {
        // vma 起址 != un 起址，且 vma 长度 != un 长度
        // 部分 unmap 文件，un 文件后面部分
        v->len -= len;
    }

    if (v->len == 0) {
        // 如果 unmap 整个文件
        v->va = 0;
        v->valid = 0;
        fileclose(v->f);

        // 调整 curend
        if (va == p->curend) {
        // 当最后一个文件 unmap
        p->curend += len;
        for (uint64 unva = PGROUNDDOWN(p->curend);
                unva < MAXVA - 2 * PGSIZE; unva += PGSIZE) {
            // 往上遍历每一个虚拟地址
            int i;
            for (i = 0; i < MAXVMA; ++i) {
            // 判断是否在 vma 数组中存在
            // 不存在的话，curend 上调
            if (p->vma[i].va == unva && p->vma[i].valid == 1)
                break;
            }
            if (i == MAXVMA)    p->curend += PGSIZE;// 不存在
            else    break;// 只要上面的 vma 区域有一个没取消，就停止上调
        }// end for()
        }// end if()
    }

    return 0;
}
