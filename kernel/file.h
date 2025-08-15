/**
 * @brief 定义了文件结构体（struct file），用于表示打开的文件或设备
 * 
 * 文件结构体包含了文件的类型、引用计数、读写权限等信息
 * 
 */
struct file {
  // File type
  // 文件类型：可以是普通文件、管道、设备等
  // FD_NONE 表示未使用，FD_PIPE 表示管道，
  // FD_INODE 表示索引节点文件，FD_DEVICE 表示设备文件
  // 这些类型用于区分不同的文件或设备操作方式
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count 引用计数，用于跟踪有多少个文件描述符指向该文件
  char readable; // 是否可读
  char writable; // 是否可写
  struct pipe *pipe; // 管道结构体指针，只适用于管道类型的文件
  struct inode *ip;  // inode结构体指针，适用于索引节点文件和设备文件
  uint off;          // 文件偏移量，表示当前读写位置 适用于索引节点文件
  short major;       // 主设备号（仅对设备文件有效）
};

/**
 * @brief 定义用于设备号（device number）的分解与组合的宏
 * 
 * 例如，在一个磁盘设备中，主设备号可能表示磁盘类型
 * 而次设备号则表示磁盘上的具体分区
 * 这样可以通过组合主设备号和次设备号来唯一标识一个设备
 * 
 */

 /**
  * @brief 将设备号 dev 分解为主设备号
  * 
  * 将 dev 右移 16 位，然后与 0xFFFF 进行按位与操作
  * 得到高 16 位的主设备号
  * 
  * @param dev 设备号
  * 
  * @return 返回主设备号，用于标识设备的类型或驱动程序
  * 
  */
#define major(dev)  ((dev) >> 16 & 0xFFFF)

/**
 * @brief 将设备号 dev 分解为次设备号
 * 
 * 将 dev 与 0xFFFF 进行按位与操作，得到低 16 位的次设备号
 * 
 * @param dev 设备号
 * 
 * @return 返回次设备号，用于标识同一类型设备中的具体设备实例
 */
#define minor(dev)  ((dev) & 0xFFFF)

/**
 * @brief 将主设备号 m 和次设备号 n 组合成一个 32 位的设备号
 * 
 * 通过左移 16 位将主设备号放在高位，然后与次设备号进行按位或操作
 * 这样可以生成一个唯一的设备号，用于标识特定的设备
 * 
 * @param m 主设备号，通常表示设备类型或驱动程序
 * @param n 次设备号，通常表示同一类型设备中的具体设备实例
 * 
 * @return 返回组合后的设备号，用于唯一标识一个设备
 */
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode

/**
 * @brief inode 的结构体
 * 
 * inode（索引节点）是文件系统中用于描述文件或目录的元数据结构
 * 它包含了文件的类型、大小、权限、数据块地址等信息
 * 
 */
struct inode {
  uint dev;           // Device number 设备号，用于标识文件所在的设备
  uint inum;          // Inode number 索引节点号，用于唯一标识文件系统中的一个 inode
  int ref;            // Reference count 引用计数，用于跟踪有多少个文件描述符指向该 inode
  struct sleeplock lock; // protects everything below here 互斥锁，用以避免对 inode 的并发访问
  int valid;          // inode has been read from disk? 是否已从磁盘读取到内存中

  // 磁盘上对应的 dinode 结构体
  short type;         // copy of disk inode 
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

// map major device number to device functions.
/**
 * @brief devsw 的结构体，用于将主设备号（major device number）映射到具体的设备操作函数
 * 
 */
struct devsw {
  /**
   * 指向设备的读操作函数指针
   * 
   * 参数通常包括设备号、数据缓冲区地址和读取的字节数
   * 
   * 返回值为实际读取的字节数或错误码
   * 
   */
  int (*read)(int, uint64, int); 

  /**
   * 指向设备的写操作函数指针
   * 
   * 参数通常包括设备号、数据缓冲区地址和写入的字节数
   * 
   * 返回值为实际写入的字节数或错误码
   * 
   */
  int (*write)(int, uint64, int);
};

// devsw结构体数组，每一个元素对应一个设备
extern struct devsw devsw[];

#define CONSOLE 1 // console设备的主设备号
