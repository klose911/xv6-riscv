// Long-term locks for processes

/**
 * @brief 定义了互斥锁结构，是一种用于多线程或多核环境下同步访问共享资源的锁机制
 * 
 * 互斥锁的工作原理是：当一个线程尝试获取锁时，如果锁已经被其他线程持有，
 * 它会进入休眠状态，直到锁被释放。这种方式可以有效地避免线程之间的竞争条件，
 * 
 * 适用于锁持有时间较长、线程切换开销较小的场景，可以减少自旋带来的性能损失
 * 
 */
struct sleeplock {
  uint locked;       // Is the lock held? 锁是否被持有 1表示被持有，0 表示未被持有
  struct spinlock lk; // spinlock protecting this sleep lock 内部自旋锁，用于保护该互斥锁的状态 
  
  // For debugging:
  char *name;        // Name of lock. 锁的名称，用于调试和日志记录
  int pid;           // Process holding lock 持有锁的进程 ID
};

