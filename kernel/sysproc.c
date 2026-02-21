#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

/**
 * @brief 退出当前进程，终止进程的执行
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_exit(void)
{
  int n;
  argint(0, &n); // 获取第 0 个参数，作为进程的退出状态码
  exit(n); // 调用 exit 函数退出当前进程，传递退出状态码
  return 0;  // not reached 实际上该行代码永远不会被执行到，因为 exit 函数会终止当前进程的执行
}

/**
 * @brief 获取当前进程的进程 ID
 * 
 * @return uint64 当前进程的进程 ID
 */
uint64
sys_getpid(void)
{
  return myproc()->pid;
}

/**
 * @brief 创建一个新的进程，复制当前进程的内存映像
 * 
 * @return uint64 成功返回子进程的进程 ID，失败返回 -1
 */
uint64
sys_fork(void)
{
  return fork();
}

/**
 * @brief 等待子进程退出，并获取子进程的退出状态码
 * 
 * @return uint64 成功返回子进程的进程 ID，失败返回 -1
 */
uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p); // 获取第 0 个参数，作为用户空间的虚拟地址，指向一个整数，用于存储子进程的退出状态码
   // 调用 wait 函数等待子进程退出，并将子进程的退出状态码复制到用户空间地址 p 中
  return wait(p);
}

/**
 * @brief 动态分配/释放当前进程的内存
 * 
 * @return uint64 成功返回当前进程的内存大小，失败返回 -1
 */
uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n); // 获取第 0 个参数，作为要增加的内存字节数
   // 获取当前进程的内存大小，作为返回值
  addr = myproc()->sz;
  if(growproc(n) < 0) // 调用 growproc 函数增加当前进程的内存，如果失败返回 -1
    return -1;
  return addr; // 成功返回当前进程的内存大小
}

/**
 * @brief 让当前进程睡眠 n 个时钟周期
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n); // 获取第 0 个参数，作为要睡眠的时钟周期数
   // 如果 n 小于 0，直接返回错误
  if(n < 0)
    n = 0;
  acquire(&tickslock); // 获取 tickslock 锁，保护对全局变量 ticks 的访问
  ticks0 = ticks;
  while(ticks - ticks0 < n){ // 循环检查已经过去的时钟周期数是否达到 n，如果没有达到就继续睡眠
    if(killed(myproc())){ // 如果当前进程被杀死了，释放 tickslock 锁并返回错误
      release(&tickslock); // 释放 tickslock 锁
      return -1; // 返回 -1 表示睡眠失败
    }
    sleep(&ticks, &tickslock); // 当前进程进入睡眠状态，等待 ticks 变量的变化，sleep 函数会自动释放 tickslock 锁，并在被唤醒后重新获取该锁
  }
  release(&tickslock); // 释放 tickslock 锁
   // 成功返回 0
  return 0;
}

/**
 * @brief 发送一个信号给指定的进程，要求它终止执行
 * 
 * @return uint64 成功返回 0，失败返回 -1
 */
uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid); // 获取第 0 个参数，作为要杀死的进程的进程 ID
   // 调用 kill 函数发送一个信号给指定的进程，要求它终止执行
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
// 返回自系统启动以来发生的时钟中断的次数
/**
 * @brief 获取自系统启动以来发生的时钟中断的次数
 * 
 * @return uint64 自系统启动以来发生的时钟中断的次数
 */
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock); // 获取 tickslock 锁，保护对全局变量 ticks 的访问
   // 将全局变量 ticks 的值复制到局部变量 xticks 中，作为返回值
  xticks = ticks;
  release(&tickslock); // 释放 tickslock 锁
   // 返回自系统启动以来发生的时钟中断的次数
  return xticks;
}
