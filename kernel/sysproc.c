#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  // 解析参数
  uint64 start_va;
  int num_pages;
  uint64 output_addr;
  
  if(argaddr(0, &start_va) < 0)
    return -1;
  if(argint(1, &num_pages) < 0)
    return -1;
  if(argaddr(2, &output_addr) < 0)
    return -1;
  
  // 限制最大页数，避免缓冲区过大
  if(num_pages > 64 || num_pages < 0)
    return -1;
  
  struct proc *p = myproc();
  uint64 bitmask = 0;
  
  // 检查每一页的访问位
  for(int i = 0; i < num_pages; i++){
    uint64 va = start_va + i * PGSIZE;
    
    // 使用walk找到PTE
    pte_t *pte = walk(p->pagetable, va, 0);
    if(pte == 0)
      continue; // 页不存在
    
    if(*pte & PTE_A){ // 如果访问位被设置
      bitmask |= (1 << i); // 设置对应的位
      *pte &= ~PTE_A; // 清除访问位
    }
  }
  
  // 将结果复制到用户空间
  if(copyout(p->pagetable, output_addr, (char *)&bitmask, sizeof(bitmask)) < 0)
    return -1;
 
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
