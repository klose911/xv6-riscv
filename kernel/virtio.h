//
// virtio device definitions.
// for both the mmio interface, and virtio descriptors.
// only tested with qemu.
//
// the virtio spec:
// https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf
//

// virtio mmio control registers, mapped starting at 0x10001000.
// from qemu virtio_mmio.h
// 一组与 Virtio MMIO（内存映射 I/O）设备相关的寄存器偏移量常量
// 每个宏都表示设备寄存器在 MMIO 区域 (0x10001000) 中的偏移地址，用于驱动程序与 Virtio 设备进行交互

// 设备类型、厂商和协议版本的标识
#define VIRTIO_MMIO_MAGIC_VALUE		0x000 // 0x74726976
#define VIRTIO_MMIO_VERSION		0x004 // version; should be 2
#define VIRTIO_MMIO_DEVICE_ID		0x008 // device type; 1 is net, 2 is disk
#define VIRTIO_MMIO_VENDOR_ID		0x00c // 0x554d4551

// 设备和驱动之间协商支持的功能特性 
#define VIRTIO_MMIO_DEVICE_FEATURES	0x010
#define VIRTIO_MMIO_DRIVER_FEATURES	0x020

// 配置和管理 Virtio 的队列机制，包括选择队列、设置队列大小、通知设备队列有新数据等
#define VIRTIO_MMIO_QUEUE_SEL		0x030 // select queue, write-only
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034 // max size of current queue, read-only
#define VIRTIO_MMIO_QUEUE_NUM		0x038 // size of current queue, write-only
#define VIRTIO_MMIO_QUEUE_READY		0x044 // ready bit
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050 // write-only

// 中断状态和中断确认寄存器
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060 // read-only
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064 // write-only

// 设备状态寄存器，用于表示设备当前的状态
#define VIRTIO_MMIO_STATUS		0x070 // read/write

// 设置队列描述符表、可用环和已用环的物理地址，实现主机和设备之间的数据交换
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080 // physical address for descriptor table, write-only
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084
#define VIRTIO_MMIO_DRIVER_DESC_LOW	0x090 // physical address for available ring, write-only
#define VIRTIO_MMIO_DRIVER_DESC_HIGH	0x094
#define VIRTIO_MMIO_DEVICE_DESC_LOW	0x0a0 // physical address for used ring, write-only
#define VIRTIO_MMIO_DEVICE_DESC_HIGH	0x0a4

// status register bits, from qemu virtio_config.h

// Virtio 设备配置状态相关的常量，每个宏代表设备初始化和驱动协商过程中的一个阶段
#define VIRTIO_CONFIG_S_ACKNOWLEDGE	1 // 驱动程序已识别并响应 Virtio 设备，表示开始初始化流程
#define VIRTIO_CONFIG_S_DRIVER		2 // 驱动程序已准备好与设备通信，表示驱动程序已经加载并准备好进行配置
#define VIRTIO_CONFIG_S_DRIVER_OK	4 // 驱动程序已完成所有必要的初始化，设备可以开始正常工作
#define VIRTIO_CONFIG_S_FEATURES_OK	8 // 驱动程序已成功协商并接受设备的功能特性 

// device feature bits

// 一组 Virtio 块设备和环队列相关的功能特性标志，每个宏代表设备或驱动支持的某项能力：
#define VIRTIO_BLK_F_RO              5	/* Disk is read-only */ // 只读磁盘
#define VIRTIO_BLK_F_SCSI            7	/* Supports scsi command passthru */ // 支持 SCSI 命令直通
#define VIRTIO_BLK_F_CONFIG_WCE     11	/* Writeback mode available in config */ // 配置中可用的写回模式
#define VIRTIO_BLK_F_MQ             12	/* support more than one vq */ // 支持多个虚拟队列
#define VIRTIO_F_ANY_LAYOUT         27 // 允许描述符环（descriptor ring）采用任意布局
#define VIRTIO_RING_F_INDIRECT_DESC 28 // 允许一个描述符指向一组其他描述符，适合复杂或大批量数据传输
#define VIRTIO_RING_F_EVENT_IDX     29 // 使用事件索引机制来优化设备和驱动之间的通知，减少不必要的中断

// this many virtio descriptors.
// must be a power of two.
// 必须是2的幂 
#define NUM 8 // 最多8个 virtio 描述符 

// a single descriptor, from the spec.

/**
 * @brief Virtio 环队列（virtqueue）对应的描述符结构体，用于描述一次数据传输的缓冲区信息
 * 
 * 每个 Virtio I/O 操作都由一个或多个描述符组成，驱动和设备通过这些描述符进行数据交换
 */
struct virtq_desc {
  uint64 addr; // 缓冲区的物理地址，指向实际的数据存储位置
  uint32 len; // 缓冲区的长度，表示数据的大小（字节数）
  uint16 flags; // 描述符的标志位，指示该描述符的属性（如是否有下一个描述符，读写权限等）
  uint16 next; // 如果 flags 包含 VRING_DESC_F_NEXT 标志，则 next 指向下一个描述符的索引，实现描述符链
};

// 如果设置了该标志，设备或驱动需要继续处理 next 字段指向的下一个描述符，实现多缓冲区链式传输 
#define VRING_DESC_F_NEXT  1 // chained with another descriptor
// 该缓冲区是设备写入（输出）用的
#define VRING_DESC_F_WRITE 2 // device writes (vs read)

// the (entire) avail ring, from the spec.

/**
 * @brief Virtio 环队列（virtqueue）中的可用环（avail ring）结构体，用于驱动和设备之间传递哪些描述符可以被设备处理
 * 
 * 
 */
struct virtq_avail {
  uint16 flags; // always zero 总是 0 
  // 索引值，表示驱动下次要写入的位置。每当驱动将新的描述符链加入队列时，都会递增该索引
  uint16 idx;   // driver will write ring[idx] next 
  // 可用描述符编号数组，NUM 是队列长度
  // 每个元素存储一个描述符链的头部编号，驱动通过它告诉设备哪些描述符已经准备好可以处理
  uint16 ring[NUM]; // descriptor numbers of chain heads
  uint16 unused;
};

// one entry in the "used" ring, with which the
// device tells the driver about completed requests.

/**
 * @brief Virtio 环队列（virtqueue）中的已用环元素结构体，用于表示设备已经处理完成的描述符链的信息
 * 
 */
struct virtq_used_elem {
  // 已完成的描述符链的起始索引
  // 驱动可以通过这个索引找到对应的请求，进行后续处理（如释放缓冲区或通知上层）
  uint32 id;   // index of start of completed descriptor chain
  uint32 len; // 备实际处理的数据长度（字节数），用于告知驱动本次 I/O 操作的结果数据量
};

/**
 * @brief Virtio 环队列（virtqueue）中的已用环（used ring）结构体，用于设备向驱动报告已完成的数据传输请求
 * 
 */
struct virtq_used {
  uint16 flags; // always zero 总是 0 
  // 索引值，表示设备已处理的请求数量
  // 每当设备完成一个描述符链的处理，就会在 ring 数组中添加一个条目，并递增该索引
  uint16 idx;   // device increments when it adds a ring[] entry
  struct virtq_used_elem ring[NUM]; // 已完成请求的数组，NUM 是队列长度
};

// these are specific to virtio block devices, e.g. disks,
// described in Section 5.2 of the spec.

// virtio 块设备请求类型 0: 读，1: 写
#define VIRTIO_BLK_T_IN  0 // read the disk
#define VIRTIO_BLK_T_OUT 1 // write the disk

// the format of the first descriptor in a disk request.
// to be followed by two more descriptors containing
// the block, and a one-byte status.

// 这是磁盘请求中第一个描述符的格式，后面还会有两个描述符，分别用于传递数据块内容和一个字节的状态信息
/**
 * @brief Virtio 块设备请求结构体，用于描述一次磁盘 I/O 操作的请求格式
 * 
 * 驱动在发起磁盘请求时，会先用该结构体描述操作类型和目标扇区
 * 然后再通过额外的描述符传递数据和状态，实现完整的块设备 I/O 流程
 * 
 */
struct virtio_blk_req {
  uint32 type; // VIRTIO_BLK_T_IN or ..._OUT 请求类型，读或写
  uint32 reserved; // 保留字段，通常设置为 0
  uint64 sector; // 磁盘扇区号，指定本次操作针对磁盘上的哪个扇区进行读写
};
