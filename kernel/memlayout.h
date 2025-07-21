// Physical memory layout

// qemu -machine virt is set up like this,
// based on qemu's hw/riscv/virt.c:
// 这段注释描述了在 QEMU 模拟器下，RISC-V 虚拟机（`-machine virt`）的物理内存布局
// 它基于 QEMU 源码中的硬件配置 hw/riscv/virt.c，详细列出了各个重要硬件组件在物理地址空间中的分布：
// 00001000 -- boot ROM, provided by qemu 启动 ROM（boot ROM），由 QEMU 提供，系统上电后首先执行这里的代码
// 02000000 -- CLINT （Core Local Interruptor），用于处理定时器和软件中断
// 0C000000 -- PLIC (Platform-Level Interrupt Controller），负责外部中断的管理和分发。
// 10000000 -- uart0 串口设备，常用于内核和外部世界的通信（如调试输出）
// 10001000 -- virtio disk 虚拟磁盘设备，供操作系统读写持久化数据
// 80000000 -- boot ROM jumps here in machine mode 启动 ROM 在机器模式下跳转到这里
//             -kernel loads the kernel here -kernel 选项也会把内核加载到这个地址
// unused RAM after 80000000. 此地址之后的空间为未使用的 RAM，供内核和用户程序分配和使用


// the kernel uses physical memory thus: 这段注释描述了 xv6 内核在物理内存中的使用布局：
// 80000000 -- entry.S, then kernel text and data 内核的启动代码（如 entry.S）、内核的代码段（text）和数据段（data）都从这里开始加载
// end -- start of kernel page allocation area 内核代码和数据段的结束位置，也是内核页分配区（用于动态分配内存页）的起始地址
// PHYSTOP -- end RAM used by the kernel 内核可用物理内存的结束地址，即内核能管理和使用的 RAM 的上限

// qemu puts UART registers here in physical memory.
#define UART0 0x10000000L // UART 寄存器映射从物理地址 0x10000000L 开始 
#define UART0_IRQ 10 // UART0 中断号为 10 

// virtio mmio interface 
#define VIRTIO0 0x10001000 // virtio disk registers start at 0x10001000
#define VIRTIO0_IRQ 1 // virtio disk interrupt number is 1

// qemu puts platform-level interrupt controller (PLIC) here.
// 定义了 RISC-V 平台下 PLIC（Platform-Level Interrupt Controller，平台级中断控制器）相关的物理地址常量
#define PLIC 0x0c000000L // PLIC 寄存器映射从物理地址 0x0c000000L 开始
#define PLIC_PRIORITY (PLIC + 0x0) // PLIC 的优先级寄存器起始地址，用于设置各个中断源的优先级
#define PLIC_PENDING (PLIC + 0x1000) // PLIC 的挂起寄存器起始地址，用于指示哪些中断源有待处理
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100) // PLIC 的使能寄存器起始地址，`hart` 是处理器核心的编号，用于指定哪个核心的中断使能
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000) // PLIC 的核心优先级寄存器起始地址，`hart` 是处理器核心的编号，用于指定哪个核心的优先级
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000) // PLIC 的核心声明寄存器起始地址，`hart` 是处理器核心的编号，用于指定哪个核心正在处理哪个中断

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x80000000 to PHYSTOP.
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024) // KERNBASE + 128 MiB of RAM

// map the trampoline page to the highest address,
// in both user and kernel space.
/**
 * @brief 宏定义: 指定内存中 trampoline 代码的物理或虚拟地址
 * `MAXVA` 表示系统支持的最大虚拟地址，而 `PGSIZE` 是一页的大小（通常为 4KB）
 * 将 `MAXVA` 减去 `PGSIZE`，就得到了最后一页的起始地址
 * 
 * trampoline 代码通常用于内核和用户态之间的切换（如上下文切换或中断返回），需要放在一个固定且特殊的位置
 * 通过将 trampoline 放在虚拟地址空间的最高一页，可以保证它不会与普通的内核或用户空间内存冲突，同时便于硬件和操作系统进行特殊处理
 * 
 * 这种设计在操作系统内核开发中非常常见，有助于提升安全性和简化内存管理
 * 
 */
#define TRAMPOLINE (MAXVA - PGSIZE) 

// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.

/** 
 * @brief 宏定义: 用于计算第 p 个内核栈（kernel stack）在虚拟内存中的起始地址
 * @param p 内核栈的索引，从 0 开始 
 * 
 * 具体来说，TRAMPOLINE 是虚拟地址空间的最高一页，通常用于存放 trampoline 代码
 * 每个内核栈的大小为 PGSIZE（一页，通常为 4KB），并且每个内核栈之间还会插入一页无效的“保护页”（guard page），防止栈溢出时破坏相邻内存区域
 * ((p)+1)*2*PGSIZE 计算出第 `p` 个内核栈顶部距离 trampoline 的偏移量：
 *    每个内核栈和保护页共占用 2 页空间
 *    (p)+1 表示第 p 个栈在布局中的位置
 * 最终，KSTACK(p) 得到的是第 p 个内核栈的起始虚拟地址
 * 
 */
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   TRAMPOLINE (the same page as in the kernel)

/**
 * @brief 宏定义:用于指定 trapframe（陷入帧）在虚拟内存中的地址
 * TRAMPOLINE 通常被定义为虚拟地址空间的最高一页，用于存放内核与用户态切换时的 trampoline 代码
 * PGSIZE 表示一页的大小（通常为 4KB）
 * 
 * 将 TRAMPOLINE 减去 PGSIZE，就得到了倒数第二页的起始地址，这一页专门用于存放 trapframe 结构
 * trapframe 结构用于保存进程在发生中断、异常或系统调用时的寄存器状态，方便内核在处理完毕后能正确恢复进程的执行
 * 
 * 将 trapframe 放在 trampoline 之下的固定位置，有助于内核快速定位和管理进程的上下文信息，同时避免与其他内存区域冲突
 * 
 */
#define TRAPFRAME (TRAMPOLINE - PGSIZE) // 陷入帧
