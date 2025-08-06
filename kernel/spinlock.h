// Mutual exclusion lock.
/**
 * @brief 定义了自旋锁结构，是一种用于多线程或多核环境下同步访问共享资源的锁机制
 * 
 * 自旋锁的工作原理是：当一个线程尝试获取锁时，如果锁已经被其他线程持有，它不会进入休眠状态
 * 而是会在一个循环中不断检查锁的状态（即“自旋”），直到锁可用为止
 * 
 * 这种方式适用于锁持有时间很短、线程切换开销较大的场景，可以减少上下文切换带来的性能损失
 * 
 */
struct spinlock {
  uint locked;       // Is the lock held? 锁是否被持有 1表示被持有，0 表示未被持有

  // For debugging:
  char *name;        // Name of lock. 锁的名称，用于调试和日志记录
  struct cpu *cpu;   // The cpu holding the lock. 持有锁的 CPU
};

