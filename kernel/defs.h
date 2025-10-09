struct buf;
struct context;
struct file;
struct inode;
struct pipe;
struct proc;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;

// bio.c
/**
 * @brief 初始化磁盘块缓冲区
 * 
 * 所有缓冲区被组织成一个循环双向链表，并初始化了必要的锁机制
 * 为后续的磁盘块缓存管理和 LRU 淘汰策略做好了准备
 * 
 */
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);
void            bpin(struct buf*);
void            bunpin(struct buf*);

// console.c 控制台功能
/**
 * @brief 初始化控制台
 * 
 * @param void  
 * 
 * @return void
 * 
 */
void            consoleinit(void);

/**
 * @brief 控制台输入中断处理函数 
 * 
 * 每当串口（UART）收到输入字符时，uartintr() 就会调用这个处理函数
 * 
 * @param c 输入的字符 
 * 
 * @return void 
 * 
 */
void            consoleintr(int);

/**
 * @brief 向控制台输出单个字符
 * 
 * @param c 要输出的字符 
 * 
 * @return void
 * 
 */
void            consputc(int);

// exec.c
int             exec(char*, char**);

// file.c
struct file*    filealloc(void);
void            fileclose(struct file*);
struct file*    filedup(struct file*);

/**
 * @brief 初始化内核中的文件表
 * 
 */
void            fileinit(void);
int             fileread(struct file*, uint64, int n);
int             filestat(struct file*, uint64 addr);
int             filewrite(struct file*, uint64, int n);

// fs.c
void            fsinit(int);
int             dirlink(struct inode*, char*, uint);
struct inode*   dirlookup(struct inode*, char*, uint*);
struct inode*   ialloc(uint, short);
struct inode*   idup(struct inode*);

/**
 * @brief 初始化内核中的 inode 表
 * 
 * 为后续的 inode 分配、查找和同步访问做好了准备
 * 
 */
void            iinit();
void            ilock(struct inode*);

/**
 * @brief 释放或递减指定 inode 的引用计数
 * 
 * 当文件系统中的某个 inode 不再被使用时，调用 iput 可以减少其引用次数
 * 如果引用次数降为零，则会进一步释放与该 inode 相关的资源（如缓存、内存等）
 * 
 * @param ip 释放或递减指定 inode 的引用计数
 * 
 */
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
/**
 * @brief 根据给定的路径名查找并返回对应的 inode 结构体指针
 * 
 * inode 是文件系统中用于描述文件元数据（如类型、大小、权限、数据块位置等）的核心结构
 * 
 * @param name 给定的路径名
 * 
 * @return struct inode* 返回对应的 inode 结构体指针 
 * 
 * 通过 namei，内核或文件系统模块可以根据路径快速定位到具体的文件或目录对象，进而进行后续的读写、权限检查等操作
 * 
 */
struct inode*   namei(char*);
struct inode*   nameiparent(char*, char*);
int             readi(struct inode*, int, uint64, uint, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, int, uint64, uint, uint);
void            itrunc(struct inode*);

// ramdisk.c
void            ramdiskinit(void);
void            ramdiskintr(void);
void            ramdiskrw(struct buf*);

// kalloc.c
/**
 * @brief 分配一页物理内存
 * 
 * @return void* 指向分配的内存页的指针，返回0 代表分配失败（内存全满）
 */
void*           kalloc(void);

/**
 * @brief 释放一页物理内存
 * 
 * 当某个内存页不再被使用时，调用 kfree 可以将这页内存归还给内存分配器
 * 
 * @param pa 指向某页内存的开始处
 * 
 * 通常，kfree 只应被用于释放由 kalloc 分配出来的页面，确保内存管理的正确性和系统的稳定性
 * 
 */
void            kfree(void *);

/**
 * @brief 内核物理内存分配器的初始化
 * 
 */
void            kinit(void);

// log.c
void            initlog(int, struct superblock*);
void            log_write(struct buf*);
void            begin_op(void);
void            end_op(void);

// pipe.c
int             pipealloc(struct file**, struct file**);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, uint64, int);
int             pipewrite(struct pipe*, uint64, int);

// printf.c
/**
 * @brief 格式化输出函数 
 * 
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * 
 */

// __attribute__ ((format (printf, 1, 2))) 是 GCC 和 Clang 编译器支持的一个函数属性
// 用于检查类似 printf 的格式化输出函数的参数类型和数量是否匹配
// 具体含义如下：
// format (printf, 1, 2) 指定该函数的第 1 个参数（从 1 开始计数）是格式字符串，
//                       第 2 个参数及其后续参数是可变参数（即要被格式化输出的内容）
// 编译器会根据格式字符串自动检查后续参数的类型和数量是否正确
// 如果格式字符串和参数类型不匹配，编译器会在编译时给出警告或错误提示
int            printf(char*, ...) __attribute__ ((format (printf, 1, 2)));

/**
 * @brief 触发内核崩溃 
 * 
 */
// __attribute__((noreturn)) 是 GCC 和 Clang 编译器支持的一个函数属性
// 用于告诉编译器：被修饰的函数不会返回到调用者
// 常见的用法是在实现死循环、直接终止程序（如 exit()、panic()）或触发异常的函数前加上这个属性
// 这样做有两个主要好处：
// 1. 编译器可以进行更激进的优化，因为它知道该函数调用后不会有后续代码执行
// 2. 如果在调用 __attribute__((noreturn)) 的函数后还有代码，编译器会发出警告，帮助开发者发现潜在的逻辑错误 
void            panic(char*) __attribute__((noreturn));

/**
 * @brief 初始化 printf 函数
 * 
 */
void            printfinit(void);

// proc.c

/**
 * @brief 获取当前正在运行代码的 CPU（或硬件线程，hart）的编号（ID）
 * 
 * 在多核或多处理器系统中，每个 CPU 都有唯一的编号，内核通过 cpuid() 可以识别当前代码运行在哪个 CPU 上
 * 
 * @return int 当前正在运行代码的 CPU 的编号（ID）
 * 
 */
int             cpuid(void);

/**
 * @brief 终止当前进程的执行
 * 
 * 调用 exit 后，操作系统会进行资源回收（如关闭文件、释放内存等），并将进程状态设置为“已终止”，等待父进程处理
 * 
 * @param status 进程的退出状态码，父进程可以通过该状态码获知子进程的退出原因
 * 
 */
void            exit(int);

/**
 * @brief 建一个当前进程的子进程
 * 
 * 调用 fork() 后，系统会复制当前进程的大部分资源（如内存空间、文件描述符等），生成一个新的子进程
 * 
 * 
 * @return int  在父进程中，fork() 返回新创建子进程的进程 ID（PID）
 *              在子进程中，fork() 返回 0
 *              如果创建失败，返回 -1
 * 
 * 父进程和子进程的执行几乎完全独立
 * 
 */
int             fork(void);
int             growproc(int);

/**
 * @brief 映射进程的栈页
 * 
 * @param pagetable 进程的页表
 * 
 */
 void            proc_mapstacks(pagetable_t);

 /**
  * @brief 返回某个进程对应的内存页表
  * 
  * @param p 进程结构体指针
  * 
  * @return pagetable_t 该进程对应的页表
  * 
  * 通过这个函数，内核可以方便地获取某个进程的页表，用于内存分配、回收、地址转换等操作
  * 
  */
pagetable_t     proc_pagetable(struct proc *);

/**
 * @brief 释放进程的页表及其映射的物理内存
 * 
 * 该函数会解除对 trampoline 和 trapframe 页的映射，并释放所有用户内存
 * 
 * @param pagetable 进程的页表
 * @param sz 进程的地址空间大小
 * 
 * 通常在进程退出或需要回收内存时被调用
 * 确保与该进程相关的所有虚拟内存映射都被正确清理，防止内存泄漏
 * 
 */
void            proc_freepagetable(pagetable_t, uint64);
/**
 * @brief 向指定 pid 的进程发送终止信号，请求操作系统内核终止该进程
 * 调用后，内核会将目标进程标记为“已杀死”
 * 并在适当的时机（如进程下次被调度或执行系统调用时）将其安全终止
 * 
 * @param pid 进程标识符
 * 
 * @return int 0 表示成功，-1 表示失败（如找不到对应的进程）
 * 
 */
int             kill(int);
/**
 * @brief 检查指定进程是否被标记为“已杀死”
 * 在 xv6 这样的操作系统内核中，进程可能会被其他进程或内核自身请求终止（如通过 kill 命令或异常处理）
 * 该函数通常会检查进程结构体中的某个标志位（如 killed 字段）
 * 
 * @return int 如果进程已被请求终止，则返回非零值，否则返回 0
 * 
 */
int             killed(struct proc*);

/**
 * @brief 向指定的进程发送终止信号
 * 
 * @param proc 进程结构指针
 * 
 */
void            setkilled(struct proc*);

/**
 * @brief 获取 当前正在运行代码的 CPU（或硬件线程，hart）对应的 cpu 结构体指针
 * 
 * 多核或多处理器系统中，内核需要区分和管理每个 CPU 的本地状态（如当前运行的进程、调度信息、中断嵌套等）
 * 通过调用 mycpu()，内核代码可以方便地获取当前 CPU 的本地数据结构，实现对多核环境下各自资源的隔离和管理
 * 
 * @return struct cpu* 当前运行的CPU 结构体指针
 * 
 */
struct cpu*     mycpu(void);

/**
 * @brief 获取当前 CPU 正在运行的进程的指针
 * 
 * @return struct proc*   指向当前 CPU 正在运行进程结构体的指针
 *                        如当前CPU没有正在运行的进程，返回 0 (NULL)
 * 
 * 在多核或多线程操作系统中，每个 CPU（或硬件线程）都可能在运行不同的进程
 * 通过调用 myproc()，内核代码可以方便地获取当前上下文下的进程结构体
 * 进而访问或修改该进程的状态、内存、文件等信息
 * 
 */
struct proc*    myproc();

/**
 * @brief 初始化进程表
 * 
 */
void            procinit(void);

/**
 * @brief 核心调度器函数
 * 
 * 它负责不断地选择和切换可运行的进程，让 CPU 能够在多个进程之间轮流执行，实现多任务并发
 * 
 * @note __attribute__((noreturn)) 告诉编译器，这个函数不会返回到调用者
 * 也就是说，一旦进入 scheduler，就不会再回到原来的执行点
 * 通常是一个无限循环，只有通过上下文切换（如 swtch）跳转到其他进程
 */
void            scheduler(void) __attribute__((noreturn));

/**
 * @brief 把当前进程的上下文切换成调度器进程的上下文
 * 
 * 当进程因为等待资源、进入睡眠或主动让出 CPU 时，会调用 sched()，恢复scheduler函数的执行
 * 
 * 在调用前，通常需要先修改当前进程的状态（如设置为 SLEEPING 或 RUNNABLE）
 * 并确保只持有当前进程的锁，以保证调度过程的安全和一致性
 * 
 * sched() 会保存当前进程的上下文，然后切换到调度器上下文，等到该进程再次被调度时再恢复执行
 * 这是多任务操作系统实现进程切换和 CPU 资源分配的基础机制
 * 
 */
void            sched(void);

/**
 * @brief 内核中让当前进程进入睡眠状态
 * 
 * 直到某个条件（通常与 chan 相关）被满足或事件发生
 * sleep 会在进程睡眠前自动释放该锁，并在被唤醒后重新获取，确保临界区的并发安全
 * 
 * @param chan 用作等待队列的标识，表示进程因等待某个资源或事件而睡眠
 * @param lk 自旋锁指针，表示当前持有的自旋锁 
 * 
 * @return void 无返回
 * 
 */
void            sleep(void*, struct spinlock*);
/**
 * @brief 初始化第一个用户进程
 * 
 * 它的作用是在系统启动时创建并设置初始用户环境（如加载 init 程序），为后续用户进程的运行打下基础
 * 
 * 该函数的具体实现会分配进程结构、设置内存空间、加载用户代码，并将进程状态设置为可运行
 * 
 */
void            userinit(void);

/**
 * @brief 当前进程回收某个已经终止的子进程
 * 当有子进程退出时，wait 会回收该子进程的资源，并将其退出状态写入 addr 指向的内存
 * 
 * @param addr 一个用户空间内存地址，用于存放子进程的退出状态
 * 
 * @return int 返回值为被回收子进程的 PID，如果没有子进程可等待，则返回 -1
 * 
 * 用于实现父子进程之间的同步和资源管理，确保父进程能够获知子进程的退出信息并及时回收系统资源
 * 
 */
int             wait(uint64);

/**
 * @brief 唤醒所有在 chan（某个等待条件或资源）上睡眠的进程
 * 
 * 在内核中，进程可能因为等待某个事件（如 I/O 完成、资源可用等）而主动进入睡眠状态
 * wakeup 函数的作用就是通知所有等待某个特定条件或资源的进程，让它们从睡眠状态变为可运行状态（RUNNABLE）
 * 以便调度器可以重新调度它们执行
 * 
 * @param chan 某个等待条件或资源
 * 
 */
void            wakeup(void*);
/**
 * @brief 让当前正在运行的进程或线程主动让出 CPU 的使用权
 * 
 * 调用 yield() 后，将当前进程的状态设置为可运行（RUNNABLE）
 * 然后调度器会选择下一个可运行的进程进行调度
 * 
 */
void            yield(void);

/**
 * @brief 根据 user_dst 的值，决定将数据从内核缓冲区复制到用户空间（需要地址转换和权限检查）或直接复制到内核空间
 * 
 * @param user_dst 0: 内核空间，非0：用户空间
 * @param dst 目标地址，可以是用户虚拟地址或内核地址，取决于 user_dst 的值
 * @param src 源数据的指针，通常指向内核空间的缓冲区
 * @param len 要复制的数据字节数
 * @return int 0 表示复制成功，-1 表示失败（如地址非法或权限不足）
 * 
 */
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len);

/**
 * @brief 根据 user_src 的值，决定将数据从用户空间（需要地址转换和权限检查）或内核空间 复制到内核空间的缓冲区
 * 
 * @param dst 目标数据的指针，通常指向内核空间的缓冲区
 * @param user_src 0: 内核空间，非0：用户空间
 * @param src 源数据的地址，根据use_src 决定是内核缓冲区或 用户空间
 * @param len 要复制的数据字节数
 * @return int 0 表示复制成功，-1 表示失败（如地址非法或权限不足）
 * 
 */
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);

/**
 * @brief 打印当前系统中所有进程的状态信息
 * 
 * 通常包括进程的 PID、状态、名称、父进程等
 * 它常用于内核调试或系统监控，帮助开发者或管理员了解进程的分布和运行情况
 */
void            procdump(void);

// swtch.S

/**
 * @brief 上下文切换（context switch）的核心函数
 * 它的作用是在多任务环境下，将当前执行流从一个进程（或线程）的上下文切换到另一个进程（或线程）的上下文
 * 
 * @param old 保存当前进程的寄存器等上下文信息，以便将来能恢复执行
 * @param new 目标进程的上下文，函数会加载该上下文，使 CPU 从目标进程的状态继续运行
 * 
 * swtch 通常由汇编实现，直接操作底层寄存器，是多任务调度和进程切换的基础
 * 
 */
void            swtch(struct context*, struct context*);

// spinlock.c

/**
 * @brief 获取自旋锁 
 * 
 * @param lk 自旋锁结构体指针
 *  
 * @return void 无返回
 */
void            acquire(struct spinlock*);

/**
 * @brief 检查自旋锁是否被持有
 * 
 * @param lk 自旋锁结构体指针
 * 
 * @return int 如果锁被持有返回 1，否则返回 0
 */
int             holding(struct spinlock*);

/**
 * @brief 初始化自旋锁
 * 
 * @param lk 自旋锁结构体指针
 * @param name 自旋锁名称字符串
 * 
 * @return void 无返回
 * 
 */
void            initlock(struct spinlock*, char*);

/**
 * @brief 释放自旋锁
 * 
 * @param lk 自旋锁结构体指针 
 * 
 * @return int 
 */
void            release(struct spinlock*);

/**
 * @brief 关闭中断并增加嵌套深度
 * 
 */
void            push_off(void);

/**
 * @brief 恢复中断并减少嵌套深度
 * 
 */
void            pop_off(void);

// sleeplock.c
/**
 * @brief 获取互斥锁（sleeplock） 
 * 
 * @param lk 互斥锁结构体指针
 * 
 * @return void 无返回
 * 
 */
void            acquiresleep(struct sleeplock*);

/**
 * @brief 释放互斥锁（sleeplock） 
 * 
 * @param lk 互斥锁结构体指针 
 * 
 * @return void 无返回
 * 
 */
void            releasesleep(struct sleeplock*);

/**
 * @brief 检查互斥锁是否被当前进程持有
 * 
 * @param lk 互斥锁结构体指针
 * 
 * @return int 如果锁被当前进程持有返回 1，否则返回 0
 * 
 */
int             holdingsleep(struct sleeplock*);

/**
 * @brief 初始化一个互斥锁（sleeplock） 
 * 
 * @param lk 互斥锁结构体指针
 * @param name 互斥锁名称字符串
 * 
 * @return void 无返回
 */
void            initsleeplock(struct sleeplock*, char*);

// string.c
int             memcmp(const void*, const void*, uint);

/**
 * @brief 将内存区域 src 的前 n 个字节安全地复制到目标区域 dst
 * 
 * 与 memcpy 不同，memmove 能正确处理源和目标区域重叠的情况 
 * 
 * @param dst 目标内存指针
 * @param src 源内存指针
 * @param n 需要复制的字节大小
 * 
 * @return void* 复制后的目标内存指针 
 * 
 */
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);

/**
 * @brief 安全地将源字符串复制到目标字符串中，最多复制指定数量的字符，并确保目标字符串以 \0 结尾
 * 
 * @param dst 目标字符串指针
 * @param src 源字符串指针 
 * @param n 需要拷贝的字符数
 * 
 * @return char* 目标字符串的指针
 * 
 * 可以有效防止因字符串过长导致的内存越界，是内核或底层代码中常用的安全字符串复制函数
 * 
 */
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);

// syscall.c
void            argint(int, int*);
int             argstr(int, char*, int);
void            argaddr(int, uint64 *);
int             fetchstr(uint64, char*, int);
int             fetchaddr(uint64, uint64*);
void            syscall();

// trap.c
extern uint     ticks; // 全局时钟 ticks 

/**
 * @brief 要用于陷入（trap）机制的相关设置
 * 
 * 陷入机制包括中断、异常和系统调用等事件的处理
 * 
 * 通常，trapinit 的作用是初始化与中断和异常处理相关的资源
 * 例如，它会初始化自旋锁（如 tickslock），为后续的中断处理提供同步保障
 * 这样可以确保在多核环境下，内核对共享资源（如时钟节拍计数器）的访问是安全的
 * 
 */
void            trapinit(void);

/**
 * @brief 专门用于为每个硬件线程（hart，Hardware Thread）设置陷入（trap）相关的硬件状态
 * 
 * 它通常在每个 CPU 或硬件线程启动时被调用
 * 该函数的主要作用是配置当前 hart 的陷入向量（trap vector）
 * 即设置当发生中断、异常或系统调用时，CPU 应该跳转到哪个处理程序地址
 * 这样可以确保每个处理器核心都能正确响应和处理各种异常和中断事件
 * 
 */
void            trapinithart(void);
extern struct spinlock tickslock; // 全局时钟自旋锁

/**
 * @brief 用户态中断在内核态处理完毕后，返回用户态
 * 
 * 为最后调用 trampoline.S 中的 userret 函数做准备 
 * 1. 设置陷入向量寄存器 stvec，指向 trampoline.S 中的 uservec 入口 
 * 2. 设置当前进程的 trapframe 结构体中的关键字段，供 uservec 使用
 * 3. 设置 sstatus 和 sepc 寄存器，确保从内核态返回用户态时的正确状态
 * 4. 跳转到 trampoline.S 中的 userret 入口，完成从内核态返回用户态的切换
 * 
 * @note 该函数必须在关闭中断的情况下调用 
 */
void            usertrapret(void);

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartputc(int);
void            uartputc_sync(int);
int             uartgetc(void);

// vm.c 虚拟内存管理
/**
 * @brief 初始化虚拟内存管理
 * 
 */
void            kvminit(void);

/**
 * @brief 开启虚拟内存管理
 * 
 */
void            kvminithart(void);

/**
 * @brief 在内核页表中建立虚拟地址到物理地址的映射关系
 * 
 * 将一段连续的虚拟地址空间映射到对应的物理内存区域，并设置相应的访问权限（如可读、可写、可执行等）
 * 
 * @param pagetable_t 内核页表指针
 * @param va 虚拟地址起始位置
 * @param pa 物理地址起始位置
 * @param sz 映射区域的大小（以字节为单位）
 * @param perm 访问权限标志
 * 
 */
void            kvmmap(pagetable_t, uint64, uint64, uint64, int);

/**
 * @brief 从虚拟地址 va 开始的一段地址区间创建页表项（PTE），使它们映射到从物理地址 pa 开始的物理内存区域
 * 
 * @param pagetable 页表指针
 * @param va 虚拟地址起始位置
 * @param pa 物理地址起始位置
 * @param sz 映射区域的大小（以字节为单位）
 * @param perm 访问权限标志
 * 
 * @return int 返回 0 表示成功，返回 -1 表示失败
 */
int             mappages(pagetable_t, uint64, uint64, uint64, int);
/**
 * @brief 创建并初始化一个新的用户页表
 * 
 * @return pagetable_t 创建后的用户进程页表地址，返回 0 代表内存不够分配
 * 
 * 通常会分配一组用于虚拟内存管理的数据结构，并设置好初始的映射关系（如空页表或只包含必要的内核映射）
 * 
 */
pagetable_t     uvmcreate(void);

/**
 * @brief 新创建的用户页表中，映射并初始化用户空间的第一页（通常是进程的起始代码）
 * 它会将指定的数据（如 initcode）复制到用户虚拟地址空间的起始位置，为用户进程的启动做好准备
 *
 * @param pagetable 用户页表指针
 * @param src 用户进程的初始代码
 * @param sz 用户进程的初始代码大小 
 * 
 * @return void 无返回
 * @note sz 必须小于一页（通常为 4096 字节）
 * 
 */
void            uvmfirst(pagetable_t, uchar *, uint);
uint64          uvmalloc(pagetable_t, uint64, uint64, int);
uint64          uvmdealloc(pagetable_t, uint64, uint64);

/**
 * @brief 将一个进程的用户虚拟内存空间（包括页表和物理页内容）复制到另一个进程的页表中
 * 
 * @param pagetable_t：源页表，表示要复制的原进程的页表
 * @param pagetable_t：目标页表，表示新进程的页表
 * @param uint64：要复制的虚拟地址空间的大小（通常为字节数）
 * 
 * @return int 回值为 0 表示复制成功，非 0 表示失败（如内存不足等)
 * 
 * 常用于实现 fork() 系统调用时子进程对父进程内存空间的复制
 * 保证了父子进程拥有独立但内容相同的用户空间，是多进程操作系统实现进程隔离和资源复制的关键步骤
 * 
 */
int             uvmcopy(pagetable_t, pagetable_t, uint64);

/**
 * @brief 释放整个用户页表及其对应的物理内存资源
 * 
 * @param pagetable 用户页表指针
 * @param sz 用户地址空间大小
 * 
 * 
 */
void            uvmfree(pagetable_t, uint64);
/**
 * @brief 将指定虚拟地址范围从页表中解除映射
 * 
 * @param pagetable 页表指针
 * @param va 虚拟地址起始位置
 * @param npages 要解除映射的页面数量
 * @param do_free 如果非零，则释放对应的物理内存页面
 * 
 * @return void 无返回
 * 
 * 用于进程释放内存或回收资源时，确保虚拟地址空间和物理内存的正确管理
 * 
 */
void            uvmunmap(pagetable_t, uint64, uint64, int);
void            uvmclear(pagetable_t, uint64);

/**
 * @brief 查找虚拟地址 va 对应的页表项, 如果不存在则分配新的页表页
 * 
 * @param pagetable 页表指针
 * @param va 虚拟地址
 * @param alloc 如果页表项不存在且 alloc 非零，则分配新的页表页
 *
 * @return pte_t* 返回 查找到或新创建的页表项指针
 * 
 */
pte_t *         walk(pagetable_t, uint64, int);
uint64          walkaddr(pagetable_t, uint64);
/**
 * @brief 内核空间的数据复制到用户空间的指定虚拟地址
 * 根据目标进程的页表进行地址转换和权限检查，确保数据安全地从内核传递到用户进程
 * 
 * @param pagetable 目标进程的页表，表示要写入的用户虚拟地址空间
 * @param dstva 目标虚拟地址，指定用户空间中的写入起始地址
 * @param src 内核缓冲区的指针，表示要复制的数据来源
 * @param len 要复制的数据字节数
 * 
 * @return int 返回 0 表示复制成功，-1 表示失败（如地址非法或权限不足）
 * 
 */
int             copyout(pagetable_t, uint64, char *, uint64);
int             copyin(pagetable_t, char *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);

// plic.c
/**
 * @brief 指定的中断源设置优先级
 * 
 * 保证关键设备的中断不会被屏蔽，从而能够及时响应外部事件
 * 
 */
void            plicinit(void);

/**
 * @brief 初始化当前硬件线程（hart）的 PLIC（平台级中断控制器）设置
 * 
 * 每个处理器核心都能正确响应 UART0 和 VIRTIO0 的中断请求，
 * 并且不会屏蔽任何优先级的中断，这是多核系统中断初始化的关键步骤
 * 
 */
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// virtio_disk.c

/**
 * @brief 初始化 Virtio 磁盘设备 
 * 
 * 该函数设置 Virtio 设备的 DMA 描述符、队列和状态
 * 并确保设备准备好进行磁盘 I/O 操作
 * 
 */
void            virtio_disk_init(void);
void            virtio_disk_rw(struct buf *, int);
void            virtio_disk_intr(void);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
