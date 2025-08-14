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
    // 当检测到输入字符是退格键（BACKSPACE）时
    // 函数会依次输出 \b（退格）、空格和再一次 \b
    // 这样做的目的是：先将光标向左移动一格（退格），然后用空格覆盖原来的字符
    // 再退格一次将光标移回原位，实现“删除”字符的效果
    // 这是终端常用的退格处理方式，这种实现方式可以让控制台正确处理退格操作，提升用户输入体验
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    // 如果输入的不是退格键，函数就直接调用 uartputc_sync(c) 输出该字符到串口终端
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

/**
 * @brief 数据写入控制台（通常是串口终端）
 * 
 * @param user_src 标记数据源是用户空间还是内核空间, 0 表示内核空间，1 表示用户空间
 * @param src 源数据的起始地址
 * @param n 要写入的字节数
 * 
 * @return int 返回实际写入的字节数 
 */
int
consolewrite(int user_src, uint64 src, int n)
{
  int i;

  for(i = 0; i < n; i++){ // 循环遍历要写入的字节数
    char c;
    // either_copyin(&c, user_src, src+i, 1) 用于从指定的源空间（用户或内核）拷贝一个字节到变量 c
    // 如果拷贝失败（返回 -1），则提前结束循环
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    uartputc(c); // 将读取的字符发送到 UART 设备进行输出
  }

  return i; // 返回实际写入的字节数
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
