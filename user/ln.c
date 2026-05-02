#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 3){ // 如果参数数量不等于 3（程序名称 + 旧路径 + 新路径），打印使用说明并退出
    fprintf(2, "Usage: ln old new\n");
    exit(1);
  }
  // 调用 link 函数创建路径 new 作为指向与 old 相同 inode 的链接
  if(link(argv[1], argv[2]) < 0) // 如果失败则打印错误信息并退出
    fprintf(2, "link %s %s: failed\n", argv[1], argv[2]);
  exit(0);
}
