#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

//
// wrapper so that it's OK if main() does not call exit().
//
void
start()
{
  extern int main();
  main();
  exit(0);
}

char*
strcpy(char *s, const char *t)
{
  char *os;

  os = s; // 保存原始指针，以便最后返回
  // 将字符串 t 中的字符逐个复制到 s 中，直到遇到字符串结束符 '\0'，同时 s 和 t 都向后移动一个位置
  while((*s++ = *t++) != 0) 
    ;
  return os; // 返回指向 s 的原始位置的指针，即复制后的字符串的起始地址
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q) // 当两个字符串的当前字符相等且未到达字符串末尾时，继续比较下一个字符
    p++, q++;
  return (uchar)*p - (uchar)*q; // 返回第一个不相等字符的差值，或者如果字符串相等则返回 0
}


uint
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n; // 返回字符串的长度，不包括字符串结束符 '\0'
}

void*
memset(void *dst, int c, uint n)
{
  char *cdst = (char *) dst; // 将 void* 类型的指针转换为 char* 类型，以便按字节操作
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c; // 将每个字节设置为指定的值 c
  }
  return dst; // 返回指向目标内存区域的指针

}

char*
strchr(const char *s, char c)
{
  for(; *s; s++) // 遍历字符串 s 中的每个字符，直到遇到字符串结束符 '\0'
    if(*s == c) // 如果当前字符等于 c，返回指向该字符的指针
      return (char*)s; // 注意这里需要将 const char* 转换为 char*，因为函数返回类型是 char*
  return 0; // 如果没有找到字符 c，返回 0（NULL 指针）
}

char*
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){ // 循环读取字符，直到达到最大长度 max-1 或者遇到换行符
    cc = read(0, &c, 1); // 从标准输入（文件描述符 0）读取一个字符到变量 c 中，返回实际读取的字节数
    if(cc < 1) // 如果 read 返回 0 或负数，说明输入结束或发生错误，退出循环
      break;
    buf[i++] = c; // 将读取的字符存储到缓冲区 buf 中，并将索引 i 增加 1
    if(c == '\n' || c == '\r') // 如果读取到换行符或回车符，退出循环
      break;
  }
  buf[i] = '\0'; // 在缓冲区末尾添加字符串结束符 '\0'
  return buf; // 返回指向缓冲区的指针

}

int
stat(const char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY); // 打开文件，返回文件描述符
  if(fd < 0)
    return -1;
  r = fstat(fd, st); // 获取文件状态信息，存储到 stat 结构体中
  close(fd); // 关闭文件  
  return r;
}

int
atoi(const char *s)
{
  int n;

  n = 0;
  // 遍历字符串中的每个字符，如果是数字字符，则将其转换为对应的整数值，并累加到 n 中
  while('0' <= *s && *s <= '9') 
    n = n*10 + *s++ - '0';
  return n;
}


void*
memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst; // 将 void* 类型的指针转换为 char* 类型，以便按字节操作
  src = vsrc; // 同上，转换为 char* 类型
  if (src > dst) { // 如果源地址在目标地址之后，说明内存区域没有重叠，可以直接从前往后复制
    while(n-- > 0) // 循环 n 次，每次复制一个字节
      *dst++ = *src++; // 复制当前字节后，dst 和 src 都向后移动一个位置
  } else { // 如果源地址在目标地址之前，说明内存区域可能重叠，需要从后往前复制，以避免覆盖未复制的数据
    dst += n; // 将 dst 指针移动到目标区域的末尾，即最后一个字节的下一个位置
    src += n; // 将 src 指针移动到源区域的末尾，即最后一个字节的下一个位置
    while(n-- > 0) // 循环 n 次，每次复制一个字节
      // 先将 dst 和 src 都向前移动一个位置，然后复制当前字节，这样可以确保在重叠的情况下不会覆盖未复制的数据
      *--dst = *--src; 
  }
  return vdst;
}

int
memcmp(const void *s1, const void *s2, uint n)
{
  const char *p1 = s1, *p2 = s2; // 将 void* 类型的指针转换为 char* 类型，以便按字节比较
  while (n-- > 0) { // 循环 n 次，每次比较一个字节
    if (*p1 != *p2) { // 如果当前字节不相等，返回它们的差值，通常是 s1 中的字节值减去 s2 中的字节值
      return *p1 - *p2; // 这样可以得到一个正数、负数或零，分别表示 s1 大于、s1 小于或 s1 等于 s2
    }
    p1++; // 如果当前字节相等，继续比较下一个字节，p1 和 p2 都向后移动一个位置
    p2++; 
  }
  return 0;
}

void *
memcpy(void *dst, const void *src, uint n)
{
  // 直接调用 memmove 函数来实现 memcpy 的功能，因为 memmove 已经处理了内存重叠的情况，memcpy 只需要调用它即可
  return memmove(dst, src, n); 
}
