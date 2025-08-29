// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
    struct run *next;
};

struct {
    struct spinlock lock;
    struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  // 为每个CPU初始化锁
  for (int i = 0; i < NCPU; i++) {
    char lockname[8];
    snprintf(lockname, sizeof(lockname), "kmem%d", i);
    initlock(&kmem[i].lock, lockname);
  }
  freerange(end, (void*)PHYSTOP);
}

// 辅助函数：释放页面到指定CPU的空闲链表
void
kfree_single_cpu(void *pa, int cpu)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
}


void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    // 获取当前CPU ID（需要在关闭中断的情况下）
    push_off();
    int cpu = cpuid();
    pop_off();
    
    acquire(&kmem[cpu].lock);
    kfree_single_cpu(p, cpu);  // 辅助函数
    release(&kmem[cpu].lock);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  push_off();
  int cpu = cpuid();
  pop_off();
  
  acquire(&kmem[cpu].lock);
  kfree_single_cpu(pa, cpu);
  release(&kmem[cpu].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cpu = cpuid();
  pop_off();
  
  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if(r) {
    kmem[cpu].freelist = r->next;
    release(&kmem[cpu].lock);
  } else {
    release(&kmem[cpu].lock);
    
    // 从其他CPU偷取一个页面
    for (int i = 0; i < NCPU; i++) {
      if (i == cpu) continue;
      
      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      if (r) {
        kmem[i].freelist = r->next;
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE);
  return (void*)r;
}
