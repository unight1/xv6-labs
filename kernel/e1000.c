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
  acquire(&e1000_lock);
  
  uint32 index = regs[E1000_TDT];
  struct tx_desc *desc = &tx_ring[index];
  
  // 检查描述符是否可用
  if (!(desc->status & E1000_TXD_STAT_DD)) {
    release(&e1000_lock);
    return -1;
  }
  
  // 释放之前可能存在的mbuf
  if (tx_mbufs[index]) {
    mbuffree(tx_mbufs[index]);
  }
  
  // 设置描述符
  desc->addr = (uint64)m->head;
  desc->length = m->len;
  desc->cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
  desc->status = 0;
  
  // 保存mbuf指针
  tx_mbufs[index] = m;
  
  // 确保更新操作对硬件可见
  __sync_synchronize();
  
  // 更新TX环尾指针
  regs[E1000_TDT] = (index + 1) % TX_RING_SIZE;
  
  release(&e1000_lock);
  return 0;
}


static void
e1000_recv(void)
{
  // 注意：这个函数在中断上下文中被调用
  // 可能需要特殊处理锁
  
  uint32 index;
  struct rx_desc *desc;
  
  // 使用循环处理所有可用的数据包
  while (1) {
    // 获取当前RDT并计算下一个索引
    index = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
    desc = &rx_ring[index];
    
    // 检查是否有新数据包
    if (!(desc->status & E1000_RXD_STAT_DD)) {
      break;
    }
    
    // 获取mbuf
    struct mbuf *m = rx_mbufs[index];
    m->len = desc->length;
    
    // 在释放锁之前传递数据包给网络栈
    // 注意：net_rx可能会需要锁，所以这里先释放锁
    release(&e1000_lock);
    net_rx(m);
    acquire(&e1000_lock);
    
    // 分配新的mbuf
    if ((rx_mbufs[index] = mbufalloc(0)) == 0) {
      panic("e1000_recv: no mbuf available");
    }
    
    // 设置新描述符
    desc->addr = (uint64)rx_mbufs[index]->head;
    desc->status = 0;
    
    // 更新RDT
    regs[E1000_RDT] = index;
  }
}

void
e1000_intr(void)
{
  // 在中断处理中获取锁
  acquire(&e1000_lock);
  e1000_recv();
  release(&e1000_lock);
  
  // 确认中断
  regs[E1000_ICR] = 0xffffffff;
}
