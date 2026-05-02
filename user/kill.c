#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char **argv)
{
  int i;

  if(argc < 2){ // 如果没有指定要杀死的进程 ID，打印使用说明并退出
    fprintf(2, "usage: kill pid...\n");
    exit(1);
  }
  for(i=1; i<argc; i++) // 从第一个参数开始遍历命令行参数，argv[0] 是程序名称
    // 将参数字符串转换为整数，作为要杀死的进程 ID，调用 kill 函数发送信号杀死该进程
    kill(atoi(argv[i]));
  exit(0);
}
