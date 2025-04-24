// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright Hewlett Packard Enterprise Development LP.
 *
 * Jean-Marie Verdun <verdun@hpe.com>
 *
 * inspired from the linux kernel driver from
 * Copyright (C) 2006-2008 WIZnet Co.,Ltd.
 * Copyright (C) 2012 Mike Sinkovsky <msink@permonline.ru>
 *
 * available at
 *
 * https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/wiznet/w5100.c
 *
 * Datasheet:
 * http://www.wiznet.co.kr/wp-content/uploads/wiznethome/Chip/W5100/Document/W5100_Datasheet_v1.2.6.pdf
 * http://wiznethome.cafe24.com/wp-content/uploads/wiznethome/Chip/W5200/Documents/W5200_DS_V140E.pdf
 * http://wizwiki.net/wiki/lib/exe/fetch.php?media=products:w5500:w5500_ds_v106e_141230.pdf
 *
 */

#include <dm.h>
#include <log.h>
#include <malloc.h>
#include <spi.h>
#include <net.h>
#include "w5500.h"
#include <asm/global_data.h>
#include <linux/delay.h>

DECLARE_GLOBAL_DATA_PTR;
static int w5500_command(struct udevice *dev, u8 cmd)
{
	struct eth_w5500_priv *priv = dev_get_priv(dev);
	u16 counter = 0;

	w5500_spi_write(dev, W5100_S0_CR(priv), cmd);
	while (w5500_spi_read(dev, W5100_S0_CR(priv)) != 0) {
		counter++;
		if (counter == 500)
			return -EIO;
		udelay(10);
	}
	return 0;
}

/*
 * Set priv ptr
 *
 * priv - priv void ptr to store in the device
 */
void w5500_eth_set_priv(int index, void *priv)
{
	struct udevice *dev;
	struct eth_w5500_priv *dev_priv;
	int ret;

	ret = uclass_get_device(UCLASS_ETH, index, &dev);
	if (ret)
		return;

	dev_priv = dev_get_priv(dev);

	dev_priv->priv = priv;
}

static int w5500_eth_start(struct udevice *dev)
{
	struct eth_w5500_priv *priv = dev_get_priv(dev);

	debug("eth_w5500: Start\n");

	u8 mode = S0_MR_MACRAW;

	if (!priv->promisc)
		mode |= W5500_S0_MR_MF;

	w5500_spi_write(dev, W5100_S0_MR(priv), mode);
	w5500_command(dev, S0_CR_OPEN);
	priv->offset = 0;
	return 0;
}

static int w5500_eth_send(struct udevice *dev, void *packet, int length)
{
	struct eth_w5500_priv *priv = dev_get_priv(dev);
	u16 offset;

	if (priv->disabled)
		return 0;

	offset = w5500_spi_read16(dev, W5100_S0_TX_WR(priv));
	w5500_writebuf(dev, offset, packet, length);
	w5500_spi_write16(dev, W5100_S0_TX_WR(priv), offset + length);
	w5500_command(dev, S0_CR_SEND);
	return 0;
}

static int w5500_eth_recv(struct udevice *dev, int flags, u8 **packetp)
{
	struct eth_w5500_priv *priv = dev_get_priv(dev);
	u16 rx_len;
	u16 offset;
	u8 data[9000];
	u16 tmp;

	u16 rx_buf_len = w5500_spi_read16(dev, W5100_S0_RX_RSR(priv));

	while ((tmp =
		w5500_spi_read16(dev, W5100_S0_RX_RSR(priv))) != rx_buf_len)
		rx_buf_len = tmp;

	if (rx_buf_len == 0)
		return 0;

	offset = w5500_spi_read16(dev, W5100_S0_RX_RD(priv));
	rx_len = rx_buf_len - 2;
	w5500_readbuf(dev, offset + 2, data, rx_len);
	w5500_spi_write16(dev, W5100_S0_RX_RD(priv), offset + 2 + rx_len);
	w5500_command(dev, S0_CR_RECV);
	*packetp = data;

	priv->offset += rx_buf_len;
	return rx_len;
}

static int w5500_eth_free_pkt(struct udevice *dev, u8 *packet, int length)
{
	struct eth_w5500_priv *priv = dev_get_priv(dev);
	int i;

	if (!priv->recv_packets)
		return 0;

	--priv->recv_packets;
	for (i = 0; i < priv->recv_packets; i++) {
		priv->recv_packet_length[i] = priv->recv_packet_length[i + 1];
		memcpy(priv->recv_packet_buffer[i],
		       priv->recv_packet_buffer[i + 1],
		       priv->recv_packet_length[i + 1]);
	}
	priv->recv_packet_length[priv->recv_packets] = 0;

	return 0;
}

static void w5500_eth_stop(struct udevice *dev)
{
	debug("eth_w5500: Stop\n");
}

static int xfer(struct udevice *dev, void *dout, unsigned int bout, void *din,
		unsigned int bin)
{
	/* Ok the xfer function in uboot is symmetrical
	 * (write and read operation happens at the same time)
	 * w5500 requires bytes send followed by receive operation on the MISO line
	 * So we need to create a buffer which contain bout+bin bytes and pass the various
	 * pointer to the xfer function
	 */

	struct udevice *bus = dev_get_parent(dev);
	u8 buffin[BUFFER_SZ];
	u8 buffout[BUFFER_SZ];

	if ((bout + bin) < BUFFER_SZ) {
		for (int i = 0; i < bout; i++) {
			buffout[i] = ((u8 *)(dout))[i];
			buffin[i] = 0;
		}
		for (int i = bout; i < (bin + bout); i++) {
			buffin[i] = 0;
			buffout[i] = 0;
		}
		if (bus) {
			dm_spi_xfer(dev, 8 * (bout + bin), buffout, buffin,
				    SPI_XFER_BEGIN | SPI_XFER_END);
			for (int i = bout; i < (bin + bout); i++)
				((u8 *)(din))[bin + bout - i - 1] = buffin[i];
		} else {
			return -1;
		}
	}
	return 0;
}

static int w5500_spi_write(struct udevice *dev, u32 addr, u8 data)
{
	u8 bank;
	u8 din = 0;
	u8 cmd[4];

	bank = (addr >> 16);

	if (bank > 0)
		bank = bank << 3;

	cmd[0] = (addr >> 8) & 0xff;
	cmd[1] = addr & 0xff;
	cmd[2] = bank | 0x4;
	cmd[3] = data;

	return xfer(dev, cmd, sizeof(cmd), &din, 0);
}

static int w5500_spi_read(struct udevice *dev, u32 addr)
{
	u8 bank;
	u8 cmd[3];
	u8 data;
	int ret;

	bank = (addr >> 16);

	if (bank > 0)
		bank = bank << 3;

	cmd[0] = addr >> 8;
	cmd[1] = addr & 0xff;
	cmd[2] = bank;

	ret = xfer(dev, cmd, sizeof(cmd), &data, 1);

	return data;
}

static int w5500_spi_read16(struct udevice *dev, u32 addr)
{
	u16 data;
	int ret;
	u8 bank;
	u8 cmd[3];

	bank = (addr >> 16);

	if (bank > 0)
		bank = bank << 3;

	cmd[0] = addr >> 8;
	cmd[1] = addr & 0xff;
	cmd[2] = bank;

	ret = xfer(dev, cmd, sizeof(cmd), &data, 2);

	return data;
}

static int w5500_spi_write16(struct udevice *dev, u32 addr, u16 data)
{
	int ret;
	u8 buf[2];

	buf[0] = data >> 8;
	buf[1] = data & 0xff;

	ret = w5500_writebulk(dev, addr, &buf[0], 2);

	return ret;
}

static int w5500_readbulk(struct udevice *dev, u32 addr, u8 *buf, int len)
{
	int i;
	u8 data[9000];
	u8 bank;
	u8 cmd[3];
	int ret;

	bank = (addr >> 16);

	if (bank > 0)
		bank = bank << 3;

	cmd[0] = addr >> 8;
	cmd[1] = addr & 0xff;
	cmd[2] = bank;

	ret = xfer(dev, cmd, sizeof(cmd), &data, len);

	for (i = 0; i < len; i++)
		buf[(len - 1) - i] = data[i];

	return 0;
}

static int w5500_writebulk(struct udevice *dev, u32 addr, const u8 *buf,
			   int len)
{
	int i;
	u8 bank;
	u8 cmd[9000];
	u8 din = 0;

	bank = (addr >> 16);
	if (bank > 0)
		bank = bank << 3;

	cmd[0] = (addr >> 8) & 0xff;
	cmd[1] = addr & 0xff;
	cmd[2] = bank | 0x4;

	for (i = 0; i < len; i++)
		cmd[i + 3] = buf[i];

	return xfer(dev, cmd, len + 3, &din, 0);
}

static int w5500_writebuf(struct udevice *dev, u16 offset, const u8 *buf,
			  int len)
{
	u32 addr;
	int ret;
	int remain = 0;
	struct eth_w5500_priv *priv = dev_get_priv(dev);
	const u32 mem_start = priv->s0_tx_buf;
	const u16 mem_size = priv->s0_tx_buf_size;

	offset %= mem_size;
	addr = mem_start + offset;

	if (offset + len > mem_size) {
		remain = (offset + len) % mem_size;
		len = mem_size - offset;
	}

	ret = w5500_writebulk(dev, addr, buf, len);

	if (ret || !remain)
		return ret;

	return w5500_writebulk(dev, mem_start, buf + len, remain);
}

static int w5500_readbuf(struct udevice *dev, u16 offset, u8 *buf, int len)
{
	u32 addr;
	int remain = 0;
	int ret;
	struct eth_w5500_priv *priv = dev_get_priv(dev);
	const u32 mem_start = priv->s0_rx_buf;
	const u16 mem_size = priv->s0_rx_buf_size;

	offset %= mem_size;
	addr = mem_start + offset;

	if (offset + len > mem_size) {
		remain = (offset + len) % mem_size;
		len = mem_size - offset;
	}

	ret = w5500_readbulk(dev, addr, buf, len);
	if (ret || !remain)
		return ret;

	return w5500_readbulk(dev, mem_start, buf + len, remain);
}

static int w5500_eth_write_hwaddr(struct udevice *dev)
{
	struct eth_pdata *pdata = dev_get_plat(dev);
	struct eth_w5500_priv *priv = dev_get_priv(dev);
	u8 eth[6];
	u8 mode;

	if (memcmp(priv->host_hwaddr, pdata->enetaddr, ARP_HLEN) != 0) {
		memcpy(priv->host_hwaddr, pdata->enetaddr, ARP_HLEN);
		w5500_writebulk(dev, W5100_SHAR, priv->host_hwaddr, ARP_HLEN);
	}

	w5500_readbulk(dev, W5100_SHAR, eth, 6);

	mode = 0x84;
	w5500_spi_write(dev, W5100_S0_MR(priv), mode);
	w5500_command(dev, S0_CR_OPEN);
	w5500_enable_intr(dev);

	return 0;
}

static const struct eth_ops w5500_eth_ops = {
	.start = w5500_eth_start,
	.send = w5500_eth_send,
	.recv = w5500_eth_recv,
	.free_pkt = w5500_eth_free_pkt,
	.stop = w5500_eth_stop,
	.write_hwaddr = w5500_eth_write_hwaddr,
};

static int w5500_eth_remove(struct udevice *dev)
{
	return 0;
}

int w5500_eth_of_to_plat(struct udevice *dev)
{
	struct eth_pdata *pdata = dev_get_plat(dev);
	struct eth_w5500_priv *priv = dev_get_priv(dev);
	u8 mac[8];
	u32 ret;
	ofnode remote;
	const void *ptr;
	int size;

	size = 0;
	ptr = ofnode_read_prop(remote, "local-mac-address", &size);
	if (size == 6) {
		mac[0] = ((u8 *)ptr)[0];
		mac[1] = ((u8 *)ptr)[1];
		mac[2] = ((u8 *)ptr)[2];
		mac[3] = ((u8 *)ptr)[4];
		mac[4] = ((u8 *)ptr)[5];
		mac[5] = ((u8 *)ptr)[5];
	} else {
		return ret;
	}

	memcpy(pdata->enetaddr, (void *)mac, ARP_HLEN);

	priv->disabled = false;

	return 0;
}

static void w5500_socket_intr_mask(struct udevice *dev, u8 mask)
{
	u32 imr;

	imr = W5500_SIMR;

	w5500_spi_write(dev, imr, mask);
}

static void w5500_disable_intr(struct udevice *dev)
{
	w5500_socket_intr_mask(dev, 0);
}

static void w5500_enable_intr(struct udevice *dev)
{
	w5500_socket_intr_mask(dev, IR_S0);
}

int w5500_eth_probe(struct udevice *dev)
{
	struct eth_w5500_priv *priv = dev_get_priv(dev);
	int i;
	u16 rtr;
	u8 cmd[3];

	if (device_get_uclass_id(dev->parent) != UCLASS_SPI) {
		debug("Error device not attached to a SPI controlled\n");
		return -ENODEV;
	}

	cmd[0] = 0x00;
	cmd[1] = 0x19;
	cmd[2] = 0;

	w5500_spi_write(dev, W5100_MR, MR_RST);
	w5500_spi_write(dev, W5100_MR, MR_PB);

	rtr = W5500_RTR;
	if (w5500_spi_read16(dev, rtr) != RTR_DEFAULT) {
		debug("RTR issue in probe .... %x\n",
		      w5500_spi_read16(dev, rtr));
		return -ENODEV;
	}

	w5500_disable_intr(dev);

	/* Configure internal RX memory as 16K RX buffer and
	 * internal TX memory as 16K TX buffer
	 */

	w5500_spi_write(dev, W5500_Sn_RXMEM_SIZE(0), 0x10);
	w5500_spi_write(dev, W5500_Sn_TXMEM_SIZE(0), 0x10);

	for (i = 1; i < 8; i++) {
		w5500_spi_write(dev, W5500_Sn_RXMEM_SIZE(i), 0);
		w5500_spi_write(dev, W5500_Sn_TXMEM_SIZE(i), 0);
	}

	priv->s0_regs = W5500_S0_REGS;
	priv->s0_tx_buf = W5500_TX_MEM_START;
	priv->s0_tx_buf_size = W5500_TX_MEM_SIZE;
	priv->s0_rx_buf = W5500_RX_MEM_START;
	priv->s0_rx_buf_size = W5500_RX_MEM_SIZE;

	priv->offset = 0;
	return 0;
}

static const struct udevice_id w5500_eth_ids[] = {
	{.compatible = "wiznet,w5500" },
	{ }
};

U_BOOT_DRIVER(eth_w5500) = {
	.name = "eth_w5500",
	.id = UCLASS_ETH,
	.of_match = w5500_eth_ids,
	.of_to_plat = w5500_eth_of_to_plat,
	.probe = w5500_eth_probe,
	.remove = w5500_eth_remove,
	.ops = &w5500_eth_ops,
	.priv_auto = sizeof(struct eth_w5500_priv),
	.plat_auto = sizeof(struct eth_pdata),
};
