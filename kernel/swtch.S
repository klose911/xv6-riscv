# Context switch
#
#   void swtch(struct context *old, struct context *new);
# 
# Save current registers in old. Load from new.	


.globl swtch
swtch:
        # 将当前寄存器（ra、sp、s0-s11）的值保存到 a0 指向的内存区域（即旧的 context 结构体）中
        # 这样可以在将来恢复当前线程/进程的执行状态
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        # 从 a1 指向的内存区域（即新的 context 结构体）中加载寄存器值，恢复另一个线程/进程的执行状态
        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
        
        # 此时 CPU 的寄存器已经切换为新线程/进程的上下文，执行流也会跳转到新 context 保存的返回地址
        ret 

	
