// Format of an ELF executable file
/**
 * @brief ELF 格式头文件
 * 
 */
#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian ELF 魔数

// File header
/**
 * @brief 描述 ELF（Executable and Linkable Format，可执行与可链接格式）文件的头部信息
 * 
 * ELF 是类 Unix 系统中常用的可执行文件、目标文件和共享库的标准格式
 * 
 * 这个结构体通常用于解析和操作 ELF 文件头部信息，是加载和执行 ELF 格式程序的基础
 * 
 */
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12]; // ELF 文件头的标识信息，包括 ELF 版本、数据编码方式等
  ushort type; // 文件类型（如可执行文件、目标文件、共享库等）
  ushort machine; // 目标体系结构类型（如 x86、RISC-V 等）
  uint version; // ELF 文件格式的版本号
  uint64 entry; // 程序入口点的“虚拟地址”，即程序开始执行的地址
  uint64 phoff; // 程序头表（Program Header Table）在文件中的偏移量
  uint64 shoff; // 节区头表（Section Header Table）在文件中的偏移量 
  uint flags; // 与处理器相关的标志位
  ushort ehsize; // ELF 文件头的大小(字节数)
  ushort phentsize; // 程序头表中每个条目的大小 
  ushort phnum; // 程序头表中的条目数量
  ushort shentsize; // 节区头表中每个条目的大小
  ushort shnum; // 节区头表中的条目数量
  ushort shstrndx; // 节区头字符串表在节区头表中的索引
};

// Program section header

/**
 * @brief ELF 文件中的“程序头表”（Program Header Table）中的每一个条目
 * 
 * 每个 proghdr 结构体实例都对应 ELF 文件中的一个段（segment），用于指导操作系统如何加载和映射程序的各个部分到内存
 * 
 * 这个结构体是操作系统加载 ELF 格式可执行文件时的重要数据结构，帮助内核正确地将文件内容映射到进程的虚拟内存空间
 */
struct proghdr {
  uint32 type; // 段的类型，指明该段的用途（如可加载段、动态链接信息等）
  uint32 flags; // 段的权限标志，比如可读、可写、可执行等
  uint64 off; // 该段在文件中的偏移量，表示从文件开头到该段数据的距离
  uint64 vaddr; // 该段在进程虚拟地址空间中的起始地址
  uint64 paddr; // 该段在物理内存中的起始地址（通常在现代操作系统中用得较少）
  uint64 filesz; // 该段在文件中的实际大小（字节数）
  uint64 memsz; // 该段在内存中占用的大小（字节数），可能大于 filesz，（存在 BSS 等未初始化数据段）
  uint64 align; // 该段在内存和文件中的对齐要求
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1 
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
