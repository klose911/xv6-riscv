#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0; // 保证在第一个cpu核上启用大部分初始代码

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit(); // 初始化串口终端
    printfinit(); // 初始化打印
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator 初始化物理内存分页
    kvminit();       // create kernel page table 创建内核分页表
    kvminithart();   // turn on paging 开启分页
    procinit();      // process table 进程表
    trapinit();      // trap vectors 内核级中断向量
    trapinithart();  // install kernel trap vector 设置内核中断处理向量
    plicinit();      // set up interrupt controller 设置中断控制器
    plicinithart();  // ask PLIC for device interrupts 启用 PLIC 设备中断
    binit();         // buffer cache 缓冲区缓存
    iinit();         // inode table 索引节点表
    fileinit();      // file table 文件表
    virtio_disk_init(); // emulated hard disk 虚拟硬盘
    userinit();      // first user process 第一个用户进程
    __sync_synchronize(); // 设置内存屏障
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}
