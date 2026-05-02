#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;

  for(i = 1; i < argc; i++){ // 从第一个参数开始遍历命令行参数，argv[0] 是程序名称
    write(1, argv[i], strlen(argv[i])); // 将参数字符串写入标准输出（文件描述符 1）
    if(i + 1 < argc){
      write(1, " ", 1); // 如果不是最后一个参数，写入空格分隔
    } else {
      write(1, "\n", 1); // 如果是最后一个参数，写入换行符
    }
  }
  exit(0); // 正常退出程序
}
