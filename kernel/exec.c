#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

/**
 * @brief 进程加载可执行文件（如 ELF 文件）时，将文件中的一段内容读取到指定的虚拟内存地址，并建立相应的内存映射
 * 
 * @param pagetable 页表指针，用于指定目标进程的页表结构
 * @param va 虚拟地址，表示要加载到内存的目标地址
 * @param ip 指向可执行文件的 inode 结构体指针 
 * @param offset 文件偏移量，表示从文件的哪个位置开始读取
 * @param sz 要读取的数据字节数
 * 
 * @return int 0 表示成功，非 0 表示出错
 */
static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

/**
 * @brief 将 ELF 文件段的权限标志转换为页表项（PTE）的权限标志
 * 
 * @param flags ELF 文件段的权限标志
 * 
 * @return int 对应的页表项权限标志
 */
int flags2perm(int flags)
{
  // 注意这里没有处理只读权限，通常只读页表项默认不设置写或执行权限
    int perm = 0;
    if(flags & 0x1) 
      perm = PTE_X;
    if(flags & 0x2)
      perm |= PTE_W;
    return perm;
}

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc(); // 获取当前进程

  begin_op(); // 每次文件 system call 前调用 begin_op，结束时调用 end_op

  if((ip = namei(path)) == 0){ // 根据路径名查找对应的 inode 结构体指针
    end_op(); // 无法查找到inode，返回 -1, 结束调用
    return -1;
  }

  // 确保在后续对该 inode 进行读写操作时，不会被其他进程或内核线程同时访问，从而避免数据竞争和一致性问题
  // 加锁后，只有当前持有锁的上下文可以安全地操作该 inode，直到显式解锁
  ilock(ip); // 对指定的 inode 结构体（ip）加锁

  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf)) // 读取 ELF 头部信息，判断是否成功读取完整头部
    goto bad;

  if(elf.magic != ELF_MAGIC) // 检查 ELF 魔数，验证文件格式是否正确
    goto bad;

  if((pagetable = proc_pagetable(p)) == 0) // 创建新的页表，用于加载程序
    goto bad;

  // Load program into memory.
  // 把ELF每段加载到内存里
  // 遍历 ELF 文件的所有程序头（program header），每次循环处理一个段描述符
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    // 从 inode 指向的 ELF 文件中读取一个程序头到本地变量 ph
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph)) // 判断是否成功读取完整的程序头
      goto bad; 
    if(ph.type != ELF_PROG_LOAD) // 只处理类型为 ELF_PROG_LOAD 的段（即需要加载到内存的段），其他类型跳过
      continue;
    if(ph.memsz < ph.filesz) // 段在内存中的大小不能小于在文件中的大小，ELF文件损坏
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr) // 段的虚拟地址加上大小发生溢出
      goto bad;
    if(ph.vaddr % PGSIZE != 0) // 段的虚拟地址必须是页对齐的
      goto bad;
    uint64 sz1;
    // 为该段分配所需的虚拟内存空间，并设置相应的权限
    // 从当前内存上限 sz 扩展到该段结束地址 ph.vaddr + ph.memsz
    // 并根据段的权限标志 ph.flags 设置页表项权限（如可执行、可写）
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0) 
      goto bad; // 分配失败，跳转到错误处理
    sz = sz1; // 更新当前进程的内存空间上限、
    // 将段内容从文件加载到分配好的内存中
    // 把 ELF 文件中从偏移量 ph.off 开始、长度为 ph.filesz 字节的数据
    // 读取到页表 pagetable 的虚拟地址 ph.vaddr 处
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad; // 加载失败
  }
  iunlockput(ip); // 解锁 inode 
  end_op(); 
  ip = 0; // inode清空

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate some pages at the next page boundary.
  // Make the first inaccessible as a stack guard.
  // Use the rest as the user stack.
  // 为新进程分配用户栈空间，并设置栈保护页（stack guard page），以提升内存安全性
  sz = PGROUNDUP(sz); // 将当前内存上限 sz 向上对齐到页边界，确保后续分配从完整的页开始
  uint64 sz1; // 临时变量，用于存储分配后的新大小
  // 在页表 pagetable 中，从 sz 开始分配 USERSTACK+1 页的空间（其中一页作为保护页），并设置为可写（PTE_W）
  if((sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK+1)*PGSIZE, PTE_W)) == 0)
    goto bad;
  sz = sz1; // 更新内存上限 sz
  // 将分配的第一个页面（最靠近低地址的那一页）设置为用户不可访问，作为栈保护页，防止栈溢出直接破坏其他内存区域
  uvmclear(pagetable, sz-(USERSTACK+1)*PGSIZE);
  sp = sz; // 设置用户栈顶指针（stack pointer），指向分配空间的高地址端
  stackbase = sp - USERSTACK*PGSIZE; // 计算用户栈的基地址（低地址端），用于后续栈空间管理

  // Push argument strings, prepare rest of stack in ustack.
  // 把参数字符串压入用户栈，并准备好其余的栈空间
  for(argc = 0; argv[argc]; argc++) { // 遍历所有参数字符串，直到遇到空指针为止
    if(argc >= MAXARG) // 参数个数超过 MAXARG
      goto bad; // 跳转到错误处理，防止参数过多导致栈溢出
    sp -= strlen(argv[argc]) + 1; // 为参数字符串分配栈空间，包含字符串结束符 '\0'
    sp -= sp % 16; // riscv sp must be 16-byte aligned 满足 RISC-V 架构对栈指针对齐的要求
    if(sp < stackbase) // sp 是否越过了栈底 stackbase
      goto bad; // 越界则跳转到错误处理，防止非法内存访问
    // 将 argv[argc] 指向的参数字符串（包括结尾的 \0）从内核缓冲区复制到用户空间的虚拟地址 sp 处
    // 虚拟地址使用指定的页表 pagetable 进行地址转换和权限检查
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad; // 复制失败
    ustack[argc] = sp; // 保存参数字符串在用户栈中的地址
  }
  ustack[argc] = 0; // ustack数组最后一个元素设置为NULL

  // push the array of argv[] pointers.
  // 将参数指针数组（即 argv[] 指针数组）压入新进程的用户栈，为进程启动时的参数传递做准备
  // 为 argc+1 个指针（每个指针 8 字节，64 位系统）在栈上分配空间
  // argc 是参数个数，+1 是为了最后的空指针结尾，符合 C 语言 argv 规范
  sp -= (argc+1) * sizeof(uint64); 
  sp -= sp % 16; // riscv sp must be 16-byte aligned 满足 RISC-V 架构对栈指针对齐的要求
  if(sp < stackbase) // 检查栈指针是否越过栈底
    goto bad;
  // 把内核中的 ustack 数组（保存了所有参数字符串在用户栈中的地址）复制到用户空间的栈顶位置 sp
  // 总共复制 argc+1 个指针（每个指针 8 字节）
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  // argc（参数个数）会作为系统调用的返回值，存放在寄存器 a0，这是 RISC-V 调用约定

  // 将用户栈上 argv 指针数组的地址（即 sp，栈顶指针）存入进程 trapframe 的 a1 寄存器
  // 用户程序启动时，main(int argc, char *argv[]) 的 argv 参数会自动获得正确的地址，可以访问所有命令行参数
  p->trapframe->a1 = sp; 

  // Save program name for debugging.
  // 保存当前进程正在执行的程序名，方便调试和诊断

  // 遍历程序路径字符串 path，每当遇到字符 '/' 时，就把 last 指向下一个字符
  // 最终，last 会指向路径中最后一个斜杠后的第一个字符，也就是程序的文件名部分
  for(last=s=path; *s; s++) 
    if(*s == '/')
      last = s+1;
  // 将提取到的程序名安全地复制到进程结构体 p 的 name 字段中，长度不超过 p->name 的最大容量
  safestrcpy(p->name, last, sizeof(p->name));
    
  // Commit to the user image.
  // 加载新程序的最后提交阶段，主要作用是让新用户程序正式生效

  oldpagetable = p->pagetable; // 保存当前进程原有的页表指针，便于后续释放
  p->pagetable = pagetable; // 将进程的页表切换为新加载程序的页表，使新程序的内存布局生效
  p->sz = sz; // 更新进程的内存空间大小为新程序的大小
  // 设置用户程序的初始入口地址（即 main 函数的地址），让进程从新程序的入口开始执行
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer 设置用户栈顶指针，确保新程序启动时栈空间正确
  proc_freepagetable(oldpagetable, oldsz); // 释放原有的页表和相关内存，避免内存泄漏

  // 返回参数个数 argc，它会被传递到用户程序的 main 函数（通过寄存器 a0），作为参数数量
  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad: // 错误处理
  if(pagetable) 
    proc_freepagetable(pagetable, sz); // 释放分配的内存分页表
  if(ip){
    iunlockput(ip); // 释放inode的锁
    end_op(); // 释放log锁
  }
  return -1;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.

// 将程序段（如 ELF 文件中的代码或数据段）加载到指定页表 pagetable 的虚拟地址 va 处。
// va 必须是页对齐的（即 va 是页大小的整数倍）
// 并且从 va 到 va+sz 范围内的所有虚拟页必须已经在页表中建立了映射（即这些虚拟地址已经分配了物理内存）
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  for(i = 0; i < sz; i += PGSIZE){ // 按页为单位遍历要加载的整个段（sz 字节），每次处理一页 
    pa = walkaddr(pagetable, va + i); // 查找虚拟地址 va + i 在页表中的物理地址
    if(pa == 0) // 查找失败，内核奔溃
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE) // 如果剩余字节不足一页，只读取剩余部分
      n = sz - i;
    else
      n = PGSIZE;
    // 从 inode 指向的文件（通常是 ELF 可执行文件）中读取 n 字节数据，写入物理地址 pa 对应的内存
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n) // 读取到的字符数目不等于n，说明读取失败
      return -1;
  }
  
  return 0;
}
