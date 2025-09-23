#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU]; // cpu状态数组

struct proc proc[NPROC]; // 进程数组

struct proc *initproc; // init进程

int nextpid = 1; // 累计进程号
struct spinlock pid_lock; // 用于分配进程 ID 的自旋锁

/**
 * @brief forkret 函数的声明
 * 
 * 通常用于新进程（由 fork 创建）第一次被调度运行时的入口点
 * 内核会将新进程的上下文（context）设置为从 forkret 开始执行
 * forkret 负责完成最后的内核初始化步骤，然后让进程安全地切换回用户空间，开始正常运行用户代码
 * 
 */
extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.

// wait 自旋锁有助于确保在父进程调用 wait() 等待子进程时，子进程唤醒父进程的操作不会丢失
// 该锁还能帮助在访问或修改 p->parent（进程的父进程指针）时，遵守内存模型的要求
// 避免并发访问带来的数据不一致问题，保证进程同步的正确性

// 使用规范上，必须在获取任何单个进程的自旋锁（p->lock）之前，先获取这个全局锁
// 这可以防止死锁和竞态条件，确保进程管理相关的操作安全有序地进行
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.

// 为每个进程分配一页物理内存作为其内核栈
// 这意味着每个进程在内核态运行时都有独立的栈空间，保证内核操作的安全和隔离

// 在内核栈之后紧跟着映射一个无效的“保护页”（guard page）。这页内存不可访问，用于捕捉栈溢出错误
// 如果内核栈使用过度导致越界访问，程序会因为访问无效页而触发异常，从而及时发现和定位栈溢出问题
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) { // 遍历进程表中的每一个进程结构体
    char *pa = kalloc(); // 分配一页物理内存作为内核栈
    if(pa == 0) // 分配失败
      panic("kalloc"); // 系统奔溃
    uint64 va = KSTACK((int) (p - proc)); // 计算内核栈的虚拟地址
    // 刚刚分配的物理页 pa 映射到内核页表 kpgtbl 的虚拟地址 va
    // 映射大小为一页，并设置读写权限 
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
// 初始化进程表
void
procinit(void)
{
  struct proc *p;
  
  // 初始化全局锁 pid_lock 和 wait_lock
  // 分别用于分配进程 ID 和进程等待队列的同步，确保多核环境下的线程安全
  initlock(&pid_lock, "nextpid"); 
  initlock(&wait_lock, "wait_lock");
  // 循环遍历进程表 proc 数组，初始化每一个进程结构体
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc"); // 初始化该进程的自旋锁，用于保护进程自身的数据结构
      p->state = UNUSED; // 该进程槽当前未被使用
      p->kstack = KSTACK((int) (p - proc)); // 设置该进程的内核栈顶地址 (对应的页内存已经在之前分配)
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

/**
 * @brief 初始化某个进程的 PID
 * 
 * @return int 返回新分配的进程 ID 
 * 
 * 注意：该函数在分配 PID 时会获取 pid_lock 全局自旋锁，确保在多核环境下的线程安全
 */
int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.

// 该函数会在进程表中查找一个状态为 UNUSED（未使用）的进程结构体 proc
// 如果找到空闲的进程项，就会初始化它，使其具备在内核中运行所需的状态
// 并在返回时持有该进程的锁（p->lock）
// 如果没有空闲进程项，或者内存分配失败，则返回 0（NULL 指针）

/**
 * @brief 分配一个新的进程结构体
 * 
 * @return struct proc* 成功返回指向新进程结构体的指针，失败返回 0 
 * 
 */
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock); // 获取该进程的自旋锁，防止并发访问
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock); // 无法找到空闲进程，释放锁继续查找
    }
  }
  return 0;

  // 找到空闲进程，进行初始化
found:
  p->pid = allocpid(); // 分配唯一的进程 ID
  p->state = USED; // 将进程状态设置为 USED（已使用）

  // Allocate a trapframe page. 为该进程分配一个 trapframe 页面
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){ // 分配失败
    freeproc(p); // 释放该进程结构体
    release(&p->lock); // 释放该进程的自旋锁
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p); // 为进程分配一个空的用户级别内存页表
  if(p->pagetable == 0){ // 分配页表失败
    freeproc(p); // 释放该进程结构体
    release(&p->lock); // 释放该进程的自旋锁
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  // 新进程的上下文会被设置为从 forkret 函数开始执行，forkret 最终会让进程返回到用户空间
  memset(&p->context, 0, sizeof(p->context)); // 将进程的 context 结构体清零，确保所有寄存器初始值为 0，避免遗留脏数据
  p->context.ra = (uint64)forkret; // 设置返回地址寄存器（ra），让进程被调度运行时，从 forkret 函数入口开始执行
  p->context.sp = p->kstack + PGSIZE; // 设置栈指针（sp）为该进程内核栈的栈顶，保证内核代码运行时有独立的栈空间

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.

// 调用 freeproc 时，必须已经持有该进程的锁（p->lock）
// 以保证在多核或多线程环境下的并发安全，防止资源被其他线程同时访问或修改

/**
 * @brief 释放一个进程结构体（proc）以及与其相关联的所有资源，包括用户空间分配的内存页等
 * 
 * @param p 进程结构体指针 
 * 
 * @return void 无返回 
 * 
 */
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe); // 释放该进程的 trapframe 页面
  p->trapframe = 0; 
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz); // 释放该进程的页表及其映射的物理内存
  // 释放完毕后，将相关指针和字段清零，防止悬挂指针和数据污染
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.

// 为指定进程创建一个用户页表
// 新页表中不包含任何用户内存（即用户代码和数据还未映射）
// 但会包含 trampoline（跳板代码）和 trapframe（陷入帧）这两个特殊页面的映射
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate(); // 创建空的用户进程页表
  if(pagetable == 0) // 内存不够，返回 0 
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  // trampoline 代码被映射到用户虚拟地址空间的最高地址处 
  // 但实际上只有内核（supervisor）会使用它，用户态无法直接访问（系统调用返回时候，会调用这部分代码）
  // 因此映射时不设置 PTE_U（用户可访问）权限

  // 将 trampoline 代码的物理地址映射到虚拟地址 TRAMPOLINE
  // 大小为一页（PGSIZE），权限为只读和可执行（PTE_R | PTE_X）
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){ 
    uvmfree(pagetable, 0); // 映射失败，释放页表，返回 0 
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  // 映射 trapframe（陷入帧）页面，为 trampoline.S（跳板汇编代码）提供支持 

  // 将当前进程的 trapframe 物理地址映射到虚拟地址 TRAPFRAME
  // 大小为一页（PGSIZE），权限为只读和可写（PTE_R | PTE_W）
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){ // 映射失败
    uvmunmap(pagetable, TRAMPOLINE, 1, 0); // 解除 TRAMPOLINE 的映射
    uvmfree(pagetable, 0); // 释放内存页表
    return 0; 
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
// 释放进程相关的页表及其映射的物理内存 
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  // 解除页表中 TRAMPOLINE 虚拟地址对应的一个页的映射，但不释放物理内存（最后一个参数为 0）
  uvmunmap(pagetable, TRAMPOLINE, 1, 0); // TRAMPOLINE 通常用于用户态和内核态切换的跳板代码
  // 解除页表中 TRAPFRAME 虚拟地址对应的一个页的映射，但不释放物理内存
  uvmunmap(pagetable, TRAPFRAME, 1, 0); // TRAPFRAME 用于保存进程在发生中断或系统调用时的寄存器状态
  uvmfree(pagetable, sz); // 释放进程对应的内存页表及其映射的物理内存
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc(); // 创建第一个进程
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  // 分配一页用户内存，并将 initcode 的指令和数据复制到这页内存中
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE; // 设置进程的内存大小为一页（PGSIZE）

  // prepare for the very first "return" from kernel to user.
  // 为进程的第一次从内核态“返回”到用户态做准备

  // 设置用户程序计数器（epc）为 0，表示用户程序将从虚拟地址 0 开始执行
  // 对于初始进程，这通常对应 initcode 的入口地址。
  p->trapframe->epc = 0;      // user program counter
  // 设置用户栈指针（sp）为一页的顶部（PGSIZE），即用户栈从虚拟地址 0 到 PGSIZE，栈顶在 PGSIZE 处
  p->trapframe->sp = PGSIZE;  // user stack pointer

  // 将字符串 "initcode" 安全地复制到进程结构体 p 的 name 字段中，最多复制 sizeof(p->name) 个字节
  safestrcpy(p->name, "initcode", sizeof(p->name)); 
  p->cwd = namei("/"); // 进程 p 设置当前工作目录（cwd）为根目录

  p->state = RUNNABLE; // 设置初始进程为可执行

  release(&p->lock); // 释放初始进程的锁
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      intr_on();
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.

// 新创建的子进程（通过 fork 产生）在被调度器（scheduler）第一次调度运行时
// 会切换（switch）到 forkret 函数执行
// 保证了文件系统初始化只会被执行一次，并且在多核环境下不会出现竞态条件
void
forkret(void)
{
  // 声明并初始化一个静态变量 first，用于是否已经初始化
  // 静态变量只在本函数内可见，并且在多次调用间保持其值
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock); // 释放当前进程的锁，此时已经不再需要保护进程结构体的数据

  if (first) { 
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().

    // 必须在普通进程上下文中执行（如可能调用 sleep），不能在 main() 里直接运行
    fsinit(ROOTDEV); // 调用文件系统初始化函数，通常会挂载根文件系统等

    first = 0; // 设置标志，表示文件系统已经初始化
    // ensure other cores see first=0.
    __sync_synchronize(); // 内存屏障，确保 first=0 的写操作对所有 CPU 核心可见，防止多核下的可见性问题
  }

  usertrapret(); // 让进程返回用户态，继续正常执行用户代码
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
