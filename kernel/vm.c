#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h" // ELF 头文件
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable; // 内核页表

extern char etext[];  // kernel.ld sets this to end of kernel code. 内核代码段的结束位置

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.

/**
 * @brief 创建内核虚拟内存页表
 * 
 * 分配并初始化一个新的页表，将内核需要用到的物理内存区域（如内核代码、数据、设备寄存器等）映射到内核虚拟地址空间
 * 这样，内核就可以通过虚拟地址安全且高效地访问物理资源
 * 
 * 通常，kvmmake 会设置好内核空间的各种映射关系，并为后续的内存管理和进程切换提供基础
 * 这个函数在系统启动时被调用
 * 
 * @return pagetable_t 初始化后的内核页表指针
 */
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc(); // 向内核申请一页内存
  memset(kpgtbl, 0, PGSIZE); // 将申请到的内存清零

  // uart registers
  // 将物理地址 UART0 映射到内核页表 kpgtbl 的虚拟地址 UART0
  // 映射的大小为一页（PGSIZE，通常为 4096 字节），并设置了读写权限（PTE_R | PTE_W）
  // 这样的映射通常用于把 UART0（串口设备）的物理寄存器区域直接映射到内核的虚拟地址空间
  // 使内核能够通过虚拟地址方便地访问 UART0 设备
  // 设置读写权限保证了内核既可以读取也可以写入该设备寄存器
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  // 映射 virtio0 设备寄存器，大小为一页，并设置了读写权限
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  // 映射 PLIC 平台中断控制器，大小为 0x4000000 字节，并设置了读写权限
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  // 映射内核代码段地址，大小为etext-KERNBASE， 设置只读和执行权限
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  // 映射内核数据段 和 可使用的物理内存，大小为 PHYSTOP-(uint64)etext，设置读写权限
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  // 映射内核的 trampoline 区域，大小为一页，并设置只读和执行权限
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl); // 为每个进程预先分配内核栈
  
  return kpgtbl; // 返回内存页表指针
}

// Initialize the one kernel_pagetable
// 初始化虚拟内存页表
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.

// 将硬件的页表寄存器切换到内核的页表。这意味着 CPU 后续的虚拟地址访问都将通过内核页表进行地址转换
// 启用分页机制（paging）
// 分页是现代操作系统内存管理的基础，通过它可以实现虚拟内存、内存保护等功能
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  // 确保之前对页表的所有写操作（如新建或修改页表项）都已经对硬件可见，避免 CPU 仍然缓存着旧的虚拟地址到物理地址的映射
  // 这是为了防止“脏数据”影响后续的地址转换
  sfence_vma(); // 刷新TLB，使得所有虚拟内存映射无效

  // 将内核页表的物理地址写入硬件的页表基址寄存器（SATP），正式切换到内核的页表
  // 此后，CPU 的虚拟地址访问都会通过新的页表进行转换。
  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  // 切换 SATP 后，TLB 里可能还残留着旧页表下的虚拟地址映射（TLB 缓存不会自动失效）
  // 再次刷新 TLB，可以确保所有的地址转换都基于新的页表，彻底清除所有旧的映射，避免出现地址转换错误
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
// 返回页表 pagetable 中与虚拟地址 va 对应的页表项（PTE）的地址
// 如果参数 alloc 不为 0，函数会在查找过程中自动分配所需的页表页
//                     （即如果中间某一级页表不存在，就会分配新的页表页）


// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.

// Sv39 模式采用三级页表结构，每一级页表页都包含 512 个 64 位的页表项（PTE）
// 一个 64 位的虚拟地址被划分为五个部分：
// 位 39 到 63 必须为零（实际只使用 39 位虚拟地址空间）
// 位 30 到 38（9 位）作为第 2 级页表的索引
// 位 21 到 29（9 位）作为第 1 级页表的索引
// 位 12 到 20（9 位）作为第 0 级页表的索引
// 位 0 到 11（12 位）表示页内偏移，用于定位页面内的偏移
// 这种分层结构使得虚拟地址空间可以高效地映射到物理内存，同时支持大容量的虚拟内存和灵活的内存管理
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  // 通过一个从 2 到 1 的循环，依次处理虚拟地址的每一级页表（Sv39 有三级页表，level 2 是最高级）
  for(int level = 2; level > 0; level--) { 
    pte_t *pte = &pagetable[PX(level, va)]; // 计算当前级别的索引，获取对应的页表项指针
    if(*pte & PTE_V) { // 下一级页表已经存在
      pagetable = (pagetable_t)PTE2PA(*pte); // 将 pagetable 更新为下一级页表的物理地址
    } else { // 如果当前页表项无效，说明下一级页表还没有分配
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0) // 不需要分配，或者申请页表内存失败
        return 0; // 直接返回 0 
      memset(pagetable, 0, PGSIZE); //  memset 清零
      *pte = PA2PTE(pagetable) | PTE_V; // 然后将新页表的物理地址和有效位写入当前页表项
    }
  }
  return &pagetable[PX(0, va)]; // 返回最低一级（level 0）页表中对应虚拟地址的页表项指针
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
// 只被用来查找用户进程中的页表
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA) // 超过最大虚拟地址
    return 0; // 返回0，表示无效

  pte = walk(pagetable, va, 0); // 一层层查找虚拟地址 va 对应的页表项（PTE）
  if(pte == 0) // 查找失败
    return 0; // 该虚拟地址没有映射
  if((*pte & PTE_V) == 0) // 有效位 PTE_V 没有设置
    return 0;
  if((*pte & PTE_U) == 0) // 用户访问位 PTE_U 没有设置
    return 0;
  pa = PTE2PA(*pte); // 根据 PTE 项 转换成物理地址
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
// 在内核页表时添加一个物理地址到虚拟地址的映射
// 只在启动时使用，不会刷新 TLB 或 启用分页
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.

// 从虚拟地址 va 开始的一段地址区间创建页表项（PTE），使它们映射到从物理地址 pa 开始的物理内存区域

// 要求 va 和 size 必须是页对齐的，也就是说它们都要是页大小（通常为 4096 字节）的整数倍
// 这是因为页表只能按页粒度进行映射

// 函数返回值为 0 表示映射成功
// 如果在创建页表项的过程中，walk() 函数无法分配所需的页表页，则返回 -1，表示失败

int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0) // 校验虚拟地址是否页对齐 
    panic("mappages: va not aligned");

  if((size % PGSIZE) != 0) // 校验内存大小是否页对齐
    panic("mappages: size not aligned");

  if(size == 0) // 内存大小不能为0 
    panic("mappages: size");
  
  a = va;
  // 计算映射区间的最后一个页面的起始地址。由于映射是按页进行的，所以要减去一页的大小
  last = va + size - PGSIZE; 
  // 循环，逐页处理整个映射区间，直到所有页面都完成映射
  for(;;){
    // 调用 walk 函数查找或创建当前虚拟地址 a 对应的页表项指针 pte
    if((pte = walk(pagetable, a, 1)) == 0) 
      return -1; // 如果失败（返回 0），说明无法分配所需的页表页，函数直接返回 -1 表示出错
    if(*pte & PTE_V) // 检查该页表项是否已经有效（PTE_V 标志位被设置）
      panic("mappages: remap"); // 如果已经被映射，说明出现了重复映射，直接触发 panic 报错
    // 将当前物理地址 pa 转换为页表项格式，并与权限标志 perm 及有效位 PTE_V 组合
    // 写入页表项，实现虚拟地址到物理地址的映射
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last) // 如果已经处理到最后一个页面，跳出循环
      break;
    // 反之将虚拟地址和物理地址都向后移动一页，继续写入下一项映射项
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0; // 所有页面都成功映射后，返回 0 表示操作成功
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.

// 从虚拟地址 va 开始的 npages 页内存映射
// va 必须是页对齐的，并且这些映射必须已经存在
// 如果 do_free 非零，则释放对应的物理内存页面
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0) // 校验虚拟地址是否页对齐
    panic("uvmunmap: not aligned");

  // 遍历从起始虚拟地址 va 开始、长度为 npages 页的所有页，每次循环处理一页
  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0) // 查找对应的pte项失败
      panic("uvmunmap: walk"); // 内核奔溃
    if((*pte & PTE_V) == 0) // 有效位没有设置
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V) // 如果仅有 PTE_V，说明该页表项不是叶子节点（即没有映射到实际物理页）
      panic("uvmunmap: not a leaf");
    if(do_free){ // 释放动态分配的内存页
      uint64 pa = PTE2PA(*pte); 
      kfree((void*)pa);
    }
    *pte = 0; // 对应的页表项写入NULL，解除虚拟地址与物理地址的映射关系
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc(); // 动态分配一页内存，作为用户进程的内存映射表
  if(pagetable == 0) // 内存不够，无法分配内存映射表
    return 0; // 直接返回 0 
  memset(pagetable, 0, PGSIZE); // 初始化新分配的页面
  return pagetable; // 这个地址是物理地址，内核可以直接使用
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.

// 将用户初始代码（initcode）加载到页表的虚拟地址 0 处，这是系统启动时第一个用户进程的初始化步骤
// 只有第一个用户进程（通常是 init 或 initcode）会这样做
// sz（代码大小）必须小于一页（page），否则无法完整加载到单个页面中
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE) // 复制的代码大小超过一页，直接奔溃
    panic("uvmfirst: more than a page");
  mem = kalloc(); // 分配一页物理内存
  memset(mem, 0, PGSIZE); // 清空内存

  // 内存页表中加入映射项：
  // 虚拟地址起始为 0，映射长度为一页（PGSIZE）
  // 物理地址为 mem 指向的内存区域（强制转换为 uint64 类型）
  // 权限标志为 PTE_W | PTE_R | PTE_X | PTE_U
  //     表示该页可写（W）、可读（R）、可执行（X），并且用户态（U）可访问
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz); // 移动代码到内存
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
// oldsz 和 newsz 不要求页对齐，函数内部会自动处理非对齐情况
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz) // 如果新请求的内存空间小于原有空间，直接返回原大小，不做任何分配
    return oldsz;

  oldsz = PGROUNDUP(oldsz); // 将 oldsz 向上对齐到最近的页边界
  // 从对齐后的旧地址开始，按页大小（PGSIZE）循环，直到分配到新地址上限
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc(); // 分配一页物理内存
    if(mem == 0){ // 分配失败
      uvmdealloc(pagetable, a, oldsz); // 释放已分配的部分
      return 0; // 返回 0，表示分配失败
    }
    memset(mem, 0, PGSIZE); // 新分配的物理页清零，保证内存安全
    // 将虚拟地址 a 映射到物理地址 mem，并设置相应的权限（只读、用户可访问、额外权限）
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem); // 映射失败，释放该页内存
      uvmdealloc(pagetable, a, oldsz); // 释放已分配的部分
      return 0; // 返回 0，表示分配失败
    }
  }
  return newsz; // 分配成功，返回新的内存大小
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
// oldsz 和 newsz 不要求页对齐，函数内部会自动处理非对齐情况。
// newsz 不一定小于 oldsz，即允许传入的参数顺序不严格。
// oldsz 甚至可以大于实际的进程空间大小，函数会做容错处理
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz) // 如果newsz 大于等于 oldsz，直接返回oldsz，不做任何操作
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE; // 计算需要释放的页数
    // 从对齐后的新空间上限开始，解除 npages 个页面的映射
    // 释放对应的物理内存（最后一个参数为 1 表示释放物理页）
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);  
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
// 递归地释放页表所占用的内存页面，包括多级页表结构中的所有页表页 
// 调用该函数之前，所有叶子节点（即虚拟地址到物理地址的实际映射）必须已经被解除
// 也就是说，页表项不再指向任何物理页，只剩下页表结构本身
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  // 每个页表页包含 2^9 = 512 个页表项（PTE）
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    // 当前页表项有效（PTE_V），但没有读、写、执行权限（PTE_R|PTE_W|PTE_X 都为 0）
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      // 说明它不是叶子节点，而是指向下一级页表（即多级页表结构中的中间节点）
      uint64 child = PTE2PA(pte); // 获取下一级页表的物理地址
      freewalk((pagetable_t)child); // 递归释放下一级页表
      pagetable[i] = 0; // 清空当前页表项
    } else if(pte & PTE_V){ // 如果页表项有效，但有读、写、执行权限，说明这是叶子节点（映射到实际物理页）
      panic("freewalk: leaf"); // 此时不应该由 freewalk 处理，直接 panic，防止误删物理页
    }
  }
  kfree((void*)pagetable); // 释放当前页表页的内存
}

// Free user memory pages,
// then free page-table pages.
// 先释放物理内存对应的页面，然后释放页表对应的页面
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    // 虚拟地址 0 开始，解除所有页的映射关系，并释放对应的物理内存（参数 1 表示释放物理页）
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable); // 递归释放整个页表结构本身，确保所有页表页都被回收
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.

// 该函数接收父进程的页表，并将父进程的内存内容复制到子进程的页表中
// 复制过程包括页表结构本身以及物理内存
// 如果复制成功，函数返回 0；如果失败（如内存分配不足），返回 -1
// 一旦发生失败，函数会自动释放在复制过程中已经分配的所有内存页，避免内存泄漏
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){ // 遍历从 0 到 sz 的每一页
    if((pte = walk(old, i, 0)) == 0) // 无法在旧进程的内存页表中找到对应的pte
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0) // 找到pte无效
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte); // 获得pte对应的物理地址 
    flags = PTE_FLAGS(*pte); // 获取 pte对应的标志位
    // 注意：mem返回的是内核态的虚拟地址，实际是物理地址
    if((mem = kalloc()) == 0) // 申请一页新的物理内存
      goto err;
    memmove(mem, (char*)pa, PGSIZE); // 把old内存页表里的数据拷贝新分配的内存中
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){ // 把新分配的内存页映射到new内存页表对应的pte项
      kfree(mem); // 映射失败，释放刚分配的内存页
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1); // 虚拟地址 0 开始，解除所有已经存在的映射关系，并释放对应的物理内存
  return -1; // 返回 -1，表示复制失败
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
// 表示将某个页表项（PTE）设置为用户不可访问，通常是通过去掉用户访问权限标志（如 PTE_U）
// 主要用于 exec 系统调用，为用户栈设置保护页（guard page）。这样可以防止用户进程访问或越界到栈保护页
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0); // 查询虚拟地址va对应的pte页表项
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U; // 清空 PTE_U 位，这样用户进程无法访问这个pte页表项映射的物理内存
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva); // 将目标虚拟地址 dstva 向下对齐到页边界，得到当前页的起始地址
    if(va0 >= MAXVA) // 对齐后的虚拟地址超出最大允许范围，返回错误
      return -1;
    pte = walk(pagetable, va0, 0); // 查找该虚拟地址对应的页表项（PTE）
    // 如果页表项不存在、无效、没有用户访问权限或没有写权限，返回错误
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ||
       (*pte & PTE_W) == 0)
      return -1;
    pa0 = PTE2PA(*pte); // 获取该页表项对应的物理地址
    n = PGSIZE - (dstva - va0); // 计算当前页还能复制的最大字节数（从 dstva 到本页末尾）
    if(n > len) // 如果本页可用空间 n 大于 需要复制的长度 len, 只复制 len 字节
      n = len;
    // 将内核缓冲区 src 的数据复制到用户空间的物理地址
    memmove((void *)(pa0 + (dstva - va0)), src, n); 

    // 更新剩余长度、源指针和目标虚拟地址，进入下一页继续复制
    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.

// 逻辑类似于 copyout 
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    // 计算出源字符串在物理内存中的实际起始地址
    // pa0 是当前页的物理地址，srcva - va0 是虚拟地址在页内的偏移
    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){ // 循环控制，每次复制一个字节，直到本页剩余可读字节数 n 用完
      if(*p == '\0'){ // 检查当前字符是否为字符串结束符
        *dst = '\0'; // 将目标缓冲区当前位置写入 \0
        got_null = 1; // 设置 got_null = 1 标记已遇到结尾
        break; // 跳出循环
      } else {
        *dst = *p;
      }
      // 递减 n 和 max（通常 max 用于限制总共可复制的最大字节数）
      // 并将源指针 p 和目标指针 dst 向后移动一位
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE; // 开始下一页复制
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}
