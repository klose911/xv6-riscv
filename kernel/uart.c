//
// low-level driver routines for 16550a UART.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// the UART control registers are memory-mapped
// at address UART0. this macro returns the
// address of one of the registers.

// UART 的控制寄存器采用内存映射方式，基地址为 UART0
// 也就是说，UART 的硬件寄存器直接映射在内存的某个固定地址区间 

/**
 * @brief 定义了一个用于访问 UART 控制寄存器的宏 Reg(reg) 
 * 给定一个寄存器偏移量 reg，返回该寄存器在内存中的实际地址
 * 它通过 (UART0 + (reg)) 计算出目标寄存器的地址
 * 然后将其强制转换为 volatile unsigned char * 类型
 * 
 * @param reg 寄存器的偏移地址 
 * @return volatile unsigned char* 指向对应 UART 寄存器的指针
 * 
 * @note volatile 关键字用于告诉编译器该内存地址可能会被硬件或其他线程异步修改
 * 因此，编译器在优化代码时不会对对该地址的读写操作进行缓存或重排序，确保每次访问都是直接从内存中读取最新的值
 * 
 */
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html

// UART 控制寄存器定义 
// 有些寄存器在读写时含义不同
// 具体细节可参考 http://byterunner.com/16550.html

// 接收保持寄存器，用于读取输入字节
#define RHR 0                 // receive holding register (for input bytes) 
// 发送保持寄存器，用于写入输出字节 
// 注意：RHR 和 THR 地址相同，通过读写区分
#define THR 0                 // transmit holding register (for output bytes) 
// 中断使能寄存器，用于控制 UART 的中断功能
#define IER 1                 // interrupt enable register 
#define IER_RX_ENABLE (1<<0) // 接收中断使能位，值为 1（即第 0 位），用于使能 UART 的接收中断 
#define IER_TX_ENABLE (1<<1) // 发送中断使能位，值为 2（即第 1 位），用于使能 UART 的发送中断
// FIFO 控制寄存器
#define FCR 2                 // FIFO control register 
#define FCR_FIFO_ENABLE (1<<0) // FIFO 使能位，值为 1（即第 0 位），用于启用 UART 的 FIFO 功能
// 清除接收和发送 FIFO 的内容，值为 6（即第 1 和第 2 位都置为 1）
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs 
// 中断状态寄存器
#define ISR 2                 // interrupt status register 
// 线路控制寄存器
#define LCR 3                 // line control register 
#define LCR_EIGHT_BITS (3<<0) // 设置数据位长度为 8 位，值为 3（即第 0 和第 1 位都置为 1）
// 设置波特率的特殊模式，值为 128（即第 7 位置为 1）
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate 
// 线路状态寄存器
#define LSR 5                 // line status register 
// 接收保持寄存器 RHR 中有数据可读，值为 1（即第 0 位）
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR 
// 发送保持寄存器 THR 可以接受另一个字符进行发送，值为 32（即第 5 位）
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send 

#define ReadReg(reg) (*(Reg(reg))) // 读取指定 UART 寄存器的值
#define WriteReg(reg, v) (*(Reg(reg)) = (v)) // 向指定 UART 寄存器写入值 v

// the transmit output buffer.
struct spinlock uart_tx_lock; // 用于保护 UART 发送缓冲区的自旋锁
#define UART_TX_BUF_SIZE 32 // 定义 UART 发送缓冲区的大小为 32 字节
char uart_tx_buf[UART_TX_BUF_SIZE]; // UART 发送缓冲区
// 缓冲区读写指针
uint64 uart_tx_w; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE]
uint64 uart_tx_r; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]

extern volatile int panicked; // from printf.c 

/**
 * @brief 把缓冲区发送到 UART（串口）
 * 
 */
void uartstart();

void
uartinit(void)
{
  // disable interrupts.
  WriteReg(IER, 0x00); // 禁用所有 UART 中断

  // special mode to set baud rate.
  WriteReg(LCR, LCR_BAUD_LATCH); // 进入设置波特率的特殊模式

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03); // 设置波特率的最低有效字节（LSB）

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00); // 设置波特率的最高有效字节（MSB）

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS); // 退出设置波特率模式，设置数据位长度为 8 位，无奇偶校验

  // reset and enable FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR); // 启用 FIFO 并清空接收和发送 FIFO

  // enable transmit and receive interrupts.
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE); // 启用发送和接收中断

  initlock(&uart_tx_lock, "uart"); // 初始化用于保护 UART 发送缓冲区的自旋锁
}

// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().

// 向缓冲区添加一个字符，并在 UART 尚未发送时启动发送 
// 如果输出缓冲区已满，则阻塞等待
// 由于该函数可能会阻塞，因此不能在中断上下文中调用，只能由 write() 等非中断代码使用 
void
uartputc(int c)
{
  acquire(&uart_tx_lock); // 获取 UART 发送缓冲区的自旋锁

  //这段代码在检测到系统处于 panic 状态时，让当前执行流陷入一个空转的无限循环，从而“冻结”在当前位置
  // panicked 通常是内核级的全局标志，一旦发生致命错误被置位，后续路径（例如 UART 输出）就不再继续执行
  // 以避免进一步破坏系统状态或与其他 CPU/上下文交错输出，确保 panic 信息的可读性与一致性
  if(panicked){
    for(;;)
      ;
  }
  while(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){ // 如果发送缓冲区已满，阻塞等待
    // buffer is full.
    // wait for uartstart() to open up space in the buffer.
    sleep(&uart_tx_r, &uart_tx_lock); // 休眠，等待 uartstart() 释放缓冲区空间
  }
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c; // 将字符 c 添加到发送缓冲区
  uart_tx_w += 1; // 发送缓冲区写指针前移
  uartstart(); // 启动 UART 发送缓冲区中的数据
  release(&uart_tx_lock); // 释放 UART 发送缓冲区的自旋锁
}


// alternate version of uartputc() that doesn't 
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.

// uartputc_sync 是 uartputc 的一个替代版本
// 不使用中断机制，适用于内核的 printf() 函数和字符回显
// 该函数通过轮询等待 UART 的输出寄存器变为空闲，然后直接写入字符
void
uartputc_sync(int c)
{
  // 避免与串口中断处理例程（如 uartintr）并发访问同一发送缓冲区或硬件寄存器，从而防止竞态条件和数据破坏
  // push_off() 并不是通用的锁：它只能防止中断上下文的抢占，无法跨多核保护共享数据
  // 因此对跨 CPU 的共享结构仍需配合自旋锁（如 spinlock）
  push_off(); // 关闭中断，防止在发送过程中被打断

  if(panicked){
    for(;;)
      ;
  }

  // wait for Transmit Holding Empty to be set in LSR.
  // 轮询等待，直到 UART 的发送保持寄存器（THR）为空，可以接受新的字符
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c); // 将字符 c 写入发送保持寄存器，触发发送

  pop_off(); // 恢复中断
}

// if the UART is idle, and a character is waiting
// in the transmit buffer, send it.
// caller must hold uart_tx_lock.
// called from both the top- and bottom-half.
// 如果UART是空闲的，并且发送缓冲区中有字符等待发送，则发送它 
// 调用者必须获取uart_tx_lock自旋锁 
// 该函数既可以在中断处理中top-half（快速响应硬件事件）
// 也可以在中断处理的bottom-half（较慢的后续处理）中被调用
void
uartstart()
{
  while(1){
    if(uart_tx_w == uart_tx_r){ // 缓冲区的读指针和写指针相等，表示没有数据可发送 
      // transmit buffer is empty.
      ReadReg(ISR); // 读取 UART 的中断状态寄存器（Interrupt Status Register） 
      // 虽然这里没有对读取结果进行处理，通常这样做是为了清除某些硬件状态或中断标志，确保 UART 状态保持同步
      return;
    }
    
    // 读取 UART 的线路状态寄存器（Line Status Register），然后与 LSR_TX_IDLE 进行按位与运算
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){ 
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      // 如果结果为 0，说明发送寄存器当前已满，不能再写入新的字节
      // 此时，代码会直接 return，暂时停止发送操作
      // 当发送寄存器满时，UART 会在准备好接收新字节时通过中断通知系统，届时再继续发送
      return;
    }
    
    // 从发送缓冲区读取下一个待发送的字符 
    // 取模是为了实现缓冲区的循环利用，防止指针越界
    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]; // 从环形缓冲区 uart_tx_buf 中读取当前读指针 uart_tx_r 指向的字符
    uart_tx_r += 1; // 读指针前移 
    
    // maybe uartputc() is waiting for space in the buffer.
    // 可能有 uartputc() 正在等待缓冲区有空间可用
    // 调用 wakeup 唤醒所有等待 uart_tx_r 地址的进程
    wakeup(&uart_tx_r);
    
    // 刚刚取出的字符写入 UART 的发送保持寄存器（THR），触发硬件实际发送该字节
    WriteReg(THR, c);
  }
}

// read one input character from the UART.
// return -1 if none is waiting.
// 注意：该函数不会阻塞等待数据
int
uartgetc(void)
{
  // ReadReg(LSR) 读取 UART 的线路状态寄存器（Line Status Register）
  // 该寄存器的最低位（0x01）用于指示接收缓冲区是否有数据准备好
  if(ReadReg(LSR) & 0x01){
    // input data is ready.
    return ReadReg(RHR); // 从接收保持寄存器（RHR）读取并返回输入字符
  } else {
    return -1; // 如果没有数据可读，返回 -1
  }
}

// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from devintr().

// 当 UART 设备产生中断时（可能是因为有新数据到达，或者发送缓冲区可以继续发送数据），系统会调用该函数进行响应
// 该函数通常由设备中断分发函数 devintr() 调用
void
uartintr(void)
{
  // read and process incoming characters.
  // 循环读取和处理UART 接收到的字符
  while(1){
    int c = uartgetc(); // 从 UART 读取一个输入字符
    if(c == -1) // 如果没有新字符可读，跳出循环
      break;
    consoleintr(c); // 通常是将字符传递给控制台输入处理逻辑，比如命令行或终端
  }

  // send buffered characters.
  // 发送缓冲区中的字符
  // 注意：需要给缓存区加锁，以避免不同线程同时访问引发数据竞争
  acquire(&uart_tx_lock);
  uartstart(); // 调用 uartstart() 发送缓冲区中的字符
  release(&uart_tx_lock);
}
