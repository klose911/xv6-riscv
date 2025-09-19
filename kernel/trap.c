#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// 保护与系统时钟节拍（ticks）相关的全局变量或数据结构
// 例如，在时钟中断处理程序中，系统会更新计时器变量（如 ticks）
// 为了防止多个 CPU 或中断同时修改这些变量，必须使用自旋锁进行同步
struct spinlock tickslock;
uint ticks; // 全局时钟 ticks 变量，记录系统启动以来的时钟节拍数

// 声明了三个外部符号：trampoline[]、uservec[] 和 userret[]
// 它们分别表示在其他源文件（通常是汇编文件，如 trampoline.S）中定义的代码段的起始地址
// extern 关键字表示这三个符号在本文件中只是声明，实际定义在别处
// 这三个符号通常用于内核与用户态切换、陷入（trap）处理等关键流程。比如：
//    trampoline：跳板代码入口，用于安全切换页表和上下文
//    uservec：用户态陷入处理的入口地址
//    userret：从内核返回用户态的恢复代码入口
extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
// 实现在 kernelvec.S 中，处理内核级别中的异常和中断
// 具体中断处理逻辑在 kerneltrap() 中
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time"); // 初始化时钟自旋锁
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec); // 设置陷入向量寄存器 stvec，指向内核trap处理函数 kernelvec
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//

// 处理从用户态进入内核态的陷入（trap）包括系统调用、中断和异常等
// 通常是由 trampoline.S 中的陷入处理代码调用

/**
 * @brief 该函数用于处理来自用户空间的中断、异常或系统调用
 * 当用户程序发生这些事件时，CPU 会切换到内核态，并最终调用 usertrap
 * 
 * 
 */
void
usertrap(void)
{
  int which_dev = 0;

  // SSTATUS_SPP 是一个标志位，表示陷入发生时 CPU 的前一个特权级
  // 如果该位为 0，说明陷入来自用户态；如果为 1，说明陷入来自内核态
  if((r_sstatus() & SSTATUS_SPP) != 0) // 检查 SPP 位，确保陷入来自用户态
    panic("usertrap: not from user mode"); // 如果不是用户态陷入，内核奔溃

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  // 由于当前已经处于内核态，接下来所有的中断和异常都应该交由 kerneltrap() 处理 

  // 设置陷入向量寄存器（stvec）
  w_stvec((uint64)kernelvec); // 后续的中断和异常都跳转到 kerneltrap() 进行处理

  struct proc *p = myproc(); // 获取当前运行的（用户态）进程结构体指针
  
  // save user program counter.
  p->trapframe->epc = r_sepc(); // 保存用户程序计数器（sepc）到进程的 trapframe 中
  
  if(r_scause() == 8){ // 系统调用
    // system call

    if(killed(p)) // 进程已经停止，直接退出
      exit(-1); 

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    // sepc（Supervisor Exception Program Counter）寄存器此时指向触发系统调用的 ecall 指令本身
    // 为了让用户程序在系统调用返回后能继续执行下一条指令
    // 需要将 sepc 增加 4（RISC-V 指令长度为 4 字节）
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.

    // 中断发生时会改变 sepc、scause 和 sstatus 这几个关键寄存器的值
    // 只有在这些寄存器相关的操作全部完成后，才调用 intr_on() 重新开启中断
    // 避免在处理中断或异常信息时被新的中断打断，导致数据不一致或处理流程混乱。
    intr_on(); // 重新开启中断，允许处理其他中断

    syscall(); // 调用系统调用处理函数，根据系统调用号执行相应的内核功能
  } else if((which_dev = devintr()) != 0){ // 忽略时钟之外的设备中断
    // ok
  } else { // 其他异常 或无法识别的中断 打印出错信息，并停止进程
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p); // 标记进程为已终止状态
  }

  if(killed(p)) // 进程被标记为终止，调用 exit 退出
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2) // 时钟中断
    yield(); // 让出 CPU，进入调度器

  usertrapret(); // 返回用户态
}

//
// return to user space
// 返回用户态 
void
usertrapret(void)
{
  struct proc *p = myproc(); // 获取当前（用户态）进程结构体指针

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct. 

  // 当前即将把陷入处理的入口从 kerneltrap() 切换为 usertrap()
  // 在内核态时，陷入应该由 kerneltrap() 处理
  // 而回到用户空间后，陷入才应该由 usertrap() 处理 
  // 只有等回到用户空间后，才重新允许中断，此时陷入目标已经正确设置为 usertrap()
  intr_off(); // 关闭中断，防止在内核态时被新的中断打断

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S

  // 这段代码的作用是设置陷入向量寄存器（stvec）
  // 让后续的用户态的系统调用、中断和异常都跳转到 trampoline.S 文件中的 uservec 入口进行处理
  
  // TRAMPOLINE 是 trampoline 代码段在虚拟地址空间中的基地址
  // uservec 和 trampoline 都是外部符号，分别表示 trampoline 代码段中 uservec 和 trampoline 标签的地址
  // uservec - trampoline 计算出 uservec 相对于 trampoline 段起始的偏移
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline); // uservec 在虚拟地址空间中的实际入口地址
  w_stvec(trampoline_uservec); // 设置陷入向量寄存器 stvec，指向 uservec 入口

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  // 当前进程的 trapframe（陷入帧）结构体设置一组关键字段
  // 这些字段会在下次进程从用户态陷入内核态时，被 uservec 使用
  p->trapframe->kernel_satp = r_satp();         // kernel page table 内核页表
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack 内核栈顶
  p->trapframe->kernel_trap = (uint64)usertrap;  // usertrap() 函数地址
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid() 内核线程指针（tp）

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  // 这部分代码设置了一些寄存器，这些寄存器会被 trampoline.S 中的 sret 指令使用
  // sret 指令用于从内核态返回到用户态

  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus(); // 读取当前的 sstatus 寄存器值
  // 清除 SPP 位，设置为 0，表示返回用户态
  // SPP 位表示陷入发生时 CPU 的前一个特权级
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode 
  // 用户态允许中断
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x); // 将修改后的值写回 sstatus 寄存器

  // set S Exception Program Counter to the saved user pc.
  // sepc 寄存器保存了用户程序计数器（pc），即用户程序发生陷入时的指令地址
  // 该地址会在从内核态返回用户态时，执行sret命令后重新加载到 pc 中
  w_sepc(p->trapframe->epc); // 将保存的用户进程的 pc 写回 sepc 寄存器

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable); // 计算用户进程对应的内存页表的 satp 值

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  // 将跳转到 trampoline.S 汇编文件中的 userret 入口
  // 该入口负责切换到用户页表、恢复用户寄存器，并通过 sret 指令切换回用户模式
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline); // 计算 userret 在虚拟地址空间中的实际入口地址
  
  // 将该地址强制转换为一个接受 uint64 参数的函数指针，并调用它，同时传入 satp（用户页表的寄存器值）
  // 这样 trampoline 代码就能切换到用户页表，并完成用户态的恢复和跳转
  // (void (*)(uint64) 表示一个函数指针，指向一个接受 uint64 参数且无返回值的函数
  ((void (*)(uint64))trampoline_userret)(satp); 
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.

/**
 * @brief 处理在内核态下发生的中断或异常
 * 与用户态陷入不同，kerneltrap 只会在 CPU 已经处于内核模式时被调用，比如处理中断、定时器、设备请求等
 * 
 * 内核会根据中断或异常的类型，执行相应的处理逻辑
 * 例如，时钟中断会更新系统时钟节拍（ticks），设备中断会唤醒等待的进程等
 * 
 * 该函数还需要保证内核状态的正确保存与恢复，避免影响正在运行的内核任务
 */
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc(); // 读取（监督者中断程序计数器） 寄存器
  uint64 sstatus = r_sstatus(); // 读取（监督者状态）寄存器
  uint64 scause = r_scause(); // 读取 （监督者陷入原因）寄存器

  if((sstatus & SSTATUS_SPP) == 0) // 检查当前是否在监督模式
    panic("kerneltrap: not from supervisor mode"); 
  if(intr_get() != 0) // 检查当前是否允许中断
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){ // 处理设备中断，如果返回0，表示中断无法识别
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap"); // 未知中断，内核奔溃
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0) // 时钟中断，并且当前进程不为0（即有进程在运行）
    yield(); // 让出cpu，进入调度器

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.

  // yield() 可能会导致新的陷入（trap）发生，因此需要重新恢复陷入相关的寄存器
  w_sepc(sepc); // 将保存的 sepc（值写回硬件寄存器，指定异常返回后 CPU 应该跳转到的指令地址
  w_sstatus(sstatus); // 将保存的 sstatus值写回硬件寄存器，恢复之前的特权级和中断状态
}

/**
 * @brief 时钟中断处理函数
 * 
 */
void
clockintr()
{
  if(cpuid() == 0){ // 只有在 CPU 0 上才会执行后续的时钟节拍更新操作，避免多核环境下重复计数
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  // 设置下一个定时器中断的触发时间
  // r_time() 读取当前时间，1000000 表示大约 0.1 秒后再次触发中断，同时也清除了当前的中断请求
  w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.

/**
 * @brief 处理设备中断 
 * 
 * 函数会检查当前发生的是外部中断还是软件中断，并根据类型进行相应的处理
 * 
 * @return int  如果是定时器中断（timer interrupt），函数返回 2
 *              如果是其他设备中断（如磁盘、串口等），函数返回 1
 *              如果中断类型无法识别或不是上述两类，函数返回 0，表示无法识别
 */
int
devintr()
{
  uint64 scause = r_scause(); // 读取（监督者陷入原因）寄存器

  if(scause == 0x8000000000000009L){ // 处理外部中断
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim(); // 读取中断向量号，以此判断是哪个设备触发的

    if(irq == UART0_IRQ){ // 串口中断
      uartintr(); // 处理串口中断
    } else if(irq == VIRTIO0_IRQ){ // 虚拟磁盘中断
      virtio_disk_intr(); // 处理虚拟磁盘中断
    } else if(irq){ // 无法判断
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.

    // PLIC 规定每个设备在同一时刻最多只能触发一次中断
    // 也就是说，在内核处理完某个设备的中断之前，PLIC 不会再次向 CPU 报告该设备的新中断
    // 当内核完成了对某个设备中断的处理后，需要通过调用 plic_complete(irq) 告诉 PLIC：这个中断已经处理完毕
    // 这样，PLIC 才会允许该设备再次触发新的中断请求
    if(irq) // 判断当前是否有有效的中断号（irq），只有在有中断需要处理时才调用 plic_complete
      plic_complete(irq); // 告诉 PLIC 这个中断已经处理完毕

    return 1;
  } else if(scause == 0x8000000000000005L){ // 时钟中断
    // timer interrupt.
    clockintr(); // 处理时钟中断
    return 2;
  } else {
    return 0;
  }
}

