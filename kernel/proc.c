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

// 中断必须关闭，防止进程被挪到另一个不同的cpu上
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.

// 中断必须关闭
struct cpu*
mycpu(void)
{
  int id = cpuid(); // 获取当前cpu id
  struct cpu *c = &cpus[id]; // 查询当前cpu对应的结构体指针
  return c;
}

// Return the current struct proc *, or zero if none.
// 返回当前运行的进程指针，如果不存在，返回 0 （NULL）
struct proc*
myproc(void)
{
  push_off(); // 关闭中断并增加嵌套深度
  struct cpu *c = mycpu(); // 获得当前cpu指针
  struct proc *p = c->proc; // 从当前cpu结构获取当前进程
  pop_off(); // 打开中断并减少嵌套深度
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
  // prevents race in allocating proc[] slots for new process 
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
// 创建一个新进程，并复制父进程的相关内容（如内存页表、寄存器、文件描述符等），实现父子进程的分离
// 其次，它会设置子进程的内核栈，使得当子进程第一次被调度运行时，看起来就像是从 fork() 系统调用返回一样
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc(); // 获得当前cpu正在运行的进程

  // Allocate process.
  if((np = allocproc()) == 0){ // 创建一个新的进程失败
    return -1; // 内核奔溃，返回 -1 
  }

  // Copy user memory from parent to child.
  // 复制内存父进程的内存页表到子进程
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){ // 复制失败，释放内存，返回 -1 
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  // 复制父进程 trap帧 （父进程保存的寄存器）
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  // 让 fork() 系统调用在子进程中返回 0  
  // 根据 RISC-V 调用约定，a0 用于存放函数返回值。
  np->trapframe->a0 = 0; // 将子进程 trapframe（陷入帧）中的 a0 寄存器设置为 0

  // increment reference counts on open file descriptors.
  // 遍历父进程打开的文件，增加子进程对这些文件的引用计数
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i]) // 父进程引用这个文件
      np->ofile[i] = filedup(p->ofile[i]); 
  np->cwd = idup(p->cwd); // 设置子进程的当前目录为父进程的当前目录，并把当前目录的inode引用计数 + 1 

  safestrcpy(np->name, p->name, sizeof(p->name)); // 安全拷贝进程名字

  pid = np->pid;

  release(&np->lock); // 释放allocate时候获取到的子进程的自旋锁

  acquire(&wait_lock); 
  np->parent = p; //设置子进程的父进程ID，注意：需要对wait加锁，保证并发
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE; //设置子进程的状态为可执行，注意：需要对子进程加锁，保证并发
  release(&np->lock);

  return pid; // 父进程调用fork返回 子进程的id
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
/**
 * @brief 终止某个进程之前，将它的所有子进程重新分配，作为 init 进程的子进程
 * 
 * @param p 进程结构体指针
 * 
 * @note 这个函数调用者必须持有 wait_lock 全局自旋锁
 * 
 */
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){ // 遍历进程表
    if(pp->parent == p){ // 是否是进程p的子进程
      pp->parent = initproc; // 修改这个进程的父进程为 init 进程
      wakeup(initproc); // 唤醒所有等待“initproc”资源的进程
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().

// 当进程调用 exit 时，会立即终止当前进程的执行，且该函数不会返回到调用者
// 被终止的进程会进入 ZOMBIE（僵尸）状态，表示进程已经退出，但其父进程还没有回收它的资源（如退出码等）
// 只有当父进程调用 wait()，回收了子进程的资源后，僵尸进程才会被系统彻底清理
void
exit(int status)
{
  struct proc *p = myproc(); // 获取当前CPU的当前运行的进程

  if(p == initproc) // 无法停止 init 进程，内核奔溃
    panic("init exiting");

  // Close all open files.
  // 关闭所有当前进程打开的文件
  for(int fd = 0; fd < NOFILE; fd++){ // 遍历进程所有的文件描述符
    if(p->ofile[fd]){ // 文件被打开
      struct file *f = p->ofile[fd]; // 根据文件描述符获得文件指针
      fileclose(f);  // 关闭文件
      p->ofile[fd] = 0; // 文件描述符对应的文件指针置为 NULL 
    }
  }

  begin_op();
  iput(p->cwd); // 进程工作目录的inode引用计数 - 1 
  end_op();
  p->cwd = 0; // 设置进程工作目录为 NULL 

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p); // 重新设置“当前进程p的所有子进程”的父进程为 initproc 

  // Parent might be sleeping in wait().
  // 在修改子进程之前状态之前，唤醒等待当前进程的wait函数，粗看不安全，但实际上
  // 即使现在wait函数被提前唤醒，也无法看不到子进程已经变成ZOMBIE状态
  // 只有子进程先获取p->lock，再修改状态为ZOMBIE，才可见
  wakeup(p->parent); // 唤醒可能在等待“当前进程退出”的父进程

  // 注意：这个获取的进程锁，在进入调度器之后
  // 等待调度器切换到wait函数，在返回子进程退出状态之后的时候释放 pp->lock
  // 因此这里并不会调用 release 
  acquire(&p->lock); // 获取当前进程的进程锁，防止parent在此之前看到不一致的状态

  p->xstate = status; // 设置返回的状态码
  p->state = ZOMBIE; // 设置状态为 ZOMBIE, 等待init进程清理

  release(&wait_lock); // 释放wait_lock 

  // Jump into the scheduler, never to return.
  sched(); // 进入进程调度器，不再返回
  panic("zombie exit"); // 如果返回，内核奔溃
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
// 当前进程等待某个子进程结束并返回它的pid
// 如果当前进程没有子进程，则返回 -1 
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock); // 获取 wait_lock 锁

  for(;;){ 
    // Scan through table looking for exited children.
    // 遍历进程表，寻找存在的子进程
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        // 确保该子进程当前没有处于 exit() 或 swtch() 的临界区内
        // 如果在没有加锁的情况下访问子进程的状态或资源，可能会遇到子进程正在退出或切换上下文的瞬间，导致竞态条件
        // 因此，后续代码会先对子进程加锁，确保在安全的状态下检查和操作子进程，避免并发访问带来的问题
        acquire(&pp->lock);

        // 注意：这里加锁的顺序，必须是先获取 wait_lock，再获取 子进程 pp 的锁，避免死锁
        // 这个顺序和 exit 里面获取锁的顺序是一样的
        havekids = 1;
        if(pp->state == ZOMBIE){ // 找到某个已经终止的进程
          // Found one.
          pid = pp->pid;
          // 把子进程的终止状态 xstate （内核栈内） 复制到 addr （用户空间）
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) { // 复制失败
            release(&pp->lock); 
            release(&wait_lock);
            return -1; // 返回 -1 
          }
          freeproc(pp); // 释放子进程的资源 
          release(&pp->lock); // 释放子进程的进程锁
          release(&wait_lock); // 释放 wait_lock 全局锁
          return pid; // 返回释放的子进程的 pid 
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){ // 当前进程没有任何子进程，或者当前进程自己也终止了
      release(&wait_lock); // 释放全局wait_lock 
      return -1; // 返回 - 1 
    }
    
    // Wait for a child to exit. 
    // 当前进程进入睡眠，等待某个子进程推出 
    // 子进程在调用exit的适合，会调用 wakeup(p->parent)
    // 注意：sleep函数会释放 wait_lock, 在被 wakeup唤醒后，再次自动获得 wait_lock 
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

// 每个 CPU 都有自己的调度器实例，负责本地进程的调度
//每个 CPU 在初始化后会调用 scheduler()，并且该函数永远不会返回（即进入一个无限循环）
// 调度器的主要循环流程包括：
//  - 选择一个可运行的进程
//  - 通过 swtch 切换到该进程的上下文，开始运行该进程
//  - 当进程主动让出 CPU 或被抢占时，会再次通过 swtch 返回到调度器，继续选择下一个进程
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0; // 清空当前CPU运行的进程
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    // 最近刚刚运行的进程可能已经关闭了中断，如果调度器不主动重新开启中断
    // 可能会导致所有进程都在等待某些事件时，系统无法响应这些中断，从而陷入死锁。
    // 通过在每次调度循环开始时调用 intr_on()，可以确保中断始终处于开启状态
    // 保证调度器能够及时响应外部事件，唤醒等待中的进程，避免系统停滞
    intr_on(); // 打开中断

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock); // 获得进程的自旋锁
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING; // 设置进程的状态为运行
        c->proc = p; // 设置cpu里运行的进程
        swtch(&c->context, &p->context); // 切换cpu执行的上下文，执行找到的进程p

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        
        // 表示进程 p 本次的运行已经结束
        // 强调在进程 p 返回调度器之前，必须已经修改了自身的状态（p->state）
        // 比如从 RUNNING 改为 SLEEPING、RUNNABLE，并且在sleep，yield等方法里已经对进程上锁
        // 这样可以确保调度器能够正确地管理进程状态，避免进程被错误地再次调度或出现状态混乱
        c->proc = 0; // 清空cpu中进程域
        found = 1; // 表示找到一个可执行的进程
      }
      release(&p->lock); // 释放进程的可执行锁（由yield，sleep等函数上锁）
    }
    if(found == 0) { // 没有找到任何一个可执行的进程
      // nothing to run; stop running on this core until an interrupt.
      // 无事可做，停止运行当前cpu核心，直到中断发生
      intr_on(); // 打开中断
      asm volatile("wfi"); // 会让 CPU 进入低功耗等待状态，直到有中断（如定时器、外设等）到来时才会被唤醒
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

// 表示当前代码涉及将执行权交还给调度器，让调度器选择下一个要运行的进程

// 在切换前，必须只持有当前进程的锁（p->lock），并且已经更新了进程的状态（如从 RUNNING 改为 SLEEPING 或 RUNNABLE）
// 以保证并发安全和状态一致性

// 说明需要保存和恢复中断使能状态（intena），因为它属于当前内核线程（进程），而不是整个 CPU
// 这样可以确保进程切换后中断状态不会混乱

// 理想情况下，intena 和 noff（中断嵌套计数）应该作为进程结构体的成员
// 但在某些特殊情况下（如持有锁但没有当前进程时），这样做会导致问题
void
sched(void)
{
  int intena;
  struct proc *p = myproc(); // 获取当前CPU的当前进程

  if(!holding(&p->lock)) // 未持有当前进程的进程自旋锁，奔溃
    panic("sched p->lock"); 
  if(mycpu()->noff != 1) // 中断嵌套深度必须为 1 
    panic("sched locks");
  if(p->state == RUNNING) // 进程状态不能是可运行 
    panic("sched running"); 
  if(intr_get()) // 不允许启用中断
    panic("sched interruptible");

  intena = mycpu()->intena; // 保存 中断嵌套计数
  // mycpu 里保存的 context 一般是内核也就是调度器的上下文
  // 实际上是调度函数 scheduler() 中 switch 语句的下一条
  swtch(&p->context, &mycpu()->context); // 切换当前进程到调度器
  mycpu()->intena = intena; // 恢复 中断嵌套计数
}

// Give up the CPU for one scheduling round.
// 当前CPU运行的进程放弃运行，调度器进行进程调度
void
yield(void)
{
  struct proc *p = myproc(); // 获取当前CPU运行的进程
  // 这里给进程上锁，解锁的机制类似于sleep 
  acquire(&p->lock);
  p->state = RUNNABLE; // 修改进程状态为可执行
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

// 自动释放持有的自旋锁，并在睡眠中等待chan, 醒来之后自动再次获得自旋锁
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc(); // 获取当前CPU的当前进程
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  // 首先，必须先获取当前进程的自旋锁 p->lock，这样才能安全地修改进程状态（p->state）并调用调度器（sched）
  // 如果不加锁，可能会有竞态条件，导致进程状态不一致或调度混乱
  
  // 一旦持有了 p->lock，就可以保证不会错过任何唤醒（wakeup）操作
  // 因为 wakeup 在唤醒进程时也会加 p->lock，这样 sleep 和 wakeup 之间就不会出现竞态
  // 因此，在持有 p->lock 的前提下，可以放心地释放传入的锁 lk，避免死锁或锁顺序问题

  // 注：这里上的锁，并不是由本函数最后的release释放，而是在scheduler里释放
  acquire(&p->lock);  //DOC: sleeplock1
  release(lk); // 释放原来持有的任意自旋锁

  // Go to sleep.
  p->chan = chan; // 设置进程等待资源chan 
  p->state = SLEEPING; // 设置进程状态为 睡眠

  sched(); // 把当前进程切换出去

  // Tidy up.
  p->chan = 0; // 清理 等待条件

  // Reacquire original lock.
  // 这里释放的自旋锁，并不是开头获取的，而是在scheduler函数中获取的
  release(&p->lock); // 释放进程的自旋锁
  acquire(lk); // 再次获得调用sleep前持有的自旋锁
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.

// 唤醒所有在 chan（某个等待条件或资源）上睡眠的进程
// 也就是说，凡是因为等待 chan 而进入睡眠状态的进程，都会被唤醒，变为可运行状态
// 必须在没有持有任何进程锁（p->lock）的情况下调用此函数
// 这样可以避免死锁，因为这个函数会尝试获取p->lock 
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) { // 遍历所有进程
    if(p != myproc()){ // 非当前进程
      acquire(&p->lock); // 修改某个进程状态，需要获取这个进程对应的进程锁
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE; // 修改符合条件的进程运行状态为 “可执行”
      }
      release(&p->lock); // 释放进程锁
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
// 该函数会根据传入的进程 ID（pid）查找并标记要终止的进程。
// 被标记为“已杀死”的进程并不会立即退出，而是要等到它下次从内核态返回用户态时
//（比如系统调用结束或中断处理完成）
// 在 usertrap() 里检测到被杀死的标志后，才会真正执行退出流程
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){ // 遍历进程表
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1; // 标志进程已经结束
      // 如果目标进程当前处于 SLEEPING（睡眠）状态，就将其状态修改为 RUNNABLE（可运行）
      // 以便它能被调度器重新调度运行
      // 当进程被 kill 时，可能正因为等待某个事件而处于睡眠状态（如调用 sleep() 等待资源）
      // 如果此时不唤醒进程，它将一直停留在睡眠队列，无法检测到自己已被标记为“已杀死”，也就无法及时退出
      // 通过将进程状态设置为 RUNNABLE，可以让调度器尽快调度该进程运行
      // 进程被唤醒后，会在合适的时机检查自己的 killed 标志，并执行退出流程
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1; // 无法找到对应要终止的进程，返回 -1 
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
// 该函数会将当前系统中所有进程的信息打印到控制台，主要用于调试（debugging）
// 当用户在控制台输入 ^P（Ctrl+P）时，会触发运行此函数，方便开发者快速查看进程状态
// 为了避免在系统卡死或异常时进一步导致死锁，procdump 在打印进程信息时不会加锁
// 这种做法牺牲了一定的数据一致性，但能最大程度保证在系统异常时仍能输出有用的调试信息
void
procdump(void)
{
  // 定义了一个静态字符串数组 states[]，用于将进程状态的枚举值映射为对应的字符串
  // 每个数组元素的下标对应一个进程状态的枚举常量（如 UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE）
  // 而数组的内容则是这些状态的英文字符串

  // 这种写法的好处是可以通过进程状态的枚举值直接索引到对应的字符串
  // 方便在调试、日志输出或状态显示时，将内部的数值状态转换为易于理解的文本
  // 例如，如果某个进程的状态为 RUNNING，则可以通过 states[RUNNING] 得到字符串 "run   "

  //  这种数组初始化方式利用了 C 语言的“指定初始化器”特性
  // 确保即使枚举值不是连续的，字符串和状态也能一一对应，避免出错
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
    // p->state >= 0 && p->state < NELEM(states): 判断进程状态 p->state 是否在有效范围内
    // states[p->state]：进一步判断对应下标的字符串指针是否非空，确保该状态有对应的字符串描述
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name); // 打印进程的id，状态，名字
    printf("\n");
  }
}
