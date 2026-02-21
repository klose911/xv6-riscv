//
// Support functions for system calls that involve file descriptors.
//
// 支持和文件描述符相关的系统调用的辅助函数 

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

/**
 * @brief devsw 的结构体数组
 * 
 * 每个元素代表一个设备的操作集合（如读、写等函数指针）
 * 
 * 内核通过查找 devsw 数组，可以根据设备号快速定位并调用对应的设备驱动函数，实现统一的设备访问和管理
 * 这样设计有助于扩展和维护不同类型的设备驱动，提高系统的灵活性和可移植性
 * 
 */
struct devsw devsw[NDEV];

/**
 * @brief ftable 的全局结构体变量
 * 
 * 用于管理内核中的文件表
 * 
 */
struct {
  struct spinlock lock; // 文件表的自旋锁
  // 每个元素代表一个打开的文件，记录文件的状态、类型、引用计数、读写位置等信息
  struct file file[NFILE]; // 文件结构体数组，包含 NFILE 个文件对象
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable"); // 初始化文件表的自旋锁
}

// Allocate a file structure.
// 为文件分配一个新的文件结构体
struct file*
filealloc(void)
{
  struct file *f; // 文件结构体指针

  acquire(&ftable.lock); // 获取文件表的自旋锁
  for(f = ftable.file; f < ftable.file + NFILE; f++){ // 遍历文件表中的每个文件结构体
    if(f->ref == 0){ // 找到一个未被使用的文件结构体
      f->ref = 1; // 将引用计数设置为 1，表示该文件结构体已被分配
      release(&ftable.lock); // 释放文件表的自旋锁
      return f; // 返回分配的文件结构体指针
    }
  }
  release(&ftable.lock); // 释放文件表的自旋锁
  return 0; // 没有可用的文件结构体，返回 NULL
}

// Increment ref count for file f.
// 增加文件的引用计数
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock); // 获取文件表的自旋锁
  if(f->ref < 1) // 引用计数小于 1，表示文件结构体无效
    panic("filedup"); // 内核奔溃
  f->ref++; // 增加引用计数
  release(&ftable.lock); // 释放文件表的自旋锁
  return f; // 返回传入的文件结构体指针
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock); // 获取文件表的自旋锁
  if(f->ref < 1) // 引用计数小于 1，表示文件结构体无效
    panic("fileclose"); // 内核奔溃
  if(--f->ref > 0){ // 递减引用计数后仍大于 0，表示文件仍被其他地方引用
    release(&ftable.lock); // 释放文件表的自旋锁
    return; // 仍有引用，直接返回
  }
  ff = *f; // 复制文件结构体内容以备后续使用
  f->ref = 0; // 将引用计数设置为 0，表示文件结构体已被关闭
  f->type = FD_NONE; // 重置文件类型为未使用
  release(&ftable.lock); // 释放文件表的自旋锁

  if(ff.type == FD_PIPE){ // 管道类型文件
    pipeclose(ff.pipe, ff.writable); // 关闭管道的一端 
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){ // 索引节点文件或设备文件
    begin_op(); // 开始文件系统操作事务
    iput(ff.ip); // 释放索引节点
    end_op(); // 结束文件系统操作事务
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
// 获取文件的元数据 addr 指向 struct stat 结构体，这是一个用户进程的虚拟地址
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc(); // 获取当前进程指针
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){ // inode节点文件或设备文件
    ilock(f->ip); // inode加锁
    stati(f->ip, &st); // 读取inode的元数据信息到stat结构体
    iunlock(f->ip); // 释放inode锁
    // 将stat结构体复制到用户空间
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0) // 复制出错
      return -1; 
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
// 从文件 f 中读取数据，addr 是用户进程的虚拟地址
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0) // 文件不可读
    return -1; // 返回错误 -1 

  if(f->type == FD_PIPE){ // 管道类型文件
    r = piperead(f->pipe, addr, n); // 从管道中读取数据 
  } else if(f->type == FD_DEVICE){ // 设备类型文件
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read) // 设备号无效 || 不支持读操作
      return -1; // 返回错误 -1
    r = devsw[f->major].read(1, addr, n); // 调用设备驱动的读函数读取数据
  } else if(f->type == FD_INODE){ // inode节点文件
    ilock(f->ip); // inode加锁
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0) // 从inode节点读取数据
      f->off += r; // 更新文件偏移量
    iunlock(f->ip); // 释放inode锁
  } else {
    panic("fileread"); // 内核奔溃，表示遇到未知的文件类型
  }

  return r; // 返回实际读取的字节数
}

// Write to file f.
// addr is a user virtual address.
// 将数据写入文件 f 中，addr 是用户进程的虚拟地址
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0) // 文件不可写
    return -1;

  if(f->type == FD_PIPE){ // 管道类型文件
    ret = pipewrite(f->pipe, addr, n); // 向管道中写入数据
  } else if(f->type == FD_DEVICE){ // 设备类型文件
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write) // 设备号无效 || 不支持写操作
      return -1; 
    ret = devsw[f->major].write(1, addr, n); // 调用设备驱动的写函数写入数据
  } else if(f->type == FD_INODE){ // inode节点文件
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    // 为避免超过最大日志事务大小，分块写入数据，包括 i-node、间接块、分配块，以及用于非对齐写入的 2 个块的空间 
    // 这部分逻辑实际上应该放在更底层，因为 writei() 可能正在写入类似控制台的设备 
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE; // 计算每次写入的最大块数
    int i = 0; // 已写入的字节数
    while(i < n){ // 循环写入数据
      int n1 = n - i; // 剩余要写入的字节数
      if(n1 > max) // 如果剩余字节数超过最大块数
        n1 = max; // 限制为最大块数

      begin_op(); // 开始文件系统操作事务
      ilock(f->ip); // inode加锁
      // 写入数据到inode节点
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0) // 写入成功 
        f->off += r; // 更新文件偏移量
      iunlock(f->ip); // 释放inode锁
      end_op(); // 结束文件系统操作事务

      if(r != n1){ // 写入字节数与预期不符
        // error from writei
        // writei 出现错误，退出写入循环
        break; 
      }
      i += r; // 更新已写入的字节数
    }
    ret = (i == n ? n : -1); // 如果全部写入成功，返回写入字节数，否则返回 -1
  } else {
    panic("filewrite"); // 内核奔溃，表示遇到未知的文件类型
  }

  return ret;
}

