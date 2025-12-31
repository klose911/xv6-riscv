#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

#define PIPESIZE 512 // 管道缓冲区大小，单位为字节

/**
 * @brief pipe 的结构体，用于实现内核中的管道（pipe）机制
 * 管道是一种进程间通信（IPC）方式，允许数据在进程之间以字节流的形式传递
 * 
 */
struct pipe {
  struct spinlock lock; // 保护管道数据结构的自旋锁，确保并发访问的安全性
  char data[PIPESIZE]; // 管道缓冲区，存储管道中的数据，大小为 PIPESIZE（512 字节）
  uint nread;     // number of bytes read 从管道中已读取的字节数
  uint nwrite;    // number of bytes written 写入管道的字节数
  int readopen;   // read fd is still open 读端文件描述符是否仍然打开
  int writeopen;  // write fd is still open 写端文件描述符是否仍然打开
};

int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *pi; // 管道结构体指针

  pi = 0; // 初始化管道指针为 NULL
  *f0 = *f1 = 0; // 读写文件描述符指针初始化为 NULL 
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0) // 分配文件结构体失败
    goto bad; // 错误处理
  if((pi = (struct pipe*)kalloc()) == 0) // 分配管道结构体失败
    goto bad; // 错误处理
  pi->readopen = 1; // 读端文件描述符仍然打开
  pi->writeopen = 1; // 写端文件描述符仍然打开
  pi->nwrite = 0; // 写入字节数初始化为 0
  pi->nread = 0; // 读取字节数初始化为 0
  initlock(&pi->lock, "pipe"); // 初始化管道自旋锁 
  // 最核心的一点：读和写的文件描述符共享同一个管道结构体
  (*f0)->type = FD_PIPE; // 设置读文件结构体类型为管道 (FD_PIPE)
  (*f0)->readable = 1; // 读端可读
  (*f0)->writable = 0; // 读端不可写
  (*f0)->pipe = pi; // 将管道结构体指针赋值给读文件结构体
  (*f1)->type = FD_PIPE; // 设置写文件结构体类型为管道 (FD_PIPE)
  (*f1)->readable = 0; // 写端不可读
  (*f1)->writable = 1; // 写端可写
  (*f1)->pipe = pi; // 将管道结构体指针赋值给写文件结构体
  return 0; // 成功返回 0

 bad:
  if(pi) // 管道结构体已分配，释放它
    kfree((char*)pi); // 释放管道结构体内存
  if(*f0) // 读文件结构体已分配，关闭它
    fileclose(*f0); // 关闭读文件结构体
  if(*f1) // 写文件结构体已分配，关闭它
    fileclose(*f1); // 关闭写文件结构体
  return -1; // 返回错误码 -1 
}

void
pipeclose(struct pipe *pi, int writable)
{
  acquire(&pi->lock); // 获取管道自旋锁
  if(writable){ // 关闭写端
    pi->writeopen = 0; // 写端文件描述符标记为关闭
    wakeup(&pi->nread); // 唤醒等待读取的进程
  } else {
    pi->readopen = 0; // 关闭读端，读端文件描述符标记为关闭
    wakeup(&pi->nwrite); // 唤醒等待写入的进程
  }
  if(pi->readopen == 0 && pi->writeopen == 0){ // 如果读写端都已关闭
    release(&pi->lock); // 释放管道自旋锁
    kfree((char*)pi); // 释放管道结构体内存
  } else
    release(&pi->lock); // 释放管道自旋锁
}

int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  int i = 0;
  struct proc *pr = myproc();

  acquire(&pi->lock); // 获取管道自旋锁
  while(i < n){ // 循环写入数据
    if(pi->readopen == 0 || killed(pr)){ // 如果读端已关闭或进程被杀死
      release(&pi->lock); // 释放管道自旋锁
      return -1; // 返回错误码 -1
    }
    // 如果管道已满，等待读端读取数据以腾出空间
    if(pi->nwrite == pi->nread + PIPESIZE){ //DOC: pipewrite-full
      wakeup(&pi->nread); // 唤醒等待读取的进程
      sleep(&pi->nwrite, &pi->lock); // 进入睡眠等待空间可用
    } else {
      char ch;
      // 从用户空间缓冲区读取一个字节
      if(copyin(pr->pagetable, &ch, addr + i, 1) == -1) 
        break; // 读取失败，退出循环
      // 将读取的字节写入管道缓冲区
      pi->data[pi->nwrite++ % PIPESIZE] = ch; 
      i++;
    }
  }
  wakeup(&pi->nread);  // 唤醒等待读取的进程
  release(&pi->lock); // 释放管道自旋锁

  return i;
}

int
piperead(struct pipe *pi, uint64 addr, int n)
{
  int i;
  struct proc *pr = myproc();
  char ch;

  acquire(&pi->lock); // 获取管道自旋锁
  // 等待直到管道中有数据可读或写端关闭 
  // 如果管道为空且写端仍然打开，则等待
  // pi->nread == pi->nwrite 读取的字节数和写入的字节数相等，表示管道为空 
  // 这里必须是循环判断，防止虚假唤醒
  while(pi->nread == pi->nwrite && pi->writeopen){  //DOC: pipe-empty
    if(killed(pr)){ // 如果进程被杀死，释放锁并返回错误
      release(&pi->lock); // 释放管道自旋锁
      return -1; // 返回错误码 -1
    }
    // 进入睡眠等待数据到来
    sleep(&pi->nread, &pi->lock); //DOC: piperead-sleep
  }
  // 从管道中读取数据到用户空间缓冲区
  for(i = 0; i < n; i++){  //DOC: piperead-copy
    if(pi->nread == pi->nwrite) // 管道中没有更多数据可读，退出循环
      break;
    ch = pi->data[pi->nread++ % PIPESIZE]; // 从管道缓冲区读取一个字节
    // 将读取的字节复制到用户空间缓冲区
    if(copyout(pr->pagetable, addr + i, &ch, 1) == -1)
      break;
  }
  // 唤醒等待写入的进程
  wakeup(&pi->nwrite);  //DOC: piperead-wakeup
  release(&pi->lock); // 释放管道自旋锁
  return i; // 返回实际读取的字节数
}
