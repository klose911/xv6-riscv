// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
// 文件系统实现。五个层级：
// + 块：原始磁盘块的分配器
// + 日志：多步骤更新的崩溃恢复
// + 文件：inode分配器，读写，元数据
// + 目录：具有特殊内容的inode（其他inode的列表！）
// + 名称：像/usr/rtm/xv6/fs.c这样的路径，方便命名

// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.
// 该文件包含低级文件系统操作例程
// （更高级别的）系统调用实现位于 sysfile.c 中

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
// there should be one superblock per disk device, but we run with
// only one device
// 每个设备应该有一个超级块，但我们只运行一个设备 
struct superblock sb;  // 超级快

// Read the super block.

/**
 * @brief 读取指定设备的超级块信息到内存
 * 
 * @param dev 设备号
 * @param sb 指向超级块结构体的指针
 * 
 */
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1); // 读取设备 dev 上的第 1 块数据（超级块通常存储在块号 1）
  memmove(sb, bp->data, sizeof(*sb)); // 将读取到的超级块数据复制到 sb 指向的内存区域
  brelse(bp); // 释放缓冲区
}

// Init fs
void
fsinit(int dev) {
  readsb(dev, &sb); // 读取超级块到全局变量 sb 中
  if(sb.magic != FSMAGIC) // 检查超级块的魔数是否正确
    panic("invalid file system");
  initlog(dev, &sb); // 初始化日志系统
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
// returns 0 if out of disk space.

/**
 * @brief 分配一个空闲的磁盘块，并将其内容清零
 * 
 * @param dev 设备号
 * 
 * @return uint 返回分配的块号，如果没有可用块则返回 0
 */
static uint
balloc(uint dev)
{
  int b, bi, m; 
  struct buf *bp; 

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){ // 遍历所有bitmap块组
    bp = bread(dev, BBLOCK(b, sb)); // 读取bitmap块
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){ // 遍历bitmap块中的每个位
      // bi % 8 计算当前位在字节中的位置 
      // 1 << (bi % 8) 生成对应的掩码 
      m = 1 << (bi % 8); // 计算当前位的掩码, 用于检查和设置该位
      // bp->data[bi/8] 访问对应字节
      // & m 检查该位是否为0，表示对应块是空闲
      if((bp->data[bi/8] & m) == 0){  // Is block free? 检查该位是否为0，表示对应块是空闲的 
        bp->data[bi/8] |= m;  // Mark block in use. 设置该位为1，表示对应块已被分配 
        log_write(bp); // 写日志以记录对bitmap块的修改
        brelse(bp); // 释放bitmap块缓冲区
        bzero(dev, b + bi); // 清零新分配的块
        return b + bi; // 返回分配的块号
      }
    }
    brelse(bp); // 释放bitmap块缓冲区
  }
  printf("balloc: out of blocks\n"); // 没有可用块
  return 0; // 返回0表示分配失败
}

// Free a disk block.
/**
 * @brief 释放指定的磁盘块
 * 
 * @param dev 设备号
 * @param b 块号
 */
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb)); // 读取包含块 b 的位图块
  bi = b % BPB; // 计算块 b 在位图块中的偏移量
  m = 1 << (bi % 8); // 计算当前位的掩码, 用于检查和设置该位 
  if((bp->data[bi/8] & m) == 0) // 如果该位已经是0，表示块未被分配 
    panic("freeing free block"); // 抛出错误，尝试释放未分配的块 
  bp->data[bi/8] &= ~m; // 将该位清零，表示块已被释放 
  log_write(bp); // 写日志以记录对位图块的修改 
  brelse(bp); // 释放位图块缓冲区
}

// Inodes.
// 
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
// inode 是用于描述单个文件的核心数据结构，磁盘上的 inode 结构保存了文件的元数据
// 包括文件类型、大小、引用计数（链接数）以及存放文件内容的磁盘块列表

// The inodes are laid out sequentially on disk at block
// sb.inodestart. Each inode has a number, indicating its
// position on the disk.
// 所有 inode 在磁盘上按顺序排列，每个 inode 都有唯一编号，标识其在磁盘上的位置

// The kernel keeps a table of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The in-memory
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
// 内核会在内存中维护一个 inode 表，用于同步多进程对 inode 的访问
// 内存中的 inode 除了磁盘上的信息，还包含一些仅用于内存管理的字段
// 比如引用计数（ip->ref）和有效性标志（ip->valid）

// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
// 一个inode及其内存表示在被文件系统代码使用前，会经历几个状态阶段： 

// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
// 分配：
//      当 inode 的类型非零时表示已分配
//      通过 ialloc() 分配 inode
//      iput() 在引用和链接数都为零时释放 inode

// * Referencing in table: an entry in the inode table
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a table entry and increments its ref; iput()
//   decrements ref.
// 表引用：
//      inode 表中的条目如果 ip->ref 为零则空闲，否则 ip->ref 记录有多少内存指针引用该 inode
//      iget() 查找或创建表项并递增引用计数
//      iput() 递减引用计数

// * Valid: the information (type, size, &c) in an inode
//   table entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
// 有效：
//      只有当 ip->valid 为 1 时，inode 表项中的信息才是正确的
//      ilock() 从磁盘读取 inode 并设置 valid，
//      iput() 在引用计数为零时清除 valid

// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
// 加锁：
//      文件系统代码在访问或修改 inode 及其内容前，必须先锁定该 inode

// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
// 典型的 inode 操作流程是：
//   ip = iget(dev, inum) // 通过 iget() 获取 inode
//   ilock(ip) // 对inode加锁
//   ... examine and modify ip->xxx ... 
//   iunlock(ip) // 对inode解锁
//   iput(ip) // 释放inode 

// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays in the table and pointers to it remain valid.
// ilock() 和 iget() 分离
// 便于系统调用长期持有 inode 引用但只在需要时短暂加锁，减少死锁和竞争风险
// iget() 增加引用计数，保证 inode 在表中不会被移除

// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
// 许多文件系统内部函数要求调用者已锁定相关 inode，以便实现多步原子操作

// The itable.lock spin-lock protects the allocation of itable
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold itable.lock while using any of those fields.
// inode 表的分配由 itable.lock 自旋锁保护
// 只有持有该锁才能安全操作 ip->ref、ip->dev 和 ip->inum

// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.
// 除了这些字段外，其他 inode 字段由 ip->lock 睡眠锁保护
// 必须持有该锁才能读写 ip->valid、ip->size、ip->type 等信息

/**
 * @brief inode表
 * 
 */
struct {
  struct spinlock lock; // inode 表的自旋锁
  struct inode inode[NINODE]; // inode 表
} itable;

void
iinit()
{
  int i = 0;
  
  // 只有持有该锁才能安全操作 ip->ref、ip->dev 和 ip->inum
  initlock(&itable.lock, "itable"); // 初始化 inode 表的自旋锁
  for(i = 0; i < NINODE; i++) { // 遍历 inode 表中的每一个 inode 条目 
    // 必须持有该锁才能读写 ip->valid、ip->size、ip->type 等信息
    initsleeplock(&itable.inode[i].lock, "inode"); // 为每个 inode 初始化互斥锁
  }
}

static struct inode* iget(uint dev, uint inum);

// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode,
// or NULL if there is no free inode.
// 在设备 dev 上分配一个 inode
// 通过设置类型 type 将其标记为已分配
// 返回一个未加锁但已分配和引用的 inode
// 如果没有可用的 inode 则返回 NULL
struct inode*
ialloc(uint dev, short type)
{
  int inum; // inode 编号
  struct buf *bp; // 缓冲区指针
  struct dinode *dip; // 磁盘 inode 指针

  for(inum = 1; inum < sb.ninodes; inum++){ // 遍历所有 inode 编号，跳过编号 0
    bp = bread(dev, IBLOCK(inum, sb)); // 读取包含 inode inum 的磁盘块 
    dip = (struct dinode*)bp->data + inum%IPB; // 计算 inode 在块内的偏移位置
    if(dip->type == 0){  // a free inode 找到一个空闲的 inode
      // 初始化磁盘 inode 结构
      memset(dip, 0, sizeof(*dip)); // 清零 inode 结构
      dip->type = type; // 设置 inode 类型
      log_write(bp);   // mark it allocated on the disk // 写日志以记录对 inode 的修改
      brelse(bp); // 释放缓冲区
      return iget(dev, inum); // 返回对应的内存 inode 结构体指针
    }
    brelse(bp); // 释放缓冲区
  }
  printf("ialloc: no inodes\n"); // 没有可用的 inode
  return 0; // 没有可用的 inode，返回 NULL
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk.
// Caller must hold ip->lock.

// 将修改后的内存中 inode 复制到磁盘
// 必须在每次修改 ip->xxx 字段后调用
// 调用者必须持有 ip->lock
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb)); // 读取包含 inode 的磁盘块
  dip = (struct dinode*)bp->data + ip->inum%IPB; // 计算 inode 在块内的偏移位置
  dip->type = ip->type; // 更新磁盘 inode 的类型
  dip->major = ip->major; // 更新主设备号
  dip->minor = ip->minor; // 更新次设备号
  dip->nlink = ip->nlink; // 更新链接数
  dip->size = ip->size; // 更新文件大小
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs)); // 更新数据块地址数组
  log_write(bp); // 写日志以记录对 inode 的修改
  brelse(bp); // 释放缓冲区
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
// 根据inode编号从给定设备返回对应的内存中inode结构体指针
// 并不会锁定该inode，也不会从磁盘读取它 

/**
 * @brief 读取inode对应的内存结构体指针 
 * 
 * @param dev 设备号
 * @param inum inode编号
 * 
 * @return struct inode* 指向对应的内存inode结构体指针 
 * 
 * @note 调用该函数不会锁定inode，也不会从磁盘读取它
 *  
 */
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock); // 获取 inode 表的自旋锁

  // Is the inode already in the table?
  empty = 0; // 是否存在于内存inode表中 
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){ // 遍历 inode 表中的每个 inode 条目
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){ // 找到匹配的 inode
      ip->ref++; // 增加引用计数
      release(&itable.lock); // 释放 inode 表的自旋锁
      return ip; // 返回找到的 inode 指针
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot. 记录第一个找到的空闲槽位 
      empty = ip;
  }

  // Recycle an inode entry.
  if(empty == 0) // 没有可用的 inode 条目
    panic("iget: no inodes"); // 抛出错误 

  ip = empty; // 使用空闲的 inode 条目
  ip->dev = dev; // 设置设备号
  ip->inum = inum; // 设置 inode 编号
  ip->ref = 1; // 初始化引用计数为 1
  ip->valid = 0; // 标记为无效，表示尚未从磁盘读取
  release(&itable.lock); // 释放 inode 表的自旋锁

  return ip; // 返回新的 inode 指针
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
// 增加 inode 的引用计数
// 返回传入的 inode 指针，以支持 ip = idup(ip1) 的用法
struct inode*
idup(struct inode *ip)
{
  acquire(&itable.lock); // 获取 inode 表的自旋锁
  ip->ref++; // 增加引用计数
  release(&itable.lock); // 释放 inode 表的自旋锁
  return ip; // 返回 传入的inode 指针
}

// Lock the given inode.
// Reads the inode from disk if necessary.

// 锁定给定的 inode
// 如果有必要，从磁盘读取该 inode
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1) // 无效的 inode 指针或引用计数小于 1
    panic("ilock"); // 内核奔溃

  acquiresleep(&ip->lock); // 获取 inode 的互斥锁

  if(ip->valid == 0){ // 需要从磁盘读取 inode
    bp = bread(ip->dev, IBLOCK(ip->inum, sb)); // 读取包含 inode 的磁盘块
    dip = (struct dinode*)bp->data + ip->inum%IPB; // 计算 inode 在块内的偏移位置
    ip->type = dip->type; // 设置 inode 类型
    ip->major = dip->major; // 设置主设备号
    ip->minor = dip->minor; // 设置次设备号
    ip->nlink = dip->nlink; // 设置链接数
    ip->size = dip->size; // 设置文件大小
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs)); // 从dip复制数据块地址数组到ip
    brelse(bp); // 释放bp缓冲区
    ip->valid = 1; // 标记 inode 为有效
    if(ip->type == 0) // 检查 inode 类型是否为0
      panic("ilock: no type"); // 内核奔溃
  }
}

// Unlock the given inode.
// 释放给定的 inode
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1) // 无效的 inode 指针 || 未持有锁 || 引用计数小于 1
    panic("iunlock"); // 内核奔溃

  releasesleep(&ip->lock); // 释放 inode 的互斥锁
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode table entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
// 递减内存中某个inode的引用计数 
// 如果这是最后一个引用，则该inode表项可以被回收 
// 如果这是最后一个引用且该inode没有链接到它，则释放磁盘上的inode及其内容
// 所有对iput()的调用都必须在一个事务内，以防它需要释放inode 
void
iput(struct inode *ip)
{
  acquire(&itable.lock); // 获取 inode 表的自旋锁

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){ // 如果这是“最后一个引用”且 “inode有效”且 “没有链接”
    // inode has no links and no other references: truncate and free.
    // inode没用任何引用: 释放inode及其内容 

    // ip->ref == 1 means no other process can have ip locked,
    // so this acquiresleep() won't block (or deadlock). 
    // ip->ref == 1 意味着没有其他进程可以锁定 ip
    // 因此这个 acquiresleep() 不会阻塞（或死锁）
    acquiresleep(&ip->lock); // 获取 inode 的互斥锁 

    release(&itable.lock); // 释放 inode 表的自旋锁

    itrunc(ip); // 释放 inode 的内容
    ip->type = 0; // 标记 inode 为未分配
    iupdate(ip); // 将修改写回磁盘
    ip->valid = 0; // 标记 inode 为无效

    releasesleep(&ip->lock); // 释放 inode 的互斥锁

    acquire(&itable.lock); // 重新获取 inode 表的自旋锁
  }

  ip->ref--; // 递减引用计数
  release(&itable.lock); // 释放 inode 表的自旋锁
}

// Common idiom: unlock, then put. 
// 释放锁，然后释放 inode
void
iunlockput(struct inode *ip)
{
  iunlock(ip); // 释放 inode 的睡眠锁
  iput(ip); // 递减 inode 的引用计数
}

// Inode content
// Inode 内容

// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].
// 每个inode的数据内容存储在磁盘块中. 第一个 NDIRECT 块号保存在 ip->addrs[] 数组中
// 接下啦NINDIRECT 块，则需要保存在以 ip->addrs[NDIRECT] 为块号的“间接块的data域”中 

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
// returns 0 if out of disk space.
// 返回 inode ip 中第 n 个块的磁盘块地址
// 如果没有这样的块，bmap 会分配一个
// 如果没有可用的磁盘空间则返回 0

/**
 * @brief 获取inode中第bn个块的磁盘地址
 * 
 * @param ip 指向inode结构体的指针
 * @param bn 块号
 * 
 * @return uint 磁盘块地址，失败 返回0
 * 
 */
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){ // 直接块
    if((addr = ip->addrs[bn]) == 0){ // 如果直接块地址为0，表示未分配
      addr = balloc(ip->dev); // 分配一个新的磁盘块
      if(addr == 0) // 分配失败
        return 0; // 返回0表示失败
      ip->addrs[bn] = addr; // 更新直接块地址
    }
    return addr; // 返回直接块地址
  }
  bn -= NDIRECT; // 间接块

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    // 加载间接块，如果有必要则分配
    if((addr = ip->addrs[NDIRECT]) == 0){ // 如果间接块地址为0，表示未分配
      addr = balloc(ip->dev); // 分配一个新的磁盘块作为间接块
      if(addr == 0) // 分配失败
        return 0; // 返回0表示失败
      ip->addrs[NDIRECT] = addr; // 更新间接块地址
    }
    bp = bread(ip->dev, addr); // 读取间接块
    a = (uint*)bp->data; // 获取间接块中的块地址数组
    if((addr = a[bn]) == 0){ // 如果间接块地址为0，表示未分配
      addr = balloc(ip->dev); // 分配一个新的磁盘块
      if(addr){ // 分配成功
        a[bn] = addr; // 更新间接块地址数组
        log_write(bp); // 写日志以记录对间接块的修改
      }
    }
    brelse(bp); // 释放间接块缓冲区
    return addr; // 返回间接块地址
  }

  panic("bmap: out of range"); // 块号超出范围
}

// Truncate inode (discard contents).
// Caller must hold ip->lock.
// 释放 inode (丢弃内容) 调用者必须持有 ip->lock 
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){ // 释放inode中的直接块
    if(ip->addrs[i]){ // 如果直接块有效
      bfree(ip->dev, ip->addrs[i]); // 释放直接块 
      ip->addrs[i] = 0; // 清除直接块地址
    }
  }

  if(ip->addrs[NDIRECT]){ // 释放inode中的间接块
    bp = bread(ip->dev, ip->addrs[NDIRECT]); // 读取间接块
    a = (uint*)bp->data; // 获取间接块中的块地址数组
    for(j = 0; j < NINDIRECT; j++){ // 遍历间接块中的每个块地址
      if(a[j]) // 如果块地址有效
        bfree(ip->dev, a[j]); // 释放该块
    }
    brelse(bp); // 释放间接块缓冲区
    bfree(ip->dev, ip->addrs[NDIRECT]); // 释放间接块本身
    ip->addrs[NDIRECT] = 0; // 清除间接块地址
  }

  ip->size = 0; // 重置文件大小为0
  iupdate(ip); // 将修改写回磁盘
}

// Copy stat information from inode.
// Caller must hold ip->lock.
// 从 inode 复制状态信息 调用者必须持有 ip->lock 
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev; // 设备号
  st->ino = ip->inum; // inode 编号
  st->type = ip->type; // 文件类型
  st->nlink = ip->nlink; // 链接数
  st->size = ip->size; // 文件大小
}

// Read data from inode.
// Caller must hold ip->lock.
// If user_dst==1, then dst is a user virtual address;
// otherwise, dst is a kernel address.

// 从inode读取数据
// 调用者必须持有ip->lock
// 如果user_dst==1，则dst是用户虚拟地址；否则，dst是内核地址
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  // 1. off > ip->size 偏移量超出文件大小
  // 2. off + n < off 偏移量加读取长度溢出 
  if(off > ip->size || off + n < off) // 检查偏移量是否超出文件大小 
    return 0; // 返回0表示读取失败
  if(off + n > ip->size) // 确保不超出文件大小
    n = ip->size - off; // 调整读取长度

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){ // 循环读取数据, 直到读取完指定长度, 每次读取 m 字节
    uint addr = bmap(ip, off/BSIZE); // 获取对应块号的磁盘块地址
    if(addr == 0) // 块地址为0，表示读取失败
      break; // 退出循环
    bp = bread(ip->dev, addr); // 读取磁盘块
    // n - tot 表示本次还需要处理的总字节数：n 是用户请求的总字节数，tot 是已经处理过的字节数
    // BSIZE - off%BSIZE 计算当前块中还剩多少字节可以用。BSIZE 是磁盘块的大小，off % BSIZE 得到当前偏移量在块内的位置
    m = min(n - tot, BSIZE - off%BSIZE); // 计算本次读取的字节数
    // 将数据复制到目标地址
    // 目标地址：dst
    // 源地址： bp->data + (off % BSIZE)，当前块的偏移位置off 
    // 字数 m 字节
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) { // 拷贝失败
      brelse(bp); // 释放缓冲区
      tot = -1; // 标记读取失败
      break; // 退出循环
    }
    brelse(bp); // 释放缓冲区
  }
  return tot; // 返回成功读取的总字节数
}

// Write data to inode.
// Caller must hold ip->lock.
// If user_src==1, then src is a user virtual address;
// otherwise, src is a kernel address.
// Returns the number of bytes successfully written.
// If the return value is less than the requested n,
// there was an error of some kind.

// 向 inode 写入数据 调用者必须持有 ip->lock
// 如果 user_src==1，则 src 是用户虚拟地址；否则，src 是内核地址
// 返回成功写入的字节数，如果返回值小于请求的 n，则表示发生了某种错误
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off) // 检查偏移量是否超出文件大小或者是否off溢出
    return -1; // 返回-1表示写入失败
  if(off + n > MAXFILE*BSIZE) // 检查写入是否超出文件最大大小
    return -1; // 返回-1表示写入失败

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){ // 循环写入数据, 直到写入完指定长度, 每次写入 m 字节
    uint addr = bmap(ip, off/BSIZE); // 获取对应块号的磁盘块地址
    if(addr == 0) // 块地址为0，表示写入失败
      break; // 退出循环
    bp = bread(ip->dev, addr); // 读取磁盘块
    // n - tot 表示本次还需要处理的总字节数：n 是用户请求的总字节数，tot 是已经处理过的字节数
    // BSIZE - off%BSIZE 计算当前块中还剩多少字节可以用。BSIZE 是磁盘块的大小，off % BSIZE 得到当前偏移量在块内的位置
    m = min(n - tot, BSIZE - off%BSIZE); // 计算本次写入的字节数
    // 将数据从源地址复制到磁盘块
    // 源地址：src
    // 目标地址：bp->data + (off % BSIZE)，当前块的偏移位置off 
    // 字数 m 字节  
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) { // 拷贝失败
      brelse(bp); // 释放缓冲区
      break; // 退出循环
    }
    log_write(bp); // 写日志以记录对磁盘块的修改
    brelse(bp); // 释放缓冲区
  }

  if(off > ip->size) // 如果写入操作扩展了文件大小
    ip->size = off; // 更新文件大小

  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  // 即使文件大小没有改变，也要将 inode 写回磁盘
  // 因为上面的循环可能调用了 bmap() 并向 ip->addrs[] 添加了一个新块
  iupdate(ip); // 将inode修改写回磁盘

  return tot; // 返回成功写入的总字节数
}

// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ); // 比较两个目录项名称，最多比较 DIRSIZ 个字符
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
// 在目录nodep中查找目录项name
// 如果找到，则将*poff设置为该目录项的字节偏移量
// 返回该目录项对应的inode指针，如果未找到则返回NULL 
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR) // 检查inode类型是否为目录
    panic("dirlookup not DIR"); // 尝试在非目录中查找目录项，内核奔溃

  for(off = 0; off < dp->size; off += sizeof(de)){ // 遍历目录中的每个目录项
    // off 是目录项在目录文件中的偏移量
    // de 是用于存储读取的目录项数据的结构体
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) // 读取目录项
      panic("dirlookup read"); // 读取目录项失败, 内核奔溃
    if(de.inum == 0) // 空目录项，跳过
      continue;
    if(namecmp(name, de.name) == 0){ // 目录项名称匹配
      // entry matches path element
      if(poff) // 如果传入了poff参数
        *poff = off; // 设置目录项的字节偏移量
      inum = de.inum; // 获取目录项对应的inode编号
      return iget(dp->dev, inum); // 返回对应的inode指针
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
// Returns 0 on success, -1 on failure (e.g. out of disk blocks). 

// 将新的目录项(name, inum)写入目录dp
// 成功返回0，失败返回-1（例如磁盘块不足）
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de; 
  struct inode *ip;

  // Check that name is not present.
  // 检查名称是否已存在
  if((ip = dirlookup(dp, name, 0)) != 0){ // 目录项已存在
    iput(ip); // 释放inode
    return -1; // 返回-1表示失败
  }

  // Look for an empty dirent.
  // 查找一个空的目录项
  for(off = 0; off < dp->size; off += sizeof(de)){ // 遍历目录中的每个目录项
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) // 读取目录项
      panic("dirlink read"); // 读取目录项失败, 内核奔溃
    if(de.inum == 0) // 找到一个空目录项
      break; // 退出循环
  }

  strncpy(de.name, name, DIRSIZ); // 复制目录项名称，最多复制 DIRSIZ 个字符
  de.inum = inum; // 设置目录项的inode编号
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) // 写入目录项
    return -1; // 返回-1表示失败

  return 0; // 返回0表示成功
}

// Paths 
// 路径 

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
// 拷贝下一个路径元素从path到name，返回指向下一个路径元素的指针
// 返回的路径里没有前导斜杠，因此调用者可以检查 *path=='\0' 来判断是否是最后一个元素
// 如果没有元素可移除，则返回0

// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0

/**
 * @brief 把path中的下一个路径元素复制到name中 
 * 
 * @param path 指向路径字符串的指针
 * @param name 用于存储复制的路径元素，必须有足够空间（至少DIRSIZ字节） 
 * @return char* 指向下一个路径元素的指针 
 */
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/') // 跳过前导斜杠
    path++; // 指针前移
  if(*path == 0) // 没有更多路径元素
    return 0; // 返回0 

  s = path; // 记录当前路径元素的起始位置
  while(*path != '/' && *path != 0) // 找到下一个斜杠或字符串结束
    path++; // 指针前移
  
  len = path - s; // 计算路径元素的长度
  if(len >= DIRSIZ) // 如果路径元素长度超过DIRSIZ
    memmove(name, s, DIRSIZ); // 复制前DIRSIZ个字符到name
  else {
    memmove(name, s, len); // 复制整个路径元素到name
    name[len] = 0; // 添加字符串结束符
  }

  while(*path == '/') // 跳过斜杠
    path++; // 指针前移
  return path; // 返回指向下一个路径元素的指针
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
// 查找并返回路径对应的inode指针 
// 如果 parent 不是NULL， 返回父目录的inode指针，并将最后一个路径元素复制到name中
// name必须有足够空间（至少DIRSIZ字节）
// 必须在一个事务内调用，因为它会调用 iput()

/**
 * @brief 查找并返回路径对应的inode指针
 *
 * @param path 路径字符串
 * @param nameiparent
如果非0，返回父目录的inode指针，并将最后一个路径元素复制到name中
 * @param name 用于存储最后一个路径元素，必须有足够空间（至少DIRSIZ字节）
 *
 * @return struct inode* 指向对应的inode结构体指针
 *
 * @note 这里加锁，而iget不加锁的原因：在处理类似于 . 和 .. 这种目录项的时候可能会引起死锁
 * @note 为了避免在查询的时候，另外一个进程可能正在删除，所以这里不光是unlock,还需要对引用计数-1
 * @note 为了加速执行，所以这里采用的是对每个inode的细粒度的锁
 *
 */
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/') // path 是绝对路径  
    ip = iget(ROOTDEV, ROOTINO); // 从根目录开始查找
  else
    ip = idup(myproc()->cwd); // 否则从当前工作目录开始查找 

  while((path = skipelem(path, name)) != 0){ // 逐个处理路径元素
    ilock(ip); // 锁定当前目录的inode
    if(ip->type != T_DIR){ // 当前inode不是目录
      iunlockput(ip); // 释放锁并递减引用计数
      return 0; // 返回0表示查找失败
    }
    if(nameiparent && *path == '\0'){ // 需要返回父目录 
      // Stop one level early.
      // 提前停止一级
      iunlock(ip); // 释放当前目录的锁
      return ip; // 返回当前目录的inode指针
    }
    if((next = dirlookup(ip, name, 0)) == 0){ // 没有找到下一个路径元素对应的inode
      iunlockput(ip); // 释放锁并递减引用计数
      return 0; // 返回0表示查找失败
    }
    iunlockput(ip); // 释放当前目录的锁并递减引用计数
    ip = next; // 继续处理下一个路径元素
  }

  if(nameiparent){ // 需要返回父目录，但路径已处理完
    iput(ip); // 释放当前inode
    return 0; // 返回0表示查找失败
  }
  return ip; // 返回找到的inode指针
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name); // 查找路径对应的inode指针
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name); // 查找路径对应的父目录的inode指针，并将最后一个路径元素复制到name中
}
