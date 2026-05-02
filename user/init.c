// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"

char *argv[] = { "sh", 0 }; // 初始化 shell 的命令行参数

int
main(void)
{
  int pid, wpid; // pid 用于存储 fork() 返回的子进程 ID，wpid 用于存储 wait() 返回的子进程 ID

  // 打开控制台设备，如果失败则创建一个新的控制台设备节点并再次尝试打开
  if(open("console", O_RDWR) < 0){ 
    mknod("console", CONSOLE, 0); // 创建一个新的控制台设备节点，类型为 CONSOLE，主设备号为 0
    open("console", O_RDWR); // 再次尝试打开控制台设备
  }
  dup(0);  // stdout 重定向到控制台
  dup(0);  // stderr 重定向到控制台

   // 循环创建子进程执行 shell，直到 shell 退出后再次创建新的 shell

  for(;;){
    printf("init: starting sh\n");
    pid = fork(); // 创建一个新的子进程
    if(pid < 0){ // fork 失败，打印错误信息并退出
      printf("init: fork failed\n");
      exit(1);
    } 
    if(pid == 0){ // 子进程执行 shell
      exec("sh", argv); // 调用 exec 函数执行路径为 "sh" 的程序，传递命令行参数 argv，如果 exec 返回则说明执行失败
      printf("init: exec sh failed\n");
      exit(1);
    }

    for(;;){ // 父进程等待子进程退出
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *) 0); // 调用 wait 函数等待任意子进程退出，返回值是退出的子进程 ID，如果没有子进程退出则返回 -1
      if(wpid == pid){ // 如果退出的子进程是我们刚刚 fork 的 shell 进程，说明 shell 退出了，打印信息并跳出循环以重新创建新的 shell
        // the shell exited; restart it.
        break;
      } else if(wpid < 0){ // wait 返回 -1，说明没有子进程退出，可能是因为父进程没有子进程了，打印错误信息并退出
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }
}
