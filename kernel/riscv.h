/**
 * @file riscv.h
 * @brief This file contains RISC-V specific definitions and inline assembly functions
 * 
 */

 // 一个条件编译指令，用于判断当前文件是否在汇编环境下被处理
 // 通常在汇编源文件或被汇编器处理的头文件中定义
 // 如果没有定义 __ASSEMBLER__，说明当前代码是在 C/C++ 编译器环境下编译的
 // 这样可以让后续的代码（通常是 C/C++ 相关的声明或实现）只在 C/C++ 编译时生效，而不会被汇编器解析
 // 
 // 这种写法常用于同时被 C/C++ 和汇编代码包含的头文件中，用于屏蔽不适合汇编器处理的部分，保证代码的可移植性和兼容性
#ifndef __ASSEMBLER__

// which hart (core) is this?
static inline uint64
r_mhartid()
{
  uint64 x;
  asm volatile("csrr %0, mhartid" : "=r" (x) ); // 查询现在使用的是哪个cpu核心
  return x;
}

// Machine Status Register, mstatus
// cpu状态寄存器

/**
 * @brief 定义了与 RISC-V 架构下 `mstatus` 寄存器相关的几个常量，用于操作和解析机器状态寄存器中的特定位
 * 
 * 这些宏常用于操作系统内核或底层硬件控制代码中，方便对 `mstatus` 寄存器的特权级和中断使能状态进行读写和判断
 * 
 */
#define MSTATUS_MPP_MASK (3L << 11) // previous mode. 该字段占用第 11 和 12 位，表示上一次的特权级别。
#define MSTATUS_MPP_M (3L << 11) // 机器模式（Machine mode）
#define MSTATUS_MPP_S (1L << 11) // 监督模式（Supervisor mode）
#define MSTATUS_MPP_U (0L << 11) // 用户模式（User mode）
// MIE（Machine Interrupt Enable）位，位于第 3 位，决定是否允许机器模式下的中断
#define MSTATUS_MIE (1L << 3)    // machine-mode interrupt enable.

/**
 * @brief 这段代码定义了一个静态内联函数 `r_mstatus()`，用于读取 RISC-V 架构下的 `mstatus` 寄存器的当前值
 * 
 * 由于函数被声明为 `static inline`，编译器会尽量将其内联展开，减少函数调用开销
 * 这种函数常用于操作系统内核或底层硬件控制代码中，便于直接访问和管理 CPU 状态
 * 
 * @return uint64 
 */
static inline uint64
r_mstatus()
{
  uint64 x; // 声明了一个 64 位无符号整型变量 `x`
  // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `mstatus` 寄存器的内容读入变量 `x`
  asm volatile("csrr %0, mstatus" : "=r" (x) );
  return x; // 返回读取到的 `mstatus` 寄存器的值，这个值保存了当前 CPU 的特权级、中断使能等重要状态信息
}

/**
 * @brief 向 RISC-V 架构下的 mstatus 寄存器写入值 
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_mstatus(uint64 x)
{
  // 调用了 RISC-V 指令 csrw，将 x 的值写入 mstatus 寄存器
  asm volatile("csrw mstatus, %0" : : "r" (x));
}

// machine exception program counter, holds the
// instruction address to which a return from
// exception will go.
/**
 * mepc（Machine Exception Program Counter）寄存器用于保存发生异常或中断时的返回地址
 * 当处理完异常或中断后，CPU 会从 mepc 中读取地址，继续执行之前被打断的指令
 * @brief 向 RISC-V 架构下的 mepc 寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_mepc(uint64 x)
{
  asm volatile("csrw mepc, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `mepc` 寄存器
}

// Supervisor Status Register, sstatus
/**
 * @brief sstatus（监督者状态寄存器）相关的几个常量
 * 用于操作和解析 sstatus 寄存器中的特定位
 * 
 * 这些宏常用于操作系统内核或底层硬件控制代码中，方便对 sstatus 寄存器的特权级和中断状态进行读写和判断
 */
#define SSTATUS_SPP (1L << 8)  // 上一次的特权级模式（Previous mode），1 代表监督者模式（Supervisor），0 代表用户模式（User）
#define SSTATUS_SPIE (1L << 5) // Supervisor Previous Interrupt Enable 监督者上一次中断使能位，用于保存进入中断前 SIE 位的值
#define SSTATUS_UPIE (1L << 4) // User Previous Interrupt Enable 用户上一次中断使能位，用于保存进入中断前 UIE 位的值
#define SSTATUS_SIE (1L << 1)  // Supervisor Interrupt Enable 监督者中断使能位，控制当前是否允许监督者级别的中断
#define SSTATUS_UIE (1L << 0)  // User Interrupt Enable 用户中断使能位，控制当前是否允许用户级别的中断

/**
 * @brief 读取 RISC-V 架构下的 sstatus（Supervisor Status Register，监督者状态寄存器）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_sstatus()
{
  uint64 x;
  asm volatile("csrr %0, sstatus" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `sstatus` 寄存器的内容读入变量 `x`
  return x;
}

/**
 * @brief 向 RISC-V 架构下的 sstatus 寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_sstatus(uint64 x)
{
  asm volatile("csrw sstatus, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `sstatus` 寄存器
}

// Supervisor Interrupt Pending
/**
 * @brief 读取 RISC-V 架构下的 sip（监督者中断挂起寄存器）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_sip()
{
  uint64 x;
  asm volatile("csrr %0, sip" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `sip` 寄存器的内容读入变量 `x`
  return x;
}

/**
 * @brief 向 RISC-V 架构下的 sip 寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_sip(uint64 x)
{
  asm volatile("csrw sip, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `sip` 寄存器
}

// Supervisor Interrupt Enable
/**
 * @brief RISC-V 架构下 sie（监督者中断使能寄存器）相关的几个常量
 * 用于操作和解析 sie 寄存器中的特定位
 * 
 */
#define SIE_SEIE (1L << 9) // Supervisor External Interrupt Enable 外部中断使能位，控制是否允许监督者模式下的外部中断
#define SIE_STIE (1L << 5) // Supervisor Timer Interrupt Enable 定时器中断使能位，控制是否允许监督者模式下的定时器中断
#define SIE_SSIE (1L << 1) // Supervisor Software Interrupt Enable 软件中断使能位，控制是否允许监督者模式下的软件中断

/**
 * @brief 读取 RISC-V 架构下的 sie（监督者中断使能寄存器）寄存器的当前值
 * 
 * @return uint64  返回读取到的寄存器值
 */
static inline uint64
r_sie()
{
  uint64 x;
  asm volatile("csrr %0, sie" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `sie` 寄存器的内容读入变量 `x`
  return x;
}

/**
 * @brief 向 RISC-V 架构下的 sie 寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_sie(uint64 x)
{
  asm volatile("csrw sie, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `sie` 寄存器
}

// Machine-mode Interrupt Enable
/**
 * @brief RISC-V 架构下的 mie（机器级中断使能寄存器）相关的几个常量
 * 
 */
#define MIE_STIE (1L << 5)  // Machine Supervisor Timer Interrupt Enable 定时器中断使能位，控制是否允许机器模式下的监督者定时器中断

/**
 * @brief 读取 RISC-V 架构下的 mie（机器中断使能寄存器）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_mie()
{
  uint64 x;
  asm volatile("csrr %0, mie" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `mie` 寄存器的内容读入变量 `x`
  return x;
}

/**
 * @brief 向 RISC-V 架构下的 mie 寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_mie(uint64 x)
{
  asm volatile("csrw mie, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `mie` 寄存器
}

// supervisor exception program counter, holds the
// instruction address to which a return from
// exception will go.
/**
 * @brief 向 RISC-V 架构下的 sepc（监督者中断程序计数器）寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_sepc(uint64 x)
{
  asm volatile("csrw sepc, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `sepc` 寄存器
}

/**
 * @brief 读取 RISC-V 架构下的 sepc（监督者中断程序计数器）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_sepc()
{
  uint64 x;
  asm volatile("csrr %0, sepc" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `sepc` 寄存器的内容读入变量 `x`
  return x;
}

// Machine Exception Delegation

/**
 * @brief 读取 RISC-V 架构下的 medeleg（机器异常委托寄存器）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_medeleg()
{
  uint64 x;
  asm volatile("csrr %0, medeleg" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `medeleg` 寄存器的内容读入变量 `x`
  return x;
}

/**
 * @brief 向 RISC-V 架构下的 medeleg 寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_medeleg(uint64 x)
{
  asm volatile("csrw medeleg, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `medeleg` 寄存器
}

// Machine Interrupt Delegation

/**
 * @brief 读取 RISC-V 架构下的 mideleg（机器中断委托寄存器）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_mideleg()
{
  uint64 x;
  asm volatile("csrr %0, mideleg" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `mideleg` 寄存器的内容读入变量 `x`
  return x;
}

/**
 * @brief 向 RISC-V 架构下的 mideleg 寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_mideleg(uint64 x)
{
  asm volatile("csrw mideleg, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `mideleg` 寄存器
}

// Supervisor Trap-Vector Base Address
// low two bits are mode.
/**
 * @brief 向 RISC-V 架构下的 stvec（特权陷入向量基址寄存器）寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_stvec(uint64 x)
{
  asm volatile("csrw stvec, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `stvec` 寄存器
}

/**
 * @brief 读取 RISC-V 架构下的 stvec（特权陷入向量基址寄存器）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_stvec()
{
  uint64 x;
  asm volatile("csrr %0, stvec" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `stvec` 寄存器的内容读入变量 `x`
  return x;
}

// Supervisor Timer Comparison Register

/**
 * @brief 读取 RISC-V 架构下的 stimecmp（Supervisor 模式的定时器比较寄存器）寄存器的当前值
 * 0x14d 是 stimecmp 寄存器的机器模式地址 
 * 该寄存器用于设置监督者模式下的定时器比较值
 * 当定时器计数器达到该值时，会触发一个定时器中断
 * 这通常用于实现定时任务或调度器的功能
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_stimecmp()
{
  uint64 x;
  // asm volatile("csrr %0, stimecmp" : "=r" (x) );
  asm volatile("csrr %0, 0x14d" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `stimecmp` 寄存器的内容读入变量 `x`
  return x;
}

/**
 * @brief 向 RISC-V 架构下的 stimecmp 寄存器写入值
 * 
 * @param x  写入寄存器的新值
 */
static inline void 
w_stimecmp(uint64 x)
{
  // asm volatile("csrw stimecmp, %0" : : "r" (x));
  asm volatile("csrw 0x14d, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `stimecmp` 寄存器
}

// Machine Environment Configuration Register
/**
 * @brief 读取 RISC-V 架构下的 menvcfg（机器特权级下环境配置寄存器）寄存器的当前值
 * 0x30a 是 menvcfg 寄存器的机器模式地址
 * 该寄存器用于配置机器特权级下的环境设置
 * 包括特权级、地址转换等相关配置
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_menvcfg()
{
  uint64 x;
  // asm volatile("csrr %0, menvcfg" : "=r" (x) );
  asm volatile("csrr %0, 0x30a" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `menvcfg` 寄存器的内容读入变量 `x`

  return x;
}

/**
 * @brief 向 RISC-V 架构下的 menvcfg 寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_menvcfg(uint64 x)
{
  // asm volatile("csrw menvcfg, %0" : : "r" (x));
  asm volatile("csrw 0x30a, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `menvcfg` 寄存器
}

// Physical Memory Protection
/**
 * @brief 向 RISC-V 架构下的 pmpcfg0（物理内存保护配置寄存器 0）寄存器写入值 
 * 
 * @param x 写入寄存器的新值
 */
static inline void
w_pmpcfg0(uint64 x)
{
  asm volatile("csrw pmpcfg0, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `pmpcfg0` 寄存器
}

/**
 * @brief 写入 RISC-V 架构下的 pmpaddr0（物理内存保护地址寄存器 0）寄存器的值 
 * 
 * 
 * @param x 写入寄存器的新值
 */
static inline void
w_pmpaddr0(uint64 x)
{
  asm volatile("csrw pmpaddr0, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `pmpaddr0` 寄存器
}

// use riscv's sv39 page table scheme.

/**
 * @brief SATP_SV39 用于指定使用 Sv39 页表方案
 * 
 * 构造了 SATP（Supervisor Address Translation and Protection）寄存器的模式字段
 * RISC-V 的 satp 寄存器高 4 位用于指定地址转换模式，8 表示使用 Sv39（39 位虚拟地址，三级页表）模式
 * 因此将 8L 左移 60 位后，正好放在 satp 的模式字段
 * 
 */
#define SATP_SV39 (8L << 60)

/**
 * @brief MAKE_SATP(pagetable) 用于根据页表的物理地址生成完整的 satp 寄存器值
 * 
 * 它将页表物理地址右移 12 位（因为页表地址以页为单位存储）
 * 再与 SATP_SV39 按位或，得到最终可以写入 satp 寄存器的值
 * 
 */
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// supervisor address translation and protection;
// holds the address of the page table.
// satp（Supervisor Address Translation and Protection）寄存器
// 该寄存器用于虚拟内存管理，主要保存当前页表的物理地址
/**
 * @brief 用于向 RISC-V 架构下的 satp（Supervisor Address Translation and Protection）寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_satp(uint64 x)
{
  asm volatile("csrw satp, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `satp` 寄存器
}

/**
 * @brief 读取 RISC-V 架构下的 satp（Supervisor Address Translation and Protection）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_satp()
{
  uint64 x;
  asm volatile("csrr %0, satp" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `satp` 寄存器的内容读入变量 `x`
  return x;
}

// Supervisor Trap Cause
// 陷阱（trap）是指异常或中断事件。当 CPU 处于监督者模式（Supervisor mode）时，发生异常或中断会记录原因到相关寄存器（如 scause）
/**
 * @brief 读取 RISC-V 架构下的 scause（Supervisor Trap Cause，监督者陷入原因）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_scause()
{
  uint64 x;
  asm volatile("csrr %0, scause" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `scause` 寄存器的内容读入变量 `x`
  return x;
}

// Supervisor Trap Value
/**
 * @brief 读取 RISC-V 架构下的 stval（Supervisor Trap Value，监督者陷入值）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_stval()
{
  uint64 x;
  asm volatile("csrr %0, stval" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `stval` 寄存器的内容读入变量 `x`
  return x;
}

// Machine-mode Counter-Enable

/**
 * @brief 向 RISC-V 架构下的 mcounteren（机器模式计数器使能寄存器）寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_mcounteren(uint64 x)
{
  asm volatile("csrw mcounteren, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `csrw`，将 `x` 的值写入 `mcounteren` 寄存器
}

/**
 * @brief 读取 RISC-V 架构下的 mcounteren（机器模式计数器使能寄存器）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_mcounteren()
{
  uint64 x;
  asm volatile("csrr %0, mcounteren" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `mcounteren` 寄存器的内容读入变量 `x`
  return x;
}

// machine-mode cycle counter

/**
 * @brief 读取 RISC-V 架构下的 mcycle（机器模式周期计数器）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_time()
{
  uint64 x;
  asm volatile("csrr %0, time" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `csrr`，将 `time` 寄存器的内容读入变量 `x`
  return x;
}

// enable device interrupts

/**
 * @brief 启用 RISC-V 架构下的设备中断
 * 
 */
static inline void
intr_on()
{
  w_sstatus(r_sstatus() | SSTATUS_SIE); // 设置 sstatus 寄存器的 SIE 位，允许监督者模式下的中断
}

// disable device interrupts

/**
 * @brief 禁用 RISC-V 架构下的设备中断
 * 
 */
static inline void
intr_off()
{
  w_sstatus(r_sstatus() & ~SSTATUS_SIE); // 清除 sstatus 寄存器的 SIE 位，禁止监督者模式下的中断
}

// are device interrupts enabled?

/**
 * @brief 检查 RISC-V 架构下的设备中断是否已启用
 * 
 * @return int 返回 1 如果设备中断已启用，否则返回 0
 */
static inline int
intr_get()
{
  uint64 x = r_sstatus();
  return (x & SSTATUS_SIE) != 0;
}

/**
 * @brief 读取 RISC-V 架构下的 sp（堆栈指针）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_sp()
{
  uint64 x;
  asm volatile("mv %0, sp" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `mv`，将 sp 寄存器的内容读入变量 `x`
  return x;
}

// read and write tp, the thread pointer, which xv6 uses to hold
// this core's hartid (core number), the index into cpus[].
// 在 RISC-V 架构下，tp thread pointer，线程指针）寄存器 通常用于线程相关的数据访问
// 在 xv6 中，tp 被用来保存当前 CPU 核心的 hartid（硬件线程编号），也就是该核心在 cpus[] 数组中的索引
// 这样做可以方便地在多核环境下区分和管理不同的 CPU 核心，实现高效的调度和资源分配
/**
 * @brief 读取 RISC-V 架构下的 tp（线程指针）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_tp()
{
  uint64 x;
  asm volatile("mv %0, tp" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `mv`，将 tp 寄存器的内容读入变量 `x`
  return x;
}

/**
 * @brief 向 RISC-V 架构下的 tp（线程指针）寄存器写入值
 * 
 * @param x 写入寄存器的新值
 */
static inline void 
w_tp(uint64 x)
{
  asm volatile("mv tp, %0" : : "r" (x)); // 使用内联汇编调用 RISC-V 指令 `mv`，将变量 `x` 的值写入 tp 寄存器
}

/**
 * @brief 读取 RISC-V 架构下的 ra（返回地址）寄存器的当前值
 * 
 * @return uint64 返回读取到的寄存器值
 */
static inline uint64
r_ra()
{
  uint64 x;
  asm volatile("mv %0, ra" : "=r" (x) ); // 使用内联汇编调用 RISC-V 指令 `mv`，将 ra 寄存器的内容读入变量 `x`
  return x;
}

// flush the TLB.

/**
 * @brief 刷新 TLB（Translation Lookaside Buffer，转换后备缓冲区）
 * 
 */
static inline void
sfence_vma()
{
  // the zero, zero means flush all TLB entries. 
  // zero, zero 是 RISC-V 指令 sfence.vma 的参数，表示刷新所有 TLB 条目
  // 这条指令会使得 TLB 中的所有条目失效
  asm volatile("sfence.vma zero, zero"); // 使用内联汇编调用 RISC-V 指令 `sfence.vma`，刷新 TLB
}

typedef uint64 pte_t; // 页表条目，64 位无符号整数 
// 页表指针结构
typedef uint64 *pagetable_t; // 512 PTEs 每个 PTE 占用 8 字节（64 位），因此一个页表可以映射 512 个页面

#endif // __ASSEMBLER__

#define PGSIZE 4096 // bytes per page 每页是 4096 字节, 4KB
#define PGSHIFT 12  // bits of offset within a page 页内偏移的位数，12 位表示页内偏移范围为 0-4095


/**
 * @brief 这两行宏定义用于实现内存页对齐操作，是操作系统和底层内存管理中常见的工具
 * 
 * 这两个宏可以保证内存分配和管理时的地址或大小总是页对齐，有助于提高系统性能和安全性。
 * 
 */

// PGROUNDUP(sz) 用于将任意大小 sz 向上对齐到最近的页边界
// 它的原理是先加上 PGSIZE-1（页大小减一），再通过按位与 ~(PGSIZE-1) 清除低位，从而得到大于等于 sz 的最小页对齐值
// 例如，如果页大小为 4096 字节，PGROUNDUP(4100) 会返回 8192
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))

// PGROUNDDOWN(a) 用于将地址 a 向下对齐到最近的页边界
// 它直接通过按位与 ~(PGSIZE-1) 清除低位，使结果总是页对齐且不超过原地址
// 例如，PGROUNDDOWN(4100) 会返回 4096 
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

/**
 * @brief 定义了页表项（Page Table Entry）中的几个标志位常量，用于描述每个页表项的属性
 * 
 * 这些宏常用于操作系统内核或内存管理模块，方便对页表项的权限和状态进行设置和判断
 */
#define PTE_V (1L << 0) // 有效位（valid），表示该页表项可以被硬件访问。
#define PTE_R (1L << 1) // 读权限（read），表示该页允许被读取
#define PTE_W (1L << 2) // 写权限（write），表示该页允许被写入
#define PTE_X (1L << 3) // 执行权限（execute），表示该页允许被执行
#define PTE_U (1L << 4) // 用户态访问权限（user access），表示该页允许用户态程序访问

// shift a physical address to the right place for a PTE.

/**
 * @brief 这三行宏定义用于页表项（Page Table Entry, PTE）和物理地址之间的转换，以及提取页表项中的标志位。
 * 
 * 这些宏简化了页表项和物理地址之间的转换，以及标志位的提取，常用于操作系统内核的内存管理模块
 */

// 将物理地址 pa 转换为页表项中的地址部分
// 它先将物理地址右移 12 位（去掉页内偏移），再左移 10 位，使其对齐到页表项的地址字段位置
// 这是因为 RISC-V 页表项的低 10 位用于标志位，高位用于存储物理页号
// 例如，如果物理地址是 0x12345000，那么 PA2PTE(pa) 将返回 0x12345000 >> 12 << 10
// 这将得到一个页表项的物理地址表示，适用于 RISC-V 的 Sv39 页表方案
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)

// 将页表项中的地址部分转换回物理地址
// 它先将页表项右移 10 位（去掉标志位），再左移 12 位，恢复为完整的物理地址。
#define PTE2PA(pte) (((pte) >> 10) << 12)

// 提取页表项中的标志位
// 通过与 0x3FF（低 10 位全为 1）进行按位与操作，得到页表项的权限和状态标志
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// extract the three 9-bit page table indices from a virtual address.

/**
 * @brief 这三行宏定义用于 RISC-V Sv39 页表机制下的虚拟地址解析
 *
 * 
 * 这些宏简化了多级页表索引的计算过程，是操作系统内核实现虚拟内存管理的基础工具。
 */

 // 义了一个 9 位掩码（0x1FF），用于提取每一级页表索引
 #define PXMASK          0x1FF // 9 bits  Sv39 每级页表索引都是 9 位
// 计算第 level 级页表索引在虚拟地址中的起始位
// PGSHIFT 通常是页内偏移的位数（如 12），每级页表索引占 9 位
// 所以第 level 级的起始位是 PGSHIFT + 9 * level
#define PXSHIFT(level)  (PGSHIFT+(9*(level)))

// 从虚拟地址 va 中提取第 level 级页表的索引
// 它先将虚拟地址右移到对应的索引位，再用 PXMASK 取出低 9 位，得到该级页表的索引值
#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)

// one beyond the highest possible virtual address.
// MAXVA is actually one bit less than the max allowed by
// Sv39, to avoid having to sign-extend virtual addresses
// that have the high bit set.
/**
 * @brief 用于计算 RISC-V 虚拟地址空间的最大虚拟地址（MAXVA）
 * 
 * RISC-V 的 Sv39 虚拟地址方案采用三级页表，每级页表索引 9 位，加上页内偏移 12 位，总共 9 + 9 + 9 + 12 = 39 位
 * 由于最高位用于符号扩展（即虚拟地址是有符号的），所以最大有效虚拟地址是 2^(39-1)（即最高位为 0，表示正数范围）
 * 因此，表达式中的 - 1 是为了排除符号位，只计算有效地址空间
 * 1L << 38（即 1 左移 38 位）得到的就是最大有效虚拟地址的数值
 * 
 * 这个宏常用于内存管理相关的边界检查，确保地址不会超出 RISC-V 架构允许的范围
 * 
 */
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))
