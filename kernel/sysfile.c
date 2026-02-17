//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

// 文件系统相关的系统调用
// 大部分是参数校验，因为内核不能信任用户态代码
// 之后调用file.c 和 fs.c 中的函数
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

/**
 * @brief 获取第 n 个系统调用参数，作为文件描述符使用 
 * 
 * @param n 参数索引
 * @param pfd 返回文件描述符的指针
 * @param pf 返回文件结构体指针
 * @return int 成功返回 0，失败返回 -1
 */
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd); // 获取第 n 个参数，作为文件描述符
  // 文件描述符 fd 必须有效且对应一个打开的文件
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0) // 检查文件描述符有效性
    return -1; // 失败返回 -1
  if(pfd) // 如果 pfd 不为 NULL，则将文件描述符存储到 pfd 指向的地址
    *pfd = fd; 
  if(pf) // 如果 pf 不为 NULL，则将文件结构体指针存储到 pf 指向的地址
    *pf = f; 
  return 0; // 成功返回 0
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
// 为给定的文件分配一个文件描述符
// 成功时接管调用者的文件引用

/**
 * @brief 为给定的文件分配一个文件描述符
 * 
 * @param f 需要分配文件描述符的文件结构体指针
 * @return int 成功返回分配的文件描述符，失败返回 -1
 */
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){ // 遍历当前进程的文件描述符表，寻找一个空闲的文件描述符
    if(p->ofile[fd] == 0){ // 如果找到一个空闲的文件描述符
      p->ofile[fd] = f; // 将文件结构体指针存储到当前进程的文件描述符表中
      return fd; // 返回分配的文件描述符
    }
  }
  return -1; // 没有可用的文件描述符，返回 -1
}

/**
 * @brief 复制文件描述符Unix 系统中的 dup 系统调用
 * 
 * @return uint64 成功返回新的文件描述符，失败返回 -1
 */
uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0) // 获取第 0 个参数作为文件描述符，并获取对应的文件结构体指针
    return -1; // 获取失败返回 -1
  if((fd=fdalloc(f)) < 0) // 为该文件分配一个新的文件描述符
    return -1; // 分配失败返回 -1
  filedup(f); // 增加文件的引用计数，表示该文件现在有一个新的引用
  return fd; // 返回新的文件描述符
}

/**
 * @brief 从文件 f 中读取数据，addr 是用户进程的虚拟地址
 * 
 * @return uint64 实际读取的字节数，读取失败返回 -1 
 */
uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p); // 获取第 1 个参数，作为用户空间的虚拟地址
  argint(2, &n); // 获取第 2 个参数，作为要读取的最大字节数
  if(argfd(0, 0, &f) < 0) // 获取第 0 个参数作为文件描述符，并获取对应的文件结构体指针
    return -1; // 获取失败返回 -1
  // 调用 fileread 函数从文件 f 中读取数据到用户空间地址 p
  return fileread(f, p, n); // 返回实际读取的字节数
}

/**
 * @brief 从文件 f 中写入数据，addr 是用户进程的虚拟地址
 * 
 * @return uint64 实际写入的字节数，写入失败返回 -1
 */
uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p); // 获取第 1 个参数，作为用户空间的虚拟地址
  argint(2, &n); // 获取第 2 个参数，作为要写入的最大字节数
  if(argfd(0, 0, &f) < 0) // 获取第 0 个参数作为文件描述符，并获取对应的文件结构体指针
    return -1; // 获取失败返回 -1
  // 调用 filewrite 函数将用户空间地址 p 中的数据写入文件
  return filewrite(f, p, n); // 返回实际写入的字节数
}

/**
 * @brief 获取文件状态信息并复制到用户空间缓冲区
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0) // 获取第 0 个参数作为文件描述符，并获取对应的文件结构体指针
    return -1; // 获取失败返回 -1
  // 将当前进程的文件描述符表中对应的条目设置为 NULL，表示该文件描述符不再指向任何文件
  myproc()->ofile[fd] = 0; 
  fileclose(f); // 关闭文件，减少文件的引用计数，如果引用计数为 0 则释放文件资源
  return 0; // 成功返回 0
}

/**
 * @brief 获取文件状态信息并复制到用户空间缓冲区
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat 用户空间指向 struct stat 结构体的指针

  argaddr(1, &st); // 获取第 1 个参数，作为用户空间的虚拟地址
  if(argfd(0, 0, &f) < 0) // 获取第 0 个参数作为文件描述符，并获取对应的文件结构体指针
    return -1; // 获取失败返回 -1
  return filestat(f, st); // 调用 filestat 函数获取文件状态信息并复制到用户空间缓冲区
}

// Create the path new as a link to the same inode as old.
// 创建路径 new 作为指向与 old 相同 inode 的链接 

/**
 * @brief 创建路径 new 作为指向与 old 相同 inode 的链接
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  // 获取第 0 个和第 1 个参数，作为旧路径和新路径
  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0) 
    return -1;

  begin_op(); // 开始文件系统操作事务
  if((ip = namei(old)) == 0){ // 查找旧路径对应的 inode 
    end_op(); // 查找失败，结束文件系统操作事务
    return -1; 
  }

  ilock(ip); // 锁定旧路径的 inode
  if(ip->type == T_DIR){ // 不允许为目录创建硬链接
    iunlockput(ip); // 释放锁并递减引用计数
    end_op(); // 结束文件系统操作事务
    return -1;
  }

  ip->nlink++; // 增加 inode 的链接计数
  iupdate(ip); // 更新 inode 信息到磁盘
  iunlock(ip); // 释放锁

  if((dp = nameiparent(new, name)) == 0) // 查找新路径的父目录 inode 
    goto bad; // 失败跳转到 bad 标签
  ilock(dp); // 锁定新路径的父目录 inode
  // 创建新目录项，链接到旧路径的 inode
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){ // 设备号不匹配或创建目录项失败
    iunlockput(dp); // 释放锁并递减引用计数
    goto bad; // 跳转到 bad 标签
  }
  iunlockput(dp); // 释放锁并递减引用计数
  iput(ip); // 释放旧路径的 inode

  end_op(); // 结束文件系统操作事务
 
  return 0; // 成功返回 0

bad:
  ilock(ip); // 锁定旧路径的 inode
  ip->nlink--; // 减少链接计数
  iupdate(ip); // 更新 inode 信息到磁盘
  iunlockput(ip); // 释放锁并递减引用计数
  end_op(); // 结束文件系统操作事务
  return -1; // 失败返回 -1
}

// Is the directory dp empty except for "." and ".." ?

/**
 * @brief 判断目录 dp 是否为空，除了 "." 和 ".."
 * 
 * @param dp 目录的 inode 指针
 * @return int 为空返回 1，不为空返回 0
 */
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){ // 跳过前两个目录项 "." 和 ".."
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) // 读取目录项失败
      panic("isdirempty: readi"); // 内核奔溃
    if(de.inum != 0) // 目录项不为空
      return 0; // 不为空返回 0
  }
  return 1; // 为空返回 1
}

/**
 * @brief 删除路径对应的文件或目录
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_unlink(void)
{
  struct inode *ip, *dp; 
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0) // 获取第 0 个参数，作为要删除的路径
    return -1; // 失败返回 -1

  begin_op(); // 开始文件系统操作事务
  if((dp = nameiparent(path, name)) == 0){ // 查找路径的父目录 inode
    end_op(); // 失败结束文件系统操作事务
    return -1; // 失败返回 -1
  }

  ilock(dp); // 锁定父目录 inode

  // Cannot unlink "." or "..".
  // 不能删除 "." 或 ".."
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0) // 检查名称是否为 "." 或 ".."
    goto bad; // 跳转到 bad 标签

  if((ip = dirlookup(dp, name, &off)) == 0) // 查找要删除的目录项对应的 inode
    goto bad; // 跳转到 bad 标签
  ilock(ip); // 锁定要删除的 inode

  if(ip->nlink < 1) // 链接计数小于 1，表示文件系统状态异常
    panic("unlink: nlink < 1"); // 内核奔溃
  if(ip->type == T_DIR && !isdirempty(ip)){ // 不能删除非空目录
    iunlockput(ip); // 释放锁并递减引用计数
    goto bad; // 跳转到 bad 标签
  }

  memset(&de, 0, sizeof(de)); // 清空目录项结构体
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) // 写入空目录项
    panic("unlink: writei"); // 写入失败，内核奔溃
  if(ip->type == T_DIR){ // 如果删除的是目录
    dp->nlink--; // 减少父目录的链接计数
    iupdate(dp); // 更新父目录 inode 信息到磁盘
  }
  iunlockput(dp); // 释放父目录锁并递减引用计数

  ip->nlink--; // 减少要删除的 inode 的链接计数
  iupdate(ip); // 更新要删除的 inode 信息到磁盘
  iunlockput(ip); // 释放要删除的 inode 锁并递减引用计数

  end_op(); // 结束文件系统操作事务

  return 0; // 成功返回 0

bad:
  iunlockput(dp); // 释放父目录锁并递减引用计数
  end_op(); // 结束文件系统操作事务
  return -1; // 失败返回 -1
}

/**
 * @brief 创建路径对应的文件或目录
 * 
 * @param path 文件或目录的路径
 * @param type 文件类型，T_FILE 表示普通文件，T_DIR 表示目录，T_DEVICE 表示设备文件
 * @param major 设备文件的主设备号，普通文件和目录可以设置为 0
 * @param minor 设备文件的次设备号，普通文件和目录可以设置为 0
 * @return struct inode* 成功返回新创建的 inode 指针，失败返回 NULL
 */
static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp; // ip 是新创建的 inode，dp 是新创建的文件或目录所在的父目录 inode
  char name[DIRSIZ]; // 存储新创建的文件或目录的名称

  if((dp = nameiparent(path, name)) == 0) // 查找路径的父目录 inode 和新创建的文件或目录的名称
    return 0; // 查找失败返回 NULL

  ilock(dp); // 锁定父目录 inode

  if((ip = dirlookup(dp, name, 0)) != 0){ // 检查父目录中是否已经存在同名的文件或目录
    iunlockput(dp); // 释放父目录锁并递减引用计数
    ilock(ip); // 锁定已存在的 inode
    // 创建的是普通文件, 但已存在的是目录或设备文件
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE)) 
      return ip; // 返回已存在的 inode 指针
    iunlockput(ip); // 释放已存在的 inode 锁并递减引用计数
    return 0; // 已存在的 inode 类型不匹配，返回 NULL
  }

  if((ip = ialloc(dp->dev, type)) == 0){ // 分配一个新的 inode 失败
    iunlockput(dp); // 释放父目录锁并递减引用计数
    return 0; // 分配失败返回 NULL
  }

  ilock(ip); // 锁定新创建的 inode
  ip->major = major; // 设置主设备号
  ip->minor = minor; // 设置次设备号  
  ip->nlink = 1; // 初始化链接计数为 1
  iupdate(ip); // 更新新创建的 inode 信息到磁盘

  if(type == T_DIR){  // Create . and .. entries. 创建 "." 和 ".." 目录项
    // No ip->nlink++ for ".": avoid cyclic ref count. 
    // "." 的链接计数不增加，避免循环引用计数
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0) 
      goto fail;  // 创建目录项失败则跳转到 fail 标签进行清理
  }

  if(dirlink(dp, name, ip->inum) < 0) // 在父目录中创建新目录项失败
    goto fail; // 跳转到 fail 标签进行清理

  // 如果创建的是目录，为了加速执行，所以这里采用的是对每个inode的细粒度的锁
  if(type == T_DIR){ 
    // now that success is guaranteed:
    dp->nlink++;  // for ".." // 增加父目录的链接计数（新增的目录项）
    iupdate(dp); // 更新父目录 inode 信息到磁盘
  }

  iunlockput(dp); // 释放父目录锁并递减引用计数

  return ip; // 成功返回新创建的 inode 指针

 fail: // 失败处理
  // something went wrong. de-allocate ip.
  ip->nlink = 0; // 将新创建的 inode 的链接计数设置为 0，表示该 inode 不再被任何目录项引用
  iupdate(ip); // 更新新创建的 inode 信息到磁盘
  iunlockput(ip); // 释放新创建的 inode 锁并递减引用计数，最终会被 ialloc 分配的新 inode 条目回收
  iunlockput(dp); // 释放父目录锁并递减引用计数
  return 0; // 失败返回 NULL
}

/**
 * @brief 打开路径对应的文件，返回文件描述符
 * 
 * @return uint64 成功返回文件描述符，失败返回 -1
 */
uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode); // 获取第 1 个参数，作为打开文件的模式
  if((n = argstr(0, path, MAXPATH)) < 0) // 获取第 0 个参数，作为要打开的路径 
    return -1; // 获取失败返回 -1

  begin_op(); // 开始文件系统操作事务

  if(omode & O_CREATE){ // 如果打开模式包含 O_CREATE 标志，表示需要创建文件
    ip = create(path, T_FILE, 0, 0); // 创建普通文件，主设备号和次设备号设置为 0
    if(ip == 0){ // 创建失败
      end_op(); // 结束文件系统操作事务
      return -1; // 创建失败返回 -1
    }
  } else {
    if((ip = namei(path)) == 0){ // 查找路径对应的 inode 失败
      end_op(); // 结束文件系统操作事务
      return -1; // 查找失败返回 -1
    }
    ilock(ip); // 锁定找到的 inode
    if(ip->type == T_DIR && omode != O_RDONLY){ // 不允许以非只读模式打开目录
      iunlockput(ip); // 释放锁并递减引用计数
      end_op(); // 结束文件系统操作事务
      return -1; // 失败返回 -1
    }
  }

  // 如果打开的是设备文件，检查设备号是否合法
  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip); // 释放锁并递减引用计数
    end_op(); // 结束文件系统操作事务
    return -1; // 失败返回 -1
  }

  // 为打开的文件分配一个文件描述符
  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f) // 如果 filealloc 成功但 fdalloc 失败，需要释放分配的文件结构体
      fileclose(f); // 释放文件结构体
    iunlockput(ip); // 释放 inode 锁并递减引用计数
    end_op(); // 结束文件系统操作事务
    return -1; // 失败返回 -1
  }

  if(ip->type == T_DEVICE){ // 如果打开的是设备文件
    f->type = FD_DEVICE; // 设置文件类型为设备文件
    f->major = ip->major; // 设置设备文件的主设备号
  } else {
    f->type = FD_INODE; // 设置文件类型为普通文件或目录
    f->off = 0; // 初始化文件偏移量为 0
  }
  f->ip = ip; // 将文件结构体的 ip 字段指向打开的 inode
  f->readable = !(omode & O_WRONLY); // 如果打开模式不包含 O_WRONLY，则文件可读
  // 如果打开模式包含 O_WRONLY 或 O_RDWR，则文件可写
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR); 

  // 如果打开模式包含 O_TRUNC 标志，并且打开的是普通文件，则需要将文件内容截断为 0
  if((omode & O_TRUNC) && ip->type == T_FILE){ 
    itrunc(ip); // 将文件内容截断为 0
  }

  iunlock(ip); // 释放 inode 锁，文件结构体 f 已经持有对该 inode 的引用，不需要保持锁定状态 
  end_op(); // 结束文件系统操作事务

  return fd; // 成功返回分配的文件描述符
}

/**
 * @brief 创建一个目录
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op(); // 开始文件系统操作事务
  // 获取第 0 个参数，作为要创建的目录的路径，并调用 create 函数创建目录
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op(); // 失败，结束文件系统操作事务
    return -1; // 失败返回 -1
  }
  iunlockput(ip); // 释放新创建的目录 inode 的锁并递减引用计数
  end_op(); // 结束文件系统操作事务
  return 0; // 成功返回 0
}

/**
 * @brief 创建一个设备文件
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op(); // 开始文件系统操作事务
  argint(1, &major); // 获取第 1 个参数，作为设备文件的主设备号
  argint(2, &minor); // 获取第 2 个参数，作为设备文件的次设备号
  // 获取第 0 个参数，作为要创建的设备文件的路径，并调用 create 函数创建设备文件
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op(); // 失败，结束文件系统操作事务
    return -1; // 失败返回 -1
  }
  iunlockput(ip); // 释放新创建的设备文件 inode 的锁并递减引用计数
  end_op(); // 结束文件系统操作事务
  return 0; // 成功返回 0
}

/**
 * @brief 改变当前工作目录
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_chdir(void)
{
  char path[MAXPATH]; 
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op(); // 开始文件系统操作事务
  // 获取第 0 个参数，作为要切换到的目录的路径，并查找对应的 inode
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op(); // 失败，结束文件系统操作事务
    return -1; // 失败返回 -1
  }
  ilock(ip); // 锁定找到的 inode
  if(ip->type != T_DIR){ // 如果找到的 inode 不是目录
    iunlockput(ip); // 释放锁并递减引用计数
    end_op(); // 结束文件系统操作事务
    return -1; // 失败返回 -1
  }
  iunlock(ip); // 释放锁，当前进程的 cwd 字段将指向新的目录 inode，不需要保持锁定状态
  iput(p->cwd); // 释放当前进程原来的工作目录 inode 的引用
  end_op(); // 结束文件系统操作事务
  p->cwd = ip; // 将当前进程的工作目录设置为新的目录 inode
  return 0; // 成功返回 0
}

/**
 * @brief 执行路径对应的程序，替换当前进程的内存映像
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv); // 获取第 1 个参数，作为用户空间的虚拟地址，指向一个字符串数组，用于存储程序的命令行参数
   // 获取第 0 个参数，作为要执行的程序的路径
  if(argstr(0, path, MAXPATH) < 0) {
    return -1; // 获取失败返回 -1
  }
  memset(argv, 0, sizeof(argv)); // 初始化 argv 数组为 NULL
  for(i=0;; i++){ // 循环获取用户空间的命令行参数，直到遇到 NULL 结束
    if(i >= NELEM(argv)){ // 参数数量超过限制，返回错误
      goto bad; // 跳转到 bad 标签进行清理
    }
    // 从用户空间获取第 i 个参数的虚拟地址，存储到 uarg 中
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad; // 获取失败跳转到 bad 标签进行清理
    }
    if(uarg == 0){ // 遇到 NULL 结束参数获取
      argv[i] = 0; // 将 argv 数组中的对应条目设置为 NULL，表示参数列表结束
      break; // 结束参数获取
    }
    argv[i] = kalloc(); // 为参数字符串分配内核内存
    if(argv[i] == 0) // 内存分配失败，返回错误
      goto bad; // 跳转到 bad 标签进行清理
    if(fetchstr(uarg, argv[i], PGSIZE) < 0) // 从用户空间获取第 i 个参数的字符串，存储到内核内存中
      goto bad; // 获取失败跳转到 bad 标签进行清理
  }

  int ret = exec(path, argv); // 调用 exec 函数执行路径对应的程序，替换当前进程的内存映像，传递参数列表

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++) // 执行完成后，清理为参数字符串分配的内核内存
    kfree(argv[i]); // 释放内核内存

  return ret; // 返回 exec 函数的执行结果，成功返回 0，失败返回 -1

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++) // 在发生错误时，清理已经分配的内核内存
    kfree(argv[i]); // 释放内核内存
  return -1; // 失败返回 -1
}

/**
 * @brief 创建一个管道
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers 用户空间指向两个整数的数组的指针
  struct file *rf, *wf; 
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray); // 获取第 0 个参数，作为用户空间的虚拟地址，指向一个整数数组，用于存储返回的两个文件描述符
  if(pipealloc(&rf, &wf) < 0) // 调用 pipealloc 函数创建一个管道，返回读端和写端的文件结构体指针
    return -1; // 创建管道失败返回 -1
  fd0 = -1; // 初始化读端文件描述符为 -1，表示未分配
  // 为读端和写端分配文件描述符，如果分配失败需要进行清理
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0) // 如果读端文件描述符已经分配成功但写端分配失败，需要清理读端的文件描述符
      p->ofile[fd0] = 0; // 将读端文件描述符表中的条目设置为 NULL，表示该文件描述符不再指向任何文件
    fileclose(rf); // 关闭读端文件，减少文件的引用计数，如果引用计数为 0 则释放文件资源
    fileclose(wf); // 关闭写端文件，减少文件的引用计数，如果引用计数为 0 则释放文件资源
    return -1; // 分配文件描述符失败返回 -1
  }

  // 将分配的读端和写端文件描述符复制到用户空间的整数数组中，如果复制失败需要进行清理
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0; // 将读端文件描述符表中的条目设置为 NULL，表示该文件描述符不再指向任何文件
    p->ofile[fd1] = 0; // 将写端文件描述符表中的条目设置为 NULL，表示该文件描述符不再指向任何文件
    fileclose(rf); // 关闭读端文件，减少文件的引用计数，如果引用计数为 0 则释放文件资源
    fileclose(wf); // 关闭写端文件，减少文件的引用计数，如果引用计数为 0 则释放文件资源
    return -1; // 复制文件描述符失败返回 -1
  }
  return 0; // 成功返回 0
}
