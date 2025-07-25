OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  /*
   * ensure that entry.S / _entry is at 0x80000000,
   * where qemu's -kernel jumps.
   */
  . = 0x80000000;
  /*
   * 这段链接脚本定义了内核镜像中 `.text` 段的布局和内容。`.text` 段通常用于存放程序的可执行代码
   * 整体来看，这段脚本精确控制了代码段和 trampoline 的位置与大小，确保内核启动和内存管理的正确性
   */
  .text : {
  
    *(.text .text.*) /* 将所有输入目标文件中的 `.text` 和以 `.text.` 开头的段（如 `.text.startup`、`.text.init` 等）全部合并到输出文件的 `.text` 段中 */
    . = ALIGN(0x1000); /* 将当前位置对齐到 0x1000（4KB）边界，这通常是分页的大小，有助于内存管理和性能优化。*/
    _trampoline = .; /* 定义了一个符号 `_trampoline`，它记录了当前地址*/
    *(trampsec) /* 把所有属于 `trampsec` 段的内容放到这里，通常这些内容用于实现内核与用户态之间的切换（如上下文切换或中断处理）*/
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page"); /* 这条断言确保 trampoline 段的大小正好为 4KB。如果超出，将在链接时报错，防止 trampoline 代码过大影响内存布局*/
    PROVIDE(etext = .); /* 定义了符号 `etext`，表示 `.text` 段的结束地址，方便后续代码引用 */
  }

  /*
   * 这段链接脚本定义了内核镜像中 `.rodata`、`.data` 和 `.bss` 段的布局和内容
   * `.rodata` 段通常用于存放只读数据，如字符串常量和全局常量
   * `.data` 段用于存放已初始化的全局变量和静态变量
   * `.bss` 段用于存放未初始化的全局变量和静态变量
  */
  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(end = .); 
}
