        #
        # interrupts and exceptions while in supervisor
        # mode come here.
        #
        # the current stack is a kernel stack.
        # push registers, call kerneltrap().
        # when kerneltrap() returns, restore registers, return.
        #
        # 在监管模式下处理到来的中断和异常
        # 
        # 现在的栈处于内核栈，寄存器入栈，调用 kerneltrap() 
        # 当kerneltrap() 返回时，恢复寄存器，返回 
.globl kerneltrap # 声明kerneltrap函数（内核陷阱处理程序）
.globl kernelvec # 声明 kernelvec 函数（内核陷阱向量入口）
.align 4 # 对齐到4字节边界
kernelvec:
        # make room to save registers.
        addi sp, sp, -256 # 栈指针减少256，为保存寄存器分配空间

        # save caller-saved registers.
        # 保存 调用者保存的寄存器
        sd ra, 0(sp) # 返回地址 (return address)
        sd sp, 8(sp) # 栈指针 (stack pointer)
        sd gp, 16(sp) # 全局指针 (global pointer)
        sd tp, 24(sp) # 线程指针 (thread pointer)
        sd t0, 32(sp) # 临时寄存器 (caller-saved) t0
        sd t1, 40(sp) # 临时寄存器 (caller-saved) t1
        sd t2, 48(sp) # 临时寄存器 (caller-saved) t2
        sd a0, 72(sp) # 参数寄存器/返回值 a0
        sd a1, 80(sp) # 参数寄存器/返回值 a1
        sd a2, 88(sp) # 参数寄存器 a2
        sd a3, 96(sp) # 参数寄存器 a3
        sd a4, 104(sp) # 参数寄存器 a4
        sd a5, 112(sp) # 参数寄存器 a5
        sd a6, 120(sp) # 参数寄存器 a6
        sd a7, 128(sp) # 参数寄存器 a7
        sd t3, 216(sp) # 临时寄存器 (caller-saved) t3
        sd t4, 224(sp) # 临时寄存器 (caller-saved) t4
        sd t5, 232(sp) # 临时寄存器 (caller-saved) t5
        sd t6, 240(sp) # 临时寄存器 (caller-saved) t6

        # call the C trap handler in trap.c
        call kerneltrap # 调用 C 语言编写的陷阱处理程序 kerneltrap 

        # restore registers.
        # 此前保存的值出栈，恢复寄存器
        ld ra, 0(sp)
        ld sp, 8(sp)
        ld gp, 16(sp)
        # not tp (contains hartid), in case we moved CPUs
        # tp 无需恢复，因为他保存的 hartid 是当前 CPU 的标识符
        ld t0, 32(sp)
        ld t1, 40(sp)
        ld t2, 48(sp)
        ld a0, 72(sp)
        ld a1, 80(sp)
        ld a2, 88(sp)
        ld a3, 96(sp)
        ld a4, 104(sp)
        ld a5, 112(sp)
        ld a6, 120(sp)
        ld a7, 128(sp)
        ld t3, 216(sp)
        ld t4, 224(sp)
        ld t5, 232(sp)
        ld t6, 240(sp)

        addi sp, sp, 256

        # return to whatever we were doing in the kernel.
        # sret 是 RISC-V 架构中的一条特权指令，全称为 “Supervisor Return”
        # 它用于从陷入（trap）到 supervisor（S）模式的异常或中断处理程序中返回，恢复到原先的执行上下文
        # 具体来说，sret 会将 sepc（Supervisor Exception Program Counter）寄存器中的值加载到 pc（程序计数器）
        # 从而跳转回异常或中断发生时保存的指令位置
        # 同时，它还会根据 sstatus（Supervisor Status Register）中的相关位，恢复之前的特权级和中断使能状态
        sret
