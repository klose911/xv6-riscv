// Shell.

#include "kernel/types.h"
#include "user/user.h" 
#include "kernel/fcntl.h"

// Parsed command representation 
// 解析之后的命令类型
#define EXEC  1  // 可执行命令  
#define REDIR 2 // 文件重定向  ><
#define PIPE  3 // 管道执行 |  
#define LIST  4 // 列表多个命令 
#define BACK  5 // 后台执行 & 

#define MAXARGS 10 // 最大参数数量

/**
 * @brief 命令接口的结构体
 * 
 */
struct cmd {
  int type; // 命令类型
};

// 可执行命令的结构体
struct execcmd {
  int type; // EXEC 1 
  char *argv[MAXARGS]; // 执行参数数组
  char *eargv[MAXARGS]; // 环境变量参数数组
};

// 文件重定向命令的结构体
struct redircmd {
  int type; // REDIR
  struct cmd *cmd; // 执行重定向之前的cmd
  char *file; // 指向重定向的文件名字符串开始
  char *efile; // 指向重定向的文件名字符串结尾
  int mode; // 重定向文件的权限模式
  int fd; // 被重定向的文件对应的描述符
};

struct pipecmd {
  int type; // PIPE
  struct cmd *left; // 管道输入端对应的命令
  struct cmd *right; // 管道输出端对应的命令
};

struct listcmd {
  int type; // LIST 
  struct cmd *left; // 列表中第一个命令
  struct cmd *right; // 列表中其余
};

struct backcmd {
  int type; // BACK
  struct cmd *cmd; // 后台执行的命令
};

/**
 * @brief fork 失败，打印错误信息
 * 
 * @return int 成功：返回fork调用的返回值 （0代表父进程，>0 代表子进程的pid)  
 * 
 */
int fork1(void);  // Fork but panics on failure.

/**
 * @brief 在文件描述符2 （错误输出） 打印错误信息，接着以 1 作为进程结果码结束进程
 * 
 */
void panic(char*);

/**
 * @brief 将传入的命令行字符串 s 解析为一个命令树结构（struct cmd）
 * 这个结构体通常用于表示 shell 命令的语法树，包括简单命令、重定向、管道、命令序列和后台执行等
 * 解析过程会分析命令行的语法，将其拆分为不同的命令节点，并以树状结构组织，方便后续递归执行和处理
 * 
 * @return struct cmd* shell 命令的语法树
 * 
 */
struct cmd *parsecmd(char*); 

// Execute cmd.  Never returns.
/**
 * @brief 执行 shell 命令树的核心递归函数
 * 
 * @param cmd truct cmd 结构体的指针，代表一条解析后的命令（可能是简单命令、重定向、管道、命令序列或后台命令等）
 * 
 * @return 不返回 
 * 
 * 据命令类型（通过 cmd->type）分发到不同的处理分支，实现了对 shell 语法的支持，包括：
 *   普通命令执行（如 ls、cat 等）
 *   输入/输出重定向
 *   命令序列（如 cmd1 ; cmd2）
 *   管道（如 cmd1 | cmd2）
 *   后台执行（如 cmd &）
 * 
 */
void runcmd(struct cmd*) __attribute__((noreturn));

void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0) // cmd为NULL，直接退出，错误码是 1 
    exit(1);

  switch(cmd->type){
  default:
    panic("runcmd"); // type无效，错误退出

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0) // 可执行文件的文件名为NULL， 终止进程
      exit(1);
    // 执行exec 系统调用
    exec(ecmd->argv[0], ecmd->argv); 
    fprintf(2, "exec %s failed\n", ecmd->argv[0]); // 注意：如果exec执行成功，就不会再执行这行代码
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd); // 关闭文件描述符
    // 打开需要重定向的文件，
    if(open(rcmd->file, rcmd->mode) < 0){
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    // fork 一个子进程
    if(fork1() == 0) // 子进程执行第一个命令
      runcmd(lcmd->left); 
    wait(0); // 父进程等待子进程结束执行
    runcmd(lcmd->right); // 第一个命令执行结束后，执行其余的命令
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    // 生成pipe 管道对象
    // 成功后 p[0] 作为管道的输出，p[1] 作为管道的输入
    if(pipe(p) < 0) // 管道生成失败，然后退出
      panic("pipe");
    // fork一个子进程 2，作为管道的输入方，执行left子命令
    if(fork1() == 0){
      close(1); // 关闭标准输出
      dup(p[1]); // 把 p[1] 作为 标准输出
      // 关闭管道
      close(p[0]); 
      close(p[1]);
      runcmd(pcmd->left);
    }
    // 再次fork一个子进程 3，作为管道的输出方，执行right子命令
    if(fork1() == 0){
      close(0); // 关闭标准输入
      dup(p[0]); // 把 p[0] 作为 标准输入
      // 关闭管道
      close(p[0]); 
      close(p[1]);
      runcmd(pcmd->right);
    }
    // 关闭父进程的管道
    close(p[0]);
    close(p[1]);
    wait(0); // 等待子进程2/3 结束
    wait(0); // 等待子进程2/3 结束
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd; 
    // fork一个子进程，子进程执行cmd，作为后台
    // 注意：这里父进程不会等待子进程结束，所以执行顺序并不确定
    if(fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

/**
 * @brief 从用户输入读取一行命令输入
 * 
 * @param buf 存储命令的缓冲区
 * @param nbuf 缓冲区大小（字节）
 * @return int 成功：返回 0，失败：返回 -1
 */
int
getcmd(char *buf, int nbuf)
{
  write(2, "$ ", 2); // 标准错误（文件描述符 2）上输出提示符 $ ，提示用户输入命令 
  memset(buf, 0, nbuf); // 将缓冲区 buf 清零，确保后续读取的内容不会受到旧数据影响
  gets(buf, nbuf); // 从标准输入读取一行字符，存入 buf，最多读取 nbuf 个字符
  if(buf[0] == 0) // EOF 用户直接按下回车或遇到文件结束（EOF）
    return -1; // 没有有效输入
  return 0; // 成功读取到命令
}

int
main(void)
{
  static char buf[100]; // 命令的缓存区
  int fd; // 文件描述符

  // Ensure that three file descriptors are open.
  // 以读写方式不断尝试打开 "console" 设备，每次返回的文件描述符赋值给 fd
  while((fd = open("console", O_RDWR)) >= 0){  
    // 如果返回的 fd 小于 3，说明还没有占满标准输入（0）、标准输出（1）、标准错误（2）
    // 循环会继续，直到这三个文件描述符都被分配
    if(fd >= 3){ // 说明前三个文件描述符已经被占用，再打开就会得到更高的文件描述符
      close(fd); // 关闭多余的 fd，并跳出循环
      break;
    }
  }

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){ // 读取一行命令到缓存区
    // 判断用户输入的命令是否以 "cd " 开头（即前两个字符是 'c' 和 'd'，第三个字符是空格）
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Chdir must be called by the parent, not the child.
      // chdir 必须由父进程调用，而不能在子进程中调用。因为子进程改变目录不会影响父进程的工作目录 
      // 将输入字符串末尾的换行符 \n 替换为字符串结束符 \0，确保路径字符串格式正确
      buf[strlen(buf)-1] = 0;  // chop \n 
      if(chdir(buf+3) < 0) // 调用 chdir 系统调用改变当前工作目录，参数是路径字符串的起始位置（跳过 "cd "）
        fprintf(2, "cannot cd %s\n", buf+3); // 切换失败，输出错误信息
      continue; // 跳过本次循环，等待用户输入下一条命令
    }
    if(fork1() == 0) // fork一个子进程，并在子进程中执行输入命令
      runcmd(parsecmd(buf));
    wait(0); // 父进程等待子进程结束执行
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

/**
 * @brief 创建并初始化一个新的执行命令节点 execcmd 结构体
 * 
 * 注意：这里分配在堆上的内存，会在子进程执行命令结束后，父进程调用wait后回收，所以没有显示执行free
 * 
 * @return struct cmd* 指向新创建的 execcmd 结构体的指针
 */
struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd)); // 分配一块足够大的内存，用于存放一个 execcmd 结构体，并将指针赋值给 cmd
  memset(cmd, 0, sizeof(*cmd)); // 将分配的内存区域清零，确保结构体的所有字段初始值为 0 或 NULL
  cmd->type = EXEC; // 设置结构体的 type 字段为 EXEC，表示这是一个可执行命令节点
  return (struct cmd*)cmd; // 将 execcmd 结构体指针强制转换为通用的 cmd 结构体指针返回
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v"; // 空白字符的数组
char symbols[] = "<|>&;()"; // 特殊字符数组

/**
 * @brief  shell 命令解析过程中，从命令字符串中提取下一个 token（如命令、参数、操作符等）
 * 并返回 token 的类型（通常用整数表示，如普通单词、特殊符号等）
 * 同时通过 q 和 eq 返回 token 的具体位置，方便后续处理和构造命令树
 * 
 * @param ps 指向当前解析位置的指针，用于在命令字符串中遍历和更新解析进度
 * @param es 指向命令字符串的末尾，作为解析的边界
 * @param q 指向当前 token 的起始位置的指针
 * @param eq 指向当前 token 的结束位置的指针
 * 
 * @return int 返回当前 token 的类型，如普通单词、特殊符号等
 * 
 */
int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps; // 当前解析位置赋值给 s，准备开始解析
  // 跳过所有空白字符（如空格、制表符等），直到遇到非空白字符或到达字符串末尾 es
  while(s < es && strchr(whitespace, *s)) 
    s++;
  if(q) // 如果参数 q 非空，则将当前解析位置 s 赋值给 *q
    *q = s; // 记录当前 token 的起始位置
  ret = *s; // 将当前字符的值赋给 ret，用于后续判断 token 类型或作为返回值
  switch(*s){
  case 0: // 字符串结尾（NUL 字符），直接跳出分支，不做任何操作 
    break;
  case '|': // 管道
  case '(': // 括号
  case ')': 
  case ';': // 命令分隔符
  case '&': // 后台执行
  case '<': // 输入重定向
    s++; // 解析指针 s 后移一位，表示已识别该符号
    break;
  case '>': // 输出重定向 
    s++;
    if(*s == '>'){ // 如果下一个字符还是 >，则识别为追加重定向 >>  
      ret = '+'; // 将 ret 设为 '+'，并再后移一位 
      s++;
    }
    break;
  default: // 处理普通单词
    ret = 'a'; // 将 ret 设为 'a'，代表普通token
    // 然后通过循环跳过所有非空白且非特殊符号的字符
    // 直到遇到空白或特殊符号为止，完成一个 token 的识别
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq) // 如果参数 eq 非空，则将当前解析指针 s 赋值给 *eq
    *eq = s; // 用于记录当前 token 的结束位置，方便后续提取完整的 token 字符串

  // 循环跳过所有空白字符（如空格、制表符等），直到遇到非空白字符或到达字符串末尾 es
  while(s < es && strchr(whitespace, *s))
    s++; // 保证下一个 token 的解析从有效字符开始
  *ps = s; // 更新解析指针 ps，使其指向下一个待解析的位置，便于后续继续处理命令字符串
  return ret;
}

/**
 * @brief 检查下一个待处理字符是否属于指定的“分隔符”或“特殊字符”集合
 * 
 * @param ps 指向字符串开头的二级指针
 * @param es 指向字符串末尾的一级指针
 * @param toks 字符集合
 * 
 * @return int 如果是，则返回非零值，否则返回 0
 * 
 */
int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps; //  取出当前解析指针
  // 跳过所有空白字符（如空格、制表符等），直到遇到非空白字符或到达字符串末尾 es
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s; // 确保后续解析从第一个非空白字符开始
  // 判断当前字符是否非字符串结尾，并且属于 toks 指定的特殊字符集合
  return *s && strchr(toks, *s); // 如果是，则返回非零值，否则返回 0
}

/**
 * @brief 从命令字符串的当前解析位置 ps 到字符串末尾 es，解析出一行完整的命令，并将其转换为命令树结构
 * 
 * @param ps 指向字符串开头的二级指针
 * @param es 指向字符串末尾的一级指针
 * 
 * @return struct cmd* 解析后的命令树结构
 * 
 */
struct cmd *parseline(char**, char*);

/**
 * @brief 解析包含管道符号（|）的命令行
 * 
 * @param ps 指向字符串开头的二级指针
 * @param es 指向字符串末尾的一级指针
 * 
 * @return struct cmd* 解析后的命令树结构：如果包含管道，则返回 pipecmd 结构，反之返回 execcmd 
 * 
 */
struct cmd *parsepipe(char**, char*);

/**
 * @brief 从当前解析位置 ps 到字符串末尾 es，解析出一个“可执行命令”节点
 * 这个节点通常包含命令本身、参数列表以及可能的输入/输出重定向信息
 * 
 * @param ps 指向字符串开头的二级指针
 * @param es 指向字符串末尾的一级指针
 * 
 * @return struct cmd* 解析后的 execcmd 节点
 */
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s); // 指向 s 结尾处的指针
  cmd = parseline(&s, es); // 解析下一行命令
  peek(&s, es, ""); // 把s指向下一个空白字符的位置
  if(s != es){ // 如果s 不在最后位置，说明解析出错
    fprintf(2, "leftovers: %s\n", s); // 打印出错信息
    panic("syntax"); // sh奔溃
  }
  nulterminate(cmd); // NUL 终止所有字符串
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es); // 解析当前命令行片段，支持管道（|）语法，得到一个基本命令节点

  // 循环处理，支持多个连续的 &
  while(peek(ps, es, "&")){ // 检查下一个 token 是否为后台执行符号 &
    gettoken(ps, es, 0, 0); // 通过 gettoken 消费掉 & 符号
    cmd = backcmd(cmd);// 用 backcmd(cmd) 包装当前命令节点，表示该命令应在后台执行
  }

  // 类似与后台命令，但列表只支持单个 ; 所以用条件代替循环
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0); // 消费掉 ; 符号
    // 递归调用 parseline(ps, es) 解析后续命令
    cmd = listcmd(cmd, parseline(ps, es)); //将当前命令和后续命令组合成命令序列节点，实现多条命令顺序执行
  }
  return cmd; 
}


struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es); // 解析当前命令行片段，得到一个基本命令节点（如简单命令或带重定向的命令）
  if(peek(ps, es, "|")){ // 检查下一个 token 是否为管道符号 |。如果是，说明当前命令后面还有管道操作
    gettoken(ps, es, 0, 0); // 消费掉 | 符号，准备解析管道右侧的命令
    // 递归调用 parsepipe(ps, es) 解析管道右侧的命令
    // 这里不使用 parseline 说明，管道右侧命令不支持后台和列表
    cmd = pipecmd(cmd, parsepipe(ps, es)); // 当前命令和右侧命令组合成一个 pipecmd 结构，表示管道操作
  }
  return cmd;
}

/**
 * @brief 处理命令中的重定向符号（如 <、>、>>）
 * 
 * @param cmd 指向 struct cmd 结构体的指针
 * @param ps 指向字符串开头的二级指针
 * @param es 指向字符串末尾的一级指针
 * @return struct cmd* 处理重定向后的命令树结构
 */
struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){ // 检查下一个 token 是否为重定向符号
    tok = gettoken(ps, es, 0, 0); // 获取重定向符号 token
    if(gettoken(ps, es, &q, &eq) != 'a') // 获取下一个 token，应该是文件名
      panic("missing file for redirection"); // 如果不是普通单词，报错退出
    switch(tok){ 
    case '<': // 输入重定向 <
      // 这里q 和 eq 组成的输入重定向的文件名
      // O_RDONLY 表示文件只读
      // 0 表示 标准输入的文件描述符
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0); // 构造 redircmd 结构，表示输入重定向到文件
      break;
    case '>': // 输出重定向 > 
      // 这里q 和 eq 组成的输出重定向的文件名
      // O_WRONLY|O_CREATE|O_TRUNC 表示文件可写、创建新文件、截断文件
      // 1 表示 标准输出的文件描述符
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':  // >> 追加重定向
      // 类似于输出重定向，但这里的文件不会被截断为0再写入，而是在末尾追加
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

/**
 * @brief 解析一个命令块（即被括号包围的命令）
 * 
 * @param ps 指向字符串开头的二级指针
 * @param es 指向字符串末尾的一级指针
 * @return struct cmd* 解析后的命令树结构
 * 
 */
struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "(")) //检查当前解析位置是否为左括号 (
    panic("parseblock"); //如果不是，则调用 panic("parseblock") 报错，说明语法不符 
  gettoken(ps, es, 0, 0); //消费掉左括号 (，将解析指针推进到下一个 toke
  cmd = parseline(ps, es); //递归解析括号内的一整行命令，返回命令树结构并赋值给 cmd
  if(!peek(ps, es, ")")) // 检查下一个 token 是否为右括号 )
    panic("syntax - missing )"); //如果不是，报错 panic("syntax - missing )")，提示缺少右括号
  gettoken(ps, es, 0, 0); // 消费掉右括号 )，将解析指针推进到下一个 token
  cmd = parseredirs(cmd, ps, es); // 处理括号内命令可能存在的重定向
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;// 记录每个参数 token 的起止位置
  int tok, argc; // tok类型，以及 参数个数
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))// 下一个 token 是左括号 (
    return parseblock(ps, es); //调用 parseblock 解析并返回

  ret = execcmd();// 分配一个新的execcmd结构体，并赋值给ret 和 cmd
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es); // 处理可能出现在命令最前面的重定向（如 <、>）
  // 持续解析参数，直到遇到管道符号 |、右括号 )、后台符号 & 或命令分隔符 ; 为止
  while(!peek(ps, es, "|)&;")){ 
    // 调用 gettoken 获取下一个 token，并用 q、eq 记录参数的起止位置，
    if((tok=gettoken(ps, es, &q, &eq)) == 0) //没有获取到 token（返回 0），说明命令结束，跳出循环
      break;
    if(tok != 'a') // 如果 token 类型不是普通单词（'a'），说明语法错误，调用 panic("syntax") 报错
      panic("syntax");
    // 将参数的起止指针分别存入 cmd->argv 和 cmd->eargv，并递增参数计数 argc
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS) // 参数超过允许的最大值，报错退出
      panic("too many args");
    ret = parseredirs(ret, ps, es); //再次调用 parseredirs，以支持参数和重定向混合出现
  }
  // 将参数数组和结束指针数组的最后一项设为 0(NULL)，表示参数列表结束
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
/**
 * @brief 对命令树结构中的字符串成员进行 NUL（\0）结尾处理
 * 确保所有命令参数、文件名等字符串都以 NUL 字符结束
 * 这是 C 语言字符串的标准格式，便于后续字符串操作和系统调用的安全性
 * 
 * @param cmd 指向 struct cmd 结构体的指针
 * 
 * @return struct cmd* NULL处理过后的，指向 struct cmd 结构体的指针
 * 
 * 该函数通常在命令解析完成后调用，对整个命令树递归处理，保证所有字符串成员都符合 C 语言约定
 * 
 */
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
