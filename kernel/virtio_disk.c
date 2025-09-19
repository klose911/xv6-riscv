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
static int
alloc_desc()
{
  for(int i = 0; i < NUM; i++){
    if(disk.free[i]){
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void
free_desc(int i)
{
  if(i >= NUM)
    panic("free_desc 1");
  if(disk.free[i])
    panic("free_desc 2");
  disk.desc[i].addr = 0;
  disk.desc[i].len = 0;
  disk.desc[i].flags = 0;
  disk.desc[i].next = 0;
  disk.free[i] = 1;
  wakeup(&disk.free[0]);
}

// free a chain of descriptors.
static void
free_chain(int i)
{
  while(1){
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if(flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int
alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

void
virtio_disk_rw(struct buf *b, int write)
{
  uint64 sector = b->blockno * (BSIZE / 512);

  acquire(&disk.vdisk_lock);

  // the spec's Section 5.2 says that legacy block operations use
  // three descriptors: one for type/reserved/sector, one for the
  // data, one for a 1-byte status result.

  // allocate the three descriptors.
  int idx[3];
  while(1){
    if(alloc3_desc(idx) == 0) {
      break;
    }
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  // format the three descriptors.
  // qemu's virtio-blk.c reads them.

  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

  if(write)
    buf0->type = VIRTIO_BLK_T_OUT; // write the disk
  else
    buf0->type = VIRTIO_BLK_T_IN; // read the disk
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = (uint64) buf0;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = (uint64) b->data;
  disk.desc[idx[1]].len = BSIZE;
  if(write)
    disk.desc[idx[1]].flags = 0; // device reads b->data
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0xff; // device writes 0 on success
  disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
  disk.desc[idx[2]].next = 0;

  // record struct buf for virtio_disk_intr().
  b->disk = 1;
  disk.info[idx[0]].b = b;

  // tell the device the first index in our chain of descriptors.
  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  __sync_synchronize();

  // tell the device another avail ring entry is available.
  disk.avail->idx += 1; // not % NUM ...

  __sync_synchronize();

  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number

  // Wait for virtio_disk_intr() to say request has finished.
  while(b->disk == 1) {
    sleep(b, &disk.vdisk_lock);
  }

  disk.info[idx[0]].b = 0;
  free_chain(idx[0]);

  release(&disk.vdisk_lock);
}

void
virtio_disk_intr()
{
  acquire(&disk.vdisk_lock);

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // the device increments disk.used->idx when it
  // adds an entry to the used ring.

  while(disk.used_idx != disk.used->idx){
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;

    if(disk.info[id].status != 0)
      panic("virtio_disk_intr status");

    struct buf *b = disk.info[id].b;
    b->disk = 0;   // disk is done with buf
    wakeup(b);

    disk.used_idx += 1;
  }

  release(&disk.vdisk_lock);
}
