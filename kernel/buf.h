/**
 * @brief 示磁盘块缓冲区（buffer）
 * 
 * 块设备缓存管理的核心数据结构，支持高效的磁盘块读写、缓存一致性和淘汰机制
 * 
 */
struct buf {
  // 标记该缓冲区中的数据是否已经从磁盘读取。如果为 1，表示数据有效，可以直接使用；否则需要从磁盘加载
  int valid;   // has data been read from disk? 
  // 表示当前缓冲区是否被磁盘“拥有”，通常用于同步磁盘操作和缓存状态
  int disk;    // does disk "own" buf?
  uint dev; // 设备号，标识该缓冲区属于哪个块设备（如磁盘、分区等） 
  uint blockno; // 块号，标识该缓冲区对应磁盘上的哪个块
  struct sleeplock lock; // 睡眠锁，用于保护缓冲区的同步访问
  uint refcnt; // 引用计数，表示有多少个进程在使用该缓冲区
  // 指向前一个缓冲区的指针，用于将所有缓冲区组织成 LRU（最近最少使用）缓存链表，实现缓存淘汰策略
  struct buf *prev; // LRU cache list
  // 指向后一个缓冲区的指针
  struct buf *next;
  uchar data[BSIZE]; // 实际存储磁盘块数据的缓冲区，大小为 BSIZE 字节
};

