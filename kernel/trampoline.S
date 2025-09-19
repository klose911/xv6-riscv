        #
        # low-level code to handle traps from user space into
        # the kernel, and returns from kernel to user.
        #
        # the kernel maps the page holding this code
        # at the same virtual address (TRAMPOLINE)
        # in user and kernel space so that it continues
        # to work when it switches page tables.
        # kernel.ld causes this code to start at 
        # a page boundary.
        #

#include "riscv.h"
#include "memlayout.h"

.section trampsec # 将后续代码放入名为 trampsec 的自定义段（section）
# 将 trampoline、usertrap 和 uservec 声明为全局符号，使它们可以被其他文件（如 C 代码或链接脚本）引用 
.globl trampoline  # trampoline 代码用于在用户态和内核态之间安全切换页表和上下文
.globl usertrap # usertrap 处理从用户态进入内核态的陷阱

trampoline: 
.align 4 # 将接下来的代码按 16 字节 对齐，满足 RISC-V 页表和陷入处理的对齐要求
.globl uservec # uservec 处理从用户态进入内核态的陷阱

uservec:    
	#
        # trap.c sets stvec to point here, so
        # traps from user space start here,
        # in supervisor mode, but with a
        # user page table.
        
        # trap.c 会将 stvec 寄存器设置为该地址
        # 使得用户态陷入（如系统调用或异常）时，CPU 能跳转到这里执行陷入处理流程

        # save user a0 in sscratch so
        # a0 can be used to get at TRAPFRAME.

        # csrw 是 “Control and Status Register Write” 的缩写，用于将一个通用寄存器的值写入指定的 CSR
        # sscratch 是 supervisor 模式下的临时寄存器
        # 常用于陷入（trap）处理过程中保存和传递关键数据，比如内核栈指针或进程控制块指针
        # a0 是一个通用寄存器，通常在陷入切换或上下文切换时，
        # 内核会把需要后续 trap handler 使用的数据（如内核栈地址）写入 sscratch，方便 trap handler 直接读取
        csrw sscratch, a0 # 将用户态的 a0 寄存器值保存到 sscratch 寄存器中
                          # 以便后续代码可以使用 a0 寄存器来访问 TRAPFRAME

        # each process has a separate p->trapframe memory area,
        # but it's mapped to the same virtual address
        # (TRAPFRAME) in every process's user page table.

        # 每个进程都有独立的 p->trapframe 内存区域（用于保存该进程的用户态寄存器等陷入上下文）
        # 但在每个进程的用户页表中，这块物理内存都被映射到相同的虚拟地址 TRAPFRAME
        li a0, TRAPFRAME # 将 TRAPFRAME 的地址加载到 a0 寄存器中
                          # 以便后续代码可以通过 a0 访问当前进程的陷入上下文
        
        # save the user registers in TRAPFRAME
        # 把进程相关的用户态寄存器保存到 TRAPFRAME 中 
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# save the user a0 in p->trapframe->a0

        # 即把用户态 a0 的值保存到 p->trapframe->a0
        # 在 xv6 的 trap 处理流程中，sscratch 通常保存了用户态 a0 的值
        csrr t0, sscratch # 从 sscratch 寄存器中读取用户态的 a0 值到 t0 寄存器
        sd t0, 112(a0) # 将用户态的 a0 值保存到 TRAPFRAME 中的 a0 字段

        # initialize kernel stack pointer, from p->trapframe->kernel_sp
        # 从当前进程的 trapframe 结构体中的 kernel_sp 字段加载内核栈指针
        # 偏移 8 字节正好是 kernel_sp 字段的位置
        ld sp, 8(a0) # 从以 a0 为基址、偏移 8 字节的位置读取 64 位数据，并将其加载到 sp（栈指针寄存器）中

        # make tp hold the current hartid, from p->trapframe->kernel_hartid
        # 偏移 32 字节正好是 kernel_hartid 字段的位置
        ld tp, 32(a0) # 从以 a0 为基址、偏移 32 字节的位置读取 64 位数据，并将其加载到 tp（线程指针寄存器）中

        # load the address of usertrap(), from p->trapframe->kernel_trap
        # 偏移 16 字节正好是 kernel_trap 字段的位置
        ld t0, 16(a0) # 从以 a0 为基址、偏移 16 字节的位置读取 64 位数据，并将其加载到 t0 寄存器中
                          # t0 现在保存了 usertrap() 函数的地址，后续会跳转到该地址执行

        # fetch the kernel page table address, from p->trapframe->kernel_satp.
        # 偏移 24 字节正好是 kernel_satp 字段的位置
        ld t1, 0(a0) # 从以 a0 为基址、偏移 0 字节的位置读取 64 位数据，并将其加载到 t1 寄存器中
                          # t1 现在保存了内核页表的 satp 值，后续会用它来切换到内核页表

        # wait for any previous memory operations to complete, so that
        # they use the user page table.

        # 确保之前的内存操作（如保存寄存器到 TRAPFRAME）都完成
        # 以防止内存操作使用错误的页表
        sfence.vma zero, zero # sfence.vma 是 RISC-V 指令，用于刷新虚拟内存的地址转换缓存（TLB）

        # install the kernel page table.
        csrw satp, t1 # 将内核页表的 satp 值写入 satp 寄存器，切换到内核页表

        # flush now-stale user entries from the TLB.
        sfence.vma zero, zero # 再次刷新 TLB，确保切换页表后不会使用过时的用户页表条目

        # jump to usertrap(), which does not return
        # usertrap() 处理完陷入后不会返回到这里
        jr t0 # 跳转到 usertrap() 函数地址，开始执行陷入处理流程
                          

.globl userret # 声明 userret 函数，使其可以被其他文件引用
# userret 负责从内核态返回到用户态 void userret(uint64 pagetable)
userret: 
        # userret(pagetable) 
        # called by usertrapret() in trap.c to
        # switch from kernel to user.
        # a0: user page table, for satp.

        # 由 trap.c 文件中的 usertrapret() 调用，用于完成从内核返回用户空间的最后步骤
        # 切换过程中，a0 寄存器传递用户页表的物理地址
        # 这个地址会被写入 satp 寄存器，从而激活用户进程的虚拟内存映射

        # 切换到用户级的页表
        # switch to the user page table.
        sfence.vma zero, zero # 确保之前的内存操作都完成，防止使用错误的页表 
        csrw satp, a0 # 将用户页表的物理地址写入 satp 寄存器，切换到用户页表
        sfence.vma zero, zero # 刷新 TLB，确保不会使用过时的内存映射条目

        li a0, TRAPFRAME # 将 TRAPFRAME 的地址加载到 a0 寄存器中
                          # 以便后续代码可以通过 a0 访问当前进程的陷入上下文

        # restore all but a0 from TRAPFRAME 
        # 从 TRAPFRAME 中恢复除 a0 之外的所有用户态寄存器
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	# restore user a0
        ld a0, 112(a0) # 最后才恢复 a0, 因为前面一直在用它
        
        # return to user mode and user pc.
        # usertrapret() set up sstatus and sepc.

        # usertrapret() 已经设置好了 sstatus（特权级和中断状态）和 sepc（异常返回地址）
        # sret 指令是 RISC-V 架构下的“Supervisor Return”
        # 它会将 sepc 的值加载到程序计数器（pc），并根据 sstatus 恢复特权级和中断状态
        # 从而让 CPU 跳转回用户态并继续执行被中断的用户程序
        sret # 使用 sret 指令从内核态返回用户态，恢复用户程序的执行
