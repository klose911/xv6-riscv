// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name; // 设置自旋锁的名称
  lk->locked = 0; // 0 表示锁未被持有 
  lk->cpu = 0; // 初始化时没有 CPU 持有锁
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.

// 获取自旋锁，直到成功为止 
void
acquire(struct spinlock *lk)
{
  push_off(); // 关闭中断，避免死锁
  if(holding(lk)) // 如果当前 CPU 已经持有这个锁，直接奔溃 
    panic("acquire");

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  // 使用了amoswap汇编语句执行原子操作，确保在多核环境下不会出现竞态条件
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0) // 循环尝试获取锁，直到成功为止
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.

  // 它的意思是：要告诉 C 编译器和处理器，不要将内存的读写操作（load/store）重排序到这个点之前或之后
  // 这样可以确保临界区（critical section）内的所有内存访问，确实发生在锁被获取之后
  // 避免由于编译器优化或 CPU 指令乱序执行导致的数据不一致问题
  
  // 在 RISC-V 架构下，这通常通过发出一条 fence 指令来实现
  // fence 指令会强制处理器在其前后的内存操作严格按照程序顺序执行，从而保证多核或多线程环境下的同步和数据一致性
  // 这对于实现可靠的自旋锁和其他同步原语非常关键
  __sync_synchronize(); // 添加内存屏障和编译屏障

  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu(); // 记录持有自旋锁的 CPU 信息作为调试信息
}

// Release the lock.

// 释放自旋锁，必须在持有锁的情况下调用
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release"); // 如果当前 CPU 没有持有这个锁，直接奔溃

  lk->cpu = 0; // 清除持有锁的 CPU 信息（表示锁不再被任何 CPU 持有）

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize(); // 添加内存和编译屏障

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked); // 释放自旋锁

  pop_off(); // 打开中断
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.

// 检查当前cpu是否获取某个自旋锁
// 注意：这个函数需要在中断关闭的情况下调用
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu()); // 检查锁是否被持有且持有者是当前 CPU
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.
// push_off/pop_off 的作用类似于 intr_off()/intr_on()，都是用于控制中断的使能与关闭
// 但不同之处在于，push_off 和 pop_off 是成对匹配的：
// 如果连续调用了两次 push_off()，就需要调用两次 pop_off() 才能恢复到最初的中断状态
// 这种设计支持嵌套调用，适合在多层函数调用中安全地管理中断状态

// 此外，如果在最开始中断就是关闭的，那么无论调用多少次 push_off() 和 pop_off()，最终中断依然保持关闭状态，不会被错误地打开
// 这保证了中断状态的正确性和一致性，避免了并发环境下的竞态条件

void
push_off(void)
{
  int old = intr_get(); // 获取当前中断状态

  intr_off(); // 禁用中断，确保在修改嵌套深度时不会被中断打断

  // 注意：这里每次都调用mycpu()，以确保获取到当前 CPU 的状态信息 
  if(mycpu()->noff == 0) // 如果当前嵌套深度为 0，表示之前没有关闭过中断
    mycpu()->intena = old; // 记录当前的中断状态，以便在 pop_off() 时恢复 
  mycpu()->noff += 1; // 增加嵌套深度，表示当前已经关闭了一次中断
}


void
pop_off(void)
{
  struct cpu *c = mycpu(); // 获取当前 CPU 的状态信息

  // 下面两个校验表示必须至少调用过一次 push_off()，才能调用 pop_off()
  if(intr_get()) // 如果中断是开启的，直接奔溃
    panic("pop_off - interruptible");
  if(c->noff < 1) // 如果嵌套深度小于 1，表示没有对应的 push_off() 调用，直接奔溃
    panic("pop_off");
  c->noff -= 1; // 减少嵌套深度 
  if(c->noff == 0 && c->intena) // 如果嵌套深度降到 0，且之前记录的中断状态是开启的
    intr_on(); // 恢复中断，使得中断可以再次被触发
}
