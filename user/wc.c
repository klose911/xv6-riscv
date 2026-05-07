#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

char buf[512];

/**
 * @brief 统计文件中的行数、单词数和字符数，并打印结果
 * 
 * @param fd 文件描述符，指向要统计的文件 
 * @param name 文件名，用于输出结果时显示
 */
void
wc(int fd, char *name)
{
  int i, n;
  int l, w, c, inword;

  l = w = c = 0;
  inword = 0;
  while((n = read(fd, buf, sizeof(buf))) > 0){ // 从文件中读取数据到缓冲区，返回实际读取的字节数
    for(i=0; i<n; i++){ // 遍历缓冲区中的每个字符，进行统计
      c++; // 字符数增加1
      if(buf[i] == '\n') // 如果当前字符是换行符，行数增加1
        l++;
      if(strchr(" \r\t\n\v", buf[i])) // 如果当前字符是空格、回车、制表符、换行符或垂直制表符，说明单词结束
        inword = 0; // 将 inword 置为0，表示当前不在单词中
      else if(!inword){
        w++; // 单词数增加1
        inword = 1; // 将 inword 置为1，表示当前在单词中
      }
    }
  }
  if(n < 0){ // 如果 read 返回负数，说明读取过程中发生错误
    printf("wc: read error\n");
    exit(1);
  }
  printf("%d %d %d %s\n", l, w, c, name); // 输出行数、单词数、字符数和文件名，格式为 "行数 单词数 字符数 文件名"
}

int
main(int argc, char *argv[])
{
  int fd, i;

  if(argc <= 1){
    wc(0, "");
    exit(0);
  }

  for(i = 1; i < argc; i++){
    if((fd = open(argv[i], O_RDONLY)) < 0){
      printf("wc: cannot open %s\n", argv[i]);
      exit(1);
    }
    wc(fd, argv[i]);
    close(fd);
  }
  exit(0);
}
