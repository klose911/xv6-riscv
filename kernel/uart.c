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
struct spinlock uart_tx_lock;
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];
uint64 uart_tx_w; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE]
uint64 uart_tx_r; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]

extern volatile int panicked; // from printf.c

void uartstart();

void
uartinit(void)
{
  // disable interrupts.
  WriteReg(IER, 0x00);

  // special mode to set baud rate.
  WriteReg(LCR, LCR_BAUD_LATCH);

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS);

  // reset and enable FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // enable transmit and receive interrupts.
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  initlock(&uart_tx_lock, "uart");
}

// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void
uartputc(int c)
{
  acquire(&uart_tx_lock);

  if(panicked){
    for(;;)
      ;
  }
  while(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){
    // buffer is full.
    // wait for uartstart() to open up space in the buffer.
    sleep(&uart_tx_r, &uart_tx_lock);
  }
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
  uart_tx_w += 1;
  uartstart();
  release(&uart_tx_lock);
}


// alternate version of uartputc() that doesn't 
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void
uartputc_sync(int c)
{
  push_off();

  if(panicked){
    for(;;)
      ;
  }

  // wait for Transmit Holding Empty to be set in LSR.
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  pop_off();
}

// if the UART is idle, and a character is waiting
// in the transmit buffer, send it.
// caller must hold uart_tx_lock.
// called from both the top- and bottom-half.
void
uartstart()
{
  while(1){
    if(uart_tx_w == uart_tx_r){
      // transmit buffer is empty.
      ReadReg(ISR);
      return;
    }
    
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      return;
    }
    
    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1;
    
    // maybe uartputc() is waiting for space in the buffer.
    wakeup(&uart_tx_r);
    
    WriteReg(THR, c);
  }
}

// read one input character from the UART.
// return -1 if none is waiting.
int
uartgetc(void)
{
  if(ReadReg(LSR) & 0x01){
    // input data is ready.
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from devintr().
void
uartintr(void)
{
  // read and process incoming characters.
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    consoleintr(c);
  }

  // send buffered characters.
  acquire(&uart_tx_lock);
  uartstart();
  release(&uart_tx_lock);
}
