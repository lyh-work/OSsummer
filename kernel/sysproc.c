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
#define PTE_A (1L << 6)

int
sys_pgaccess(void)
{
uint64 va;
    int n;
    uint64 result;
    unsigned int bitmask = 0;

    // 获取三个参数
    if (argaddr(0, &va) < 0)    return -1;
    if (argint(1, &n) < 0)    return -1;
    if (argaddr(2, &result) < 0)    return -1;

    // 检查参数有没有超过范围
    if (va >= MAXVA)    return -1;
    if (va + PGSIZE * n >= MAXVA)    return -1;

    for (int i = 0; i < n; ++i, va += PGSIZE) {
        // 获取 va 对应的 pte
        pagetable_t pagetable = myproc()->pagetable;
        for (int level = 2; level > 0; level--) {
            pte_t *pte = &pagetable[PX(level, va)];
            if (*pte & PTE_V) {
                pagetable = (pagetable_t)PTE2PA(*pte);
            } else    return -1;
        }
        pte_t *pte = &pagetable[PX(0, va)];

        // 检查 pte 上的 flag
        unsigned int mask = 0;
        if (*pte & PTE_A) {
            // bitmask 置位
            mask = 1L << i;
            // pte 上的 PTE_A 清位（异或可以实现取反）
            *pte ^= PTE_A;
        }
        bitmask |= mask;
    }

    // 将 bitmask 送回用户空间
    if (copyout(myproc()->pagetable, result, (char *)&bitmask, sizeof(unsigned int)) < 0)
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

