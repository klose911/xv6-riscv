// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

// 磁盘块缓冲区是一个由buf结构体组成的链表结构，buf结构体包含磁盘块的数据以及相关的元数据，如锁、引用计数等
// 通过缓存磁盘块数据，可以减少对磁盘的直接读写操作，提高系统性能
// 同时，缓冲区还提供了进程间同步的机制，确保多个进程对同一磁盘块的访问不会引发数据不一致的问题

// 接口说明：
// * 要获取特定磁盘块的缓冲区，调用bread函数
// * 修改缓冲区数据后，调用bwrite函数将其写回磁盘
// * 使用完缓冲区后，调用brelse函数释放它
// * 释放缓冲区后，不要再使用它
// * 每次只能有一个进程使用缓冲区，因此不要长时间持有它们以免影响其他进程的访问

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

/**
 * @brief 全局缓冲区缓存结构体，用于管理磁盘块缓冲区（buffer）的分配和淘汰
 * 
 * 
 */
struct {
  struct spinlock lock; // 自旋锁，用于保护整个缓冲区缓存的数据结构，确保多线程或多核环境下的并发安全
  struct buf buf[NBUF]; // 缓冲区数组，包含 NBUF 个磁盘块缓冲区，每个缓冲区用于缓存一个磁盘块的数据

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;：链表头结点，用于组织所有缓冲区形成一个双向链表
  // 链表通过每个 buf 结构体中的 prev 和 next 指针连接，按照最近使用的顺序排序：
  //     head.next 指向最近被访问的缓冲区（MRU，most recently used）
  //     head.prev 指向最久未被访问的缓冲区（LRU，least recently used）
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache"); // 初始化全局缓冲区缓存的自旋锁，确保后续操作的并发安全

  // Create linked list of buffers
  // 初始化链表头结点，使其前后指针都指向自身，形成一个空的循环双向链表
  bcache.head.prev = &bcache.head; 
  bcache.head.next = &bcache.head;
  // 遍历所有缓冲区数组元素，为每个缓冲区分配链表位置和锁
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    // 将当前缓冲区插入到链表头部（最近使用的位置） 
    b->next = bcache.head.next; 
    b->prev = &bcache.head; 
    initsleeplock(&b->lock, "buffer"); // 初始化每个缓冲区的互斥锁，用于保护缓冲区数据的并发访问
    // 更新链表指针，确保链表结构正确
    bcache.head.next->prev = b; 
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer. 

// 寻找指定设备和块号的缓冲区，如果不存在则分配一个新的缓冲区
// 无论是找到的还是新分配的缓冲区，都会以锁定的状态返回
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock); // 获取全局缓冲区缓存的自旋锁，确保对缓冲区链表的操作是线程安全的

  // Is the block already cached?
  // 遍历缓冲区链表，查找是否已有对应设备号和块号的缓冲区
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){ // 找到匹配的缓冲区
      b->refcnt++; // 增加引用计数，表示有一个新的进程正在使用该缓冲区
      release(&bcache.lock); // 释放全局缓冲区缓存的自旋锁 
      acquiresleep(&b->lock); // 获取该缓冲区的互斥锁，确保对缓冲区数据的独占访问
      return b; // 返回锁定的缓冲区指针
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // 如果没有找到匹配的缓冲区，则需要分配一个新的缓冲区
  // 遍历链表，从最久未使用的缓冲区开始查找
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) { // 找到一个未被使用的缓冲区
      b->dev = dev; // 设置设备号
      b->blockno = blockno; // 设置块号
      b->valid = 0; // 标记数据无效，需要从磁盘读取
      b->refcnt = 1; // 设置引用计数为1，表示正在使用该缓冲区
      release(&bcache.lock); // 释放全局缓冲区缓存的自旋锁
      acquiresleep(&b->lock); // 获取该缓冲区的互斥锁
      return b; // 返回锁定的缓冲区指针
    }
  }
  panic("bget: no buffers"); // 如果没有可用的缓冲区，触发内核恐慌
}

// Return a locked buf with the contents of the indicated block.
// 返回一个锁定的缓冲区，包含指定设备和块号的磁盘块内容
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno); // 获取指定设备和块号的缓冲区
  if(!b->valid) { // 如果缓冲区数据无效，需要从磁盘读取
    virtio_disk_rw(b, 0); // 从磁盘读取数据到缓冲区
    b->valid = 1; // 标记数据为有效
  }
  return b; // 返回锁定的缓冲区指针
}

// Write b's contents to disk.  Must be locked.
// 将缓冲区的数据写回到磁盘
// 必须在调用该函数前锁定缓冲区
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock)) // 检查缓冲区是否被锁定
    panic("bwrite"); // 如果未锁定则触发内核奔溃
  virtio_disk_rw(b, 1); // 执行写操作，将缓冲区数据写回磁盘
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
// 释放一个已锁定的缓冲区，将其从使用中状态变为可用状态
// 并将其移动到最近最少使用（LRU）链表的头部
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock)) // 检查缓冲区是否被锁定
    panic("brelse"); // 如果未锁定则触发内核奔溃

  releasesleep(&b->lock); // 释放缓冲区的互斥锁

  acquire(&bcache.lock); // 获取全局缓冲区缓存的自旋锁
  b->refcnt--; // 减少引用计数，表示有一个进程不再使用该缓冲区
  if (b->refcnt == 0) { // 如果引用计数为0，表示没有进程在使用该缓冲区
    // no one is waiting for it.
    // 将缓冲区从链表中暂时移除
    b->next->prev = b->prev; 
    b->prev->next = b->next; 
    // 将缓冲区移动到链表头部，表示最近被使用过
    b->next = bcache.head.next; 
    b->prev = &bcache.head; 
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock); // 释放全局缓冲区缓存的自旋锁
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock); // 获取全局缓冲区缓存的自旋锁
  b->refcnt++; // 增加缓冲区的引用计数，防止其被释放
  release(&bcache.lock); // 释放全局缓冲区缓存的自旋锁
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock); // 获取全局缓冲区缓存的自旋锁
  b->refcnt--; // 减少缓冲区的引用计数，允许其被释放
  release(&bcache.lock); // 释放全局缓冲区缓存的自旋锁
}


