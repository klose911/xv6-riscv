#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void
ls(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  // 只读方式打开传入的路径 path，如果打开失败，则输出错误信息并返回
  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  // 取该路径对应文件的元数据（如类型、inode号、大小等），如果失败，同样输出错误并关闭文件描述符
  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  // 普通文件（T_FILE）或设备文件（T_DEVICE），直接调用 fmtname 格式化文件名，并输出文件类型、inode号和大小
  case T_DEVICE:
  case T_FILE:
    printf("%s %d %d %d\n", fmtname(path), st.type, st.ino, (int) st.size);
    break;
  // 目录
  case T_DIR:
    // 检查路径拼接后是否会超出缓冲区长度，防止溢出
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("ls: path too long\n");
      break;
    }
    // 将path复制到 buf，并在末尾添加斜杠 /，为后续拼接子文件名做准备
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    // 循环读取目录项，每次读取一个 dirent 结构体
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0) // 如果目录项的 inode 为 0，表示该项无效，跳过
        continue;
      // 使用 memmove 将目录项的文件名拼接到路径后，并以 \0 结尾，形成完整的子文件路径  
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      // 对每个子文件调用 stat 获取其元数据，如果失败则输出错误并跳过
      if(stat(buf, &st) < 0){
        printf("ls: cannot stat %s\n", buf);
        continue;
      }
      // 输出子文件的格式化名称、类型、inode号和大小
      printf("%s %d %d %d\n", fmtname(buf), st.type, st.ino, (int) st.size);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit(0);
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit(0);
}
