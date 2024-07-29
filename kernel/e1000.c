#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  // mbuf 包含了一个以太网帧，编程使 mbuf 送入 tx 描述符环
  // 而使 E1000 可以发送出去
  // 储存一个指针以便发送完后释放它

  acquire(&e1000_lock);

  // 获取 tx 环索引
  // 这是 E1000 期待的待发送数据包所在的描述符索引
  uint32 idx = regs[E1000_TDT]; 

  // 检查环溢出，并释放上一次传输完的 mbuf
  if ((tx_ring[idx].status & E1000_TXD_STAT_DD) == 0) {
      release(&e1000_lock);
      return -1;
  }
  if (tx_mbufs[idx])
      mbuffree(tx_mbufs[idx]);

  // 填充描述符
  tx_ring[idx].addr = (uint64)m->head;
  tx_ring[idx].length = m->len;
  tx_ring[idx].cmd = E1000_TXD_CMD_RS
                          | E1000_TXD_CMD_EOP;
  tx_mbufs[idx] = m;// stash away the pointer to the mbuf

  // 更新环位置
  regs[E1000_TDT] = (regs[E1000_TDT] + 1) % TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  // 检查已经到达 E1000 的包
  // 使用 net_rx() 为每个包送入 E1000

  while (1) {
      acquire(&e1000_lock);

      // 获取 RX 索引
      // 如果当前描述符还未有数据到达，需要后移即加一模除
      // 因为一开始这个描述符肯定为空，那么随着 E1000 不断接收
      // 数据包————即写入数据，头指针会赶上尾指针
      // 而当尾指针等于头指针时，硬件视为队列空————即已经没有
      // 地方再接收数据包了
      // 如果读到当前描述符空却不后移，那么每次循环都会读到空，
      // 那么 E1000 就再也无法接收数据包
      uint32 idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;

      // 判断包是否可用，即 E100 是否真的写入数据包了
      if ((rx_ring[idx].status & E1000_RXD_STAT_DD) == 0) {
      release(&e1000_lock);
      return;
      }

      // 更新包缓冲区（mbuf），即填充 mbuf 结构体，并推送网络栈
      rx_mbufs[idx]->len = rx_ring[idx].length;
      release(&e1000_lock);
      net_rx(rx_mbufs[idx]);

      // 为刚才的 mbuf 新分配空间
      rx_mbufs[idx] = mbufalloc(0);
      rx_ring[idx].addr = (uint64)rx_mbufs[idx]->head;
      rx_ring[idx].status = 0;

      // 更新尾指针，更新为最后处理的环描述符的索引
      regs[E1000_RDT] = idx;
    }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
