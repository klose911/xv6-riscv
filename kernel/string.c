#include "types.h"

void*
memset(void *dst, int c, uint n)
{
  char *cdst = (char *) dst;
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  return dst;
}

int
memcmp(const void *v1, const void *v2, uint n)
{
  const uchar *s1, *s2;

  s1 = v1;
  s2 = v2;
  while(n-- > 0){
    if(*s1 != *s2)
      return *s1 - *s2;
    s1++, s2++;
  }

  return 0;
}

void*
memmove(void *dst, const void *src, uint n)
{
  // 以便按字节操作
  const char *s; 
  char *d;

  if(n == 0) // 无需复制，直接返回
    return dst;
  
  s = src;
  d = dst;
  if(s < d && s + n > d){ // 内存区域重叠，则为避免数据覆盖，从后往前复制
    // 各自移动到内存区域的末尾
    s += n; 
    d += n;
    while(n-- > 0)
      *--d = *--s; // 从后往前依次拷贝
  } else // 无重叠，则从前往后依次拷贝
    while(n-- > 0)
      *d++ = *s++;

  return dst;
}

// memcpy exists to placate GCC.  Use memmove.
void*
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

int
strncmp(const char *p, const char *q, uint n)
{
  while(n > 0 && *p && *p == *q)
    n--, p++, q++;
  if(n == 0)
    return 0;
  return (uchar)*p - (uchar)*q;
}

char*
strncpy(char *s, const char *t, int n)
{
  char *os;

  os = s;
  while(n-- > 0 && (*s++ = *t++) != 0)
    ;
  while(n-- > 0)
    *s++ = 0;
  return os;
}

// Like strncpy but guaranteed to NUL-terminate.
// 功能类似于 strncpy，但有一个重要区别：它始终保证目标字符串以 NUL 字符（\0）结尾
char*
safestrcpy(char *s, const char *t, int n)
{
  char *os;

  os = s;
  if(n <= 0) // n <= 0，直接返回目标指针，不进行任何复制
    return os;
  // --n > 0 并且当前字符不为 NUL（即还没到字符串结尾）
  // 这样最多复制 n-1 个字符，预留一个位置给结尾的 NUL 字符
  while(--n > 0 && (*s++ = *t++) != 0)
    ;
  *s = 0; // 无论源字符串是否被完全复制，都会在目标字符串末尾补上 *s = 0;，确保其以 NUL 结尾
  return os;
}

int
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

