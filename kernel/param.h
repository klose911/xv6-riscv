/**
 * @file param.h
 * @brief This file defines various system parameters and limits used throughout the kernel.
 * 
 */
#define NPROC        64  // maximum number of processes 进程总数
#define NCPU          8  // maximum number of CPUs CPU总数
#define NOFILE       16  // open files per process 每个进程允许打开的文件数
#define NFILE       100  // open files per system 系统允许打开的文件数
#define NINODE       50  // maximum number of active i-nodes 活动i节点的最大数量
#define NDEV         10  // maximum major device number 设备号的最大数量
#define ROOTDEV       1  // device number of file system root disk 文件系统根设备号
#define MAXARG       32  // max exec arguments 每个进程允许的最大参数个数
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes  每个文件系统操作写入磁盘的最大块数
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log 日志中最大数据块数
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache 磁盘块缓存的大小
#define FSSIZE       2000  // size of file system in blocks 文件系统的大小（以块为单位）
#define MAXPATH      128   // maximum file path name 长度 文件路径名的最大长度
#define USERSTACK    1     // user stack pages 用户栈页数 

