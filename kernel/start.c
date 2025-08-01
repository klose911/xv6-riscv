#include "types.h" // 自定义数据变量类型
#include "param.h" // 系统限制
#include "memlayout.h" // 内存布局相关常量
#include "riscv.h" // RISC-V 特定的寄存器操作 
#include "defs.h" // 系统调用和其他函数的声明

void main();
void timerinit();

// entry.S needs one stack per CPU.

/**
 * @brief 每个 CPU 核心都有一个独立的栈空间，大小为 4096 字节
 * 
 *  __attribute__ 是 GCC 和 Clang 等编译器支持的一个扩展，用于为变量、函数或类型指定特殊的属性
 * 通过 __attribute__，开发者可以向编译器传递额外的信息，比如控制对齐方式、优化行为、函数可见性、警告抑制等
 * 
 * 常见用法包括 __attribute__((aligned(16)))（指定对齐到 16 字节）、
 *  __attribute__((noreturn))（声明函数不会返回）
 * __attribute__((packed))（结构体紧凑排列）等
 * 具体属性需要放在双括号内，并紧跟在声明的后面或前面
 * 
 * 这种机制可以帮助开发者更精细地控制代码的行为和性能，尤其在底层开发、嵌入式或操作系统项目中非常常见
 * 
 */
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// entry.S jumps here in machine mode on stack0.
/**
 * @brief entry.S 在机器模式下跳转到这里，使用 stack0 作为栈空间 
 * 
 * 在 xv6 操作系统中，`start` 函数是系统的入口点，
 * 它负责进行系统初始化，包括设置 CPU 的特权级别、配置中断处理程序、初始化内存管理等。
 * 该函数在机器模式下执行，并使用 `stack0` 作为栈空间，
 * 以便在执行过程中进行函数调用和局部变量存储等操作
 * 
 * 在 RISC-V 架构中，机器模式（Machine Mode）是最高特权级别的执行模式，
 * 主要用于操作系统内核和硬件抽象层的代码。它允许访问所有硬件资源和特权指令，
 * 并且可以直接操作 CPU 寄存器、内存和外设等。
 * 在机器模式下，操作系统可以进行初始化、设置中断处理程序、配置内存管理单元等关键任务。
 * 在这个模式下，操作系统可以设置 CPU 的特权级别、配置中断向量表、初始化硬件设备等，
 * 并且可以通过特权指令直接访问和控制硬件资源
 * 
 */
void
start()
{
  // set M Previous Privilege mode to Supervisor, for mret.

  // 清除 mstatus 中的 MPP 位，并设置为 SPP（Supervisor Previous Privilege）
  unsigned long x = r_mstatus(); // 读取当前的 mstatus 寄存器值
  
  // 通过 ~MSTATUS_MPP_MASK 取反后，只有 MPP 字段对应的位为 0，其他位为 1。把 MPP 字段清零，而不影响其他位
  // 这一步通常是为了后续设置新的特权级别做准备，确保不会保留之前的 MPP 状态。 
  x &= ~MSTATUS_MPP_MASK; 
  x |= MSTATUS_MPP_S; // 设置 MPP 字段为 SPP（Supervisor Previous Privilege），表示上一次的特权级别是监督者模式
  w_mstatus(x); // 将修改后的值写回 mstatus 寄存器，更新 CPU 的特权级别状态 

  // set M Exception Program Counter to main, for mret. requires gcc -mcmodel=medany 
  // 确保生成的代码可以正确处理任意地址范围的函数指针（如 main 的地址），避免因地址超出默认范围而导致的跳转错误
  // 这在操作系统或裸机开发中非常重要，因为内核代码可能被加载到高地址空间
  
  // 设置 mepc（Machine Exception Program Counter）寄存器为 main 函数的地址，
  // 这样当发生异常或中断时，CPU 可以从这里继续执行
  w_mepc((uint64)main); 

  // disable paging for now.
  w_satp(0); // 暂时禁用分页机制

  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff); // 将所有的机器异常委托给监督者模式处理，0xffff 表示将所有异常都委托给监督者模式
  w_mideleg(0xffff); // 将所有的机器中断委托给监督者模式处理，0xffff 表示将所有中断都委托给监督者模式
  // 设置 SIE（Supervisor Interrupt Enable）寄存器，允许监督者模式下的设备中断
  // SIE_SEIE（Supervisor External Interrupt Enable）允许监督者模式下的外部中断，
  // SIE_STIE（Supervisor Timer Interrupt Enable）允许监督者模式 下的定时器中断，
  // SIE_SSIE（Supervisor Software Interrupt Enable）允许监督者模式下的软件中断
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE); 

  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  // 将 0x3fffffffffffffull 这个较大的地址写入 PMP 地址寄存器 0（pmpaddr0）
  // 这个值通常表示允许访问的物理地址范围的上限，几乎覆盖了整个物理地址空间 
  w_pmpaddr0(0x3fffffffffffffull); 
  // 将 pmpcfg0 寄存器配置为 0xf，这通常表示允许监督者模式访问所有物理内存
  // 0xf 是一个二进制数，表示所有的四个 PMP 配置位都被设置为 1，
  // 这意味着监督者模式可以访问所有物理内存区域
  // 在 RISC-V 架构中，PMP（Physical Memory Protection）用于配置内存访问权限，
  // 通过设置 pmpcfg0 寄存器，可以控制监督者模式对物理内存的访问权限
  // 0xf 的二进制为 1111，通常表示该 PMP 区域允许读、写、执行权限
  // 并采用 NAPOT（Naturally Aligned Power-Of-Two）地址匹配模式
  w_pmpcfg0(0xf); 

  // ask for clock interrupts.
  timerinit(); // 初始化定时器中断，设置定时器中断使能和相关配置

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid(); // 获得当前允许cpu id
  w_tp(id); // 将当前 CPU 的 hartid（硬件线程编号）写入 tp（线程指针）寄存器，
  // 这样可以在多核环境下区分和管理不同的 CPU 核心，实现高效的调度和资源分配

  // switch to supervisor mode and jump to main().
  asm volatile("mret"); // 执行 mret 指令，切换到监督者模式，并跳转到 main 函数开始执行
}

// ask each hart to generate timer interrupts.
/**
 * @brief 初始化定时器中断
 * 
 */
void
timerinit()
{
  // enable supervisor-mode timer interrupts.
  w_mie(r_mie() | MIE_STIE); // 设置机器中断使能寄存器（mie），启用监督者模式下的定时器中断
  
  // enable the sstc extension (i.e. stimecmp).
  // 设置机器环境配置寄存器（menvcfg），启用 sstc 扩展（即 stimecmp），
  // 这通常用于支持监督者模式下的定时器比较功能，以便
  // 允许监督者模式使用 stimecmp 寄存器进行定时器比较和中断处理 
  w_menvcfg(r_menvcfg() | (1L << 63)); 
  
  // allow supervisor to use stimecmp and time.
  // mcounteren 的第 1 位对应 time 计数器（mtime）
  // 设置为 1 允许较低特权级（如监督者模式）访问时间计数器
  w_mcounteren(r_mcounteren() | 2);
  
  // ask for the very first timer interrupt.
  // r_time() 读取当前的时间计数器值（通常以时钟周期为单位）
  // 然后，将其加上 1,000,000，表示希望定时器在当前时间基础上再经过 1,000,000 个时钟周期后触发
  // 最后，w_stimecmp(...) 将这个新值写入 stimecmp 寄存器。
  w_stimecmp(r_time() + 1000000);
}
