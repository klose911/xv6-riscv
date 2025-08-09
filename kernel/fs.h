// On-disk file system format. 
// Both the kernel and user programs use this header file.
// 文件系统格式的头文件，内核和用户程序都使用这个头文件

#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:

// 文件系统在磁盘上的物理布局
// 磁盘被划分为多个区域，依次为：
//            启动块（boot block）、超级块（super block）、日志区（log）
//            inode 块（inode blocks）空闲位图（free bit map）和数据块（data blocks）
// 每个区域承担不同的功能，例如启动块用于系统引导，超级块保存文件系统的元数据
// 日志区用于文件系统操作的日志记录，inode 块存储文件和目录的元信息
// 空闲位图用于管理哪些数据块是空闲的，数据块则用于实际存储文件内容

// mkfs 工具会计算超级块并构建初始文件系统
// 超级块中包含了磁盘各区域的布局信息，是文件系统管理和访问磁盘数据的核心
// 通过这种分区方式，文件系统能够高效地组织和管理磁盘上的数据

/**
 * @brief superblock 是一个结构体，描述了文件系统的磁盘布局
 * 包括文件系统镜像的大小、数据块数量、inode 数量、日志块数量，以及日志块、inode 块和空闲位图块的起始位置等信息
 * 
 * 它由 mkfs 计算并用于构建初始文件系统，其中 magic 字段必须为 FSMAGIC
 * 
 */
struct superblock {
  uint magic;        // Must be FSMAGIC // 文件系统魔数，必须为 FSMAGIC
  uint size;         // Size of file system image (blocks) // 文件系统镜像的大小（以块为单位）
  uint nblocks;      // Number of data blocks // 数据块数量
  uint ninodes;      // Number of inodes // inode 数量
  uint nlog;         // Number of log blocks // 日志块数量
  uint logstart;     // Block number of first log block // 第一个日志块的块号
  uint inodestart;   // Block number of first inode block // 第一个 inode 块的块号
  uint bmapstart;    // Block number of first free map block // 第一个空闲位图块的块号
};

#define FSMAGIC 0x10203040 //文件系统魔数

#define NDIRECT 12 // 直接块数量
#define NINDIRECT (BSIZE / sizeof(uint))// 每个间接块中的块地址数量
#define MAXFILE (NDIRECT + NINDIRECT)// 每个文件的最大块数量

// On-disk inode structure
/**
 * @brief 定义了一个名为 dinode 的结构体
 * 表示磁盘上的 inode（索引节点）数据结构，是文件系统管理文件和目录元数据的核心
 * 
 */
struct dinode {
  short type;           // File type 文件类型，例如普通文件、目录或设备文件
  short major;          // Major device number (T_DEVICE only) 主设备号（仅对设备文件有效）
  short minor;          // Minor device number (T_DEVICE only) 次设备号（仅对设备文件有效）
  short nlink;          // Number of links to inode in file system 指向该 inode 的链接数量
  uint size;            // Size of file (bytes) 文件的字节大小
  // 数据块地址数组: 前 NDIRECT 项直接存储数据块地址,最后一项通常用于间接块地址，实现对大文件的支持
  uint addrs[NDIRECT+1];   // Data block addresses 
};

// Inodes per block.
/**
 * @brief 计算每个磁盘块（block）中可以存放多少个 inode 结构（struct dinode）
 * 
 * 其中，BSIZE 表示磁盘块的字节大小，sizeof(struct dinode) 表示一个磁盘 inode 结构体的字节数
 * 将块大小除以 inode 大小，就得到了每个块中可容纳的 inode 数量
 * 
 */
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
/**
 * @brief 计算包含特定 inode i 的磁盘块号
 * 
 * (i) / IPB 计算出第 i 个 inode 相对于 inode 区域起始块的偏移量
 * 再加上 sb.inodestart，就得到了该 inode 实际所在的磁盘块号
 * 
 * @param i inode 的索引号
 * @param sb 指向超级块结构的引用，包含文件系统的布局信息
 * 
 * @return 返回包含 inode i 的磁盘块号
 * 
 */
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
/**
 * @brief 计算每个磁盘块（block）可以表示多少个位（bit）
 * 
 * BSIZE 表示磁盘块的字节数。一个字节有 8 位，因此 BSIZE*8 就是一个块中总共包含的位数
 * 
 * 在文件系统的空闲位图（bitmap）管理中，每一位通常用来表示一个数据块是否空闲或已被占用
 * 
 */
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b

/**
 * @brief 计算第 b 个数据块在磁盘空闲位图（bitmap）中对应的位图块号
 * 
 * (b) / BPB 计算出第 b 个数据块相对于位图区域起始块的偏移量
 * 再加上 sb.bmapstart，就得到了该数据块实际所在的位图块号
 * 
 * @param b 数据块的索引号
 * @param sb 指向超级块结构的引用，包含文件系统的布局信息
 * 
 * @return 返回包含数据块 b 的位图块号
 * 
 */
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.

/**
 * @brief 指定目录项（directory entry）中文件名的最大长度为 14 字节
 * 
 * 在许多类 UNIX 文件系统（包括 xv6）中，目录项结构体通常会为文件名分配一个固定长度的字符数组
 * DIRSIZ 就是这个长度的上限，意味着每个文件或目录的名字最多只能有 14 个字符（不包括结尾的空字符 \0）
 * 如果文件名不足 14 字节，通常会用空字符填充
 * 
 */
#define DIRSIZ 14

/**
 * @brief 定义了一个名为 dirent 的结构体，，用于表示目录项（directory entry）
 * 
 * 在类 UNIX 文件系统（如 xv6）中，目录实际上是一个特殊的文件
 * 内部存储着一系列 dirent 结构体，每个结构体对应一个文件或子目录的信息。
 * 
 * 
 */
struct dirent {
  ushort inum; // Inode number 标识目录项对应的文件或子目录
  char name[DIRSIZ]; // File name 文件或子目录的名称，最大长度为 14 字节
};

