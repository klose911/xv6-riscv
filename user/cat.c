#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

char buf[512]; // 512字节的缓冲区，用于存储从文件中读取的数据

/**
 * @brief 模拟 Unix 系统中的 cat 命令
 * 
 * 从指定的文件描述符 fd 中读取数据，并将其写入标准输出（文件描述符 1）
 * 
 * @param fd 文件描述符，通常是通过 open() 函数获得的
 */
void
cat(int fd)
{
  int n;

  // 循环读取文件内容，直到文件末尾（read 返回 0）或发生错误（read 返回负值）
  while((n = read(fd, buf, sizeof(buf))) > 0) {
    if (write(1, buf, n) != n) { // 将读取到的数据写入标准输出，如果写入的字节数不等于 n，说明发生了写入错误
      fprintf(2, "cat: write error\n");
      exit(1);
    }
  }
  if(n < 0){ // 如果 read 返回负值，说明发生了读取错误
    fprintf(2, "cat: read error\n");
    exit(1);
  }
}

int
main(int argc, char *argv[])
{
  int fd, i;

  if(argc <= 1){ // 如果没有指定文件，默认从标准输入读取数据并输出（即直接调用 cat(0)）
    cat(0);
    exit(0);
  }

  for(i = 1; i < argc; i++){ // 遍历命令行参数，从第一个参数开始（argv[0] 是程序名称）
    if((fd = open(argv[i], O_RDONLY)) < 0){ // 尝试以只读方式打开文件，如果失败（返回值小于 0），则打印错误信息并退出
      fprintf(2, "cat: cannot open %s\n", argv[i]);
      exit(1);
    }
    cat(fd); // 调用 cat 函数处理打开的文件描述符
    close(fd); // 处理完文件后，关闭文件描述符，释放系统资源
  }
  exit(0); // 所有文件处理完成后，正常退出程序
}
