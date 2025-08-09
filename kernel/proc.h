// Saved registers for kernel context switches.
/**
 * @brief context 结构体，用于保存内核上下文切换时需要保存的寄存器内容
 * 
 * 在操作系统内核中，进行进程或线程切换时，需要将当前任务的 CPU 状态保存下来，以便将来能恢复并继续执行
 * context 结构体正是用来存储这些关键寄存器的值
 * 
 * 通过保存这些寄存器的值，内核可以安全地切换不同的进程或线程，保证每个任务恢复时都能从上次中断的位置继续执行
 * 这是多任务操作系统实现的基础之一
 */
struct context {
  uint64 ra; // 保存返回地址寄存器（return address），用于函数调用返回时恢复执行位置
  uint64 sp; // 保存栈指针寄存器（stack pointer），指向当前任务的栈顶

  // callee-saved

  // 这些是 RISC-V 架构下的被调用者保存的通用寄存器（callee-saved registers）
  // 如果一个函数要使用这些寄存器（如 s0-s11），它必须在使用前先保存原值（通常压栈），在返回前再恢复原值
  // 这样，调用方可以假定这些寄存器在函数调用后不会被改变
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// Per-CPU state.
/**
 * @brief cpu 的结构体，用于描述每个 CPU 核心在操作系统内核中的状态信息
 * 
 * 这个结构体在多核操作系统中非常重要，能够帮助内核管理和调度每个 CPU 的运行状态、进程切换和中断控制
 * 
 */
struct cpu {
  // 指向当前正在该 CPU 上运行的进程结构体。如果没有进程在运行，则为 null。
  struct proc *proc;          // The process running on this cpu, or null.

  // 该 CPU 的上下文信息。当需要切换到调度器（scheduler）时，会通过 swtch() 函数切换到这里保存的上下文
  // 这样可以在进程切换时恢复 CPU 的寄存器状态
  struct context context;     // swtch() here to enter scheduler().

  // 记录 push_off() 的嵌套深度
  // push_off() 是一种关闭中断的机制，嵌套调用时需要计数，确保中断在合适的时机恢复
  int noff;                   // Depth of push_off() nesting. 

  // 记录在调用 push_off() 时中断是否被启用
  // 如果在调用 push_off() 时中断是启用的，则 intena 为 1，否则为 0
  // 这样可以在恢复时正确地还原中断状态
  int intena;                 // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU]; // 每个 CPU 的状态信息数组， 每一项对应一个CPU核心

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.

// 每个进程都有一份专用的 trapframe 数据，用于处理中断、异常或系统调用时的寄存器保存和恢复
// trapframe 被单独放在用户页表中、紧挨着 trampoline 页（用于内核与用户态切换的特殊代码）
// 但在内核页表中并没有特殊映射

// 当用户态发生 trap 时，trampoline.S 文件中的 uservec 函数会把用户寄存器保存到 trapframe
// 然后从 trapframe 中读取 kernel_sp、kernel_hartid、kernel_satp 等内核相关寄存器
// 并跳转到 kernel_trap 处理内核逻辑

// 在返回用户态时，usertrapret() 和 trampoline.S 中的 userret 会设置 trapframe 的 kernel 相关字段
// 恢复用户寄存器，切换到用户页表，并重新进入用户空间
// trapframe 之所以包含被调用者保存的寄存器（如 s0-s11）
// 是因为返回用户态的路径不会经过完整的内核调用栈，必须在 trapframe 中完整保存这些寄存器，确保进程能安全恢复执行

/**
 * @brief trapframe 结构体用于保存进程在发生中断、异常或系统调用时的全部寄存器状态
 * 
 * 这是操作系统内核实现用户态与内核态切换的关键数据结构
 * 
 * 通过这种设计，trapframe 能完整保存和恢复进程的 CPU 状态，是多任务操作系统实现进程隔离和安全切换的基础
 * 
 */
struct trapframe {
  // 这些寄存器用于保存内核态的状态信息 
  /*   0 */ uint64 kernel_satp;   // kernel page table 内核页表
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack 内核栈顶
  /*  16 */ uint64 kernel_trap;   // usertrap() 中断处理函数地址
  /*  24 */ uint64 epc;           // saved user program counter 用户进程计数器
  /*  32 */ uint64 kernel_hartid; // saved kernel tp 内核线程指针（tp）


  // 依次保存 RISC-V 架构下所有通用寄存器的值，包括
  // 返回地址（ra）、栈指针（sp）、全局指针（gp）、线程指针（tp）、临时寄存器（t0-t6）
  // 被调用者保存寄存器（s0-s11）、参数寄存器（a0-a7）等
  // 每次发生 trap（如系统调用或异常）时，内核会将这些寄存器的值保存到 trapframe 中，处理完毕后再恢复
  // 保证用户进程能从中断点继续执行
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

/**
 * @brief 进程状态表
 * 
 * 
 */
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state

/**
 * @brief proc 结构体，用于描述操作系统中的每个进程
 * 
 * proc 结构体是操作系统内核中非常核心的数据结构之一
 * 它保存了进程的各种状态信息，包括进程的运行状态、内存使用情况、打开的文件、父进程等
 * 
 */
struct proc {
  struct spinlock lock; // Process lock 锁，用于保护进程状态的并发访问

  // p->lock must be held when using these:
  enum procstate state;        // Process state 进程状态
  // 如果非零，表示进程正在某个资源上睡眠（等待）
  void *chan;                  // If non-zero, sleeping on chan
  // 标记进程是否被终止
  int killed;                  // If non-zero, have been killed
  // 进程退出时的状态码，供父进程查询
  int xstate;                  // Exit status to be returned to parent's wait
  // 进程唯一标识符（PID）
  int pid;                     // Process ID

  // wait_lock must be held when using this:
  // 指向父进程的指针，用于实现进程层级和等待机制 
  // 使用这个字段时候，wait lock 必须被持有
  struct proc *parent;         // Parent process

  // these are private to the process, so p->lock need not be held.
  // 以下字段是进程私有的，不需要持有 p->lock 锁即可访问
  // 内核栈的虚拟地址，支持内核态操作 
  uint64 kstack;               // Virtual address of kernel stack
  // 进程的用户态内存大小（字节数），用于管理进程的内存分配
  uint64 sz;                   // Size of process memory (bytes)
  // 用户页表，管理进程的虚拟内存映射
  pagetable_t pagetable;       // User page table
  // trapframe 用于保存进程在发生中断或系统调用时的寄存器状态
  struct trapframe *trapframe; // data page for trampoline.S
  // 上下文结构体，支持进程切换时保存和恢复 CPU 状态
  struct context context;      // swtch() here to run process
  // 进程的打开文件列表，最多支持 NOFILE 个文件
  struct file *ofile[NOFILE];  // Open files
  // 进程的当前工作目录 inode，指向进程所在的目录
  struct inode *cwd;           // Current directory
  // 进程的名称，用于调试和日志记录，最多支持 16 字节的名称长度
  char name[16];               // Process name (debugging)
};
