// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

struct freelist {
  struct spinlock lock;
  struct run *freelist;
};
struct freelist kmem[NCPU];

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};


void
kinit()
{
    // 重命名所有锁，并初始化
    for (int i = 0; i < NCPU; ++i) {
        char lockname[8];
        snprintf(lockname, 8, "kmem", i);
        initlock(&kmem[i].lock, lockname);
    }
    freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
      kmem[id].freelist = r->next;
  else {
      // 偷其他 CPU 的链表结点
      for (int i = 0; i < NCPU; ++i) {
          if (kmem[i].freelist) {
              acquire(&kmem[i].lock);
              r = kmem[i].freelist;
              kmem[i].freelist = kmem[i].freelist->next;
              release(&kmem[i].lock);
              break;// 偷到一个就可以退出了，不然偷多了会内存泄漏
          }
      }
  }
  release(&kmem[id].lock);
  pop_off();

  if(r)
      memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
