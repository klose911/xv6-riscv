#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){ // 如果没有指定要创建的目录，打印使用说明并退出
    fprintf(2, "Usage: mkdir files...\n");
    exit(1);
  }

  for(i = 1; i < argc; i++){ // 从第一个参数开始遍历命令行参数，argv[0] 是程序名称
    // 调用 mkdir 函数创建目录，如果失败则打印错误信息但继续尝试创建下一个目录
    if(mkdir(argv[i]) < 0){
      fprintf(2, "mkdir: %s failed to create\n", argv[i]);
      break;
    }
  }

  exit(0);
}
