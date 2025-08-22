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
void            iinit();
void            ilock(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
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
void*           kalloc(void);
void            kfree(void *);
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
int             cpuid(void);
void            exit(int);
int             fork(void);
int             growproc(int);
void            proc_mapstacks(pagetable_t);
pagetable_t     proc_pagetable(struct proc *);
void            proc_freepagetable(pagetable_t, uint64);
int             kill(int);
int             killed(struct proc*);
void            setkilled(struct proc*);
struct cpu*     mycpu(void);
struct cpu*     getmycpu(void);
struct proc*    myproc();
void            procinit(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            sleep(void*, struct spinlock*);
void            userinit(void);
int             wait(uint64);
void            wakeup(void*);
void            yield(void);
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);
void            procdump(void);

// swtch.S
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
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
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
extern uint     ticks;
void            trapinit(void);
void            trapinithart(void);
extern struct spinlock tickslock;
void            usertrapret(void);

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartputc(int);
void            uartputc_sync(int);
int             uartgetc(void);

// vm.c
void            kvminit(void);
void            kvminithart(void);
void            kvmmap(pagetable_t, uint64, uint64, uint64, int);
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     uvmcreate(void);
void            uvmfirst(pagetable_t, uchar *, uint);
uint64          uvmalloc(pagetable_t, uint64, uint64, int);
uint64          uvmdealloc(pagetable_t, uint64, uint64);
int             uvmcopy(pagetable_t, pagetable_t, uint64);
void            uvmfree(pagetable_t, uint64);
void            uvmunmap(pagetable_t, uint64, uint64, int);
void            uvmclear(pagetable_t, uint64);
pte_t *         walk(pagetable_t, uint64, int);
uint64          walkaddr(pagetable_t, uint64);
int             copyout(pagetable_t, uint64, char *, uint64);
int             copyin(pagetable_t, char *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);

// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// virtio_disk.c
void            virtio_disk_init(void);
void            virtio_disk_rw(struct buf *, int);
void            virtio_disk_intr(void);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
