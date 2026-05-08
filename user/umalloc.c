#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.
// 存储分配器
typedef long Align; // for alignment to long boundary

/**
 * @brief header联合体，包含一个结构体和一个Align类型的成员
 * 
 * 
 * 
 */
union header {
  /**
   * @brief 内部结构体，包含一个指向下一个空闲块的指针和当前块的大小
   * 
   * @note 这里同时利用了“结构体存元数据”和“联合体控制对齐”两种机制。s 负责逻辑字段，x 负责内存布局约束
   * 
   */
  struct {
    // 指向下一个同类型块头，通常用来串联空闲块形成链表
    union header *ptr;
    // 当前块的大小，以Header单位为单位，包含头部本身
    uint size;
  } s;
  // 强制 header 的对齐要求至少与 Align 一样严格
  // 由于联合体的大小和对齐会满足其最大成员的要求，这样可保证后续返回给用户的数据区具备合适对齐
  // 避免某些平台上的未对齐访问问题
  Align x;
};

typedef union header Header; // 定义 Header 类型为 header 联合体

static Header base; // 空闲链表的起始点，base.s.ptr 指向第一个空闲块，base.s.size 通常为0
static Header *freep; // 指向空闲链表中的一个块，通常用来遍历链表

void
free(void *ap)
{
  Header *bp, *p;

  bp = (Header*)ap - 1; // 将用户指针调整为指向块头，即Header结构体的起始位置
  // 遍历空闲链表，找到合适的位置插入释放的块
  // 其中 bp > p && bp < p->s.ptr 表示“bp 比 p 大且比 p->s.ptr 小”,也就是严格落在两者之间
  // 前面的 ! 取反后，含义变成 “bp 不在这个开区间里”
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    // p >= p->s.ptr 说明当前块 p 是空闲链表中的最后一个块（因为链表是循环的），此时 bp 可能在链表的末尾和开头之间，即跨越了链表的循环边界
    // 这种情况也需要插入 bp，因为 bp 可能是一个新的空闲块，或者是一个被释放的块，应该被加入到链表中
    // bp > p 说明 bp 处于链表的末尾
    // bp < p->s.ptr 说明 bp 处于链表的开头
      break;
  // 尝试与前一个块合并
  if(bp + bp->s.size == p->s.ptr){   // 如果 bp 的末尾与 p 的起始位置相邻，则合并它们
    bp->s.size += p->s.ptr->s.size; // 合并后 bp 的大小增加了 p->s.ptr 的大小
    bp->s.ptr = p->s.ptr->s.ptr; // bp 的下一个块指向 p->s.ptr 的下一个块，跳过 p->s.ptr
  } else // 否则直接将 bp 插入链表中
    bp->s.ptr = p->s.ptr; // bp 的下一个块指向 p 的下一个块 
    
  // 尝试与后一个块合并
  if(p + p->s.size == bp){ // 如果 p 的末尾与 bp 的起始位置相邻，则合并它们
    p->s.size += bp->s.size; // 合并后 p 的大小增加了 bp 的大小
    p->s.ptr = bp->s.ptr; // p 的下一个块指向 bp 的下一个块，跳过 bp
  } else
    p->s.ptr = bp; // 否则直接将 bp 插入链表中，p 的下一个块指向 bp 
  freep = p; // 更新 freep 指针，指向当前块 p，以便下次分配时从这里开始搜索
}

/**
 * @brief 请求更多内存块，从操作系统获取新的内存块并加入空闲链表
 * 
 * @param nu 请求的块数
 * @return Header* 返回指向空闲链表的指针，如果失败返回0
 */
static Header*
morecore(uint nu)
{
  char *p;
  Header *hp;

  if(nu < 4096)
    nu = 4096; // 最小请求块数，通常为页面大小，以减少系统调用次数
  p = sbrk(nu * sizeof(Header)); // 从操作系统请求更多内存，返回指向新内存块的指针
  if(p == (char*)-1) // sbrk失败，返回-1
    return 0;
  hp = (Header*)p; // 将返回的内存块转换为Header指针
  hp->s.size = nu; // 设置新块的大小
  free((void*)(hp + 1)); // 将新块加入空闲链表，注意传入的是用户数据区的指针，即Header结构体之后的位置
  return freep; // 返回空闲链表的指针，供malloc使用
}

void*
malloc(uint nbytes)
{
  Header *p, *prevp;
  uint nunits;

  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1; // 计算需要的块数，包含Header本身，向上取整
  if((prevp = freep) == 0){ // 如果空闲链表未初始化，进行初始化
    base.s.ptr = freep = prevp = &base; // base是一个特殊的块，作为空闲链表的起始点，初始时指向自己形成循环链表
    base.s.size = 0; // base块的大小为0，不占用实际内存，只是作为链表的哨兵节点
  }
  // 遍历空闲链表，寻找第一个足够大的块
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    if(p->s.size >= nunits){ // 找到第一个足够大的块
      if(p->s.size == nunits) // 如果块的大小正好匹配请求的大小，直接从链表中移除该块
        prevp->s.ptr = p->s.ptr; // 将前一个块的下一个指针指向当前块的下一个块，跳过当前块
      else {
        p->s.size -= nunits; // 否则将当前块分割成两部分，剩余部分继续留在链表中
        p += p->s.size; // 将 p 指向分割后的新块，即原块的末尾部分
        p->s.size = nunits; // 设置新块的大小为请求的大小，准备返回给用户
      }
      freep = prevp; // 更新 freep 指针，指向当前块的前一个块，以便下次分配时从这里开始搜索
      return (void*)(p + 1); // 返回指向用户数据区的指针，即Header结构体之后的位置
    }
    if(p == freep) // 如果遍历完整个链表都没有找到合适的块，尝试请求更多内存
      if((p = morecore(nunits)) == 0) // morecore失败，返回0
        return 0; // 否则继续循环，尝试分配新请求的内存块
  }
}
