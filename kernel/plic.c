#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
plicinit(void)
{
  // set desired IRQ priorities non-zero (otherwise disabled).

  // 分别将 UART0 和 VIRTIO0 设备的中断优先级设置为 1
  // 优先级必须为非零值，否则该中断会被禁用
  // 确保这两个设备的中断能够被 PLIC 正常识别和处理
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
}

void
plicinithart(void)
{
  int hart = cpuid(); // 获取当前硬件线程的 ID

  // set enable bits for this hart's S-mode
  // for the uart and virtio disk.
  // 设置当前 hart 的 S 模式（Supervisor mode）中断使能位
  // 使 UART0 和 VIRTIO0 设备的中断能够被该 hart 接收和处理
  // 通过位运算将这两个设备的中断源打开。
  *(uint32*)PLIC_SENABLE(hart) = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);

  // set this hart's S-mode priority threshold to 0.
  // 设置当前 hart 的 S 模式中断优先级阈值为 0，表示所有优先级大于 0 的中断都可以被响应
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
