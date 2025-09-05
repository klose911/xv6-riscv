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
uint ticks; 

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
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
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

