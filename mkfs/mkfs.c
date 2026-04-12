#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "kernel/param.h"

#ifndef static_assert
// C11 标准引入了 static_assert 关键字，用于在编译时进行断言检查
// 这里定义了一个宏来模拟 static_assert 的功能，确保在编译时检查某些条件是否成立
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#define NINODES 200 // Number of inodes. 需要根据实际需求调整，过多会浪费空间，过少会限制文件数量

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]
// 引导块 | 超级块 | 日志块 | inode 块 | 位图块 | 数据块

// 位图块的数量：FSSIZE 是文件系统的总块数，BPB 是每个块中可以存储的位数（即每个块可以管理的块数量）
int nbitmap = FSSIZE/BPB + 1; 
// inode 块的数量：NINODES 是 inode 的总数量，IPB 是每个块中可以存储的 inode 数量
int ninodeblocks = NINODES / IPB + 1;
// 日志块的数量
int nlog = LOGSIZE;
// 元数据块的数量，包括引导块、超级块、日志块、inode 块和位图块
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
// 数据块的数量
int nblocks;  // Number of data blocks

int fsfd; // 文件系统镜像的文件描述符
struct superblock sb; // 超级块结构体实例，存储文件系统的元数据信息，如大小、块数量、inode 数量等
char zeroes[BSIZE]; // 用于初始化磁盘块的零填充缓冲区，大小为一个块（BSIZE），在写入新块时使用
uint freeinode = 1; // 下一个可用的 inode 编号，初始值为 1，因为 inode 编号 0 通常保留不使用
uint freeblock; // 下一个可用的数据块编号，初始值将在 fsinit 中根据 nmeta 计算得出，指向第一个可分配的数据块


/**
 * @brief 分配一个空闲的磁盘块，并将其内容清零
 * 
 * @param dev 设备号 
 * 
 */
void balloc(int);
/**
 * @brief 将数据写入指定的磁盘块
 * 
 * @param sec 块号
 * @param buf 数据缓冲区，大小为一个块（BSIZE）
 * 
 */
void wsect(uint, void*);

/**
 * @brief 将一个 dinode 数据写入指定的 inode 编号对应的磁盘位置
 * 
 * @param inum inode 编号
 * @param ip 指向要写入的 dinode 数据的指针
 */
void winode(uint, struct dinode*);

/**
 * @brief 从磁盘读取指定 inode 的数据到 dinode 结构体实例中
 * 
 * @param inum inode 编号
 * @param ip dinode 结构体实例指针，用于存储读取到的 inode 数据
 */
void rinode(uint inum, struct dinode *ip);

/**
 * @brief 从磁盘读取指定块的数据到缓冲区
 * 
 * @param sec 块号
 * @param buf 数据缓冲区，大小为一个块（BSIZE）
 */
void rsect(uint sec, void *buf);

/**
 * @brief 分配一个新的 inode
 * 
 * @param type inode 类型
 * @return uint 分配的 inode 编号
 */
uint ialloc(ushort type);

/**
 * @brief 将数据追加到指定 inode 对应的文件中，更新 inode 的大小和数据块地址
 * 
 * @param inum inode 编号
 * @param p 数据缓冲区指针，指向要追加的数据
 * @param n 追加的数据字节数
 */
void iappend(uint inum, void *p, int n);

/**
 * @brief 输出错误信息并退出程序
 * 
 * @param s 错误信息字符串
 * 
 */
void die(const char *s);

// convert to riscv byte order
/**
 * @brief 将一个 ushort 类型的数转换为 RISC-V 的字节序（小端序）
 * 
 * @param x 要转换的 ushort 数值
 * @return ushort 转换后的 ushort 数值，按照 RISC-V 的字节序存储
 */
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y; // 将 ushort 类型的数值视为一个字节数组，方便进行字节级别的操作
  a[0] = x; // 将输入的 ushort 数值的低字节存储在 a[0] 中
  a[1] = x >> 8; // 将输入的 ushort 数值的高字节存储在 a[1] 中，通过右移 8 位获取高字节
  return y; // 返回转换后的 ushort 数值，按照 RISC-V 的字节序存储
}

/**
 * @brief 将一个 uint 类型的数转换为 RISC-V 的字节序（小端序）
 * 
 * @param x 要转换的 uint 数值
 * @return uint 转换后的 uint 数值，按照 RISC-V 的字节序存储
 */
uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y; // 将 uint 类型的数值视为一个字节数组，方便进行字节级别的操作
  a[0] = x; // 将输入的 uint 数值的最低字节存储在 a[0] 中
  a[1] = x >> 8; // 将输入的 uint 数值的次低字节存储在 a[1] 中，通过右移 8 位获取
  a[2] = x >> 16; // 将输入的 uint 数值的次高字节存储在 a[2] 中，通过右移 16 位获取
  a[3] = x >> 24; // 将输入的 uint 数值的最高字节存储在 a[3] 中，通过右移 24 位获取
  return y; // 返回转换后的 uint 数值，按照 RISC-V 的字节序存储
}

/**
 * @brief 程序入口，创建一个新的文件系统镜像，并将指定的文件写入该镜像中
 * 
 * @param argc 参数个数
 * @param argv 参数数组
 * @return int 返回值，0 表示成功，非0 表示失败
 */
int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;

  // 确保 int 类型的大小为 4 字节，这是文件系统设计的一个重要假设，影响到数据结构的布局和磁盘上的存储格式
  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!"); 

  // 检查命令行参数，确保用户提供了至少一个参数（文件系统镜像的名称）
  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  // 确保块大小（BSIZE）是 dinode 和 dirent 结构体大小的整数倍，这样可以保证在磁盘上正确对齐和存储这些结构体
  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  // 创建或打开文件系统镜像文件，准备写入数据
  // O_RDWR: 以读写模式打开文件
  // O_CREAT: 如果文件不存在则创建
  // O_TRUNC: 如果文件已存在则截断为0长度
  // 0666: 文件权限，表示所有用户都具有读写权限
  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0) // 如果打开或创建文件失败，输出错误信息并退出
    die(argv[1]);

  // 1 fs block = 1 disk sector 
  nmeta = 2 + nlog + ninodeblocks + nbitmap; // 引导块 + 超级块 + 日志块 + inode 块 + 位图块
  nblocks = FSSIZE - nmeta; // 数据块的数量 = 文件系统总块数 - 元数据块数量

  sb.magic = FSMAGIC; // 文件系统魔数，用于标识文件系统类型和版本
  sb.size = xint(FSSIZE); // 文件系统的总大小，以块为单位，转换为 RISC-V 的字节序
  sb.nblocks = xint(nblocks); // 数据块的数量，转换为 RISC-V 的字节序
  sb.ninodes = xint(NINODES); // inode 的数量，转换为 RISC-V 的字节序
  sb.nlog = xint(nlog); // 日志块的数量，转换为 RISC-V 的字节序
  sb.logstart = xint(2); // 日志块的起始位置，紧跟在引导块和超级块之后  
  sb.inodestart = xint(2+nlog); // inode 块的起始位置，紧跟在日志块之后
  sb.bmapstart = xint(2+nlog+ninodeblocks); // 位图块的起始位置，紧跟在 inode 块之后

  // 输出文件系统的元数据信息，包括元数据块数量、日志块数量、inode 块数量、位图块数量、数据块数量和文件系统总大小
  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE); 

  // 计算第一个可分配的数据块编号，等于元数据块数量，因为前 nmeta 块已经被占用了
  freeblock = nmeta;     // the first free block that we can allocate 

  // 将整个文件系统镜像初始化为零，确保所有块都被清空，避免残留数据对文件系统的正确性造成影响
  for(i = 0; i < FSSIZE; i++) 
    wsect(i, zeroes); 

  memset(buf, 0, sizeof(buf)); // 将缓冲区清零，准备写入超级块数据
  memmove(buf, &sb, sizeof(sb)); // 将超级块数据复制到缓冲区中，准备写入磁盘
  wsect(1, buf); // 将超级块写入磁盘的第 1 块（第 0 块通常保留给引导程序）

  rootino = ialloc(T_DIR); // 分配一个新的 inode 用于根目录，类型为目录（T_DIR）
  assert(rootino == ROOTINO); // 确保分配的根目录 inode 编号为 ROOTINO（通常为 1），这是文件系统设计的一个约定

  bzero(&de, sizeof(de)); // 将目录项结构体实例清零，准备初始化根目录的 "." 和 ".." 目录项
  de.inum = xshort(rootino); // 设置目录项的 inode 编号为根目录的 inode 编号，转换为 RISC-V 的字节序
  strcpy(de.name, "."); // 设置目录项的名称为 "."，表示当前目录
  iappend(rootino, &de, sizeof(de)); // 将 "." 目录项追加到根目录的 inode 中，更新根目录的大小和数据块地址

  bzero(&de, sizeof(de)); // 将目录项结构体实例清零，准备初始化根目录的 ".." 目录项
  de.inum = xshort(rootino); // 设置目录项的 inode 编号为根目录的 inode 编号，转换为 RISC-V 的字节序
  strcpy(de.name, ".."); // 设置目录项的名称为 ".."，表示父目录（对于根目录来说，父目录就是它自己）
  iappend(rootino, &de, sizeof(de)); // 将 ".." 目录项追加到根目录的 inode 中，更新根目录的大小和数据块地址

  // 循环处理命令行参数中指定的文件，从第 2 个参数开始，因为第 0 个是程序名称，第 1 个是文件系统镜像的名称
  for(i = 2; i < argc; i++){ 
    // get rid of "user/"
    char *shortname; // 定义一个指针，用于存储文件的短名称，去掉路径前缀 "user/"，只保留文件名部分
    if(strncmp(argv[i], "user/", 5) == 0) // 如果文件路径以 "user/" 开头，说明这是一个用户目录下的文件，需要去掉 "user/" 前缀
      shortname = argv[i] + 5;
    else
      shortname = argv[i];
    
    assert(index(shortname, '/') == 0); // 确保短名称中不包含路径分隔符 '/'，即文件名不能包含目录路径，这样可以简化文件系统的设计和实现

    if((fd = open(argv[i], 0)) < 0) // 打开指定的文件，如果失败则输出错误信息并退出程序
      die(argv[i]);

    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    // 如果文件名以 '_' 开头，去掉这个前缀
    // 因为在构建操作系统时，这些二进制文件被命名为 _rm、_cat 等，以避免构建操作系统时尝试直接执行它们，而不是系统二进制文件 rm 和 cat
    if(shortname[0] == '_')
      shortname += 1;

    // 确保短名称的长度不超过 DIRSIZ（通常为 14 字节），这是文件系统设计的一个限制，超过这个长度的文件名将无法正确存储在目录项中
    assert(strlen(shortname) <= DIRSIZ); 
    
    inum = ialloc(T_FILE); // 分配一个新的 inode 用于存储该文件，类型为普通文件（T_FILE）
     // 初始化目录项结构体实例，设置 inode 编号和文件名，并将其追加到根目录的 inode 中，更新根目录的大小和数据块地址

    bzero(&de, sizeof(de)); // 将目录项结构体实例清零，准备初始化该文件的目录项
    de.inum = xshort(inum); // 设置目录项的 inode 编号为新分配的 inode 编号，转换为 RISC-V 的字节序
    strncpy(de.name, shortname, DIRSIZ); // 将短名称复制到目录项的 name 字段中，确保不超过 DIRSIZ 的长度限制
    iappend(rootino, &de, sizeof(de)); // 将该文件的目录项追加到根目录的 inode 中，更新根目录的大小和数据块地址

    while((cc = read(fd, buf, sizeof(buf))) > 0) // 循环读取文件内容到缓冲区，直到文件末尾
      iappend(inum, buf, cc); // 将读取到的文件内容追加到该文件的 inode 中，更新该文件的大小和数据块地址

    close(fd); // 关闭文件描述符，完成该文件的处理
  }

  // fix size of root inode dir
  // 读取根目录 inode 的数据到 dinode 结构体实例中，获取当前根目录的大小
  rinode(rootino, &din); // 获取根目录 inode 的数据到 dinode 结构体实例中，准备修正根目录的大小
  off = xint(din.size); // 获取根目录当前的大小，转换为主机字节序
   // 将根目录的大小调整为一个块的整数倍，确保根目录占用的块数量正确，避免文件系统在访问根目录时出现问题
  off = ((off/BSIZE) + 1) * BSIZE; // 将根目录的大小调整为一个块的整数倍，确保根目录占用的块数量正确，避免文件系统在访问根目录时出现问题
  din.size = xint(off); // 将调整后的大小转换为 RISC-V 的字节序，更新 dinode 结构体实例中的大小字段
  winode(rootino, &din); // 将更新后的根目录 inode 数据写回磁盘，确保根目录的大小被正确修正并保存到文件系统中

   // 分配剩余的空闲数据块，确保文件系统中的所有数据块都被正确标记为已分配或空闲，避免在后续使用中出现未定义行为
   // 这里分配了 freeblock 之后的所有块，因为前面已经分配了 nmeta 块用于元数据，剩余的块都是可用的
   // 通过调用 balloc 函数，将这些块标记为已分配，确保文件系统正确管理这些块的使用状态
   // 这也是文件系统构建过程中一个重要的步骤，确保文件系统能够正确识别和管理可用的数据块

  balloc(freeblock);

  exit(0);
}

void
wsect(uint sec, void *buf)
{
  // 将文件指针移动到指定的块位置
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE) // 如果移动文件指针失败
    die("lseek"); // 输出错误信息并退出程序
   // 将缓冲区的数据写入磁盘块
  if(write(fsfd, buf, BSIZE) != BSIZE) // 如果写入的字节数不等于块大小，说明写入失败
    die("write"); 
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE]; // 定义一个缓冲区，用于存储从磁盘读取的块数据，大小为一个块（BSIZE）
  uint bn; // 定义一个变量，用于存储 inode 所在的块号
  struct dinode *dip; // 定义一个指针，指向缓冲区中的 dinode 结构体实例，用于操作 dinode 数据

  // IBLOCK 是一个宏，根据 inode 编号和超级块信息计算出对应的块号
  bn = IBLOCK(inum, sb); // 计算包含 inode inum 的磁盘块号
  rsect(bn, buf); // 从磁盘读取包含 inode inum 的块数据到缓冲区
  dip = ((struct dinode*)buf) + (inum % IPB); // 计算出 inode inum 在块中的位置，得到指向该 inode 的指针
  *dip = *ip; // 将传入的 dinode 数据复制到缓冲区中的对应位置，更新该 inode 的数据
  wsect(bn, buf); // 将更新后的块数据写回磁盘，确保 inode 的修改被保存到文件系统中
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE]; // 定义一个缓冲区，用于存储从磁盘读取的块数据，大小为一个块（BSIZE）
  uint bn; // 定义一个变量，用于存储 inode 所在的块号
  struct dinode *dip; // 定义一个指针，指向缓冲区中的 dinode 结构体实例，用于操作 dinode 数据

  bn = IBLOCK(inum, sb); // 计算包含 inode inum 的磁盘块号
  rsect(bn, buf); // 从磁盘读取包含 inode inum 的块数据到缓冲区
  dip = ((struct dinode*)buf) + (inum % IPB); // 计算出 inode inum 在块中的位置，得到指向该 inode 的指针
  *ip = *dip; // 将缓冲区中的 dinode 数据复制到传入的 dinode 结构体实例中，返回该 inode 的数据给调用者
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE) // 将文件指针移动到指定的块位置
    die("lseek");
  if(read(fsfd, buf, BSIZE) != BSIZE) // 从磁盘读取数据到缓冲区
    die("read");
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;  // 分配一个新的 inode 编号
  struct dinode din; // 定义一个 dinode 结构体实例，用于初始化新分配的 inode

  bzero(&din, sizeof(din)); // 将 dinode 结构体实例清零，确保所有字段都被初始化为默认值
  din.type = xshort(type); // 设置 inode 的类型，转换为 RISC-V 的字节序
  din.nlink = xshort(1); // 设置 inode 的链接数量为 1，表示至少有一个链接指向该 inode
  din.size = xint(0); // 设置 inode 的大小为 0，表示文件为空
  winode(inum, &din); // 将初始化后的 inode 写入磁盘，确保在文件系统中正确存储该 inode 的信息
  return inum; // 返回分配的 inode 编号
}

void
balloc(int used)
{
  uchar buf[BSIZE]; // 定义一个缓冲区，用于存储位图块的数据，大小为一个块（BSIZE）
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BPB); // 确保已分配的块数量不超过一个块可以管理的块数量（BPB），否则需要多个位图块来管理
  bzero(buf, BSIZE); // 将缓冲区清零，准备设置已分配块的位图标志
  for(i = 0; i < used; i++){ // 循环设置前 used 个块的位图标志，表示这些块已经被分配
    buf[i/8] = buf[i/8] | (0x1 << (i%8)); // 计算出第 i 个块在位图中的位置，设置对应的位为 1，表示该块已被分配
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf); // 将更新后的位图块写入磁盘，确保文件系统正确记录已分配的块信息
}

// 计算两个整数的最小值，常用于文件系统中计算剩余空间、块大小等场景
#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp; // 将输入的数据指针转换为字符指针，方便进行字节级别的操作
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE]; // 定义一个缓冲区，用于存储从磁盘读取的块数据，大小为一个块（BSIZE）
  uint indirect[NINDIRECT]; // 定义一个数组，用于存储间接块的地址，支持大文件的存储，NINDIRECT 是间接块可以存储的块数量
  uint x; // 定义一个变量，用于存储当前操作的数据块地址

  rinode(inum, &din); // 从磁盘读取指定 inode 的数据到 dinode 结构体实例中，获取该 inode 的当前状态和信息
  off = xint(din.size); // 获取 inode 当前的文件大小，转换为主机字节序，表示当前文件的末尾位置，新的数据将从这个位置开始追加
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while(n > 0){ // 循环处理要追加的数据，直到所有数据都被追加完毕
    fbn = off / BSIZE; // 计算当前文件偏移量对应的块号，表示要写入数据的目标块在文件中的位置
    assert(fbn < MAXFILE); // 确保文件的块号不超过最大支持的块数量，否则无法继续追加数据
    if(fbn < NDIRECT){ // 如果块号在直接块范围内，直接使用 dinode 中的 addrs 数组存储数据块地址
      if(xint(din.addrs[fbn]) == 0){ // 如果当前块地址为 0，表示该块尚未分配，需要分配一个新的数据块
        din.addrs[fbn] = xint(freeblock++); // 分配一个新的数据块，并将其地址存储在 dinode 的 addrs 数组中
      } // 获取当前块的地址，准备写入数据
      x = xint(din.addrs[fbn]); // 获取当前块的地址，准备写入数据
    } else { // 如果块号超过直接块范围，使用间接块来存储数据块地址
      if(xint(din.addrs[NDIRECT]) == 0){ // 如果间接块地址为 0，表示尚未分配，需要分配一个新的数据块作为间接块
        din.addrs[NDIRECT] = xint(freeblock++); // 分配一个新的数据块作为间接块，并将其地址存储在 dinode 的 addrs 数组中
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect); // 从磁盘读取间接块的数据到 indirect 数组中，获取当前间接块的状态和内容
      if(indirect[fbn - NDIRECT] == 0){ // 如果间接块中的目标块地址为 0，表示尚未分配，需要分配一个新的数据块
        indirect[fbn - NDIRECT] = xint(freeblock++); // 分配一个新的数据块，并将其地址存储在间接块中
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect); // 将更新后的间接块写回磁盘，确保间接块的修改被保存
      }
      x = xint(indirect[fbn-NDIRECT]); // 获取当前块的地址，准备写入数据
    }
    n1 = min(n, (fbn + 1) * BSIZE - off); // 计算本次写入的数据量，确保不超过当前块的剩余空间
    rsect(x, buf); // 从磁盘读取当前块的数据到缓冲区，准备进行修改
    bcopy(p, buf + off - (fbn * BSIZE), n1); // 将要追加的数据复制到缓冲区的正确位置，准备写回磁盘
    wsect(x, buf); // 将修改后的块数据写回磁盘，确保追加的数据被保存到文件系统中
    n -= n1; // 更新剩余要追加的数据量
    off += n1; // 更新文件偏移量，指向下一个要写入的位置
    p += n1; // 更新数据指针，指向下一个要追加的数据位置
  }
  din.size = xint(off); // 更新 inode 的文件大小，转换为 RISC-V 的字节序，表示文件的新大小
  winode(inum, &din); // 将更新后的 inode 写入磁盘，确保文件大小的修改被保存到文件系统中
}

void
die(const char *s)
{
  perror(s); // 输出错误信息
  exit(1); // 退出程序，返回非0表示失败
}
