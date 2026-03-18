// SPDX-License-Identifier: GPL-2.0-only
/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include "goodix_ts_core.h"
#define TS_DRIVER_NAME "gt9916_spi"

#define SPI_TRANS_PREFIX_LEN 1
#define REGISTER_WIDTH 4
#define SPI_READ_DUMMY_LEN 4
#define SPI_READ_PREFIX_LEN \
	(SPI_TRANS_PREFIX_LEN + REGISTER_WIDTH + SPI_READ_DUMMY_LEN)
#define SPI_WRITE_PREFIX_LEN (SPI_TRANS_PREFIX_LEN + REGISTER_WIDTH)

#define SPI_WRITE_FLAG 0xF0
#define SPI_READ_FLAG 0xF1

/**
 * goodix_spi_read_bra- read device register through spi bus
 * @dev: pointer to device data
 * @addr: register address
 * @data: read buffer
 * @len: bytes to read
 * return: 0 - read ok, < 0 - spi transter error
 */
static int goodix_spi_read_bra(struct device *dev, unsigned int addr,
			       unsigned char *data, unsigned int len)
{
	struct spi_device *spi = to_spi_device(dev);
	u8 *rx_buf = NULL;
	u8 *tx_buf = NULL;
	struct spi_transfer xfers;
	struct spi_message spi_msg;
	int ret = 0;

	rx_buf = kzalloc(SPI_READ_PREFIX_LEN + len, GFP_KERNEL);
	tx_buf = kzalloc(SPI_READ_PREFIX_LEN + len, GFP_KERNEL);
	if (!rx_buf || !tx_buf)
		return -ENOMEM;

	spi_message_init(&spi_msg);
	memset(&xfers, 0, sizeof(xfers));

	/*spi_read tx_buf format: 0xF1 + addr(4bytes) + data*/
	tx_buf[0] = SPI_READ_FLAG;
	tx_buf[1] = (addr >> 24) & 0xFF;
	tx_buf[2] = (addr >> 16) & 0xFF;
	tx_buf[3] = (addr >> 8) & 0xFF;
	tx_buf[4] = addr & 0xFF;
	tx_buf[5] = 0xFF;
	tx_buf[6] = 0xFF;
	tx_buf[7] = 0xFF;
	tx_buf[8] = 0xFF;

	xfers.tx_buf = tx_buf;
	xfers.rx_buf = rx_buf;
	xfers.len = SPI_READ_PREFIX_LEN + len;
	xfers.cs_change = 0;
	spi_message_add_tail(&xfers, &spi_msg);
	ret = spi_sync(spi, &spi_msg);
	if (ret < 0) {
		ts_err(dev, "spi transfer error:%d", ret);
		goto exit;
	}
	memcpy(data, &rx_buf[SPI_READ_PREFIX_LEN], len);

exit:
	kfree(rx_buf);
	kfree(tx_buf);
	return ret;
}

static int goodix_spi_read(struct device *dev, unsigned int addr,
			   unsigned char *data, unsigned int len)
{
	struct spi_device *spi = to_spi_device(dev);
	u8 *rx_buf = NULL;
	u8 *tx_buf = NULL;
	struct spi_transfer xfers;
	struct spi_message spi_msg;
	int ret = 0;

	rx_buf = kzalloc(SPI_READ_PREFIX_LEN - 1 + len, GFP_KERNEL);
	tx_buf = kzalloc(SPI_READ_PREFIX_LEN - 1 + len, GFP_KERNEL);
	if (!rx_buf || !tx_buf)
		return -ENOMEM;

	spi_message_init(&spi_msg);
	memset(&xfers, 0, sizeof(xfers));

	/*spi_read tx_buf format: 0xF1 + addr(4bytes) + data*/
	tx_buf[0] = SPI_READ_FLAG;
	tx_buf[1] = (addr >> 24) & 0xFF;
	tx_buf[2] = (addr >> 16) & 0xFF;
	tx_buf[3] = (addr >> 8) & 0xFF;
	tx_buf[4] = addr & 0xFF;
	tx_buf[5] = 0xFF;
	tx_buf[6] = 0xFF;
	tx_buf[7] = 0xFF;

	xfers.tx_buf = tx_buf;
	xfers.rx_buf = rx_buf;
	xfers.len = SPI_READ_PREFIX_LEN - 1 + len;
	xfers.cs_change = 0;
	spi_message_add_tail(&xfers, &spi_msg);
	ret = spi_sync(spi, &spi_msg);
	if (ret < 0) {
		ts_err(dev, "spi transfer error:%d", ret);
		goto exit;
	}
	memcpy(data, &rx_buf[SPI_READ_PREFIX_LEN - 1], len);

exit:
	kfree(rx_buf);
	kfree(tx_buf);
	return ret;
}

/**
 * goodix_spi_write- write device register through spi bus
 * @dev: pointer to device data
 * @addr: register address
 * @data: write buffer
 * @len: bytes to write
 * return: 0 - write ok; < 0 - spi transter error.
 */
static int goodix_spi_write(struct device *dev, unsigned int addr,
			    unsigned char *data, unsigned int len)
{
	struct spi_device *spi = to_spi_device(dev);
	u8 *tx_buf = NULL;
	struct spi_transfer xfers;
	struct spi_message spi_msg;
	int ret = 0;

	tx_buf = kzalloc(SPI_WRITE_PREFIX_LEN + len, GFP_KERNEL);
	if (!tx_buf)
		return -ENOMEM;

	spi_message_init(&spi_msg);
	memset(&xfers, 0, sizeof(xfers));

	tx_buf[0] = SPI_WRITE_FLAG;
	tx_buf[1] = (addr >> 24) & 0xFF;
	tx_buf[2] = (addr >> 16) & 0xFF;
	tx_buf[3] = (addr >> 8) & 0xFF;
	tx_buf[4] = addr & 0xFF;
	memcpy(&tx_buf[SPI_WRITE_PREFIX_LEN], data, len);
	xfers.tx_buf = tx_buf;
	xfers.len = SPI_WRITE_PREFIX_LEN + len;
	xfers.cs_change = 0;
	spi_message_add_tail(&xfers, &spi_msg);
	ret = spi_sync(spi, &spi_msg);
	if (ret < 0)
		ts_err(dev, "spi transfer error:%d", ret);

	kfree(tx_buf);
	return ret;
}

static void goodix_pdev_release(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct goodix_ts_device *ts_dev =
		container_of(pdev, struct goodix_ts_device, pdev);

	ts_info(NULL, "goodix pdev released, id:%d", pdev->id);
	kfree(ts_dev);
}

static int goodix_spi_probe(struct spi_device *spi)
{
	struct goodix_ts_device *ts_dev = NULL;
	int ret = 0;

	ts_info(&spi->dev, "goodix spi probe in");

	/* init spi_device */
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;

	ts_info(&spi->dev, "spi_info: speed[%d] mode[%d] bits_per_word[%d]",
		spi->max_speed_hz, spi->mode, spi->bits_per_word);
	ret = spi_setup(spi);
	if (ret) {
		ts_err(&spi->dev, "failed set spi mode, %d", ret);
		return ret;
	}

	ts_dev = kzalloc(sizeof(*ts_dev), GFP_KERNEL);
	if (!ts_dev)
		return -ENOMEM;

	/* get ic type */
	ret = goodix_get_ic_type(&spi->dev, &ts_dev->bus);
	if (ret < 0)
		return ret;

	ts_dev->bus.bus_type = GOODIX_BUS_TYPE_SPI;
	ts_dev->bus.dev = &spi->dev;
	if (ts_dev->bus.ic_type == IC_TYPE_BERLIN_A)
		ts_dev->bus.read = goodix_spi_read_bra;
	else
		ts_dev->bus.read = goodix_spi_read;
	ts_dev->bus.write = goodix_spi_write;

	// platform device init
	ts_dev->pdev.name = GOODIX_CORE_DRIVER_NAME;
	ts_dev->pdev.id = g_pdev_id++;
	ts_dev->pdev.num_resources = 0;
	ts_dev->pdev.dev.release = goodix_pdev_release;

	spi_set_drvdata(spi, ts_dev);

	/* register platform device, then the goodix_ts_core
	 * module will probe the touch deivce.
	 */
	ret = platform_device_register(&ts_dev->pdev);
	if (ret) {
		ts_err(&spi->dev, "failed register goodix platform device, %d", ret);
		goto err_pdev;
	}
	ts_info(&spi->dev, "spi probe out");
	return 0;

err_pdev:
	kfree(ts_dev);
	ts_info(&spi->dev, "spi probe out, %d", ret);
	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0))
static void goodix_spi_remove(struct spi_device *spi)
{
	struct goodix_ts_device *ts_dev = spi_get_drvdata(spi);

	ts_info(&spi->dev, "goodix spi driver remove, id:%d", ts_dev->pdev.id);
	platform_device_unregister(&ts_dev->pdev);
}
#else
static int goodix_spi_remove(struct spi_device *spi)
{
	struct goodix_ts_device *ts_dev = spi_get_drvdata(spi);

	ts_info(&spi->dev, "goodix spi driver remove, id:%d", ts_dev->pdev.id);
	platform_device_unregister(&ts_dev->pdev);
	return 0;
}
#endif

#ifdef CONFIG_OF
static const struct of_device_id spi_matchs[] = {
	{
		.compatible = "goodix,brl-a",
	},
	{
		.compatible = "goodix,brl-b",
	},
	{
		.compatible = "goodix,brl-d-gt9916",
	},
	{
		.compatible = "goodix,nottingham",
	},
	{
		.compatible = "goodix,marseille",
	},
	{
		.compatible = "goodix,atb",
	},
	{},
};
#endif

static const struct spi_device_id spi_id_table[] = {
	{ TS_DRIVER_NAME, 0 },
	{},
};

static struct spi_driver goodix_spi_driver = {
	.driver = {
		.name = TS_DRIVER_NAME,
		//.owner = THIS_MODULE,
		.of_match_table = spi_matchs,
	},
	.id_table = spi_id_table,
	.probe = goodix_spi_probe,
	.remove = goodix_spi_remove,
};

int goodix_spi_bus_init(void)
{
	ts_info(NULL, "Goodix spi driver init");
	return spi_register_driver(&goodix_spi_driver);
}

void goodix_spi_bus_exit(void)
{
	ts_info(NULL, "Goodix spi driver exit");
	spi_unregister_driver(&goodix_spi_driver);
}
