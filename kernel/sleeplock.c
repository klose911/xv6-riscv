// Sleeping locks

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock"); // 初始化互斥锁内部的自旋锁
  lk->name = name; // 设置互斥锁的名称
  lk->locked = 0; // 0 表示锁未被持有
  lk->pid = 0; // 初始化时没有进程持有锁
}

void
acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk); // 获取互斥锁内部的自旋锁
  while (lk->locked) {
    sleep(lk, &lk->lk); // 如果锁已经被其他进程持有，进入休眠状态，等待唤醒，这么做可以避免忙等待
  }
  lk->locked = 1; // 设置锁为已持有状态
  lk->pid = myproc()->pid; // 记录持有锁的进程 ID 为当前进程
  release(&lk->lk); // 释放互斥锁内部的自旋锁
}

void
releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk); // 获取互斥锁内部的自旋锁
  lk->locked = 0; // 设置锁为未持有状态
  lk->pid = 0; // 清除持有锁的进程 ID
  wakeup(lk); // 唤醒所有在这个锁上休眠的进程
  release(&lk->lk); // 释放互斥锁内部的自旋锁
}

int
holdingsleep(struct sleeplock *lk)
{
  int r;
  
  acquire(&lk->lk); // 获取互斥锁内部的自旋锁
  r = lk->locked && (lk->pid == myproc()->pid); // 检查锁是否被持有且持有者是当前进程
  release(&lk->lk); // 释放互斥锁内部的自旋锁
  return r;
}



