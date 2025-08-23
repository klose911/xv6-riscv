//
// formatted console output -- printf, panic.
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

volatile int panicked = 0; // volatile 让编译器对这个变量不会优化

// lock to avoid interleaving concurrent printf's.

/**
 * @brief printf 的锁 
 * 
 */
static struct {
  struct spinlock lock; // 自旋锁
  int locking; // 是否被持有，1表示被持有，0表示空闲 
} pr;

static char digits[] = "0123456789abcdef"; // 数字字符数组 (16进制)


/**
 * @brief 打印整形值
 * 
 * 作为 printf 的内部辅助函数，用于处理 %d、%x 等格式化输出
 * 
 * 将整数按照指定进制（如十进制、十六进制）转换为字符并输出
 * 
 * @param xx 被打印的整形值
 * @param base 进制，一般支持2, 8, 10, 16 
 * @param sign 是否带符号
 *  
 */
static void
printint(long long xx, int base, int sign)
{
  char buf[16]; // 存储转换后的字符，16字节足够存储64位整数的字符串表示
  int i;
  unsigned long long x;

  // 预处理数字正负符号
  // 处理之后 x 就是无符号整数 
  // sign代表正负 
  if(sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  // 逆序生成数字字符，存储在 buf 中 
  // 根据进制，进行取模和除法运算 
  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign) // 根据 sign 值决定是否添加负号 
    buf[i++] = '-';

  while(--i >= 0) // 逆序打印字符
    consputc(buf[i]);
}

/**
 * @brief 打印指针地址
 * 
 * 用于将一个 64 位无符号整数（通常表示指针地址）以十六进制格式输出到控制台 
 * 
 * @param x 指针地址
 * 
 */
static void
printptr(uint64 x)
{
  int i;
  consputc('0');
  consputc('x'); // 输出前缀 0x，这是 C 语言中十六进制数的标准表示方式 
  // 循环次数为 sizeof(uint64) * 2，即 16 次（因为 64 位指针每 4 位对应一个十六进制字符）
  // 每次循环最后，x 左移 4 位
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    // x >> (sizeof(uint64) * 8 - 4) 右移60位，取出当前最高的 4 位（即一个十六进制数字） 
    // 用作下标从 digits 字符数组中取出对应的字符并输出
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console.
int
printf(char *fmt, ...)
{
  va_list ap;
  int i, cx, c0, c1, c2, locking;
  char *s;

  locking = pr.locking;
  if(locking) // 等待获取pr对应的自旋锁
    acquire(&pr.lock); 

  // va_start(ap, fmt); 是 C 语言中处理可变参数函数（如 printf）时的标准宏，用于初始化可变参数列表
  // ap 是一个 va_list 类型的变量，用于存储可变参数的状态
  // fmt 是函数的最后一个固定参数，va_start 通过它确定可变参数的起始位置
  // 调用 va_start 后，可以使用 va_arg 宏依次访问每一个可变参数
  // 使用完毕后，通常还需要调用 va_end(ap); 进行清理。
  va_start(ap, fmt); // 开始处理可变参数

  // for 循环的起始部分，常见于实现 printf 这类格式化输出函数时，用于逐字符遍历格式字符串 fmt
  // i = 0：循环变量 i 从 0 开始，表示格式字符串的索引
  // (cx = fmt[i] & 0xff) != 0：每次循环将 fmt[i] 的低 8 位赋值给 cx，并判断其是否为 0
  //    这样做可以确保即使 char 是有符号类型，也能正确处理所有 ASCII 字符
  //    遇到字符串结束符（\0，即 0）时循环结束
  // i++：每次循环索引递增，依次处理格式字符串的每个字符 
  for(i = 0; (cx = fmt[i] & 0xff) != 0; i++){
    if(cx != '%'){ // 当前字符 cx 不是 %，说明这是格式字符串中的普通字符，不需要特殊处理
      consputc(cx); // 打印当前字符
      continue;
    }
    i++; // 将索引 i 向后移动一位，跳过当前的 % 字符，准备读取格式说明符的内容 
    c0 = fmt[i+0] & 0xff; // 读取当前位置的字符，并通过 & 0xff 保证只取低 8 位，防止符号扩展带来的问题
    c1 = c2 = 0; 
    if(c0) c1 = fmt[i+1] & 0xff; // 如果 c0 非零（即不是字符串结尾），则读取下一个字符到 c1
    if(c1) c2 = fmt[i+2] & 0xff; // 如果 c1 非零，再读取下一个字符到 c2
    if(c0 == 'd'){ // %d: 打印整数 
      printint(va_arg(ap, int), 10, 1); // 从可变参数列表 ap 中取出下一个参数，并将其解释为 int 类型
    } else if(c0 == 'l' && c1 == 'd'){ // %ld, 打印long 整数
      printint(va_arg(ap, uint64), 10, 1);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'd'){ // %lld 
      printint(va_arg(ap, uint64), 10, 1);
      i += 2;
    } else if(c0 == 'u'){ // %u, 打印无符号整数
      printint(va_arg(ap, int), 10, 0);
    } else if(c0 == 'l' && c1 == 'u'){ // %lu 
      printint(va_arg(ap, uint64), 10, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'u'){ // %llu 
      printint(va_arg(ap, uint64), 10, 0);
      i += 2;
    } else if(c0 == 'x'){ // %x 打印十六进制整数
      printint(va_arg(ap, int), 16, 0);
    } else if(c0 == 'l' && c1 == 'x'){ // %lx 打印十六进制long整数
      printint(va_arg(ap, uint64), 16, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'x'){ // %llx 
      printint(va_arg(ap, uint64), 16, 0);
      i += 2;
    } else if(c0 == 'p'){ // %p 打印指针地址
      printptr(va_arg(ap, uint64));
    } else if(c0 == 's'){ // %s 打印字符串
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)"; // 如果字符串指针 s 为 NULL，则使用 "(null)" 代替，防止输出崩溃
      for(; *s; s++)
        consputc(*s); // 循环遍历字符串 s，逐字符输出到控制台
    } else if(c0 == '%'){ // %% '%' 字符的转义表示
      consputc('%');
    } else if(c0 == 0){ // 格式字符串结束
      break;
    } else {
      // Print unknown % sequence to draw attention.
      // 不支持的格式，直接打印作为警告
      consputc('%');
      consputc(c0);
    }

#if 0
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
#endif
  }
  va_end(ap); // 可变参数处理结束

  if(locking) 
    release(&pr.lock); // 打印结束，释放自旋锁

  return 0;
}

void
panic(char *s)
{
  pr.locking = 0; // 释放pr的自旋锁 
  printf("panic: "); // 打印 panic 消息 
  printf("%s\n", s);
  // 冻结来自其他 CPU 的 UART 输出
  // 在多核系统中，多个 CPU 可能会同时尝试向 UART（串口）输出数据
  // 为了避免输出内容混乱或交错，通常需要在某些关键区域临时阻止（冻结）其他 CPU 的 UART 输出
  // 只允许当前 CPU 进行输出操作
  // 这样可以保证输出内容的完整性和可读性
  panicked = 1; // freeze uart output from other CPUs 
  for(;;) // 死循环，阻止系统继续运行
    ;
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr"); // 初始化 pr 自旋锁
  pr.locking = 1; // 获得 pr 自旋锁
}
