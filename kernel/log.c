#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.

// 一个简单的log机制保证多个文件系统调用的并发执行。 

// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
 
// 一次log事务允许包含多次文件系统调用的更新。因此log系统在提交时不允许还有未完成的文件有关的系统调用
// 在执行。这样就不需要考虑提交时是否会把未提交的系统调用更新写入磁盘的问题 

// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.

// 一个文件系统调用应该在开始和结束时分别调用 begin_op()/end_op()
// 通常 begin_op() 只是增加正在进行的文件系统调用的计数并返回 
// 但是如果它认为 log 空间快用完了，它会睡眠直到最后一个未完成的 end_op() 提交

// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// 物理 Redo 日志 包含磁盘块内容，磁盘上的日志格式如下： 
// 头块，包含块 A、B、C 的块号
// 块 A
// 块 B
// 块 C
// ...
// 日志追加是同步的

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.

/**
 * @brief 结构体：描述简易事务日志的头
 * 
 * @note 该结构体用于描述简易事务日志的头信息，包含日志中块的数量及其对应的块号列表#提交前
 * 
 */
struct logheader {
  int n; // number of blocks in the log 日志中需要更新到磁盘的块数量
  int block[LOGSIZE]; // 每个元素存储一个对应磁盘的块号，而索引对应的是日志块的个数
};

/**
 * @brief 结构体：描述简易事务日志 
 * 
 * @note 该结构体用于描述简易事务日志的整体信息，包含自旋锁、日志起始位置、大小、当前活跃的文件系统调用数量等
 * 
 */
struct log {
  struct spinlock lock; // Lock protecting log 保护日志的自旋锁
  int start; // block number of first log block 日志数据块的起始块号，其中第 0 块是日志头块
  int size; // size of the log 日志块数量，由配置决定
  int outstanding; // how many FS sys calls are executing. 当前正在执行的文件系统调用数量
  int committing;  // in commit(), please wait. 是否正在提交，请等待
  int dev; // device number 设备号
  struct logheader lh; // in-memory log header 内存中的日志头
};

struct log log; // 全局日志实例

/**
 * @brief 从日志中恢复文件系统状态
 * 
 */
static void recover_from_log(void);

/**
 * @brief 提交当前事务
 * 
 */
static void commit();

void
initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE) // 日志头不能超过块大小 
    panic("initlog: too big logheader"); // 日志头过大，内核奔溃

  initlock(&log.lock, "log"); // 初始化日志自旋锁
  log.start = sb->logstart; // 设置日志起始块号
  log.size = sb->nlog; // 设置日志大小
  log.dev = dev; // 设置设备号
  recover_from_log(); // 从日志中恢复文件系统状态
}

// Copy committed blocks from log to their home location
/**
 * @brief 将已提交的日志块内容从日志区域复制回其原始位置
 * 
 * @param recovering 如果为非零值，表示正在进行恢复操作
 * 
 */
static void
install_trans(int recovering)
{
  int tail; 

  for (tail = 0; tail < log.lh.n; tail++) { // 遍历日志中的每个块
    // 因为第 0 块是日志头块，所以数据块从 log.start + 1 开始
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block // 读取日志对应的数据块
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst // 读取目标对应的数据块
    memmove(dbuf->data, lbuf->data, BSIZE);  // 将日志块内容复制到目标块
    bwrite(dbuf);  // 将目标块写入磁盘
    if(recovering == 0) // 如果不是恢复操作，释放日志块
      bunpin(dbuf); // 目标块使用的refcnt减1 
    brelse(lbuf); // 释放日志块
    brelse(dbuf); // 释放目标块
  }
}

// Read the log header from disk into the in-memory log header

/**
 * @brief 读取日志头块，将其内容从硬盘加载到内存中的日志头结构体
 * 
 */
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start); // 读取日志头块 
  struct logheader *lh = (struct logheader *) (buf->data); // 指向日志头数据
  int i; 
  log.lh.n = lh->n; // 设置内存日志头中的块数量
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i]; // 复制每个块号到内存日志头
  }
  brelse(buf); // 释放缓冲区
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.

/**
 * @brief 将内存日志头写回到磁盘日志头块
 * 
 * 这是当前事务提交的真正时刻
 * 
 */
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start); // 读取日志头块
  struct logheader *hb = (struct logheader *) (buf->data); // 指向日志头数据
  int i; 
  hb->n = log.lh.n; // 设置磁盘日志头中的块数量
  for (i = 0; i < log.lh.n; i++) { 
    hb->block[i] = log.lh.block[i]; // 复制每个块号到磁盘日志头
  }
  bwrite(buf); // 日志头块写回磁盘
  brelse(buf); // 释放缓冲区
}

static void
recover_from_log(void)
{
  read_head(); // 读取日志头块，将其内容从硬盘加载到内存中的日志头结构体
  install_trans(1); // if committed, copy from log to disk 如果已提交，则将日志内容复制回磁盘
  log.lh.n = 0; // 清空内存日志头中的块数量 
  write_head(); // clear the log 清空日志头块
}

// called at the start of each FS system call.
// 每个涉及到文件系统的系统调用开始时调用
void
begin_op(void)
{
  acquire(&log.lock); // 获取日志结构体的自旋锁
  while(1){ 
    if(log.committing){ // 如果正在提交事务
      sleep(&log, &log.lock); // 等待提交完成
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){ // 
      // this op might exhaust log space; wait for commit.
      // 如果日志空间不足，该操作可能会耗尽日志空间
      sleep(&log, &log.lock); // 等待提交完成
    } else {
      log.outstanding += 1; // 增加当前活跃的文件系统调用数量
      release(&log.lock); // 释放日志结构体的自旋锁
      break; // 退出循环
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
// 每个涉及到文件系统的系统调用结束时调用
// 如果这是最后一个未完成的操作，则提交事务
void
end_op(void)
{
  int do_commit = 0; // 是否需要提交事务的标志

  acquire(&log.lock); // 获取日志结构体的自旋锁
  log.outstanding -= 1; // 减少当前活跃的文件系统调用数量
  if(log.committing) // 如果正在提交事务
    panic("log.committing"); // 内核奔溃
  if(log.outstanding == 0){ // 如果没有活跃的文件系统调用
    do_commit = 1; // 设置提交标志
    log.committing = 1; // 标记正在提交事务
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    // begin_op() 可能正在等待日志空间，降低 log.outstanding 来减少了保留空间的数量
    wakeup(&log); // 唤醒等待的进程
  }
  release(&log.lock); // 释放日志结构体的自旋锁

  if(do_commit){ //最后一个系统调用，则提交事务
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    // 在不持有锁的情况下调用，因为不允许在持有锁时睡眠
    commit(); // 提交事务
    acquire(&log.lock); // 重新获取日志结构体的自旋锁
    log.committing = 0; // 重置提交标志
    wakeup(&log); // 唤醒等待的进程
    release(&log.lock); // 释放日志结构体的自旋锁
  }
}

// Copy modified blocks from cache to log.

/**
 * @brief 将内存中修改过的块复制到日志区域 
 * 
 */
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    // 读取日志对应的日志数据块的缓冲块指针
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    // 读取日志对应数据的缓存块指针
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE); // 将缓存块内容复制到日志块
    bwrite(to);  // write the log // 将日志块写入磁盘
    brelse(from); // 释放缓存块
    brelse(to); // 释放日志块
  }
}

static void
commit()
{
  if (log.lh.n > 0) { // 日志里有需要写的块
    // 将修改过的数据从缓存块（内存）复制到日志区域里的数据块（磁盘）
    write_log();     // Write modified blocks from cache to log
    // 将日志头写回磁盘，标记事务已提交
    write_head();    // Write header to disk -- the real commit
    // 将日志内容从日志区域（磁盘）复制回其原始位置（磁盘）
    install_trans(0); // Now install writes to home locations
    log.lh.n = 0; // 清空内存日志头中的块数量
    // 清空日志头块，标记日志为空
    write_head();    // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.

// 调用者已经修改了缓冲块中的数据。把修改过硬盘块号和对应缓冲块的引用计数加1记录下来 
// commit()/write_log() 会负责把数据写回磁盘 

// log_write() replaces bwrite(); a typical use is:
// log_write() 替代 bwrite()；一个典型的用法是：
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;

  acquire(&log.lock); // 获取日志结构体的自旋锁
  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1) // 检查日志是否已满
    panic("too big a transaction"); // 事务过大，内核奔溃
  if (log.outstanding < 1) // 检查是否有活跃的文件系统调用
    panic("log_write outside of trans"); // 在事务外调用 log_write，内核奔溃

  for (i = 0; i < log.lh.n; i++) { // 检查块号是否已存在于日志中
    // 要写的块已经在日志中
    if (log.lh.block[i] == b->blockno)   // log absorption 
      break;
  }
  log.lh.block[i] = b->blockno; // 记录块号到日志头 
  if (i == log.lh.n) {  // Add new block to log? // 如果是新块，增加日志块数量
    bpin(b); // 缓冲块引用计数加1
    log.lh.n++; // 增加日志要写的块数量
  }
  release(&log.lock); // 释放日志结构体的自旋锁
}

