//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h> // 函数可变参数

#include "types.h"
#include "param.h"
#include "spinlock.h" // 自旋锁
#include "sleeplock.h" // 互斥锁
#include "fs.h" // 文件系统相关数据结构
#include "file.h" // 文件相关数据结构
#include "memlayout.h"
#include "riscv.h"
#include "defs.h" // 系统调用和其他内核函数的声明
#include "proc.h" // 进程相关数据结构

/**
 * @brief 用于表示退格键（Backspace）值为 0x100
 * 
 * 通常在控制台输入处理中，用于删除前一个字符。使用一个大于常规 ASCII 范围的值可以避免与普通字符混淆
 * 
 */
#define BACKSPACE 0x100

/**
 * @brief 将字符 x 转换为对应的控制字符的宏
 * 例如，C('C') 会得到 3，因为 'C' - '@' = 67 - 64 = 3
 * 
 * @param x 要转换的字符
 * @return int 转换后的控制字符值 
 * 
 * 这种转换方式常用于识别键盘上的 Ctrl+键组合（如 Ctrl+C、Ctrl+D），便于在控制台或终端程序中处理各种控制命令
 * 
 */
#define C(x)  ((x)-'@')  // Control-x

//
// send one character to the uart.
// called by printf(), and to echo input characters,
// but not from write().
//
void
consputc(int c)
{
  if(c == BACKSPACE){
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

/**
 * @brief 匿名结构体变量 cons，用于管理控制台输入缓冲区及其相关状态
 * 
 * 能够高效地管理控制台输入，实现字符的存储、读取和编辑功能
 * 是操作系统或终端程序实现输入处理的基础
 * 
 */
struct {
  struct spinlock lock; // 自旋锁，用于保护控制台缓冲区的并发访问，确保多线程或多核环境下的数据一致性。
  
  // input
#define INPUT_BUF_SIZE 128 // 输入缓冲区大小，定义为 128 字节
  char buf[INPUT_BUF_SIZE]; // 输入缓冲区，用于存储从控制台输入的字符
  uint r;  // Read index 读取索引，指向下一个要读取的字符位置
  uint w;  // Write index 写入索引，指向下一个要写入的字符位置
  uint e;  // Edit index 编辑索引，指向当前正在编辑的字符位置
} cons;

//
// user write()s to the console go here.
//
int
consolewrite(int user_src, uint64 src, int n)
{
  int i;

  for(i = 0; i < n; i++){
    char c;
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    uartputc(c);
  }

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while(cons.r == cons.w){
      if(killed(myproc())){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if(c == C('D')){  // end-of-file
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void
consoleintr(int c)
{
  acquire(&cons.lock);

  switch(c){
  case C('P'):  // Print process list.
    procdump();
    break;
  case C('U'):  // Kill line.
    while(cons.e != cons.w &&
          cons.buf[(cons.e-1) % INPUT_BUF_SIZE] != '\n'){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if(cons.e != cons.w){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if(c != 0 && cons.e-cons.r < INPUT_BUF_SIZE){
      c = (c == '\r') ? '\n' : c;

      // echo back to the user.
      consputc(c);

      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      if(c == '\n' || c == C('D') || cons.e-cons.r == INPUT_BUF_SIZE){
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }
  
  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons"); // 初始化console相关的自旋锁，保护console的缓存区

  uartinit(); // 初始化UART硬件，设置波特率、数据格式等 

  // connect read and write system calls
  // to consoleread and consolewrite.
  // 设置console设备的读写函数指针 （devsw数组中索引为CONSOLE的元素） 
  devsw[CONSOLE].read = consoleread; 
  devsw[CONSOLE].write = consolewrite;
}
