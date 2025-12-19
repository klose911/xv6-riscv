// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

/**
 * @brief 初始化物理内存分配器的辅助函数 
 * 
 * @param pa_start 指向物理地址开始处
 * @param pa_end 指向物理地址结束处
 * 
 * 指定范围内的物理内存页面（从 pa_start 到 pa_end）逐页释放，并加入到空闲内存链表中
 * 
 */
void freerange(void *pa_start, void *pa_end);

// 紧接内核之后的内存地址
extern char end[]; // first address after kernel.
                   // defined by kernel.ld.


/**
 * @brief 表示空闲内存页面的单向链表
 * 
 * 每当有一页内存被释放时，就会用一个 run 结构体来表示这页内存，并通过 next 指针将所有空闲页串联成一个单向链表
 * 这样，内存分配器可以很方便地遍历和管理所有可用的内存页，实现高效的内存分配和回收
 * 
 */
struct run {
  struct run *next;
};

/**
 * @brief 内存分配器的全局状态结构体
 * 
 */
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem"); // 为内存分配器的自旋锁 kmem.lock 进行初始化
  // 从内核结束位置到物理内存上限的内存区域添加到空闲内存列表中
  freerange(end, (void*)PHYSTOP); 
}

// 将内核未占用的物理内存全部纳入内存分配器的管理范围，便于后续的动态分配和回收
// 这个函数通常只在内存分配器初始化阶段（如 kinit 函数中）被调用
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start); // 起始地址对齐到页面边界 
  // 遍历整个内存区间，以一页为大小作为迭代
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p); // 释放一页内存 
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  // 这段代码是对传入 kfree 函数的物理地址 pa 进行有效性检查。它包含三个条件：
  // ((uint64)pa % PGSIZE) != 0：判断 pa 是否按页大小（PGSIZE，通常为4096字节）对齐
  //     如果不是页对齐，说明这个地址不是合法的页面起始地址
  // (char*)pa < end：判断 pa 是否小于内核结束地址 end。小于 end 的内存属于内核自身，不能被释放
  // (uint64)pa >= PHYSTOP：判断 pa 是否超出了物理内存的上限 PHYSTOP。超出这个范围的地址是无效的
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // 释放内存页时，用特定的“垃圾值”覆盖原有内容
  // 这有助于在调试过程中更容易发现悬空指针或非法访问已释放内存的错误
  // 因为被填充为 1 的内存内容通常会导致程序行为异常，从而暴露潜在的 bug
  // 这是一种常见的内存安全防护措施
  memset(pa, 1, PGSIZE); // 以 pa 为起始地址的一页物理内存（通常为4096字节）全部填充为数值 1

  r = (struct run*)pa;

  acquire(&kmem.lock); // 获取自旋锁
  r->next = kmem.freelist; // 将当前要释放的页面（用 r 表示）插入到空闲链表头部
  kmem.freelist = r; // 链表头指针更新为 r，即把新释放的页面作为新的链表头
  release(&kmem.lock); // 释放自旋锁
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock); // 获取自旋锁
  r = kmem.freelist; // 获取空闲页链表的头部
  if(r) // 如果链表非空（r 不为 NULL）
    kmem.freelist = r->next;  // 将链表头指针移动到下一个节点，表示分配了这一页内存
  release(&kmem.lock); // 释放自旋锁

  if(r) // 用5填充初始化后的内存页面
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r; // 返回页面的起始地址
}
