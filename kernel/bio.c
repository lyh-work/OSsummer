// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define PRIME 13
struct hash_t {
    struct spinlock lock;
    struct buf list;
};
struct hash_t hash[PRIME];

struct {
    struct buf buf[NBUF];
} bcache;

// bget() 会调用的一个函数封装
// 淘汰某个块，这个块由最小的 ticks 指出，传入形参 rob，表示需要抢走
static struct buf *
subbget(struct buf *rob, uint dev, uint blockno) {
    int bucket = blockno % PRIME;
    //  victim 是待抢走的块 rob 所在的哈希链
    int victim = rob->blockno % PRIME;

    // 抢完 buffer 的时间点这个 buffer 从现在开始投入使用
    rob->tickstamp = ticks;
    rob->dev = dev;
    rob->blockno = blockno;
    rob->valid = 0;
    rob->refcnt = 1;

    if (victim != bucket) {
        // 被抢的哈希链，断链
        struct buf *pre = &hash[victim].list;
        for (; pre->next != rob; pre = pre->next);
        pre->next = rob->next;
        release(&hash[victim].lock);
        // 当前这个 bucket 抢完很开心，加了一块 buffer 到自己链头
        rob->next = hash[bucket].list.next;
        hash[bucket].list.next = rob;
    }

    release(&hash[bucket].lock);
    acquiresleep(&rob->lock);
    return rob;
}


void
binit(void)
{
    struct buf *b;

    // lab lock
    // 初始化哈希表的锁
    for (int i = 0; i < PRIME; ++i) {
        initlock(&hash[i].lock, "bcache");
        hash[i].list.next = 0;
    }

    for(b = bcache.buf; b < bcache.buf+NBUF; b++){
        // 初始化每个 cached buffer 的锁
        initsleeplock(&b->lock, "buffer");

        // 标识这个 buffer 属于哪个哈希 bucket
        // bget() eviction 阶段，遍历全部 cached buffer 时
        // 不使用大锁取而代之的是细粒度的锁
        b->blockno = 0;

        // 将全部 RENBUF 个的 cached buffer 数组都给第一个哈希 bucket
        b->next = hash[0].list.next;
        hash[0].list.next = b;
    }
}

static struct buf*
bget(uint dev, uint blockno)
{
    struct buf *b;

    // Is the block already cached?
    int bucket = blockno % PRIME;
    acquire(&hash[bucket].lock);
    for(b = hash[bucket].list.next; b != 0; b = b->next){
        if(b->dev == dev && b->blockno == blockno){
            b->refcnt++;
            release(&hash[bucket].lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    // 缓冲不命中时，需要找最久以前（即最小）的 ticks
    // 遍历所有的哈希链表，找出 ticks 最小那块
    // do-while() 可以先检查自己的哈希链表
    int i = bucket;
    do {
        // 如果当前遍历的哈希链是 bucket，那就不要再上自己的锁
        if (i != bucket)    acquire(&hash[i].lock);

        // 遍历当前 bucket 所在的链表，找出 ticks 最小
        struct buf *minibuf = 0;
        uint mini = 0xffffffff;
        for (b = hash[i].list.next; b != 0; b = b->next) {
            if (b->refcnt == 0 && mini > b->tickstamp) {
                mini = b->tickstamp;
                minibuf = b;
            }
        }

        // 在当前哈希链找到，那就直接 "抢" 走，然后就可以返回了
        if (minibuf != 0)
            return subbget(minibuf, dev, blockno);
        
        if (i != bucket)    release(&hash[i].lock);
        i = (i + 1) % PRIME;
    } while (i != bucket);

    panic("bget: no buffers");
}


// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

void
brelse(struct buf *b)
{
    if(!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);

    // lab lock
    int bucket = b->blockno % PRIME;
    acquire(&hash[bucket].lock);

    b->refcnt--;
    // 只需将 ticks 清零，0 必是最快被 LRU 淘汰的
    if (b->refcnt == 0)    b->tickstamp = ticks;
    
    release(&hash[bucket].lock);
}

void
bpin(struct buf *b) {
    int bucket = b->blockno % PRIME;
    acquire(&hash[bucket].lock);
    b->refcnt++;
    release(&hash[bucket].lock);
}

void
bunpin(struct buf *b) {
    int bucket = b->blockno % PRIME;
    acquire(&hash[bucket].lock);
    b->refcnt--;
    release(&hash[bucket].lock);
}


