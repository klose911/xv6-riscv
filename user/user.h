struct stat;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(const char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);

// ulib.c
/**
 * @brief 获取字符串长度，计算字符串 s 中的字符数，直到遇到字符串结束符 '\0'，返回字符串的长度
 * 
 * @param n 输入的字符串，必须以 '\0' 结尾 
 * @param st 输出参数，指向一个 stat 结构体，用于存储文件的状态信息
 * 
 * @return 如果函数执行成功返回0，否则返回-1
 * 
 */
int stat(const char*, struct stat*);

/**
 * @brief 复制字符串，将字符串 t 中的字符逐个复制到 s 中，直到遇到字符串结束符 '\0'，同时 s 和 t 都向后移动一个位置
 * 
 * @param s 目标字符串，必须有足够的空间来存储复制后的字符串，函数返回时 s 中包含 t 的内容
 * @param t 源字符串，必须以 '\0' 结尾
 * 
 * @return char* 返回指向目标字符串 s 的指针
 * 
 */
char* strcpy(char*, const char*);

/**
 * @brief 复制内存，将 src 指向的内存区域中的前 n 个字节复制到 dst 指向的内存区域中，处理内存重叠的情况
 * 
 * @param vdst 目标内存区域的指针，必须有足够的空间来存储复制后的数据
 * @param vsrc 源内存区域的指针，必须至少包含 n 个字节的数据
 * @param n 要复制的字节数
 * 
 * @return void* 返回指向目标内存区域的指针
 * 
 */
void *memmove(void*, const void*, int);

/**
 * @brief 查找字符在字符串中的位置，遍历字符串 s 中的每个字符，直到遇到字符串结束符 '\0'
 * 
 * @param s 输入的字符串，必须以 '\0' 结尾
 * @param c 要查找的字符
 * 
 * @return char* 返回指向找到的字符的指针，如果未找到则返回 0（NULL 指针）
 * 
 */
char* strchr(const char*, char c);

/**
 * @brief 比较字符串，逐个比较字符串 p 和 q 中的字符，直到遇到字符串结束符 '\0' 或者找到第一个不相等的字符
 * 
 * @param p 第一个字符串，必须以 '\0' 结尾
 * @param q 第二个字符串，必须以 '\0' 结尾
 * 
 * @return int 返回一个整数，表示字符串 p 和 q 的关系：如果 p < q 返回负数，如果 p == q 返回0，如果 p > q 返回正数
 * 
 */
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...) __attribute__ ((format (printf, 2, 3)));
void printf(const char*, ...) __attribute__ ((format (printf, 1, 2)));

/**
 * @brief 从标准输入读取一行文本，存储到 buf 中，最多读取 max-1 个字符，最后在 buf 中添加字符串结束符 '\0'
 * 
 * @param buf 存储输入文本的缓冲区，必须有足够的空间来存储读取的文本和字符串结束符 '\0'
 * @param max 最大读取的字符数，包括字符串结束符 '\0'，函数会读取最多 max-1 个字符，并在 buf 中添加 '\0' 作为结束符
 * 
 * @return char* 返回指向 buf 的指针，包含读取的文本，如果读取过程中发生错误或输入结束，返回 buf 中已经存储的文本（可能是空字符串）
 * 
 */
char* gets(char*, int max);

/**
 * @brief 获取字符串长度，计算字符串 s 中的字符数，直到遇到字符串结束符 '\0'，返回字符串的长度
 * 
 * @param s 输入的字符串，必须以 '\0' 结尾
 * 
 * @return uint 返回字符串的长度，不包括字符串结束符 '\0' 
 * 
 */
uint strlen(const char*);

/**
 * @brief 将目标内存区域 dst 中的前 n 个字节设置为指定的值 c，通常用于初始化内存
 * 
 * @param dst 目标内存区域的指针，必须有足够的空间来存储设置后的数据
 * @param c 要设置的值，通常是一个无符号字符（0-255），但函数参数类型为 int，实际使用时会被转换为 unsigned char
 * @param n 要设置的字节数，必须不超过目标内存区域的大小
 * 
 * @return void* 返回指向目标内存区域的指针，方便链式调用
 * 
 */
void* memset(void*, int, uint);

/**
 * @brief 将字符串转换为整数，遍历字符串中的每个字符，如果是数字字符，则将其转换为对应的整数值，并累加到 n 中，直到遇到非数字字符或字符串结束符 '\0'
 * 
 * @param s 输入的字符串，必须以 '\0' 结尾
 * 
 * @return int 返回转换后的整数值，如果字符串中没有数字字符，则返回0
 * 
 */
int atoi(const char*);

/**
 * @brief 比较内存区域，逐个比较 s1 和 s2 中的字节，直到找到第一个不相等的字节或比较完 n 个字节
 * 
 * @param s1 第一个内存区域的指针，必须至少包含 n 个字节的数据
 * @param s2 第二个内存区域的指针，必须至少包含 n 个字节的数据
 * @param n 要比较的字节数
 * 
 * @return int 返回一个整数，表示内存区域 s1 和 s2 的关系：如果 s1 < s2 返回负数，如果 s1 == s2 返回0，如果 s1 > s2 返回正数
 * 
 */
int memcmp(const void *, const void *, uint);

/**
 * @brief 将 src 指向的内存区域中的前 n 个字节复制到 dst 指向的内存区域中，处理内存重叠的情况
 * 
 * @param dst 目标内存区域的指针，必须有足够的空间来存储复制后的数据
 * @param src 源内存区域的指针，必须至少包含 n 个字节的数据
 * @param n 要复制的字节数
 * 
 * @return void* 返回指向目标内存区域的指针
 * 
 */
void *memcpy(void *, const void *, uint);

// umalloc.c 用户态内存分配器
/**
 * @brief 分配内存块，按照请求的字节数分配合适大小的内存块，并返回指向用户数据区的指针
 * 
 * @param nbytes 请求的字节数
 * 
 * @return void* 返回指向分配内存块的用户数据区的指针，如果分配失败返回0
 * 
 */
void* malloc(uint);

/**
 * @brief 释放内存块，将其插入空闲链表中，并尝试合并相邻的空闲块以减少碎片
 * 
 * @param ap 指向要释放的内存块的指针，实际指向用户数据区，函数内部会调整为指向块头
 * 
 */
void free(void*);
