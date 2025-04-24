/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 Hewlett Packard Enterprise LLC
 *
 * Jean-Marie Verdun <verdun@hpe.com>
 */

#ifndef __ETH_H
#define __ETH_H

#include <net.h>

#define BUFFER_SZ       16384
#define W5100_SPI_WRITE_OPCODE 0xf0
#define W5100_SPI_READ_OPCODE 0x0f
#define W5100_SHAR              0x0009	/* Source MAC address */
#define W5500_S0_REGS           0x10000
#define S0_REGS(priv)           ((priv)->s0_regs)
#define W5100_MR                0x0000	/* Mode Register */
#define   MR_RST                  0x80	/* S/W reset */
#define   MR_PB                   0x10	/* Ping block */

#define W5100_Sn_MR             0x0000	/* Sn Mode Register */
#define W5100_Sn_CR             0x0001	/* Sn Command Register */
#define W5100_Sn_IR             0x0002	/* Sn Interrupt Register */
#define W5100_Sn_SR             0x0003	/* Sn Status Register */
#define W5100_Sn_TX_FSR         0x0020	/* Sn Transmit free memory size */
#define W5100_Sn_TX_RD          0x0022	/* Sn Transmit memory read pointer */
#define W5100_Sn_TX_WR          0x0024	/* Sn Transmit memory write pointer */
#define W5100_Sn_RX_RSR         0x0026	/* Sn Receive free memory size */
#define W5100_Sn_RX_RD          0x0028	/* Sn Receive memory read pointer */

#define W5100_S0_MR(priv)       (S0_REGS(priv) + W5100_Sn_MR)

#define   S0_MR_MACRAW            0x04	/* MAC RAW mode */
#define   S0_MR_MF                0x40	/* MAC Filter for W5100 and W5200 */
#define   W5500_S0_MR_MF          0x80	/* MAC Filter for W5500 */
#define W5100_S0_MR(priv)       (S0_REGS(priv) + W5100_Sn_MR)

#define   S0_MR_MACRAW            0x04	/* MAC RAW mode */
#define   S0_MR_MF                0x40	/* MAC Filter for W5100 and W5200 */
#define   W5500_S0_MR_MF          0x80	/* MAC Filter for W5500 */

/*
 * W5100 and W5200 common registers about the same with the W5500
 */
#define W5100_IMR               0x0016	/* Interrupt Mask Register */
#define   IR_S0                   0x01	/* S0 interrupt */
#define W5100_RTR               0x0017	/* Retry Time-value Register */
#define   RTR_DEFAULT             2000	/* =0x07d0 (2000) */
#define W5500_SIMR              0x0018	/* Socket Interrupt Mask Register */
#define W5500_RTR               0x0019	/* Retry Time-value Register */

#define W5100_S0_CR(priv)       (S0_REGS(priv) + W5100_Sn_CR)
#define   S0_CR_OPEN              0x01	/* OPEN command */
#define   S0_CR_CLOSE             0x10	/* CLOSE command */
#define   S0_CR_SEND              0x20	/* SEND command */
#define   S0_CR_RECV              0x40	/* RECV command */
#define W5100_S0_IR(priv)       (S0_REGS(priv) + W5100_Sn_IR)
#define   S0_IR_SENDOK            0x10	/* complete sending */
#define   S0_IR_RECV              0x04	/* receiving data */
#define W5100_S0_SR(priv)       (S0_REGS(priv) + W5100_Sn_SR)
#define   S0_SR_MACRAW            0x42	/* mac raw mode */
#define W5100_S0_TX_FSR(priv)   (S0_REGS(priv) + W5100_Sn_TX_FSR)
#define W5100_S0_TX_RD(priv)    (S0_REGS(priv) + W5100_Sn_TX_RD)
#define W5100_S0_TX_WR(priv)    (S0_REGS(priv) + W5100_Sn_TX_WR)
#define W5100_S0_RX_RSR(priv)   (S0_REGS(priv) + W5100_Sn_RX_RSR)
#define W5100_S0_RX_RD(priv)    (S0_REGS(priv) + W5100_Sn_RX_RD)

#define W5500_TX_MEM_START      0x20000
#define W5500_TX_MEM_SIZE       0x04000
#define W5500_RX_MEM_START      0x30000
#define W5500_RX_MEM_SIZE       0x04000

#define W5500_Sn_RXMEM_SIZE(n)  \
		(0x1001e + (n) * 0x40000)	/* Sn RX Memory Size */
#define W5500_Sn_TXMEM_SIZE(n)  \
		(0x1001f + (n) * 0x40000)	/* Sn TX Memory Size */

/**
 * A packet handler
 *
 * dev - device pointer
 * pkt - pointer to the "sent" packet
 * len - packet length
 */
typedef int w5500_eth_tx_hand_f(struct udevice *dev, void *pkt,
				unsigned int len);

/**
 * struct eth_w5500_priv - memory for w5500 driver
 *
 * host_hwaddr - MAC address of mocked machine
 * disabled - Will not respond
 * recv_packet_buffer - buffers of the packet returned as received
 * recv_packet_length - lengths of the packet returned as received
 * recv_packets - number of packets returned
 * tx_handler - function to generate responses to sent packets
 * priv - a pointer to some structure a test may want to keep track of
 */
struct eth_w5500_priv {
	uchar host_hwaddr[ARP_HLEN];
	bool disabled;
	uchar *recv_packet_buffer[PKTBUFSRX];
	int recv_packet_length[PKTBUFSRX];
	int recv_packets;
	w5500_eth_tx_hand_f *tx_handler;
	const struct dm_spi_ops *spi_ops;
	struct udevice **spi_dev;
	/* Socket 0 register offset address */
	u32 s0_regs;
	/* Socket 0 TX buffer offset address and size */
	u32 s0_tx_buf;
	u16 s0_tx_buf_size;
	/* Socket 0 RX buffer offset address and size */
	u32 s0_rx_buf;
	u16 s0_rx_buf_size;
	bool promisc;
	u32 msg_enable;
	u16 offset;
	void *priv;
};

static int w5500_spi_write(struct udevice *dev, u32 addr, u8 data);
static int w5500_spi_read(struct udevice *dev, u32 addr);
static void w5500_enable_intr(struct udevice *dev);
static int w5500_spi_read16(struct udevice *dev, u32 addr);
static int w5500_spi_write16(struct udevice *dev, u32 addr, u16 data);
static int w5500_writebuf(struct udevice *dev, u16 offset, const u8 *buf,
			  int len);
static int w5500_readbuf(struct udevice *dev, u16 offset, u8 *buf, int len);
static int w5500_writebulk(struct udevice *dev, u32 addr, const u8 *buf,
			   int len);

#endif /* __ETH_H */
