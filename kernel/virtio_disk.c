//
// driver for qemu's virtio disk device.
// uses qemu's mmio interface to virtio.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h" // virtio 驱动

// the address of virtio mmio register r.

/**
 * @brief 定义了一个宏 R(r)，用于简化对 Virtio MMIO（内存映射 I/O）设备寄存器的访问 
 * 
 * @param r 寄存器的偏移地址
 * 
 * VIRTIO0 是 Virtio 设备的基地址，通常是一个固定的物理地址
 * (r) 是寄存器的偏移量，表示要访问 Virtio 设备的哪个寄存器
 * VIRTIO0 + (r) 计算出目标寄存器的实际地址
 * (volatile uint32 *) 将该地址转换为指向 32 位无符号整数的指针
 * 加上 volatile 关键字，确保每次访问都直接操作硬件寄存器，不会被编译器优化或缓存 
 */
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r))) // 可以方便地读写 Virtio 设备的各类寄存器，实现与设备的直接交互

/**
 * @brief 这段代码定义了一个静态结构体 disk，用于管理 Virtio 虚拟磁盘设备的所有核心数据
 * 
 * 它将设备的 DMA 描述符、队列、状态和操作信息集中在一起，方便驱动进行磁盘 I/O 操作和状态跟踪
 * 
 */
static struct disk {
  // a set (not a ring) of DMA descriptors, with which the
  // driver tells the device where to read and write individual
  // disk operations. there are NUM descriptors.
  // most commands consist of a "chain" (a linked list) of a couple of
  // these descriptors.

  // 指向一组 DMA 描述符（不是环结构），驱动通过这些描述符告诉设备每次磁盘操作的数据读写位置
  // 大多数命令由多个描述符链式连接组成
  struct virtq_desc *desc; // DMA 描述符数组

  // a ring in which the driver writes descriptor numbers
  // that the driver would like the device to process.  it only
  // includes the head descriptor of each chain. the ring has
  // NUM elements.

  // 指向一个环结构，驱动在其中写入希望设备处理的描述符编号
  // 仅包含每个描述符链的头部，环中有 NUM 个元素
  struct virtq_avail *avail; // 可用描述符环

  // a ring in which the device writes descriptor numbers that
  // the device has finished processing (just the head of each chain).
  // there are NUM used ring entries.

  // 指向一个环结构，设备在其中写入已完成处理的描述符编号（仅头部）
  // 环中有 NUM 个已用条目
  struct virtq_used *used; // 已写入描述符环

  // our own book-keeping.
  char free[NUM];  // is a descriptor free? 描述符是否空闲，1表示空闲，0表示已分配
  // 驱动已处理的 used 队列索引，避免重复处理
  uint16 used_idx; // we've looked this far in used[2..NUM]. 

  // track info about in-flight operations,
  // for use when completion interrupt arrives.
  // indexed by first descriptor index of chain.
  // 跟踪正在进行的磁盘操作，便于中断到来时查找对应的缓冲区和状态
  struct {
    struct buf *b; // 相关联的缓冲区指针
    char status; // 设备操作的状态，0表示成功，其他值表示错误
  } info[NUM]; 

  // disk command headers.
  // one-for-one with descriptors, for convenience.
  
  struct virtio_blk_req ops[NUM]; // 磁盘请求操作数组，与描述符一一对应，方便构造和管理请求
  
  struct spinlock vdisk_lock; // 自旋锁，保护该结构体的并发访问
  
} disk;

void
virtio_disk_init(void)
{
  uint32 status = 0; 

  initlock(&disk.vdisk_lock, "virtio_disk"); // 初始化disk的自旋锁

  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 2 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk"); // 检查 Virtio 设备的标识符，确保找到正确的 Virtio 磁盘设备 
  }
  
  // reset device
  *R(VIRTIO_MMIO_STATUS) = status; // 复位设备，清除状态寄存器

  // set ACKNOWLEDGE status bit
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE; // 设置 ACKNOWLEDGE 状态位，表示驱动已识别设备
  *R(VIRTIO_MMIO_STATUS) = status; // 写回状态寄存器 

  // set DRIVER status bit
  status |= VIRTIO_CONFIG_S_DRIVER; // 设置 DRIVER 状态位，表示驱动已准备好与设备通信
  *R(VIRTIO_MMIO_STATUS) = status;  

  // negotiate features
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES); // 读取设备支持的功能特性 
  features &= ~(1 << VIRTIO_BLK_F_RO); // 不支持只读磁盘，清除该位
  features &= ~(1 << VIRTIO_BLK_F_SCSI); // 不支持 SCSI 命令直通，清除该位
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE); // 不支持写回缓存，清除该位
  features &= ~(1 << VIRTIO_BLK_F_MQ); // 不支持多队列，清除该位
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT); // 不支持任意布局，清除该位
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX); // 不支持事件索引，清除该位
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC); // 不支持间接描述符，清除该位
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features; // 写回协商后的功能特性

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK; // 设置 FEATURES_OK 状态位，表示功能协商完成
  *R(VIRTIO_MMIO_STATUS) = status; 

  // re-read status to ensure FEATURES_OK is set.
  status = *R(VIRTIO_MMIO_STATUS); // 重新读取状态寄存器，确保 FEATURES_OK 位被设置
  if(!(status & VIRTIO_CONFIG_S_FEATURES_OK)) 
    panic("virtio disk FEATURES_OK unset"); // 如果未设置，表示协商失败，内核奔溃

  // initialize queue 0.
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0; // 初始化选择队列 0

  // ensure queue 0 is not in use.
  if(*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready"); // 确保队列未被使用，READY 位应为 0

  // check maximum queue size.
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX); // 读取队列的最大支持大小
  if(max == 0) // 不支持队列 内核奔溃
    panic("virtio disk has no queue 0");
  if(max < NUM) // 队列太小，无法满足驱动需求 内核奔溃
    panic("virtio disk max queue too short");

  // allocate and zero queue memory.
  disk.desc = kalloc(); // 分配一页内存用于 DMA 描述符
  disk.avail = kalloc(); // 分配一页内存用于可用描述符环
  disk.used = kalloc(); // 分配一页内存用于已完成描述符环 
  if(!disk.desc || !disk.avail || !disk.used)
    panic("virtio disk kalloc"); // 任何一个分配失败，内核奔溃
  // 清零分配的内存
  memset(disk.desc, 0, PGSIZE); 
  memset(disk.avail, 0, PGSIZE);
  memset(disk.used, 0, PGSIZE);

  // set queue size.
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM; // 设置队列大小为 NUM

  // write physical addresses.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)disk.desc; // 设置描述符表的低 32 位物理地址
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)disk.desc >> 32; // 设置描述符表的高 32 位物理地址
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)disk.avail; // 设置可用环的低 32 位物理地址
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)disk.avail >> 32; // 设置可用环的高 32 位物理地址
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)disk.used; // 设置已用环的低 32 位物理地址
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)disk.used >> 32; // 设置已用环的高 32 位物理地址

  // queue is ready.
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1; // 设置队列为就绪状态

  // all NUM descriptors start out unused.
  // 遍历描述符表，初始化所有描述符为空闲状态
  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK; // 设置 DRIVER_OK 状态位，表示驱动已完成初始化，设备可以开始工作
  *R(VIRTIO_MMIO_STATUS) = status;

  // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
  // plic.c 和 trap.c 这两个文件负责配置和管理来自 VIRTIO0_IRQ（Virtio 虚拟磁盘设备中断号）的中断
  // 具体来说，plic.c 负责在平台级中断控制器（PLIC）中使能和优先级设置该设备的中断
  // 而 trap.c 负责在内核中处理中断请求
  // 这样确保当 Virtio 设备有事件发生时，内核能够及时响应并进行相应的 I/O 操作
  // 这种分工保证了设备中断能够被正确捕获和处理，是驱动与硬件交互的关键环节
}

// find a free descriptor, mark it non-free, return its index.
/**
 * @brief 在 VirtIO 磁盘驱动中从描述符池中分配一个可用的描述符索引
 * 按顺序遍历从disk.free布尔数组的索引 0 到 NUM - 1，找到第一个空闲项（非零），将其标记为占用（置为 0），并立即返回该索引
 * @return int disk.free数组相应的索引，如果没有可用描述符则返回 -1
 */
static int
alloc_desc()
{
  for(int i = 0; i < NUM; i++){ // 遍历描述符池
    if(disk.free[i]){ // 找到第一个空闲描述符
      disk.free[i] = 0;
      return i; // 返回该描述符的索引
    }
  }
  return -1; // 如果没有可用描述符，返回 -1
}

// mark a descriptor as free.
/**
 * @brief 在 VirtIO 磁盘驱动中将指定索引的描述符标记为空闲
 * 
 * @param i 描述符的索引
 */
static void
free_desc(int i)
{
  if(i >= NUM)  // 如果索引超出范围，内核奔溃
    panic("free_desc 1");
  if(disk.free[i]) // 如果描述符已经是空闲状态，内核奔溃
    panic("free_desc 2");
  disk.desc[i].addr = 0; // 清除描述符的地址字段
  disk.desc[i].len = 0; // 清除描述符的长度字段
  disk.desc[i].flags = 0; // 清除描述符的标志字段
  disk.desc[i].next = 0; // 清除描述符的下一个字段
  disk.free[i] = 1; // 将描述符标记为空闲状态
  wakeup(&disk.free[0]); // 唤醒等待该描述符的进程
}

// free a chain of descriptors.
/**
 * @brief 在 VirtIO 磁盘驱动中释放一条描述符链
 * 
 * @param i 描述符链的起始索引
 */
static void
free_chain(int i)
{
  while(1){ // 循环释放描述符链
    int flag = disk.desc[i].flags; // 获取当前描述符的标志
    int nxt = disk.desc[i].next; // 获取下一个描述符的索引
    free_desc(i); // 释放当前描述符
    if(flag & VRING_DESC_F_NEXT) // 如果有下一个描述符，继续释放
      i = nxt; // 移动到下一个描述符
    else
      break; // 无法找到下一个描述符，则结束释放
  }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
// 分配3条磁盘描述符（他们之间不需要连续），磁盘传输总是需要三条描述符
/**
 * @brief 在 VirtIO 磁盘驱动中分配三条描述符，用于一次磁盘传输操作
 * 
 * @param idx 描述符索引指针，用于存储分配的三个描述符的索引
 * @return int 返回 0 表示成功分配，返回 -1 表示分配失败
 */
static int
alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc(); // 分配一个描述符
    if(idx[i] < 0){ // 如果分配失败，释放已分配的描述符并返回 -1
      for(int j = 0; j < i; j++) // 释放已分配的描述符
        free_desc(idx[j]); 
      return -1;
    }
  }
  return 0;
}

void
virtio_disk_rw(struct buf *b, int write)
{
  // 文件系统的块号转换为磁盘的扇区号（LBA）
  // b->blockno 以文件系统块为单位，块大小为 BSIZE 字节
  // 而 VirtIO 块设备以 512 字节为最小寻址单位（扇区）
  // 因此需要乘以每个块包含的扇区数，即 𝐵𝑆𝐼𝑍𝐸 / 512 
  // 得到该块对应的起始扇区号并存入 64 位的 sector
  // 与 VirtIO 请求头的 64 位 LBA 字段匹配，避免大磁盘上溢出
  uint64 sector = b->blockno * (BSIZE / 512); // 计算要读写的磁盘扇区号

  acquire(&disk.vdisk_lock); // 获取磁盘锁，确保对共享数据结构的互斥访问

  // the spec's Section 5.2 says that legacy block operations use
  // three descriptors: one for type/reserved/sector, one for the
  // data, one for a 1-byte status result.

  // allocate the three descriptors.
  // 这里分配三条描述符，用于一次磁盘传输操作
  // 根据 VirtIO 规范第 5.2 节，传统的块设备操作需要三条描述符：
  // 第一条描述符用于存放请求头（type/reserved/sector）
  // 第二条描述符用于存放数据缓冲区
  // 第三条描述符用于存放 1 字节的状态结果
  int idx[3];
  // 分配三条描述符，直到成功为止
  while(1){
    if(alloc3_desc(idx) == 0) { // 成功分配三条描述符
      break;
    }
    sleep(&disk.free[0], &disk.vdisk_lock); // 分配失败则等待并释放锁，直到有描述符可用
  }

  // format the three descriptors.
  // qemu's virtio-blk.c reads them.
  // 格式化这三条描述符，准备传输请求
  // 这些描述符将被 QEMU 的 virtio-blk.c 驱动读取和处理
  struct virtio_blk_req *buf0 = &disk.ops[idx[0]]; // 获取第一个描述符对应的请求头缓冲区

  if(write)
    buf0->type = VIRTIO_BLK_T_OUT; // write the disk // 设置请求类型为写操作
  else
    buf0->type = VIRTIO_BLK_T_IN; // read the disk // 设置请求类型为读操作
  buf0->reserved = 0; // 保留字段置为 0
  buf0->sector = sector; // 设置请求的磁盘扇区号

  disk.desc[idx[0]].addr = (uint64) buf0; // 设置第一个描述符的地址为请求头缓冲区的物理地址
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req); // 设置第一个描述符的长度为请求头结构体的大小
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT; // 设置标志，表示后面还有下一个描述符
  disk.desc[idx[0]].next = idx[1]; // 设置下一个描述符的索引为第二个描述符

  disk.desc[idx[1]].addr = (uint64) b->data; // 设置第二个描述符的地址为数据缓冲区的物理地址
  disk.desc[idx[1]].len = BSIZE; // 设置第二个描述符的长度为数据块大小 
  if(write) // 如果是写操作
    disk.desc[idx[1]].flags = 0; // device reads b->data // 设备从数据缓冲区读取数据
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data // 设备向数据缓冲区写入数据
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT; // 设置标志，表示后面还有下一个描述符
  disk.desc[idx[1]].next = idx[2]; // 设置下一个描述符的索引为第三个描述符

  disk.info[idx[0]].status = 0xff; // device writes 0 on success // 初始化状态字节为 0xff，表示未完成
  disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status; // 设置第三个描述符的地址为状态字节的物理地址
  disk.desc[idx[2]].len = 1; // 设置第三个描述符的长度为 1 字节
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status // 设备向状态字节写入结果
  disk.desc[idx[2]].next = 0; // 第三个描述符是链的末尾，没有下一个描述符

  // record struct buf for virtio_disk_intr().
  b->disk = 1; // 标记缓冲区正在进行磁盘操作
  disk.info[idx[0]].b = b; // 关联缓冲区指针，便于中断处理时查找

  // tell the device the first index in our chain of descriptors.
  // 将描述符链的头索引写入可用环，通知设备有新的请求可处理

  // 本次 I/O 请求的描述符链“发布”到 VirtIO 的可用环（avail ring）
  // idx[0] 是描述符链的头索引，设备从它开始沿着 next 字段遍历整条链

  // 驱动把这个索引写入 disk.avail->ring[...] 的当前生产者位置，位置由 disk.avail->idx % NUM 决定，以支持环形缓冲的回绕
  // 可用环是驱动与设备共享的环形队列：驱动在 ring 槽位写入新的头索引，随后会通过内存屏障确保描述符和 ring 写入顺序可见
  // 再递增 disk.avail->idx 表示“可用条目计数+1”，最后通过 MMIO 向设备发通知

  // 这里对数组下标使用取模是为了在缓冲区末尾回绕
  // 而对 avail->idx 本身不取模（它按规范单调递增，设备用它与自己的已处理计数比较来确定新提交的条目数量）。
  disk.avail->ring[disk.avail->idx % NUM] = idx[0]; // 将描述符链的头索引写入可用环

  __sync_synchronize(); // 全局内存屏障，确保前面的内存写入在时序上“先于”后续的写入

  // tell the device another avail ring entry is available.
  // 通知设备有新的可用环条目
  disk.avail->idx += 1; // not % NUM ... // 递增可用条目计数，不取模

  __sync_synchronize(); // 全局内存屏障，确保前面的内存写入在时序上“先于”后续的写入

  // 通知设备有新的可用描述符链可处理
  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number // 向队列通知寄存器写入 0，表示队列 0 有新请求

  // Wait for virtio_disk_intr() to say request has finished.
  // 等待中断处理程序通知请求已完成
  while(b->disk == 1) { // 等待中断处理程序将 b->disk 置为 0，表示请求已完成
    sleep(b, &disk.vdisk_lock); // 进入睡眠，等待中断唤醒
  }

  disk.info[idx[0]].b = 0; // 清除关联的缓冲区指针
  free_chain(idx[0]); // 释放描述符链

  release(&disk.vdisk_lock); // 释放磁盘锁
}

void
virtio_disk_intr()
{
  acquire(&disk.vdisk_lock); // 获取磁盘锁，确保对共享数据结构的互斥访问

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  // 设备在我们告诉它已处理该中断之前不会触发另一个中断
  // 这可能与设备向“used”环写入新条目发生竞争
  // (读取状态与写确认之间，设备可能又向 “used” 环写入了新的完成条目)
  // 在这种情况下，可能会在本次中断中处理新完成的条目
  // 而在下次中断中没有任何事情要做，这种情况是无害的

  // 完成对 VirtIO 设备中断的确认（ack） 
  // 读取中断状态寄存器 VIRTIO_MMIO_INTERRUPT_STATUS
  // 再将低两位（按位与 0x3）写回到中断确认寄存器 VIRTIO_MMIO_INTERRUPT_ACK，表示“这些中断原因我已经看到了”
  // 0x3 覆盖了 VirtIO 规范中定义的两类中断原因位：队列事件（vring，通常是有已完成的条目可处理）和配置变更
  // 完成确认后，设备在这次原因被清除前不会再次触发同类中断，从而避免中断风暴
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  // 全局内存屏障（full memory barrier）
  // 它既是编译器屏障，也会在目标架构上发出硬件栅栏指令，保证该调用之前的所有内存读/写在时序上“先于”该调用之后的所有内存读/写完成
  // 这样可阻止编译器与 CPU 对内存访问的重排序，确保跨核、跨设备共享数据时的可见性与顺序性
  __sync_synchronize();

  // the device increments disk.used->idx when it
  // adds an entry to the used ring.
  // 设备每向 “used” 环追加一个完成条目，就会把共享内存里的 disk.used->idx 递增一次
  // 驱动维护一个本地影子计数 disk.used_idx，表示自己已经处理到哪一项了
  // 驱动通过比较 disk.used_idx 和 disk.used->idx 来判断是否有新的完成条目需要处理
  // 注意: 循环会把当前所有可用的完成条目一次性“抽干”（drain），而不是只处理一个，避免漏处理或依赖下一次中断
  // 其周围配合内存屏障与持有的 disk.vdisk_lock，确保对 used->idx 与环条目的读取顺序正确、与设备的写入互相可见
  while(disk.used_idx != disk.used->idx){ // 有新的已完成条目
    __sync_synchronize();
    // used_idx % NUM 处理环形缓冲的回绕 
    // id 是该完成请求对应的描述符链头索引（提交请求时由驱动填入），用来在驱动侧找到对应的请求上下文
    int id = disk.used->ring[disk.used_idx % NUM].id; // 读取已完成描述符链的起始索引

    if(disk.info[id].status != 0) // 状态字节为 0 表示成功（VIRTIO_BLK_S_OK），非 0 表示设备报告 I/O 错误或不支持
      // 这是教学用内核的简化做法
      panic("virtio_disk_intr status"); // 如果状态不为 0，表示设备报告了错误，内核奔溃

    struct buf *b = disk.info[id].b; // 获取与该请求关联的缓冲区指针
    // buf提交请求时会将 b->disk 置为 1，等待方以此为条件进入睡眠。
    b->disk = 0;   // disk is done with buf // 标记缓冲区的 disk 字段为 0，表示该缓冲区的磁盘操作已完成
    wakeup(b); // 唤醒等待该缓冲区的进程，通知其 I/O 操作已完成

    disk.used_idx += 1; // 更新已处理的完成条目索引
  }

  release(&disk.vdisk_lock); // 释放磁盘锁
}
