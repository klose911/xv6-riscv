#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
// 从当前进程的用户空间内存中安全地读取一个 uint64 类型的数据
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc(); // 获得当前进程 p 
  // 检查传入的地址 addr 是否在进程的有效用户空间范围内
  //  1. addr >= p->sz 检查起始地址是否合法
  //  2. addr+sizeof(uint64) > p->sz 检查读取的 8 字节是否会越界（防止溢出）
  // 这两个条件都必须判断，防止地址溢出导致的安全漏洞
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz) // both tests needed, in case of overflow
    return -1; // 无效地址
  // 从进程的页表 p->pagetable 中，将用户空间的 addr 处的 8 字节内容复制到内核空间的 ip 指针所指向的位置
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1; // 复制失败，返回 -1 
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
// 当前进程的用户空间内存中，读取以 NUL 字符（\0）结尾的字符串，起始地址由 addr 指定
// 如果读取成功，函数返回字符串的长度（不包括结尾的 NUL 字符）
// 如果发生错误（如地址无效、越界或未找到 NUL 结尾），则返回 -1
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  // 从进程的页表 p->pagetable 中，将用户空间的 addr 处的 max 字节内容复制到内核空间的 buf 指针所指向的位置
  if(copyinstr(p->pagetable, buf, addr, max) < 0) 
    return -1; // 复制失败，返回 -1 
  return strlen(buf); // 计算并返回以 NUL 字符（\0）结尾的字符串 buf 的实际长度（不包括结尾的 NUL 字符）
}

// 获取当前系统调用的第 n 个原始参数
static uint64
argraw(int n)
{
  struct proc *p = myproc(); // 获取当前进程 p 
  // 参数会按照 RISC-V 调用约定，依次存放在 trapframe 的 a0~a5 寄存器中 
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw"); // 如果 n 超出 0~5 的范围，会调用 panic("argraw") 触发内核崩溃，防止非法访问
  return -1; // 只是为了让编译器满意，实际上不会被执行
}

// Fetch the nth 32-bit system call argument.
void
argint(int n, int *ip)
{
  *ip = argraw(n);
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
// 用于将系统调用的某个参数当作指针类型提取出来
// 函数本身不会检查指针的合法性（比如是否越界、是否指向有效的用户空间）
// 因为后续的 copyin 或 copyout 函数会负责这些安全检查
void
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
// 获取第 n 个“字长”（word-sized）的系统调用参数，并将其作为以 NUL 结尾的字符串读取出来。
// 读取到的字符串会被复制到内核缓冲区 buf 中，最多复制 max 个字节，防止缓冲区溢出。
// 如果操作成功，函数返回字符串的长度（包括结尾的 NUL 字符）；如果出错，则返回 -1
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr); // 调用 argaddr 获取第 n 个参数的值，假设它是一个指向用户空间字符串的地址
  return fetchstr(addr, buf, max); // 
}

// Prototypes for the functions that handle system calls.
// 明的是用于处理系统调用的函数原型（即函数声明）
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_chdir(void);
extern uint64 sys_dup(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_uptime(void);
extern uint64 sys_open(void);
extern uint64 sys_write(void);
extern uint64 sys_mknod(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_close(void);

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
// 该数组用于将 syscall.h 中定义的系统调用编号（syscall numbers）映射到对应的系统调用处理函数
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
};

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7; // 获取系统调用号
  // 检查系统调用号是否有效
  // 1. num > 0 
  // 2. num < NELEM(syscalls) 系统调用号在 syscalls 数组内
  // 3. syscalls[num] 对应的系统调用函数指针有效
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    // 根据系统调用号调用对应的函数指针，并把返回值放入  p->trapframe->a0
    p->trapframe->a0 = syscalls[num]();
  } else { // 系统调用号无效
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1; // 把 -1 作为返回值
  }
}
