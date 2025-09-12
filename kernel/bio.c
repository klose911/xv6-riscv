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
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


