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

/**
 * @brief 从控制台读取数据
 * 
 * @param user_dst 标记目标地址是用户空间还是内核空间, 0 表示内核空间，1 表示用户空间
 * @param dst 目标地址，数据将被写入到该地址
 * @param n 要读取的字节数
 * 
 * @return int 返回实际读取的字节数 
 * 
 */
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target; // 目标字节数，记录要读取的总字节数
  int c; // 用于存储从控制台读取的字符
  char cbuf; // 用于存储从控制台读取的字符，准备写入用户空间或内核空间

  target = n; // 保存初始的要读取的字节数，以便在函数结束时返回 
  acquire(&cons.lock); // 获取控制台自旋锁，确保对控制台缓冲区的独占访问
  while(n > 0){ 
    // wait until interrupt handler has put some
    // input into cons.buffer.
    // 等待控制台缓冲区有可读数据
    // 如果 cons.r 等于 cons.w，表示缓冲区为空，进入休眠
    // 当 cons.r 不等于 cons.w 时，表示有数据可读
    while(cons.r == cons.w){
      if(killed(myproc())){ // 检查当前进程是否被杀死，如果是，则释放锁并返回 -1
        release(&cons.lock); 
        return -1;
      }
      sleep(&cons.r, &cons.lock); // 休眠，等待控制台输入
    }

    // 从控制台缓冲区读取一个字符，并更新读取索引 cons.r 
    // 这里使用模运算确保索引在缓冲区大小范围内循环
    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];  


    // 这段代码用于处理控制台输入中的文件结束符（EOF），即用户按下 Ctrl+D 的情况
    if(c == C('D')){  // end-of-file 即 Ctrl+D）时，表示用户希望结束输入，相当于到达文件末尾
      if(n < target){ // 已经读取了一些数据（n < target），说明本次读取还没有消耗掉所有请求的字节数
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        // 为了保证下次读取时还能正确检测到 EOF，代码会将读取索引 cons.r 回退一位，把 Ctrl+D 留在缓冲区中
        // 这样，下一次读取操作会立即遇到 EOF，返回 0 字节，符合标准输入行为
        // 这种处理方式确保了 Ctrl+D 能正确地作为输入结束标志，并且多次读取时行为一致
        cons.r--;
      }
      break; // 直接跳出循环，结束读取
    }

    // copy the input byte to the user-space buffer.
    // 拷贝读取的字符到用户空间或内核空间
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++; // 更新目标地址，准备写入下一个字符
    --n; // 减少剩余要读取的字节数

    if(c == '\n'){ // 如果读取到换行符（\n），表示一行输入结束
      // a whole line has arrived, return to
      // the user-level read().
      break; // 跳出循环，结束读取
    }
  }
  release(&cons.lock); // 释放控制台自旋锁，允许其他线程访问控制台缓冲区

  return target - n; // 返回实际读取的字节数，即初始请求的字节数减去剩余未读取的字节数
}

//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//

// 该函数会处理输入中的删除（erase）和行清除（kill）操作
// 并将有效字符追加到控制台输入缓冲区 cons.buf 中
// 
// 如果用户输入了一整行（通常以回车或换行结束），函数会唤醒正在等待输入的 consoleread()
// 让其可以读取完整的一行数据
void
consoleintr(int c)
{
  acquire(&cons.lock); // 获取控制台自旋锁，确保对控制台缓冲区的独占访问

  switch(c){
  case C('P'):  // Print process list. 
    procdump(); // 打印当前进程列表
    break;
  case C('U'):  // Kill line. 删除行
    // 一次性删除当前输入行的所有字符，直到遇到上一行的换行符 \n 或缓冲区为空 
    // cons.e 是编辑索引，指向当前正在编辑的位置；cons.w 是写入索引，指向输入缓冲区的起始位置 
    // cons.e != cons.w 保证不会越过当前输入的起始位置，防止删除超出本行的内容 
    // cons.buf[(cons.e-1) % INPUT_BUF_SIZE] != '\n' 检查当前要删除的字符是否为换行符
    // 如果遇到换行符就停止，确保只删除本行内容
    while(cons.e != cons.w &&
          cons.buf[(cons.e-1) % INPUT_BUF_SIZE] != '\n'){
      cons.e--; // 将编辑索引向前移动一位
      consputc(BACKSPACE); // 在终端上执行退格操作，视觉上删除一个字符
    }
    break;
  case C('H'): // Backspace 
  case '\x7f': // Delete key
    // 如果用户输入了退格键或删除键，检查是否有字符可以删除
    if(cons.e != cons.w){
      cons.e--;
      consputc(BACKSPACE);
    }
    break; 
  default: // 处理控制台输入字符的常规情况，并将其存入输入缓冲区
    if(c != 0 && cons.e-cons.r < INPUT_BUF_SIZE){ // 检查输入字符 c 是否有效且缓冲区未满 
      c = (c == '\r') ? '\n' : c; // 如果输入的是回车符 \r，会被转换为换行符 \n，以统一行结束符的处理 

      // echo back to the user.
      consputc(c); // 将输入的字符回显到控制台

      // store for consumption by consoleread().
      // 字符被存入输入缓冲区 cons.buf，
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c; // 编辑索引 cons.e 递增，确保缓冲区循环利用 

      if(c == '\n' || c == C('D') || cons.e-cons.r == INPUT_BUF_SIZE){ 
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        // 如果输入的是换行符、Ctrl+D（文件结束符，C('D')），或 缓冲区已满 
        cons.w = cons.e; // 将写入索引 cons.w 更新为当前编辑索引 cons.e
        // 唤醒等待输入的进程（如 consoleread()），表示一整行输入或输入结束已经到达，可以被读取
        // 这保证了控制台输入的同步和行缓冲行为
        wakeup(&cons.r); 
      }
    }
    break;
  }
  
  release(&cons.lock); // 释放控制台自旋锁，允许其他线程访问控制台缓冲区
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
